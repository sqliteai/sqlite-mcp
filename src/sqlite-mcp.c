//
//  sqlite-mcp.c
//  sqlitemcp
//
//  Created by Gioele Cantoni on 05/11/25.
//

// Uncomment to enable debug output
// #define MCP_DEBUG 1

#ifdef MCP_DEBUG
  #define D(x) printf("[DEBUG] " x "\n"); fflush(stdout)
  #define DF(fmt, ...) printf("[DEBUG] " fmt "\n", __VA_ARGS__); fflush(stdout)
#else
  #define D(x)
  #define DF(fmt, ...)
#endif

#include "sqlite-mcp.h"
#include "mcp_ffi.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Ensure SQLITE_INDEX_CONSTRAINT_FUNCTION is defined (SQLite 3.9.0+)
#ifndef SQLITE_INDEX_CONSTRAINT_FUNCTION
#define SQLITE_INDEX_CONSTRAINT_FUNCTION 150
#endif

SQLITE_EXTENSION_INIT1

/* Helper function to set result as text */
static void mcp_result_text(sqlite3_context *context, const char *text, int len) {
  sqlite3_result_text(context, text, len, SQLITE_TRANSIENT);
}

static void mcp_version_func(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  char *version = mcp_get_version();
  if (version) {
    sqlite3_result_text(context, version, -1, SQLITE_TRANSIENT);
    mcp_free_string(version);
  } else {
    sqlite3_result_error(context, "Failed to get MCP version", -1);
  }
}

/*
** SQL function: mcp_connect(server_url, [headers_json], [legacy_sse])
** Connects to an MCP server with optional custom headers
**
** Returns NULL on successful connection, error string on failure
*/
static void mcp_connect_func(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  if (argc < 1 || argc > 3) {
    sqlite3_result_error(context, "mcp_connect requires 1-3 arguments: (server_url, [headers_json], [legacy_sse])", -1);
    return;
  }

  const char *server_url = (const char*)sqlite3_value_text(argv[0]);
  if (!server_url) {
    sqlite3_result_error(context, "mcp_connect requires a URL", -1);
    return;
  }

  const char *headers_json = NULL;
  if (argc >= 2 && sqlite3_value_type(argv[1]) != SQLITE_NULL) {
    headers_json = (const char*)sqlite3_value_text(argv[1]);
  }

  int legacy_sse = 0;
  if (argc >= 3) {
    legacy_sse = sqlite3_value_int(argv[2]);
  }

  char *result = mcp_connect(NULL, server_url, headers_json, legacy_sse);

  if (!result) {
    // NULL result means success
    sqlite3_result_null(context);
    return;
  }

  // Non-NULL result is an error message
  sqlite3_result_text(context, result, -1, SQLITE_TRANSIENT);
  mcp_free_string(result);
}

/*
** STREAMING Virtual Table for mcp_list_tools
** Returns parsed tool information as rows using streaming API
*/

// Include Rust FFI streaming types
typedef struct StreamResult {
  int result_type;
  char *data;
} StreamResult;

// Stream type constants (must match Rust)
#define STREAM_TYPE_TOOL  0
#define STREAM_TYPE_TEXT  1
#define STREAM_TYPE_ERROR 2
#define STREAM_TYPE_DONE  3

// Rust FFI streaming functions
extern size_t mcp_list_tools_init(void);
extern size_t mcp_call_tool_init(const char* tool_name, const char* arguments);
extern StreamResult* mcp_stream_next(size_t stream_id);
extern StreamResult* mcp_stream_wait(size_t stream_id, uint64_t timeout_ms);
extern void mcp_stream_cleanup(size_t stream_id);
extern void mcp_stream_free_result(StreamResult* result);

// Rust FFI JSON functions
extern char* mcp_list_tools_json(void*);
extern char* mcp_call_tool_json(void*, const char*, const char*);
extern void mcp_free_string(char*);

// JSON parsing functions (using serde_json in Rust)
extern size_t mcp_parse_tools_json(const char* json_str);
extern char* mcp_get_tool_field(const char* json_str, size_t tool_index, const char* field_name);
extern size_t mcp_parse_call_result_json(const char* json_str);
extern char* mcp_get_call_result_text(const char* json_str, size_t content_index);

typedef struct mcp_stream_vtab {
  sqlite3_vtab base;
} mcp_stream_vtab;

typedef struct mcp_stream_cursor {
  sqlite3_vtab_cursor base;
  size_t stream_id;           // Rust stream ID
  char *current_data;         // Current tool JSON data
  int eof;                    // End of stream flag
  sqlite_int64 rowid;
} mcp_stream_cursor;

// Forward declarations
static int mcp_stream_next_impl(sqlite3_vtab_cursor *cur);
static int mcp_stream_vtab_next(sqlite3_vtab_cursor *cur);

static int mcp_stream_connect(
  sqlite3 *db,
  void *pAux,
  int argc, const char *const*argv,
  sqlite3_vtab **ppVtab,
  char **pzErr
){
  mcp_stream_vtab *pNew = sqlite3_malloc(sizeof(*pNew));
  if (pNew==0) return SQLITE_NOMEM;
  memset(pNew, 0, sizeof(*pNew));
  *ppVtab = (sqlite3_vtab*)pNew;

  int rc = sqlite3_declare_vtab(db,
    "CREATE TABLE x(name TEXT, title TEXT, description TEXT, "
    "inputSchema TEXT, outputSchema TEXT, annotations TEXT)");

  return rc;
}

static int mcp_stream_disconnect(sqlite3_vtab *pVtab){
  sqlite3_free(pVtab);
  return SQLITE_OK;
}

static int mcp_stream_open(sqlite3_vtab *pVtab, sqlite3_vtab_cursor **ppCursor){
  mcp_stream_cursor *pCur = sqlite3_malloc(sizeof(*pCur));
  if (pCur==0) return SQLITE_NOMEM;
  memset(pCur, 0, sizeof(*pCur));
  *ppCursor = (sqlite3_vtab_cursor*)pCur;
  return SQLITE_OK;
}

