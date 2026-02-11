// Copyright (c) 2025 SQLite Cloud, Inc.
// Licensed under the Elastic License 2.0 (see LICENSE.md).

import 'dart:ffi';

import 'package:sqlite3/sqlite3.dart';

// @Native resolves from the code asset declared in hook/build.dart.
// The asset ID is 'package:sqlite_mcp/src/native/sqlite_mcp_extension.dart'.
@Native<Int Function(Pointer<Void>, Pointer<Void>, Pointer<Void>)>(
  assetId: 'package:sqlite_mcp/src/native/sqlite_mcp_extension.dart',
)
external int sqlite3_mcp_init(
  Pointer<Void> db,
  Pointer<Void> pzErrMsg,
  Pointer<Void> pApi,
);

extension SqliteMcpExtension on Sqlite3 {
  /// Loads the sqlite-mcp extension.
  ///
  /// Call once at app startup. All subsequently opened databases
  /// will have MCP functions available.
  ///
  /// Works with both `sqlite3` package and `drift` ORM.
  void loadSqliteMcpExtension() {
    ensureExtensionLoaded(
      SqliteExtension(
        Native.addressOf<
            NativeFunction<
                Int Function(Pointer<Void>, Pointer<Void>, Pointer<Void>)>>(
          sqlite3_mcp_init,
        ).cast(),
      ),
    );
  }
}
