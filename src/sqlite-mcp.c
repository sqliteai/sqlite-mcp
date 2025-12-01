//
//  sqlite-mcp.c
//  sqlitemcp
//
//  Created by Gioele Cantoni on 05/11/25.
//

// Uncomment to enable debug output
//#define MCP_DEBUG 1

#ifdef MCP_DEBUG
  #define D(x) fprintf(stderr, "[DEBUG] " x "\n")
  #define DF(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", __VA_ARGS__)
#else
  #define D(x)
  #define DF(fmt, ...)
#endif

#include "sqlite-mcp.h"
#include "mcp_ffi.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

SQLITE_EXTENSION_INIT1

/* Global flag to track if JSON extension mode is enabled */
static int g_use_json_ext = 0;

/* Helper function to set result with optional JSON subtype */
static void mcp_result_text_json(sqlite3_context *context, const char *text, int len) {
  if (g_use_json_ext) {
    sqlite3_result_text(context, text, len, SQLITE_TRANSIENT);
  } else {
    /* Regular text result */
    sqlite3_result_text(context, text, len, SQLITE_TRANSIENT);
  }
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
** SQL function: mcp_connect(server_url, [legacy_sse], [headers_json], [use_json_ext])
** Connects to an MCP server with optional custom headers and JSON extension mode
**
** In JSON mode (use_json_ext=1): Returns no rows if connection succeeds, error otherwise
** In regular mode: Returns JSON string with connection status
*/
static void mcp_connect_func(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  if (argc < 1 || argc > 4) {
    sqlite3_result_error(context, "mcp_connect requires 1-4 arguments: (server_url, [legacy_sse], [headers_json], [use_json_ext])", -1);
    return;
  }

  const char *server_url = (const char*)sqlite3_value_text(argv[0]);
  if (!server_url) {
    sqlite3_result_error(context, "mcp_connect requires a URL", -1);
    return;
  }

  int legacy_sse = 0;
  if (argc >= 2) {
    legacy_sse = sqlite3_value_int(argv[1]);
  }

  const char *headers_json = NULL;
  if (argc >= 3 && sqlite3_value_type(argv[2]) != SQLITE_NULL) {
    headers_json = (const char*)sqlite3_value_text(argv[2]);
  }

  int use_json_ext = 0;
  if (argc >= 4) {
    use_json_ext = sqlite3_value_int(argv[3]);
  }

  g_use_json_ext = use_json_ext;

  char *result = mcp_connect(NULL, server_url, headers_json, legacy_sse);

  if (!result) {
    sqlite3_result_error(context, "Failed to connect to MCP server", -1);
    return;
  }

  // Check if connection was successful
  int is_connected = 0;

  if (g_use_json_ext) {
    // In JSON extension mode: Parse and check status using json_extract
    sqlite3 *db = sqlite3_context_db_handle(context);
    sqlite3_stmt *stmt;

    // Use sqlite3_mprintf for proper escaping
    char *query = sqlite3_mprintf("SELECT json_extract(%Q, '$.status')", result);
    if (!query) {
      sqlite3_result_error(context, "Out of memory", -1);
      mcp_free_string(result);
      return;
    }

    int rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    sqlite3_free(query);

    if (rc == SQLITE_OK) {
      rc = sqlite3_step(stmt);
      if (rc == SQLITE_ROW) {
        const unsigned char *status = sqlite3_column_text(stmt, 0);
        if (status && strcmp((const char*)status, "connected") == 0) {
          is_connected = 1;
        }
      }
      sqlite3_finalize(stmt);
    }
  } else {
    // In regular mode: Check if the JSON string contains "status":"connected" or "status": "connected"
    if (strstr(result, "\"status\"") != NULL && strstr(result, "\"connected\"") != NULL) {
      is_connected = 1;
    }
  }

  if (is_connected) {
    // Connected successfully
    if (g_use_json_ext) {
      // In JSON extension mode: Don't set any result (returns NULL)
      // This will cause sqlite3_step() to return SQLITE_ROW with NULL value
      mcp_free_string(result);
      return;
    } else {
      // In regular mode: Return the JSON result
      sqlite3_result_text(context, result, -1, SQLITE_TRANSIENT);
      mcp_free_string(result);
      return;
    }
  } else {
    // Connection failed - return error
    // This will cause sqlite3_step() to return SQLITE_ERROR
    sqlite3_result_error(context, result, -1);
    mcp_free_string(result);
  }
}

/*
** Virtual table for mcp_list_tools
** Returns parsed tool information as rows
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
    // Get tools list from MCP
    char *result = mcp_list_tools(NULL);
    if (!result) {
      pCur->eof = 1;
      return SQLITE_OK;
    }

    // Check for errors
    if (strstr(result, "\"error\"")) {
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

    // Parse JSON and insert into temp table
    char *escaped_result = sqlite3_mprintf("%Q", result);
    char *insert_sql = sqlite3_mprintf(
      "INSERT INTO temp.%s (name, title, description, inputSchema, outputSchema, annotations) "
      "SELECT "
      "  json_extract(value, '$.name'), "
      "  json_extract(value, '$.title'), "
      "  json_extract(value, '$.description'), "
      "  json_extract(value, '$.inputSchema'), "
      "  json_extract(value, '$.outputSchema'), "
      "  json_extract(value, '$.annotations') "
      "FROM json_each(%s, '$.tools')",
      pVtab->temp_table_name, escaped_result);

    rc = sqlite3_exec(pVtab->db, insert_sql, NULL, NULL, NULL);
    sqlite3_free(insert_sql);
    sqlite3_free(escaped_result);
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
};

/*
** Virtual table for mcp_call_tool
** Returns parsed text results as rows
*/
typedef struct mcp_results_vtab {
  sqlite3_vtab base;
  sqlite3 *db;
} mcp_results_vtab;

typedef struct mcp_results_cursor {
  sqlite3_vtab_cursor base;
  sqlite3_stmt *stmt;
  int eof;
  char *tool_name;
  char *arguments;
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

  pNew->db = db;
  *ppVtab = (sqlite3_vtab*)pNew;

  return sqlite3_declare_vtab(db, "CREATE TABLE x(text TEXT, tool_name HIDDEN, arguments HIDDEN)");
}

static int mcp_results_disconnect(sqlite3_vtab *pVtab){
  mcp_results_vtab *p = (mcp_results_vtab*)pVtab;
  sqlite3_free(p);
  return SQLITE_OK;
}

static int mcp_results_open(sqlite3_vtab *pVtab, sqlite3_vtab_cursor **ppCursor){
  mcp_results_cursor *pCur = sqlite3_malloc(sizeof(*pCur));
  if (pCur==0) return SQLITE_NOMEM;
  memset(pCur, 0, sizeof(*pCur));
  *ppCursor = (sqlite3_vtab_cursor*)pCur;
  return SQLITE_OK;
}

static int mcp_results_close(sqlite3_vtab_cursor *cur){
  mcp_results_cursor *pCur = (mcp_results_cursor*)cur;
  if (pCur->stmt) {
    sqlite3_finalize(pCur->stmt);
  }
  if (pCur->tool_name) {
    sqlite3_free(pCur->tool_name);
  }
  if (pCur->arguments) {
    sqlite3_free(pCur->arguments);
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

  if (pCur->stmt) {
    sqlite3_finalize(pCur->stmt);
    pCur->stmt = NULL;
  }

  // Need tool_name and arguments from WHERE clause
  if (argc < 2) {
    pCur->eof = 1;
    return SQLITE_OK;
  }

  const char *tool_name = (const char*)sqlite3_value_text(argv[0]);
  const char *arguments = (const char*)sqlite3_value_text(argv[1]);

  if (!tool_name || !arguments) {
    pCur->eof = 1;
    return SQLITE_OK;
  }

  // Call the tool
  char *result = mcp_call_tool(NULL, tool_name, arguments);
  if (!result) {
    pCur->eof = 1;
    return SQLITE_OK;
  }

  // Parse result.content array for type="text" items with proper escaping
  char *escaped_result = sqlite3_mprintf("%Q", result);
  char *query = sqlite3_mprintf(
    "SELECT json_extract(value, '$.text') "
    "FROM json_each(%s, '$.result.content') "
    "WHERE json_extract(value, '$.type') = 'text'", escaped_result);

  int rc = sqlite3_prepare_v2(pVtab->db, query, -1, &pCur->stmt, NULL);
  sqlite3_free(query);
  sqlite3_free(escaped_result);
  mcp_free_string(result);

  if (rc != SQLITE_OK) {
    pCur->eof = 1;
    return rc;
  }

  rc = sqlite3_step(pCur->stmt);
  pCur->eof = (rc != SQLITE_ROW);

  return SQLITE_OK;
}

static int mcp_results_next(sqlite3_vtab_cursor *cur){
  mcp_results_cursor *pCur = (mcp_results_cursor*)cur;

  if (pCur->stmt) {
    int rc = sqlite3_step(pCur->stmt);
    pCur->eof = (rc != SQLITE_ROW);
  } else {
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
    // text column
    if (!pCur->stmt || pCur->eof) {
      sqlite3_result_null(ctx);
      return SQLITE_OK;
    }

    const unsigned char *text = sqlite3_column_text(pCur->stmt, 0);
    if (text) {
      sqlite3_result_text(ctx, (const char*)text, -1, SQLITE_TRANSIENT);
    } else {
      sqlite3_result_null(ctx);
    }
  }

  return SQLITE_OK;
}

static int mcp_results_rowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid){
  *pRowid = 0;
  return SQLITE_OK;
}

static int mcp_results_best_index(sqlite3_vtab *tab, sqlite3_index_info *pIdxInfo){
  int hasToolName = 0;
  int hasArguments = 0;
  int toolNameIdx = -1;
  int argumentsIdx = -1;

  for(int i=0; i<pIdxInfo->nConstraint; i++){
    struct sqlite3_index_constraint *p = &pIdxInfo->aConstraint[i];
    if (p->usable && p->op == SQLITE_INDEX_CONSTRAINT_EQ) {
      if (p->iColumn == 1) { // tool_name
        hasToolName = 1;
        toolNameIdx = i;
      } else if (p->iColumn == 2) { // arguments
        hasArguments = 1;
        argumentsIdx = i;
      }
    }
  }

  if (hasToolName && hasArguments) {
    pIdxInfo->aConstraintUsage[toolNameIdx].argvIndex = 1;
    pIdxInfo->aConstraintUsage[toolNameIdx].omit = 1;
    pIdxInfo->aConstraintUsage[argumentsIdx].argvIndex = 2;
    pIdxInfo->aConstraintUsage[argumentsIdx].omit = 1;
    pIdxInfo->estimatedCost = 100.0;
    pIdxInfo->idxNum = 1;
  } else {
    pIdxInfo->estimatedCost = 1e99;
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
};

/*
** Scalar functions for JSON output
*/
static void mcp_tools_json_func(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  char *result = mcp_list_tools(NULL);
  if (result) {
    mcp_result_text_json(context, result, -1);
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

  char *result = mcp_call_tool(NULL, tool_name, arguments);
  if (result) {
    mcp_result_text_json(context, result, -1);
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
  rc = sqlite3_create_function(db, "mcp_tools_json", 0,
                               SQLITE_UTF8,
                               0, mcp_tools_json_func, 0, 0);
  if (rc != SQLITE_OK) return rc;

  rc = sqlite3_create_function(db, "mcp_call_tool_json", 2,
                               SQLITE_UTF8,
                               0, mcp_call_tool_json_func, 0, 0);
  if (rc != SQLITE_OK) return rc;

  // Backward compatibility aliases (same as _json functions)
  rc = sqlite3_create_function(db, "mcp_list_tools", 0,
                               SQLITE_UTF8,
                               0, mcp_tools_json_func, 0, 0);
  if (rc != SQLITE_OK) return rc;

  rc = sqlite3_create_function(db, "mcp_call_tool", 2,
                               SQLITE_UTF8,
                               0, mcp_call_tool_json_func, 0, 0);
  if (rc != SQLITE_OK) return rc;

  // Virtual tables that return structured rows
  rc = sqlite3_create_module(db, "mcp_tools_table", &mcp_tools_module, 0);
  if (rc != SQLITE_OK) return rc;

  rc = sqlite3_create_module(db, "mcp_call_tool_table", &mcp_results_module, 0);
  if (rc != SQLITE_OK) return rc;

  return rc;
}
