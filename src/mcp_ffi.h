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

/* Opaque handle for MCP client */
typedef struct McpClient McpClient;

/**
 * Initialize the MCP library
 * Returns 0 on success, non-zero on error
 */
int32_t mcp_init(void);

/**
 * Get the version string of the MCP library
 * Returns a null-terminated string that must be freed with mcp_free_string
 */
char* mcp_get_version(void);

/**
 * Free a string allocated by the MCP library
 */
void mcp_free_string(char* s);

/**
 * Create a new MCP client
 * Returns NULL on error
 */
McpClient* mcp_client_new(void);

/**
 * Free an MCP client
 */
void mcp_client_free(McpClient* client);

/**
 * Connect to an MCP server with optional custom headers
 * client: MCP client pointer (can be NULL to use global client)
 * server_url: URL of the MCP server (e.g., "http://localhost:8931/mcp")
 * headers_json: Optional JSON string with custom headers (e.g., "{\"Authorization\": \"Bearer token\", \"X-MCP-Readonly\": \"true\"}"), can be NULL
 * legacy_sse: 1 to use SSE transport (legacy), 0 to use streamable HTTP transport (default)
 * Returns: JSON string with status (must be freed with mcp_free_string)
 */
char* mcp_connect(McpClient* client, const char* server_url, const char* headers_json, int32_t legacy_sse);

/**
 * Disconnect from MCP server and reset global client state
 * Returns: NULL on success
 */
char* mcp_disconnect(void);

#ifdef __cplusplus
}
#endif

#endif /* MCP_FFI_H */