static int mcp_stream_close(sqlite3_vtab_cursor *cur){
  mcp_stream_cursor *pCur = (mcp_stream_cursor*)cur;

  // Cleanup stream resources
  if (pCur->stream_id > 0) {
    mcp_stream_cleanup(pCur->stream_id);
  }

  if (pCur->current_data) {
    sqlite3_free(pCur->current_data);
  }

  sqlite3_free(pCur);
  return SQLITE_OK;
}

static int mcp_stream_filter(
  sqlite3_vtab_cursor *pVtabCursor,
  int idxNum, const char *idxStr,
  int argc, sqlite3_value **argv
){
  mcp_stream_cursor *pCur = (mcp_stream_cursor*)pVtabCursor;

  D("mcp_stream_filter: Starting mcp_list_tools streaming");

  // Initialize streaming
  pCur->stream_id = mcp_list_tools_init();
  DF("mcp_stream_filter: stream_id=%zu", pCur->stream_id);

  if (pCur->stream_id == 0) {
    D("mcp_stream_filter: Failed to initialize stream (stream_id=0)");
    pCur->eof = 1;
    return SQLITE_ERROR;
  }

  pCur->rowid = 0;
  pCur->eof = 0;

  D("mcp_stream_filter: Getting first result");
  // Get first result
  return mcp_stream_next_impl(pVtabCursor);
}

static int mcp_stream_next_impl(sqlite3_vtab_cursor *cur) {
  mcp_stream_cursor *pCur = (mcp_stream_cursor*)cur;

  DF("mcp_stream_next_impl: rowid=%lld, stream_id=%zu", pCur->rowid, pCur->stream_id);

  // Free previous data
  if (pCur->current_data) {
    sqlite3_free(pCur->current_data);
    pCur->current_data = NULL;
  }

  // Poll for next chunk (with 100ms wait if needed)
  D("mcp_stream_next_impl: Waiting for next stream result...");
  StreamResult *result = mcp_stream_wait(pCur->stream_id, 100);

  if (!result) {
    D("mcp_stream_next_impl: No result received (NULL)");
    pCur->eof = 1;
    return SQLITE_OK;
  }

  DF("mcp_stream_next_impl: Received result_type=%d", result->result_type);

  switch (result->result_type) {
    case STREAM_TYPE_TOOL:
      // Store tool JSON data
      if (result->data) {
        size_t data_len = strlen(result->data);
        pCur->current_data = sqlite3_mprintf("%s", result->data);
        DF("mcp_stream_next_impl: STREAM_TYPE_TOOL received (%zu bytes)", data_len);
        if (data_len < 200) {
          DF("  Data: %s", result->data);
        } else {
          DF("  Data (first 200 chars): %.200s...", result->data);
        }
      }
      pCur->rowid++;
      break;

    case STREAM_TYPE_ERROR:
      // Stream error - stop iteration
      DF("mcp_stream_next_impl: STREAM_TYPE_ERROR - %s", result->data ? result->data : "unknown error");
      pCur->eof = 1;
      break;

    case STREAM_TYPE_DONE:
      // Stream complete
      D("mcp_stream_next_impl: STREAM_TYPE_DONE - stream complete");
      pCur->eof = 1;
      break;

    default:
      DF("mcp_stream_next_impl: Unknown result_type=%d", result->result_type);
      pCur->eof = 1;
      break;
  }

  mcp_stream_free_result(result);
  return SQLITE_OK;
}

static int mcp_stream_vtab_next(sqlite3_vtab_cursor *cur){
  return mcp_stream_next_impl(cur);
}

static int mcp_stream_eof(sqlite3_vtab_cursor *cur){
  mcp_stream_cursor *pCur = (mcp_stream_cursor*)cur;
  return pCur->eof;
}

static int mcp_stream_column(sqlite3_vtab_cursor *cur, sqlite3_context *context, int iCol){
  mcp_stream_cursor *pCur = (mcp_stream_cursor*)cur;

  DF("mcp_stream_column: iCol=%d, rowid=%lld", iCol, pCur->rowid);

  if (!pCur->current_data) {
    D("mcp_stream_column: current_data is NULL");
    sqlite3_result_null(context);
    return SQLITE_OK;
  }

  // Parse JSON using Rust serde_json functions
  const char *json = pCur->current_data;

  const char *field_name = NULL;
  switch (iCol) {
    case 0: field_name = "name"; break;
    case 1: field_name = "title"; break;
    case 2: field_name = "description"; break;
    case 3: field_name = "inputSchema"; break;
    case 4: field_name = "outputSchema"; break;
    case 5: field_name = "annotations"; break;
    default: sqlite3_result_null(context); return SQLITE_OK;
  }

  DF("mcp_stream_column: Extracting field '%s'", field_name);

  // Parse JSON in the Rust layer
  char *field_value = mcp_get_tool_field(json, 0, field_name); // index 0 for single tool
  if (field_value && strlen(field_value) > 0) {
    DF("mcp_stream_column: Field '%s' = '%s'", field_name, field_value);
    sqlite3_result_text(context, field_value, -1, SQLITE_TRANSIENT);
    mcp_free_string(field_value);
  } else {
    DF("mcp_stream_column: Field '%s' is NULL or empty", field_name);
    sqlite3_result_null(context);
  }

  return SQLITE_OK;
}

static int mcp_stream_rowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid){
  mcp_stream_cursor *pCur = (mcp_stream_cursor*)cur;
  *pRowid = pCur->rowid;
  return SQLITE_OK;
}

static int mcp_stream_best_index(sqlite3_vtab *tab, sqlite3_index_info *pIdxInfo){
  pIdxInfo->estimatedCost = (double)10;
  return SQLITE_OK;
}

