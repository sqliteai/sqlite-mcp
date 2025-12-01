//
//  sqlite-mcp.h
//  sqlitemcp
//
//  Created by Gioele Cantoni on 05/11/25.
//

#ifndef __SQLITE_MCP__
#define __SQLITE_MCP__

#include <stdint.h>
#include <stdbool.h>

#ifndef SQLITE_CORE
#include "sqlite3ext.h"
#else
#include "sqlite3.h"
#endif

#ifdef _WIN32
  #define SQLITE_MCP_API __declspec(dllexport)
#else
  #define SQLITE_MCP_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SQLITE_MCP_VERSION "0.1.1"

/**
 * SQLite extension entry point
 *
 * @param db SQLite database connection
 * @param pzErrMsg Error message output
 * @param pApi SQLite API routines
 * @return SQLITE_OK on success, error code otherwise
 */
SQLITE_MCP_API int sqlite3_mcp_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi);

#ifdef __cplusplus
}
#endif

#endif /* __SQLITE_MCP__ */
