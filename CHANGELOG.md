# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.1] - 2026-11-01

Re-release of 0.2.0, which never completed. Its npm step failed on a Sigstore
transparency-log conflict (`TLOG_CREATE_ENTRY_ERROR`, 409) after the Android
artifact had already been accepted by Maven Central, so `ai.sqlite:mcp:0.2.0`
exists there while npm, the git tag, the GitHub release and pub.dev were left at
0.1.9. The code is identical to 0.2.0; see its entry below for what changed.

## [0.2.0] - 2026-11-01

Superseded by 0.2.1: published to Maven Central only, never released elsewhere.


This release changes behaviour that callers may depend on. See **Changed** below
before upgrading.

### Security

- An MCP session is now bound to the SQLite connection that opened it. It used
  to be process-wide, so any other connection in the same process could list and
  call tools through it, using credentials it never had, and could tear it down
  with `mcp_disconnect()`. Sessions are released when the connection closes.
- `mcp_connect()`, `mcp_disconnect()`, `mcp_list_tools_json()`,
  `mcp_call_tool_json()` and the four virtual tables are now registered
  `SQLITE_DIRECTONLY` / `SQLITE_VTAB_DIRECTONLY`, so they can no longer be
  invoked from a VIEW, a TRIGGER or any other schema structure. Without this, a
  database file whose schema had been modified by someone else could make an
  application open a connection to a server of the attacker's choosing, and run
  remote tools, from an ordinary `SELECT` or `INSERT`.
- An invalid header value is no longer echoed back in the error message returned
  to SQL, since that value may be a credential.

### Changed

- `Authorization` is sent exactly as given. It used to be passed through the
  transport's bearer-token field, which prepends `Bearer ` itself, so the
  documented `{"Authorization": "Bearer <token>"}` went out as
  `Bearer Bearer <token>` and any other scheme, such as `Basic`, was corrupted
  the same way. Callers who worked around this by passing a bare token must now
  pass the full header value.
- A tool that fails at the application level (`isError: true` in its result) now
  raises a SQL error from `mcp_call_tool` and `mcp_call_tool_respond`, with the
  tool's own message prefixed `Tool reported an error:`. It used to be returned
  as ordinary rows, indistinguishable from a successful call.
  `mcp_call_tool_json()` is unchanged and still returns the raw result.
- The streaming virtual tables report a failure as a SQL error instead of
  returning an empty result set.

### Fixed

- `mcp_list_tools` and `mcp_call_tool` returned zero rows and no error when no
  session was open: the error was written to a channel whose receiver had already
  been dropped. They now report `Not connected. Call mcp_connect() first`.
- A timeout waiting for the server was treated as the end of the stream, so
  `mcp_list_tools` silently returned an incomplete or empty list for any server
  slower than 100 ms, and `mcp_call_tool` truncated results. A timeout is now
  distinct from the end of a stream, the budget is 60 s, and exhausting it is
  reported as an error.
- An error arriving as the first chunk of a `mcp_call_tool` stream was dropped,
  producing one row with a NULL text instead of an error. `xFilter` and `xNext`
  now share one code path.
- Stream ids are unique for the lifetime of the process, so a cursor outliving
  its session can no longer clean up another session's stream.
- `mcp_disconnect()` no longer creates a throwaway tokio runtime, and the
  `unwrap()` calls on that path, which aborted the host process under
  `panic = "abort"`, are gone.
- The Node package threw `ExtensionNotFoundError` for every ESM consumer: the ESM
  bundle has no bare `require`, so the lookup of the platform package always
  failed. It now resolves through `createRequire`.

### Removed

- `mcp_client_new()` and `mcp_client_free()`. The session handle is the SQLite
  connection, so these returned an unusable client.
- The `lazy_static` dependency, no longer used.

### Documentation

- Fixed four examples that could not work as written: three in `USAGE.md` and one
  in `README.md` passed `mcp_connect()` its arguments in the wrong order, so the
  transport flag was read as the headers JSON and the connection failed.
- Documented session scope, the schema restrictions above, and `isError`
  handling in `API.md`.

[0.2.1]: https://github.com/sqliteai/sqlite-mcp/releases/tag/0.2.1
[0.2.0]: https://github.com/sqliteai/sqlite-mcp/releases/tag/0.2.0