static sqlite3_module mcp_stream_module = {
  0,                          /* iVersion */
  0,                          /* xCreate */
  mcp_stream_connect,         /* xConnect */
  mcp_stream_best_index,      /* xBestIndex */
  mcp_stream_disconnect,      /* xDisconnect */
  0,                          /* xDestroy */
  mcp_stream_open,            /* xOpen - open a cursor */
  mcp_stream_close,           /* xClose - close a cursor */
  mcp_stream_filter,          /* xFilter - configure scan constraints */
  mcp_stream_vtab_next,       /* xNext - advance a cursor */
  mcp_stream_eof,             /* xEof - check for end of scan */
  mcp_stream_column,          /* xColumn - read data */
  mcp_stream_rowid,           /* xRowid - read data */
  0,                          /* xUpdate */
  0,                          /* xBegin */
  0,                          /* xSync */
  0,                          /* xCommit */
  0,                          /* xRollback */
  0,                          /* xFindMethod */
  0,                          /* xRename */
  0,                          /* xSavepoint */
  0,                          /* xRelease */
  0,                          /* xRollbackTo */
  0,                          /* xShadowName */
  0,                          /* xIntegrity */
};

/*
** Virtual table for mcp_call_tool (streaming)
** Returns streamed text chunks from tool calls
*/
typedef struct mcp_call_tool_stream_vtab {
  sqlite3_vtab base;
} mcp_call_tool_stream_vtab;

typedef struct mcp_call_tool_stream_cursor {
  sqlite3_vtab_cursor base;
  size_t stream_id;
  char *current_text;
  int eof;
  sqlite_int64 rowid;
} mcp_call_tool_stream_cursor;

static int mcp_call_tool_stream_connect(
  sqlite3 *db,
  void *pAux,
  int argc, const char *const*argv,
  sqlite3_vtab **ppVtab,
  char **pzErr
){
  mcp_call_tool_stream_vtab *pNew = sqlite3_malloc(sizeof(*pNew));
  if (pNew==0) return SQLITE_NOMEM;
  memset(pNew, 0, sizeof(*pNew));

  DF("mcp_call_tool_stream_connect: argc=%d", argc);
  for (int i = 0; i < argc; i++) {
    DF("  argv[%d]='%s'", i, argv[i] ? argv[i] : "NULL");
  }

  *ppVtab = (sqlite3_vtab*)pNew;

  int rc = sqlite3_declare_vtab(db, "CREATE TABLE x(text TEXT, tool_name HIDDEN, arguments HIDDEN)");
  return rc;
}

static int mcp_call_tool_stream_disconnect(sqlite3_vtab *pVtab){
  mcp_call_tool_stream_vtab *p = (mcp_call_tool_stream_vtab*)pVtab;
  sqlite3_free(p);
  return SQLITE_OK;
}

static int mcp_call_tool_stream_open(sqlite3_vtab *pVtab, sqlite3_vtab_cursor **ppCursor){
  mcp_call_tool_stream_cursor *pCur = sqlite3_malloc(sizeof(*pCur));
  if (pCur==0) return SQLITE_NOMEM;
  memset(pCur, 0, sizeof(*pCur));
  *ppCursor = (sqlite3_vtab_cursor*)pCur;
  return SQLITE_OK;
}

static int mcp_call_tool_stream_close(sqlite3_vtab_cursor *cur){
  mcp_call_tool_stream_cursor *pCur = (mcp_call_tool_stream_cursor*)cur;

  if (pCur->stream_id > 0) {
    mcp_stream_cleanup(pCur->stream_id);
  }

  if (pCur->current_text) {
    sqlite3_free(pCur->current_text);
  }

  sqlite3_free(pCur);
  return SQLITE_OK;
}

static int mcp_call_tool_stream_filter(
  sqlite3_vtab_cursor *pVtabCursor,
  int idxNum, const char *idxStr,
  int argc, sqlite3_value **argv
){
  mcp_call_tool_stream_cursor *pCur = (mcp_call_tool_stream_cursor*)pVtabCursor;

  D("mcp_call_tool_stream_filter: called");
  DF("  argc=%d, idxNum=%d", argc, idxNum);
  for (int i = 0; i < argc; i++) {
    DF("    argv[%d] = '%s'", i, sqlite3_value_text(argv[i]) ? (const char*)sqlite3_value_text(argv[i]) : "NULL");
  }

  if (idxNum == 0) {
    D("mcp_call_tool_stream_filter: Invalid query plan (idxNum=0)");
    pCur->eof = 1;
    return SQLITE_ERROR;
  }

  const char *tool_name = NULL;
  const char *arguments = NULL;

  // Function arguments come through the constraint system
  if (argc >= 2) {
    tool_name = (const char*)sqlite3_value_text(argv[0]);
    arguments = (const char*)sqlite3_value_text(argv[1]);
  } else {
    D("mcp_call_tool_stream_filter: Not enough arguments");
    pCur->eof = 1;
    return SQLITE_ERROR;
  }

  if (!tool_name || !arguments) {
    D("mcp_call_tool_stream_filter: tool_name or arguments is NULL");
    pCur->eof = 1;
    return SQLITE_ERROR;
  }

  DF("mcp_call_tool_stream_filter: tool_name='%s', arguments='%s'", tool_name, arguments);

  // Initialize streaming
  pCur->stream_id = mcp_call_tool_init(tool_name, arguments);
  DF("mcp_call_tool_stream_filter: stream_id=%d", (int)pCur->stream_id);
  if (pCur->stream_id == 0) {
    D("mcp_call_tool_stream_filter: stream_id is 0, failed");
    pCur->eof = 1;
    return SQLITE_ERROR;
  }

  pCur->rowid = 0;
  pCur->eof = 0;

  // Get first result - use longer timeout for tool execution
  StreamResult *result = mcp_stream_wait(pCur->stream_id, 5000); // 5 seconds timeout
  if (!result) {
    D("mcp_call_tool_stream_filter: No initial result");
    pCur->eof = 1;
    return SQLITE_OK;
  }

  if (result->result_type == STREAM_TYPE_DONE) {
    D("mcp_call_tool_stream_filter: Stream immediately done");
    pCur->eof = 1;
    mcp_stream_free_result(result);
    return SQLITE_OK;
  }

  if (result->result_type == STREAM_TYPE_TEXT && result->data) {
    pCur->current_text = sqlite3_mprintf("%s", result->data);
    pCur->rowid++;
    DF("mcp_call_tool_stream_filter: Got first text chunk (%d bytes)", (int)strlen(result->data));
  }

  mcp_stream_free_result(result);
  return SQLITE_OK;
}

