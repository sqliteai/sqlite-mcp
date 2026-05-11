<div align="center">
  <a href="https://sqlite.ai">
    <img src="https://www.sqlite.ai/social/logo-ai.png" alt="SQLite AI" height="56">
  </a>

  <h1>SQLite-MCP</h1>
  <p><strong>Call MCP tools directly from SQL.</strong><br>
  Integrates the Model Context Protocol into SQLite — connect to any MCP server and invoke its tools from inside a query. Bridge between your database and the agent ecosystem.</p>

  <p>
    <a href="https://dashboard.sqlitecloud.io/auth/sign-in"><strong>Free managed instance →</strong></a> ·
    <a href="https://docs.sqlitecloud.io/docs/ai-overview">Docs</a> ·
    <a href="https://sqlite.ai">Website</a> ·
    <a href="https://blog.sqlite.ai">Blog</a>
  </p>

  <p>
    <sub><strong>Data:</strong>
    <a href="https://github.com/sqliteai/sqlite-vector">Vector</a> ·
    <a href="https://github.com/sqliteai/sqlite-sync">Sync</a> ·
    <a href="https://github.com/sqliteai/sqlite-columnar">Columnar</a> ·
    <a href="https://github.com/sqliteai/sqlite-js">JS</a>
    <br>
    <strong>AI:</strong>
    <a href="https://github.com/sqliteai/sqlite-ai">AI</a> ·
    <a href="https://github.com/sqliteai/sqlite-agent">Agent</a> ·
    <a href="https://github.com/sqliteai/sqlite-memory">Memory</a> ·
    <a href="https://github.com/sqliteai/sqlite-mcp">MCP</a>
    </sub>
  </p>
</div>

<br>

> **Want managed MCP infrastructure?** SQLite-MCP connects to any MCP server locally; **[SQLite Cloud](https://dashboard.sqlitecloud.io/auth/sign-in)** includes a built-in MCP server, edge functions, and auth — so you can expose your data as an MCP target. Free tier available.

---

# SQLite MCP

A SQLite extension that integrates the [Model Context Protocol (MCP)](https://modelcontextprotocol.io/) Rust SDK, enabling SQLite databases to connect to MCP servers and call their tools.

## 🚀 Quick Start

### Installation

#### Pre-built Binaries

Download for your platform: **macOS**, **Linux**, **Windows**, **Android**, and **iOS**.

#### Flutter Package

Add the [sqlite_mcp](https://pub.dev/packages/sqlite_mcp) package to your project:

```bash
flutter pub add sqlite_mcp  # Flutter projects
dart pub add sqlite_mcp     # Dart projects
```

Usage with `sqlite3` package:
```dart
import 'package:sqlite3/sqlite3.dart';
import 'package:sqlite_mcp/sqlite_mcp.dart';

sqlite3.loadSqliteMcpExtension();
final db = sqlite3.openInMemory();
print(db.select('SELECT mcp_version()'));
```

For a complete example, see the [Flutter example](https://github.com/sqliteai/sqlite-extensions-guide/blob/main/examples/flutter/README.md).

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

---

## 📄 License

MIT License - See [LICENSE](LICENSE) for details

---

## ☁️ Hosted version

Want to expose your database as an MCP target? **[SQLite Cloud](https://sqlite.ai)** includes a built-in MCP server, REST APIs, auth, and edge functions — so any MCP-compatible agent (Claude, Cursor, custom clients) can read and write your data securely.

[**Start free →**](https://dashboard.sqlitecloud.io/auth/sign-in)

---

## Part of the SQLite AI stack

SQLite-MCP is one piece of a larger ecosystem that turns SQLite into a runtime for intelligent, distributed data:

**Data layer**
- [sqlite-vector](https://github.com/sqliteai/sqlite-vector) — ANN vector search inside SQLite
- [sqlite-sync](https://github.com/sqliteai/sqlite-sync) — Offline-first CRDT sync across devices
- [sqlite-columnar](https://github.com/sqliteai/sqlite-columnar) — Column-oriented analytics for OLAP queries
- [sqlite-js](https://github.com/sqliteai/sqlite-js) — Custom SQLite functions written in JavaScript

**AI layer**
- [sqlite-ai](https://github.com/sqliteai/sqlite-ai) — On-device LLM inference and embeddings
- [sqlite-agent](https://github.com/sqliteai/sqlite-agent) — Autonomous AI agents running inside SQLite
- [sqlite-memory](https://github.com/sqliteai/sqlite-memory) — Persistent, searchable memory for agents
- [**sqlite-mcp**](https://github.com/sqliteai/sqlite-mcp) — Call MCP tools directly from SQL queries *(you are here)*

**Managed platform**
- [SQLite Cloud](https://sqlite.ai) — Hosted SQLite with sync, auth, edge functions, and analytics. [Free tier →](https://dashboard.sqlitecloud.io/auth/sign-in)

Built by [SQLite AI](https://sqlite.ai). Questions? [Open a discussion](https://github.com/sqliteai/sqlite-mcp/discussions) or [contact us](https://sqlite.ai/support).

