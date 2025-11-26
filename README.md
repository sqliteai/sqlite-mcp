# SQLite MCP

A SQLite extension that integrates the [Model Context Protocol (MCP)](https://modelcontextprotocol.io/) Rust SDK, enabling SQLite databases to connect to MCP servers and call their tools.

## 🚀 Quick Start

### Installation

#### Pre-built Binaries

Download for your platform: **macOS**, **Linux**, **Windows**, **Android**, and **iOS**

Or build from source (see [Building from Source](#building-from-source)).

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
SELECT mcp_list_tools();
-- Returns JSON with tool schemas

-- Call a tool
SELECT mcp_call_tool('airbnb_search', '{"location": "Rome", "maxPrice": 100}');
-- Returns search results
```

## 📖 API Reference

### Available Functions

| Function | Description |
|----------|-------------|
| `mcp_version()` | Returns extension version |
| `mcp_connect(url, [sse], [headers])` | Connect to MCP server with optional custom headers |
| `mcp_list_tools()` | List available tools with schemas |
| `mcp_call_tool(name, args)` | Call a tool on the MCP server |

See [API.md](API.md) for complete API documentation with examples.

## 🏗️ Building from Source

### Requirements

- **Rust**: Nightly toolchain (`rustup default nightly`)
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

## 🚦 Usage with Different Languages

### Python

```python
import sqlite3

conn = sqlite3.connect('data.db')
conn.enable_load_extension(True)
conn.load_extension('./dist/mcp')

cursor = conn.cursor()

# Connect to MCP server
cursor.execute("SELECT mcp_connect('http://localhost:8000/mcp')")

# List tools
result = cursor.execute("SELECT mcp_list_tools()").fetchone()[0]
tools = json.loads(result)
print(f"Available tools: {len(tools['tools'])}")

# Call a tool
cursor.execute("""
    SELECT mcp_call_tool('airbnb_search',
                         '{"location": "Paris", "maxPrice": 150}')
""")
result = cursor.fetchone()[0]
print(result)
```

### Node.js

```javascript
const Database = require('better-sqlite3');
const db = new Database(':memory:');

db.loadExtension('./dist/mcp');

// Connect to MCP server
const result = db.prepare("SELECT mcp_connect('http://localhost:8000/mcp')").get();
console.log(result);

// Call tool
const listings = db.prepare(`
    SELECT mcp_call_tool('airbnb_search',
                         '{"location": "Tokyo", "adults": 2}')
`).get();
console.log(listings);
```

### C

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
        "SELECT mcp_call_tool('airbnb_search', '{\"location\": \"NYC\"}')",
        -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("Result: %s\n", sqlite3_column_text(stmt, 0));
    }
    sqlite3_finalize(stmt);

    sqlite3_close(db);
    return 0;
}
```

## 🤝 Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## 📄 License

MIT License - See [LICENSE](LICENSE) for details

## 🔗 Related Projects

- [sqlite-agent](https://github.com/sqlitecloud/sqlite-agent) - A powerful AI agent for SQLite
- [sqlite-ai](https://github.com/sqlitecloud/sqlite-ai) - LLM integration for SQLite
- [sqlite-vector](https://github.com/sqlitecloud/sqlite-vector) - Vector search extension
- [sqlite-sync](https://github.com/sqlitecloud/sqlite-sync) - Cloud synchronization
- [sqlite-js](https://github.com/sqlitecloud/sqlite-js) - JavaScript engine integration