static int mcp_call_tool_stream_next(sqlite3_vtab_cursor *cur){
  mcp_call_tool_stream_cursor *pCur = (mcp_call_tool_stream_cursor*)cur;

  if (pCur->current_text) {
    sqlite3_free(pCur->current_text);
    pCur->current_text = NULL;
  }

  StreamResult *result = mcp_stream_wait(pCur->stream_id, 1000); // 1 second timeout for subsequent results
  if (!result) {
    pCur->eof = 1;
    return SQLITE_OK;
  }

  if (result->result_type == STREAM_TYPE_DONE) {
    pCur->eof = 1;
    mcp_stream_free_result(result);
    return SQLITE_OK;
  }

  if (result->result_type == STREAM_TYPE_TEXT && result->data) {
    pCur->current_text = sqlite3_mprintf("%s", result->data);
    pCur->rowid++;
  }

  mcp_stream_free_result(result);
  return SQLITE_OK;
}

static int mcp_call_tool_stream_eof(sqlite3_vtab_cursor *cur){
  mcp_call_tool_stream_cursor *pCur = (mcp_call_tool_stream_cursor*)cur;
  return pCur->eof;
}

static int mcp_call_tool_stream_column(
  sqlite3_vtab_cursor *cur,
  sqlite3_context *context,
  int iCol
){
  mcp_call_tool_stream_cursor *pCur = (mcp_call_tool_stream_cursor*)cur;

  if (iCol == 0 && pCur->current_text) {
    sqlite3_result_text(context, pCur->current_text, -1, SQLITE_TRANSIENT);
  } else {
    sqlite3_result_null(context);
  }

  return SQLITE_OK;
}

static int mcp_call_tool_stream_rowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid){
  mcp_call_tool_stream_cursor *pCur = (mcp_call_tool_stream_cursor*)cur;
  *pRowid = pCur->rowid;
  return SQLITE_OK;
}

static int mcp_call_tool_stream_best_index(sqlite3_vtab *tab, sqlite3_index_info *pIdxInfo){
  D("mcp_call_tool_stream_best_index: called");
  DF("  nConstraint=%d", pIdxInfo->nConstraint);
  
  int has_tool_name = 0, has_arguments = 0;
  int has_function_constraint = 0;
  
  for (int i = 0; i < pIdxInfo->nConstraint; i++) {
    DF("    constraint[%d]: iColumn=%d, op=%d, usable=%d", 
       i, pIdxInfo->aConstraint[i].iColumn, 
       pIdxInfo->aConstraint[i].op, 
       pIdxInfo->aConstraint[i].usable);
    
    if (!pIdxInfo->aConstraint[i].usable) continue;
    
    // Check for function-style constraints (arguments passed directly to table function)
    if (pIdxInfo->aConstraint[i].op == SQLITE_INDEX_CONSTRAINT_FUNCTION) {
      has_function_constraint = 1;
      pIdxInfo->aConstraintUsage[i].argvIndex = 1; // Use first position for function args
      pIdxInfo->aConstraintUsage[i].omit = 1;
      continue;
    }
    
    // Check for WHERE clause constraints on hidden columns
    if (pIdxInfo->aConstraint[i].op == SQLITE_INDEX_CONSTRAINT_EQ) {
      if (pIdxInfo->aConstraint[i].iColumn == 1) { // tool_name HIDDEN column
        has_tool_name = 1;
        pIdxInfo->aConstraintUsage[i].argvIndex = has_function_constraint ? 3 : 1;
        pIdxInfo->aConstraintUsage[i].omit = 1;
      } else if (pIdxInfo->aConstraint[i].iColumn == 2) { // arguments HIDDEN column  
        has_arguments = 1;
        pIdxInfo->aConstraintUsage[i].argvIndex = has_function_constraint ? 4 : 2;
        pIdxInfo->aConstraintUsage[i].omit = 1;
      }
    }
  }
  
  if (has_function_constraint || (has_tool_name && has_arguments)) {
    D("mcp_call_tool_stream_best_index: Found function constraint or both tool_name and arguments constraints");
    pIdxInfo->estimatedCost = 100.0;
    pIdxInfo->idxNum = 1; // Mark as valid query plan
  } else {
    D("mcp_call_tool_stream_best_index: Missing required constraints");
    pIdxInfo->estimatedCost = 1000000.0; // High cost for unsupported query
    pIdxInfo->idxNum = 0;
  }
  
  return SQLITE_OK;
}

static sqlite3_module mcp_call_tool_stream_module = {
  0,                                   /* iVersion */
  0,                                   /* xCreate */
  mcp_call_tool_stream_connect,       /* xConnect */
  mcp_call_tool_stream_best_index,    /* xBestIndex */
  mcp_call_tool_stream_disconnect,    /* xDisconnect */
  0,                                   /* xDestroy */
  mcp_call_tool_stream_open,          /* xOpen */
  mcp_call_tool_stream_close,         /* xClose */
  mcp_call_tool_stream_filter,        /* xFilter */
  mcp_call_tool_stream_next,          /* xNext */
  mcp_call_tool_stream_eof,           /* xEof */
  mcp_call_tool_stream_column,        /* xColumn */
  mcp_call_tool_stream_rowid,         /* xRowid */
  0,                                   /* xUpdate */
  0,                                   /* xBegin */
  0,                                   /* xSync */
  0,                                   /* xCommit */
  0,                                   /* xRollback */
  0,                                   /* xFindMethod */
  0,                                   /* xRename */
  0,                                   /* xSavepoint */
  0,                                   /* xRelease */
  0,                                   /* xRollbackTo */
  0,                                   /* xShadowName */
  0,                                   /* xIntegrity */
};

