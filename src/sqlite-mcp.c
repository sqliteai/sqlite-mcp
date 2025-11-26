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
** SQL function: mcp_connect(server_url, [legacy_sse], [headers_json])
** Connects to an MCP server with optional custom headers
**
** Arguments:
**   server_url: URL of the MCP server (required)
**   legacy_sse: 0 for streamable HTTP (default), 1 for SSE transport (optional)
**   headers_json: JSON string with custom headers (optional)
**                 e.g., '{"Authorization": "Bearer token", "X-MCP-Readonly": "true"}'
**
** Examples:
**   SELECT mcp_connect('http://localhost:8000/mcp');
**   SELECT mcp_connect('http://localhost:8000/mcp', 0);
**   SELECT mcp_connect('http://localhost:8000/mcp', 0, '{"Authorization": "Bearer token"}');
**   SELECT mcp_connect('http://localhost:8000/mcp', 0, '{"Authorization": "Bearer token", "X-MCP-Readonly": "true"}');
*/
static void mcp_connect_func(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  if (argc < 1 || argc > 3) {
    sqlite3_result_error(context, "mcp_connect requires 1-3 arguments: (server_url, [legacy_sse], [headers_json])", -1);
    return;
  }

  const char *server_url = (const char*)sqlite3_value_text(argv[0]);
  if (!server_url) {
    sqlite3_result_error(context, "mcp_connect requires a URL", -1);
    return;
  }

  // Get legacy_sse parameter (default to 0 = streamable HTTP)
  int legacy_sse = 0;
  if (argc >= 2) {
    legacy_sse = sqlite3_value_int(argv[1]);
  }

  // Get optional headers_json parameter
  const char *headers_json = NULL;
  if (argc >= 3 && sqlite3_value_type(argv[2]) != SQLITE_NULL) {
    headers_json = (const char*)sqlite3_value_text(argv[2]);
  }

  // Call Rust FFI function with optional custom headers
  char *result = mcp_connect(NULL, server_url, headers_json, legacy_sse);

  if (result) {
    sqlite3_result_text(context, result, -1, SQLITE_TRANSIENT);
    mcp_free_string(result);
  } else {
    sqlite3_result_error(context, "Failed to connect to MCP server", -1);
  }
}

/*
** SQL function: mcp_list_tools()
** Lists available tools from connected MCP server
*/
static void mcp_list_tools_func(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  // Pass NULL as client - Rust FFI uses global GLOBAL_CLIENT
  char *result = mcp_list_tools(NULL);
  if (result) {
    sqlite3_result_text(context, result, -1, SQLITE_TRANSIENT);
    mcp_free_string(result);
  } else {
    sqlite3_result_error(context, "Failed to list tools", -1);
  }
}

/*
** SQL function: mcp_call_tool(tool_name, arguments_json)
** Calls a tool on the connected MCP server
*/
static void mcp_call_tool_func(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  if (argc != 2) {
    sqlite3_result_error(context, "mcp_call_tool requires 2 arguments", -1);
    return;
  }

  const char *tool_name = (const char*)sqlite3_value_text(argv[0]);
  const char *arguments = (const char*)sqlite3_value_text(argv[1]);

  if (!tool_name || !arguments) {
    sqlite3_result_error(context, "mcp_call_tool requires tool_name and arguments_json", -1);
    return;
  }

  // Pass NULL as client - Rust FFI uses global GLOBAL_CLIENT
  char *result = mcp_call_tool(NULL, tool_name, arguments);
  if (result) {
    sqlite3_result_text(context, result, -1, SQLITE_TRANSIENT);
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

  // MCP client functions
  rc = sqlite3_create_function(db, "mcp_connect", -1,
                               SQLITE_UTF8,
                               0, mcp_connect_func, 0, 0);
  if (rc != SQLITE_OK) return rc;

  rc = sqlite3_create_function(db, "mcp_list_tools", 0,
                               SQLITE_UTF8,
                               0, mcp_list_tools_func, 0, 0);
  if (rc != SQLITE_OK) return rc;

  rc = sqlite3_create_function(db, "mcp_call_tool", 2,
                               SQLITE_UTF8,
                               0, mcp_call_tool_func, 0, 0);
  if (rc != SQLITE_OK) return rc;

  return rc;
}
