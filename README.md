# SQLite MCP

A SQLite extension that integrates the [Model Context Protocol (MCP)](https://modelcontextprotocol.io/) Rust SDK, enabling SQLite databases to connect to MCP servers and call their tools.

## 🚀 Quick Start

### Installation

#### Pre-built Binaries

Download for your platform: **macOS**, **Linux**, **Windows**, **Android**, and **iOS**.

### Basic Usage

```sql
-- Load the extension
.load ./dist/mcp.dylib

-- Check version
SELECT mcp_version();
-- 0.1.0

-- Connect to an MCP server
SELECT mcp_connect('http://localhost:8000/mcp');
-- {"status": "connected", "transport": "streamable_http"}

-- List available tools
SELECT mcp_list_tools_json();
-- Returns JSON with tool schemas

-- Call a tool
SELECT mcp_call_tool_json('airbnb_search', '{"location": "Rome", "maxPrice": 100}');
-- Returns search results
```

## 📖 API Reference

### Available Functions

| Function | Description |
|----------|-------------|
| `mcp_version()` | Returns extension version |
| `mcp_connect(url, [headers], [sse])` | Connect to MCP server with optional custom headers |
| `mcp_list_tools_json()` | List available tools with schemas |
| `mcp_call_tool_json(name, args)` | Call a tool on the MCP server |
| `mcp_list_tools_respond` | Virtual table (cached) that returns each tool as a row with structured columns |
| `mcp_call_tool_respond(name, args)` | Virtual table that extracts text results from tool calls |
| `mcp_list_tools` | Streaming virtual table that returns tools as they arrive |
| `mcp_call_tool(name, args)` | Streaming virtual table for real-time tool results |

See [API.md](API.md) for complete API documentation with examples.

## 🏗️ Building from Source

### Requirements

- **Rust**: 1.85+ toolchain
- **C Compiler**: gcc or clang
- **Make**: GNU Make

### Build Instructions

```bash
# Clone the repository
git clone https://github.com/sqliteai/sqlite-mcp.git
cd sqlite-mcp

# Initialize submodules
git submodule update --init --recursive

# Build the extension
make
```

The compiled extension will be in `dist/mcp.{dylib,so,dll}`.

## 🔧 Transport Protocols

The extension supports two MCP transport protocols:

### Streamable HTTP (Default)
Modern streaming HTTP transport for MCP servers.
```sql
SELECT mcp_connect('http://localhost:8000/mcp');
```

### SSE (Legacy)
Server-Sent Events for compatibility with older servers.
```sql
SELECT mcp_connect('http://localhost:8931/sse', 1);
```

## 🚦 Quick Usage Example

```c
#include <sqlite3.h>
#include <stdio.h>

int main() {
    sqlite3 *db;
    sqlite3_open(":memory:", &db);
    sqlite3_enable_load_extension(db, 1);
    sqlite3_load_extension(db, "./dist/mcp", NULL, NULL);

    sqlite3_stmt *stmt;

    // Connect to MCP
    sqlite3_prepare_v2(db,
        "SELECT mcp_connect('http://localhost:8000/mcp')",
        -1, &stmt, NULL);
    sqlite3_step(stmt);
    printf("Connected: %s\n", sqlite3_column_text(stmt, 0));
    sqlite3_finalize(stmt);

    // Call tool
    sqlite3_prepare_v2(db,
        "SELECT mcp_call_tool_json('airbnb_search', '{\"location\": \"NYC\"}')",
        -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("Result: %s\n", sqlite3_column_text(stmt, 0));
    }
    sqlite3_finalize(stmt);

    sqlite3_close(db);
    return 0;
}
```

See [USAGE.md](USAGE.md) for complete usage examples.

## 🤝 Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## 📄 License

MIT License - See [LICENSE](LICENSE) for details

## 🔗 Related Projects

- [sqlite-agent](https://github.com/sqliteai/sqlite-agent) - A powerful AI agent for SQLite
- [sqlite-ai](https://github.com/sqliteai/sqlite-ai) - LLM integration for SQLite
- [sqlite-vector](https://github.com/sqliteai/sqlite-vector) - Vector search extension
- [sqlite-sync](https://github.com/sqliteai/sqlite-sync) - Cloud synchronization
- [sqlite-js](https://github.com/sqliteai/sqlite-js) - JavaScript engine integration