/*
** Virtual table for mcp_list_tools_respond (cached)
** Returns parsed tool information as rows from cached temp table
*/
typedef struct mcp_tools_vtab {
  sqlite3_vtab base;
  sqlite3 *db;
  char *temp_table_name;  // Name of temporary table with cached results
  int table_created;       // Flag to track if temp table was created
} mcp_tools_vtab;

typedef struct mcp_tools_cursor {
  sqlite3_vtab_cursor base;
  sqlite3_stmt *stmt;
  int eof;
} mcp_tools_cursor;

static int mcp_tools_connect(
  sqlite3 *db,
  void *pAux,
  int argc, const char *const*argv,
  sqlite3_vtab **ppVtab,
  char **pzErr
){
  mcp_tools_vtab *pNew = sqlite3_malloc(sizeof(*pNew));
  if (pNew==0) return SQLITE_NOMEM;

  pNew->db = db;
  pNew->temp_table_name = sqlite3_mprintf("mcp_tools_cache_%p", (void*)pNew);
  pNew->table_created = 0;
  *ppVtab = (sqlite3_vtab*)pNew;

  return sqlite3_declare_vtab(db,
    "CREATE TABLE x(name TEXT, title TEXT, description TEXT, "
    "inputSchema TEXT, outputSchema TEXT, annotations TEXT)");
}

static int mcp_tools_disconnect(sqlite3_vtab *pVtab){
  mcp_tools_vtab *p = (mcp_tools_vtab*)pVtab;

  // Drop temporary table if it was created
  if (p->table_created && p->temp_table_name) {
    char *drop_sql = sqlite3_mprintf("DROP TABLE IF EXISTS temp.%s", p->temp_table_name);
    if (drop_sql) {
      sqlite3_exec(p->db, drop_sql, NULL, NULL, NULL);
      sqlite3_free(drop_sql);
    }
  }

  // Free temp table name
  if (p->temp_table_name) {
    sqlite3_free(p->temp_table_name);
  }

  sqlite3_free(p);
  return SQLITE_OK;
}

static int mcp_tools_open(sqlite3_vtab *pVtab, sqlite3_vtab_cursor **ppCursor){
  mcp_tools_cursor *pCur = sqlite3_malloc(sizeof(*pCur));
  if (pCur==0) return SQLITE_NOMEM;
  memset(pCur, 0, sizeof(*pCur));
  *ppCursor = (sqlite3_vtab_cursor*)pCur;
  return SQLITE_OK;
}

static int mcp_tools_close(sqlite3_vtab_cursor *cur){
  mcp_tools_cursor *pCur = (mcp_tools_cursor*)cur;
  if (pCur->stmt) {
    sqlite3_finalize(pCur->stmt);
  }
  sqlite3_free(pCur);
  return SQLITE_OK;
}

static int mcp_tools_filter(
  sqlite3_vtab_cursor *pVtabCursor,
  int idxNum, const char *idxStr,
  int argc, sqlite3_value **argv
){
  mcp_tools_cursor *pCur = (mcp_tools_cursor*)pVtabCursor;
  mcp_tools_vtab *pVtab = (mcp_tools_vtab*)pVtabCursor->pVtab;

  if (pCur->stmt) {
    sqlite3_finalize(pCur->stmt);
    pCur->stmt = NULL;
  }

  // Create and populate temp table if not already done
  if (!pVtab->table_created) {
    D("mcp_tools_filter: Creating temp table");
    // Get tools list from MCP
    char *result = mcp_list_tools_json(NULL);
    if (!result) {
      D("mcp_tools_filter: mcp_list_tools_json returned NULL");
      pCur->eof = 1;
      return SQLITE_OK;
    }

    DF("mcp_tools_filter: Got JSON result (%d bytes)", (int)strlen(result));
    // Check for errors - only check if it starts with error
    if (strncmp(result, "{\"error\"", 8) == 0) {
      D("mcp_tools_filter: JSON contains error");
      mcp_free_string(result);
      pCur->eof = 1;
      return SQLITE_OK;
    }

    // Create temporary table
    char *create_sql = sqlite3_mprintf(
      "CREATE TEMP TABLE IF NOT EXISTS %s("
      "name TEXT, title TEXT, description TEXT, "
      "inputSchema TEXT, outputSchema TEXT, annotations TEXT)",
      pVtab->temp_table_name);

    int rc = sqlite3_exec(pVtab->db, create_sql, NULL, NULL, NULL);
    sqlite3_free(create_sql);

    if (rc != SQLITE_OK) {
      mcp_free_string(result);
      pCur->eof = 1;
      return rc;
    }

    // Parse JSON in the Rust layer
    size_t tool_count = mcp_parse_tools_json(result);
    DF("mcp_tools_filter: Parsed %d tools", (int)tool_count);
    if (tool_count == 0) {
      D("mcp_tools_filter: No tools found in JSON");
      mcp_free_string(result);
      pCur->eof = 1;
      return SQLITE_OK;
    }

    // Insert each tool using Rust JSON parsing
    for (size_t i = 0; i < tool_count; i++) {
      char *name = mcp_get_tool_field(result, i, "name");
      char *title = mcp_get_tool_field(result, i, "title");  
      char *description = mcp_get_tool_field(result, i, "description");
      char *inputSchema = mcp_get_tool_field(result, i, "inputSchema");
      char *outputSchema = mcp_get_tool_field(result, i, "outputSchema");
      char *annotations = mcp_get_tool_field(result, i, "annotations");

      char *insert_sql = sqlite3_mprintf(
        "INSERT INTO temp.%s (name, title, description, inputSchema, outputSchema, annotations) "
        "VALUES (%Q, %Q, %Q, %Q, %Q, %Q)",
        pVtab->temp_table_name,
        name ? name : "",
        title ? title : "",
        description ? description : "",
        inputSchema ? inputSchema : "",
        outputSchema ? outputSchema : "",
        annotations ? annotations : ""
      );

      rc = sqlite3_exec(pVtab->db, insert_sql, NULL, NULL, NULL);
      sqlite3_free(insert_sql);

      // Free the allocated strings
      if (name) mcp_free_string(name);
      if (title) mcp_free_string(title);
      if (description) mcp_free_string(description);
      if (inputSchema) mcp_free_string(inputSchema);
      if (outputSchema) mcp_free_string(outputSchema);
      if (annotations) mcp_free_string(annotations);

      if (rc != SQLITE_OK) {
        mcp_free_string(result);
        pCur->eof = 1;
        return rc;
      }
    }
    mcp_free_string(result);

    if (rc != SQLITE_OK) {
      pCur->eof = 1;
      return rc;
    }

    pVtab->table_created = 1;
  }

  // Query the temp table
  char *query = sqlite3_mprintf("SELECT * FROM temp.%s", pVtab->temp_table_name);
  int rc = sqlite3_prepare_v2(pVtab->db, query, -1, &pCur->stmt, NULL);
  sqlite3_free(query);

  if (rc != SQLITE_OK) {
    pCur->eof = 1;
    return rc;
  }

  // Move to first row
  rc = sqlite3_step(pCur->stmt);
  pCur->eof = (rc != SQLITE_ROW);

  return SQLITE_OK;
}

