//
//  mcp_ffi.h
//  sqlitemcp
//
//  Created by Gioele Cantoni on 05/11/25.
//

#ifndef MCP_FFI_H
#define MCP_FFI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * Initialize the MCP library
 * Returns 0 on success, non-zero on error
 */
int32_t mcp_init(void);

/**
 * Free a string allocated by the MCP library
 */
void mcp_free_string(char* s);

/**
 * Connect to an MCP server with optional custom headers
 *
 * Every session is bound to one SQLite connection: another connection in the
 * same process cannot use it, and closing the connection releases it.
 *
 * db: the SQLite connection the session belongs to (sqlite3*)
 * server_url: URL of the MCP server (e.g., "http://localhost:8931/mcp")
 * headers_json: Optional JSON string with custom headers (e.g., "{\"Authorization\": \"Bearer token\", \"X-MCP-Readonly\": \"true\"}"), can be NULL
 * legacy_sse: 1 to use SSE transport (legacy), 0 to use streamable HTTP transport (default)
 * Returns: NULL on success, error string on failure (must be freed with mcp_free_string)
 */
char* mcp_connect(void* db, const char* server_url, const char* headers_json, int32_t legacy_sse);

/**
 * Disconnect one SQLite connection from its MCP server
 * Returns: NULL on success
 */
char* mcp_disconnect(void* db);

/**
 * Release everything bound to a SQLite connection that is going away.
 * Registered as the destructor of an application-defined function, so SQLite
 * calls it when the connection is closed.
 */
void mcp_conn_close(void* db);

#ifdef __cplusplus
}
#endif

#endif /* MCP_FFI_H */
