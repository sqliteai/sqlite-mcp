# sqlite_mcp

A SQLite extension that integrates the Model Context Protocol (MCP) Rust SDK, enabling SQLite databases to connect to MCP servers and call their tools.

## Installation

```
dart pub add sqlite_mcp
```

Requires Dart 3.10+ / Flutter 3.38+.

## Usage

### With `sqlite3`

```dart
import 'package:sqlite3/sqlite3.dart';
import 'package:sqlite_mcp/sqlite_mcp.dart';

void main() {
  // Load once at startup.
  sqlite3.loadSqliteMcpExtension();

  final db = sqlite3.openInMemory();

  // Check version.
  final result = db.select('SELECT mcp_version() AS version');
  print(result.first['version']);

  db.dispose();
}
```

### With `drift`

```dart
import 'package:sqlite3/sqlite3.dart';
import 'package:sqlite_mcp/sqlite_mcp.dart';
import 'package:drift/native.dart';

Sqlite3 loadExtensions() {
  sqlite3.loadSqliteMcpExtension();
  return sqlite3;
}

// Use when creating the database:
NativeDatabase.createInBackground(
  File(path),
  sqlite3: loadExtensions,
);
```

## Supported platforms

| Platform | Architectures |
|----------|---------------|
| Android  | arm64, arm, x64 |
| iOS      | arm64 (device + simulator) |
| macOS    | arm64, x64 |
| Linux    | arm64, x64 |
| Windows  | x64 |

## API

See the full [sqlite-mcp API documentation](https://github.com/sqliteai/sqlite-mcp/blob/main/API.md).

## License

See [LICENSE](LICENSE).