static int mcp_tools_next(sqlite3_vtab_cursor *cur){
  mcp_tools_cursor *pCur = (mcp_tools_cursor*)cur;

  if (pCur->stmt) {
    int rc = sqlite3_step(pCur->stmt);
    pCur->eof = (rc != SQLITE_ROW);
  } else {
    pCur->eof = 1;
  }

  return SQLITE_OK;
}

static int mcp_tools_eof(sqlite3_vtab_cursor *cur){
  mcp_tools_cursor *pCur = (mcp_tools_cursor*)cur;
  return pCur->eof;
}

static int mcp_tools_column(
  sqlite3_vtab_cursor *cur,
  sqlite3_context *ctx,
  int i
){
  mcp_tools_cursor *pCur = (mcp_tools_cursor*)cur;

  if (!pCur->stmt || pCur->eof) {
    sqlite3_result_null(ctx);
    return SQLITE_OK;
  }

  const unsigned char *text = sqlite3_column_text(pCur->stmt, i);
  if (text) {
    sqlite3_result_text(ctx, (const char*)text, -1, SQLITE_TRANSIENT);
  } else {
    sqlite3_result_null(ctx);
  }

  return SQLITE_OK;
}

static int mcp_tools_rowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid){
  *pRowid = 0;
  return SQLITE_OK;
}

static int mcp_tools_best_index(sqlite3_vtab *tab, sqlite3_index_info *pIdxInfo){
  pIdxInfo->estimatedCost = 1000.0;
  return SQLITE_OK;
}

static sqlite3_module mcp_tools_module = {
  0,                         /* iVersion */
  0,                         /* xCreate */
  mcp_tools_connect,         /* xConnect */
  mcp_tools_best_index,      /* xBestIndex */
  mcp_tools_disconnect,      /* xDisconnect */
  0,                         /* xDestroy */
  mcp_tools_open,            /* xOpen - open a cursor */
  mcp_tools_close,           /* xClose - close a cursor */
  mcp_tools_filter,          /* xFilter - configure scan constraints */
  mcp_tools_next,            /* xNext - advance a cursor */
  mcp_tools_eof,             /* xEof - check for end of scan */
  mcp_tools_column,          /* xColumn - read data */
  mcp_tools_rowid,           /* xRowid - read data */
  0,                         /* xUpdate */
  0,                         /* xBegin */
  0,                         /* xSync */
  0,                         /* xCommit */
  0,                         /* xRollback */
  0,                         /* xFindMethod */
  0,                         /* xRename */
  0,                         /* xSavepoint */
  0,                         /* xRelease */
  0,                         /* xRollbackTo */
  0,                         /* xShadowName */
  0,                         /* xIntegrity */
};

/*
** Virtual table for mcp_call_tool_respond (non-streaming, no caching)
** Returns parsed text results as rows from fresh tool calls
*/
typedef struct mcp_results_vtab {
  sqlite3_vtab base;
  sqlite3 *db;
  char *tool_name;    // Stored from function arguments
  char *arguments;    // Stored from function arguments
} mcp_results_vtab;

typedef struct mcp_results_cursor {
  sqlite3_vtab_cursor base;
  char *json_result;     // Stored JSON result from tool call (no caching)
  size_t content_count;  // Number of text items in result
  size_t current_index;  // Current row index
  int eof;
} mcp_results_cursor;

static int mcp_results_connect(
  sqlite3 *db,
  void *pAux,
  int argc, const char *const*argv,
  sqlite3_vtab **ppVtab,
  char **pzErr
){
  mcp_results_vtab *pNew = sqlite3_malloc(sizeof(*pNew));
  if (pNew==0) return SQLITE_NOMEM;
  memset(pNew, 0, sizeof(*pNew));

  pNew->db = db;

  DF("mcp_results_connect: argc=%d", argc);
  for (int i = 0; i < argc; i++) {
    DF("  argv[%d] = '%s'", i, argv[i] ? argv[i] : "NULL");
  }

  // For table-valued functions: SELECT * FROM mcp_call_tool_respond('tool', 'args')
  // argc will be 5: module_name, db_name, table_name, tool_name, arguments
  if (argc >= 5) {
    pNew->tool_name = sqlite3_mprintf("%s", argv[3]);
    pNew->arguments = sqlite3_mprintf("%s", argv[4]);
    DF("mcp_call_tool_respond table-valued function args: tool_name='%s', arguments='%s'", argv[3], argv[4]);
  } else {
    D("mcp_call_tool_respond: No function arguments, will expect constraints");
  }

  *ppVtab = (sqlite3_vtab*)pNew;

  return sqlite3_declare_vtab(db, "CREATE TABLE x(text TEXT, tool_name HIDDEN, arguments HIDDEN)");
}

static int mcp_results_disconnect(sqlite3_vtab *pVtab){
  mcp_results_vtab *p = (mcp_results_vtab*)pVtab;
  if (p->tool_name) sqlite3_free(p->tool_name);
  if (p->arguments) sqlite3_free(p->arguments);
  sqlite3_free(p);
  return SQLITE_OK;
}

static int mcp_results_open(sqlite3_vtab *pVtab, sqlite3_vtab_cursor **ppCursor){
  mcp_results_cursor *pCur = sqlite3_malloc(sizeof(*pCur));
  if (pCur==0) return SQLITE_NOMEM;
  memset(pCur, 0, sizeof(*pCur));
  pCur->eof = 1;
  *ppCursor = (sqlite3_vtab_cursor*)pCur;
  return SQLITE_OK;
}

static int mcp_results_close(sqlite3_vtab_cursor *cur){
  mcp_results_cursor *pCur = (mcp_results_cursor*)cur;
  if (pCur->json_result) {
    mcp_free_string(pCur->json_result);
  }
  sqlite3_free(pCur);
  return SQLITE_OK;
}

static int mcp_results_filter(
  sqlite3_vtab_cursor *pVtabCursor,
  int idxNum, const char *idxStr,
  int argc, sqlite3_value **argv
){
  mcp_results_cursor *pCur = (mcp_results_cursor*)pVtabCursor;
  mcp_results_vtab *pVtab = (mcp_results_vtab*)pVtabCursor->pVtab;

  // Free any previous result
  if (pCur->json_result) {
    mcp_free_string(pCur->json_result);
    pCur->json_result = NULL;
  }

  D("mcp_results_filter: called");
  DF("  argc=%d, idxNum=%d", argc, idxNum);
  for (int i = 0; i < argc; i++) {
    DF("    argv[%d] = '%s'", i, sqlite3_value_text(argv[i]) ? (const char*)sqlite3_value_text(argv[i]) : "NULL");
  }

  // For table-valued functions, arguments were stored in xConnect
  // If not available, try to get them from filter constraints
  const char *tool_name = pVtab->tool_name;
  const char *arguments = pVtab->arguments;

  if (!tool_name || !arguments) {
    D("mcp_results_filter: No arguments from xConnect, checking constraints");
    if (argc >= 2) {
      tool_name = (const char*)sqlite3_value_text(argv[0]);
      arguments = (const char*)sqlite3_value_text(argv[1]);
      DF("mcp_results_filter: Using constraint args: tool_name='%s', arguments='%s'",
         tool_name ? tool_name : "NULL", arguments ? arguments : "NULL");
    }
  }

  if (!tool_name || !arguments) {
    D("mcp_results_filter: Missing tool_name or arguments from both xConnect and constraints");
    pCur->eof = 1;
    return SQLITE_ERROR;
  }

  D("mcp_results_filter: Using arguments from xConnect");
  DF("  tool_name='%s', arguments='%s'", tool_name, arguments);

  // Call the tool - NO CACHING, fresh call every time
  pCur->json_result = mcp_call_tool_json(NULL, tool_name, arguments);
  if (!pCur->json_result) {
    D("  Tool call failed");
    pCur->eof = 1;
    return SQLITE_OK;
  }

  // Parse JSON in the Rust layer
  DF("  Tool result (%d bytes): %.200s%s", (int)strlen(pCur->json_result),
     pCur->json_result, strlen(pCur->json_result) > 200 ? "..." : "");
  pCur->content_count = mcp_parse_call_result_json(pCur->json_result);
  DF("  Parsed content count: %d", (int)pCur->content_count);

  if (pCur->content_count == 0) {
    D("  No content parsed");
    pCur->eof = 1;
    return SQLITE_OK;
  }

  // Start at first result - no temp table, no caching
  pCur->current_index = 0;
  pCur->eof = 0;

  return SQLITE_OK;
}

static int mcp_results_next(sqlite3_vtab_cursor *cur){
  mcp_results_cursor *pCur = (mcp_results_cursor*)cur;

  pCur->current_index++;
  if (pCur->current_index >= pCur->content_count) {
    pCur->eof = 1;
  }

  return SQLITE_OK;
}

static int mcp_results_eof(sqlite3_vtab_cursor *cur){
  mcp_results_cursor *pCur = (mcp_results_cursor*)cur;
  return pCur->eof;
}

static int mcp_results_column(
  sqlite3_vtab_cursor *cur,
  sqlite3_context *ctx,
  int i
){
  mcp_results_cursor *pCur = (mcp_results_cursor*)cur;

  if (i == 0) {
    // text column - extract directly from stored JSON (no caching)
    if (pCur->json_result && pCur->current_index < pCur->content_count) {
      char *text = mcp_get_call_result_text(pCur->json_result, pCur->current_index);
      if (text && strlen(text) > 0) {
        sqlite3_result_text(ctx, text, -1, SQLITE_TRANSIENT);
        mcp_free_string(text);
      } else {
        sqlite3_result_null(ctx);
      }
    } else {
      sqlite3_result_null(ctx);
    }
  }

  return SQLITE_OK;
}

static int mcp_results_rowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid){
  mcp_results_cursor *pCur = (mcp_results_cursor*)cur;
  *pRowid = pCur->current_index + 1;
  return SQLITE_OK;
}

static int mcp_results_best_index(sqlite3_vtab *tab, sqlite3_index_info *pIdxInfo){
  D("mcp_results_best_index: called");
  DF("  nConstraint=%d", pIdxInfo->nConstraint);
  
  int tool_name_idx = -1;
  int arguments_idx = -1;
  
  for (int i = 0; i < pIdxInfo->nConstraint; i++) {
    DF("    constraint[%d]: iColumn=%d, op=%d, usable=%d", 
       i, pIdxInfo->aConstraint[i].iColumn, 
       pIdxInfo->aConstraint[i].op, pIdxInfo->aConstraint[i].usable);
       
    if (pIdxInfo->aConstraint[i].usable && 
        pIdxInfo->aConstraint[i].op == SQLITE_INDEX_CONSTRAINT_EQ) {
      if (pIdxInfo->aConstraint[i].iColumn == 1) { // tool_name HIDDEN
        tool_name_idx = i;
      } else if (pIdxInfo->aConstraint[i].iColumn == 2) { // arguments HIDDEN
        arguments_idx = i;
      }
    }
  }
  
  if (tool_name_idx >= 0 && arguments_idx >= 0) {
    D("mcp_results_best_index: Found both tool_name and arguments constraints");
    // Tell SQLite to use these constraints
    pIdxInfo->aConstraintUsage[tool_name_idx].argvIndex = 1;
    pIdxInfo->aConstraintUsage[tool_name_idx].omit = 1;
    pIdxInfo->aConstraintUsage[arguments_idx].argvIndex = 2;
    pIdxInfo->aConstraintUsage[arguments_idx].omit = 1;
    
    pIdxInfo->idxNum = 1; // indicates we have function arguments
    pIdxInfo->estimatedCost = 100.0;
  } else {
    D("mcp_results_best_index: No function constraints found");
    pIdxInfo->idxNum = 0; // no constraints
    pIdxInfo->estimatedCost = 1000.0;
  }
  
  return SQLITE_OK;
}

static sqlite3_module mcp_results_module = {
  0,                         /* iVersion */
  0,                         /* xCreate */
  mcp_results_connect,       /* xConnect */
  mcp_results_best_index,    /* xBestIndex */
  mcp_results_disconnect,    /* xDisconnect */
  0,                         /* xDestroy */
  mcp_results_open,          /* xOpen */
  mcp_results_close,         /* xClose */
  mcp_results_filter,        /* xFilter */
  mcp_results_next,          /* xNext */
  mcp_results_eof,           /* xEof */
  mcp_results_column,        /* xColumn */
  mcp_results_rowid,         /* xRowid */
  0,                         /* xUpdate */
  0,                         /* xBegin */
  0,                         /* xSync */
  0,                         /* xCommit */
  0,                         /* xRollback */
  0,                         /* xFindMethod */
  0,                         /* xRename */
  0,                         /* xSavepoint */
  0,                         /* xRelease */
  0,                         /* xRollbackTo */
  0,                         /* xShadowName */
  0,                         /* xIntegrity */
};

/*
** Scalar functions for JSON output
*/
static void mcp_tools_json_func(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  char *result = mcp_list_tools_json(NULL);
  if (result) {
    mcp_result_text(context, result, -1);
    mcp_free_string(result);
  } else {
    sqlite3_result_error(context, "Failed to list tools", -1);
  }
}

static void mcp_call_tool_json_func(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  if (argc != 2) {
    sqlite3_result_error(context, "mcp_call_tool_json requires 2 arguments", -1);
    return;
  }

  const char *tool_name = (const char*)sqlite3_value_text(argv[0]);
  const char *arguments = (const char*)sqlite3_value_text(argv[1]);

  if (!tool_name || !arguments) {
    sqlite3_result_error(context, "mcp_call_tool_json requires tool_name and arguments_json", -1);
    return;
  }

  char *result = mcp_call_tool_json(NULL, tool_name, arguments);
  if (result) {
    mcp_result_text(context, result, -1);
    mcp_free_string(result);
  } else {
    sqlite3_result_error(context, "Failed to call tool", -1);
  }
}

#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_mcp_init(
  sqlite3 *db,
  char **pzErrMsg,
  const sqlite3_api_routines *pApi
){
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);

  if (mcp_init() != 0) {
    *pzErrMsg = sqlite3_mprintf("Failed to initialize MCP library");
    return SQLITE_ERROR;
  }

  rc = sqlite3_create_function(db, "mcp_version", 0,
                               SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                               0, mcp_version_func, 0, 0);
  if (rc != SQLITE_OK) return rc;

  rc = sqlite3_create_function(db, "mcp_connect", -1,
                               SQLITE_UTF8,
                               0, mcp_connect_func, 0, 0);
  if (rc != SQLITE_OK) return rc;

  // Scalar functions that return JSON strings
  rc = sqlite3_create_function(db, "mcp_list_tools_json", 0,
                               SQLITE_UTF8,
                               0, mcp_tools_json_func, 0, 0);
  if (rc != SQLITE_OK) return rc;

  rc = sqlite3_create_function(db, "mcp_call_tool_json", 2,
                               SQLITE_UTF8,
                               0, mcp_call_tool_json_func, 0, 0);
  if (rc != SQLITE_OK) return rc;

  // Virtual tables that return structured rows
  rc = sqlite3_create_module(db, "mcp_list_tools_respond", &mcp_tools_module, 0);
  if (rc != SQLITE_OK) return rc;

  rc = sqlite3_create_module(db, "mcp_call_tool_respond", &mcp_results_module, 0);
  if (rc != SQLITE_OK) return rc;

  // Streaming virtual tables
  rc = sqlite3_create_module(db, "mcp_list_tools", &mcp_stream_module, 0);
  if (rc != SQLITE_OK) return rc;

  rc = sqlite3_create_module(db, "mcp_call_tool", &mcp_call_tool_stream_module, 0);
  if (rc != SQLITE_OK) return rc;

  return rc;
}
