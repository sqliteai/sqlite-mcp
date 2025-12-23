# SQLite MCP Extension API Reference

## Overview

The SQLite MCP extension provides full integration with the Model Context Protocol (MCP), enabling SQLite databases to connect to MCP servers and call tools.

---

## SQL Functions

### `mcp_version()`

Returns the version of the MCP extension.

**Syntax:**
```sql
SELECT mcp_version();
```

**Returns:** `TEXT` - The version string (e.g., "0.1.0")

**Example:**
```sql
sqlite> SELECT mcp_version();
0.1.0
```

---

### `mcp_connect(server_url, [headers_json], [legacy_sse])`

Connects to an MCP server using either Streamable HTTP (default) or SSE transport, with optional custom HTTP headers.

**Syntax:**
```sql
SELECT mcp_connect(server_url);
SELECT mcp_connect(server_url, headers_json);
SELECT mcp_connect(server_url, headers_json, legacy_sse);
```

**Parameters:**
- `server_url` (TEXT) - URL of the MCP server (e.g., "http://localhost:8000/mcp")
- `headers_json` (TEXT, optional) - JSON string with custom HTTP headers (e.g., `{"Authorization": "Bearer token"}`) or NULL
- `legacy_sse` (INTEGER, optional) - 1 to use SSE transport (legacy), 0 for Streamable HTTP (default)

**Returns:**
- `NULL` on successful connection
- Error message string on failure

**Examples:**
```sql
-- Connect using Streamable HTTP (default)
SELECT mcp_connect('http://localhost:8000/mcp');

-- Connect using legacy SSE transport
SELECT mcp_connect('http://localhost:8931/sse', NULL, 1);

-- Connect with authorization header (GitHub Copilot)
SELECT mcp_connect(
  'https://api.githubcopilot.com/mcp/',
  '{"Authorization": "Bearer ghp_your_token", "X-MCP-Readonly": "true"}',
  0
);
```

See [USAGE.md](USAGE.md) for more examples of using custom headers.

---

### `mcp_list_tools_json()`

Lists all tools available on the connected MCP server with their complete signatures.

**Syntax:**
```sql
SELECT mcp_list_tools_json();
```

**Returns:** `TEXT` - JSON array of tool definitions including:
- Tool name
- Description
- Complete input schema with all parameters
- Required vs optional parameters

**Example:**
```sql
sqlite> SELECT mcp_connect('http://localhost:8000/mcp');
sqlite> SELECT mcp_list_tools_json();
```

**Response:**
```json
{
  "tools": [
    {
      "name": "airbnb_search",
      "description": "Search for Airbnb listings with various filters",
      "inputSchema": {
        "type": "object",
        "properties": {
          "location": {
            "type": "string",
            "description": "Location to search for"
          },
          "maxPrice": {
            "type": "number",
            "description": "Maximum price per night"
          },
          "adults": {
            "type": "number",
            "description": "Number of adults"
          }
        },
        "required": ["location"]
      }
    }
  ]
}
```

---

### `mcp_call_tool_json(tool_name, arguments_json)`

Calls a tool on the connected MCP server.

**Syntax:**
```sql
SELECT mcp_call_tool_json(tool_name, arguments_json);
```

**Parameters:**
- `tool_name` (TEXT) - Name of the tool to call
- `arguments_json` (TEXT) - JSON object containing tool arguments

**Returns:** `TEXT` - JSON response from the tool

**Example:**
```sql
SELECT mcp_call_tool_json(
  'airbnb_search',
  '{"location": "Rome", "maxPrice": 100, "adults": 2}'
);
```

**Response:**
```json
{
  "result": {
    "content": [
      {
        "type": "text",
        "text": "{\"listings\": [{\"title\": \"Cozy Apartment\", \"price\": 85}]}"
      }
    ]
  }
}
```

**Error Handling:**
```sql
-- Returns error if not connected
SELECT mcp_call_tool_json('test', '{}');
-- {"error": "Not connected. Call mcp_connect() first"}
```

---

## Virtual Tables

The extension provides virtual tables that automatically parse MCP responses into structured rows. These are ideal for SQL queries that need to process multiple tools or results.

### `mcp_list_tools_respond`

A virtual table that returns each tool as a row with structured columns.

**Syntax:**
```sql
SELECT * FROM mcp_list_tools_respond;
SELECT name, description FROM mcp_list_tools_respond;
SELECT * FROM mcp_list_tools_respond WHERE name LIKE 'airbnb%';
```

**Columns:**
- `name` (TEXT) - Unique identifier for the tool
- `title` (TEXT) - Optional human-readable name of the tool
- `description` (TEXT) - Human-readable description of functionality
- `inputSchema` (TEXT) - JSON Schema defining expected parameters
- `outputSchema` (TEXT) - Optional JSON Schema defining expected output structure
- `annotations` (TEXT) - Optional properties describing tool behavior

**Example:**
```sql
-- Connect to server
SELECT mcp_connect('http://localhost:8000/mcp');

-- Query tools as rows
SELECT name, description
FROM mcp_list_tools_respond
WHERE name LIKE 'airbnb%';
```

**Result:**
```
name              description
----------------  ------------------------------------------
airbnb_search     Search for Airbnb listings with filters
airbnb_details    Get details for a specific listing
```

---

### `mcp_call_tool_respond`

A virtual table that extracts text results from tool calls. Returns one row for each `type="text"` content item in the result.

**Syntax:**
```sql
SELECT text FROM mcp_call_tool_respond('<tool_name>', '<json_arguments>');
```

**Parameters:**
- `tool_name` (TEXT) - Name of tool to call (first function argument)
- `arguments` (TEXT) - JSON arguments for the tool (second function argument)

**Returns:**
- `text` (TEXT) - Text content from each result item

**Example:**
```sql
-- Call tool and get text results as rows
SELECT text FROM mcp_call_tool_respond(
  'airbnb_search',
  '{"location": "Rome", "maxPrice": 100}'
);
```

**Result:**
```
text
----------------------------------------------------
Found 5 listings in Rome under $100
Listing 1: Cozy Apartment - $85/night
Listing 2: Historic Studio - $95/night
```

**Important Notes:**
- Uses function-style syntax with positional parameters
- Automatically extracts only `type="text"` results from the MCP response
- Each text item in the response becomes a separate row

---

### `mcp_list_tools`

A streaming virtual table that returns tools as they are received from the server. Provides real-time access to tool listings.

**Syntax:**
```sql
SELECT name, description FROM mcp_list_tools;
```

**Columns:**
- `name` (TEXT) - Unique identifier for the tool
- `description` (TEXT) - Human-readable description of functionality

**Example:**
```sql
-- Connect to server
SELECT mcp_connect('http://localhost:8000/mcp');

-- Stream tools as they arrive
SELECT name, description FROM mcp_list_tools;
```

**Result:**
```
name              description
----------------  ------------------------------------------
airbnb_search     Search for Airbnb listings with filters
airbnb_details    Get details for a specific listing
```

**When to use:**
- Real-time tool discovery
- Large tool catalogs where latency matters
- Interactive applications requiring immediate feedback

**Comparison with `mcp_list_tools_respond`:**
- `mcp_list_tools`: Streaming, delivers results immediately
- `mcp_list_tools_respond`: Buffered, waits for complete response

---

### `mcp_call_tool`

A streaming virtual table that returns tool results in real-time as text chunks arrive. Ideal for long-running operations where you need immediate feedback.

**Syntax:**
```sql
SELECT text FROM mcp_call_tool('<tool_name>', '<json_arguments>');
```

**Parameters:**
- `tool_name` (TEXT) - Name of tool to call (first function argument)
- `arguments` (TEXT) - JSON arguments for the tool (second function argument)

**Returns:**
- `text` (TEXT) - Text content streamed as it arrives

**Example:**
```sql
-- Stream results from a tool call
SELECT text FROM mcp_call_tool(
  'browser_navigate',
  '{"url": "https://sqlite.ai"}'
);

-- Stream search results
SELECT text FROM mcp_call_tool(
  'airbnb_search',
  '{"location": "Rome", "maxPrice": 100}'
);
```

**Result:**
```
text
----------------------------------------------------
Found 5 listings in Rome under $100
Listing 1: Cozy Apartment - $85/night
Listing 2: Historic Studio - $95/night
```

**When to use:**
- Long-running tool operations (web scraping, large data processing)
- Real-time feedback needed (progress updates, partial results)
- Memory-efficient processing of large responses
- Interactive applications requiring immediate output

**Comparison with `mcp_call_tool_respond`:**
| Feature | `mcp_call_tool` | `mcp_call_tool_respond` |
|---------|----------------------|---------------------|
| Response delivery | Real-time chunks | Complete response |
| Memory usage | Low (streaming) | Higher (full result) |
| Use case | Long operations | Quick queries |
| Latency | Immediate first chunk | Wait for completion |

---

## Function Variants

The extension provides multiple ways to access MCP functionality:

### Scalar Functions (Return JSON strings)

- `mcp_list_tools_json()` - Returns JSON string of all tools
- `mcp_call_tool_json(tool_name, arguments)` - Returns JSON string of tool result

**Behavior:**
- Returns the complete JSON response from MCP as plain text
- Use these when you need the raw JSON response

Use these when you need the raw JSON response or want to process with `json_extract()`.

### Virtual Tables (Return structured rows)

**Non-Streaming Tables:**
- `mcp_list_tools_respond` - Returns tools as rows with named columns
- `mcp_call_tool_respond(tool_name, arguments)` - Returns text results as rows

**Streaming Tables:**
- `mcp_list_tools` - Streams tools as they arrive from server  
- `mcp_call_tool(tool_name, arguments)` - Streams text results in real-time

**Behavior:**
- Always extract and parse the JSON response
- Streaming tables deliver results immediately as they arrive, non-streaming tables wait for complete response

Use non-streaming tables for quick queries and when you need the complete response. Use streaming tables for long-running operations, real-time feedback, or memory-efficient processing.

**Example comparing approaches:**

```sql
-- Scalar function approach
SELECT json_extract(value, '$.name')
FROM json_each((SELECT mcp_list_tools_json()), '$.tools')
WHERE json_extract(value, '$.name') LIKE 'airbnb%';

-- Virtual table approach (simpler)
SELECT name
FROM mcp_list_tools_respond
WHERE name LIKE 'airbnb%';
```

---

## Transport Protocols

The extension supports two MCP transport protocols:

### Streamable HTTP (Default)
Modern streaming HTTP transport for MCP servers.

```sql
SELECT mcp_connect('http://localhost:8000/mcp');
SELECT mcp_connect('http://localhost:8000/mcp', NULL, 0);  -- Explicit
```

### SSE (Legacy)
Server-Sent Events transport for compatibility with older MCP servers.

```sql
SELECT mcp_connect('http://localhost:8931/sse', NULL, 1);
```

---

## Error Handling

The sqlite-mcp extension has consistent error handling across all interfaces:

1. **JSON Functions** (ending with `_json`): Return JSON with error information
2. **Non-JSON Functions**: Return error strings directly (extracted from JSON)
3. **Virtual Tables**: Return SQL errors with extracted error messages (like non-JSON functions)

### mcp_connect()

Returns `NULL` on success, or an error string on failure:

```sql
-- Check if connection succeeded
SELECT mcp_connect('http://localhost:8000/mcp') IS NULL;
-- Returns 1 (true) on success, 0 (false) on failure

-- Get error message if connection failed
SELECT mcp_connect('http://invalid:8000/mcp');
-- Returns: "Failed to connect to MCP server: ..."
```

### JSON Functions: mcp_list_tools_json() and mcp_call_tool_json()

Always return JSON - either with results or error information:

```json
{"error": "Not connected. Call mcp_connect() first"}
{"error": "Tool not found: invalid_tool"}
{"error": "Invalid JSON arguments"}
```

Use SQLite's `json_extract()` to handle errors:

```sql
SELECT
  CASE
    WHEN json_extract(result, '$.error') IS NOT NULL
    THEN 'Error: ' || json_extract(result, '$.error')
    ELSE 'Success'
  END
FROM (SELECT mcp_call_tool_json('test', '{}') as result);
```

### Virtual Tables: mcp_list_tools, mcp_call_tool, mcp_list_tools_respond, mcp_call_tool_respond

Virtual tables return SQL errors with extracted error messages (similar to `mcp_connect()`):

```sql
-- Query virtual table - errors are automatically returned as SQL errors
SELECT name, description FROM mcp_list_tools;
-- If not connected, returns SQL error: "Not connected. Call mcp_connect() first"

-- Query call tool virtual table
SELECT text FROM mcp_call_tool('nonexistent_tool', '{}');
-- Returns SQL error: "Tool not found: nonexistent_tool"

-- Errors can be caught in application code using sqlite3_errmsg()
-- Or handled in SQL with error handlers
```

**Error Behavior:**
- When an MCP error occurs (not connected, tool not found, invalid JSON, etc.), the virtual table returns `SQLITE_ERROR`
- The error message is extracted from the JSON error response and set as the SQL error message
- This provides immediate, clear feedback without needing to check JSON functions separately

### Common Error Messages

All functions may return these error types:

- **Connection errors**: `"Not connected. Call mcp_connect() first"`
- **Server errors**: `"Failed to connect to MCP server: ..."`
- **Tool errors**: `"Tool not found: tool_name"`
- **Argument errors**: `"Invalid JSON arguments"`
- **Transport errors**: `"Transport error: ..."`
- **Timeout errors**: `"Request timeout"`

### Error Handling Best Practices

1. **Always check mcp_connect() result**:
```sql
-- Good practice: Check connection first
SELECT
  CASE
    WHEN mcp_connect('http://localhost:8931/mcp') IS NULL
    THEN 'Connected successfully'
    ELSE mcp_connect('http://localhost:8931/mcp')
  END;
```

2. **Virtual tables automatically return errors**:
```sql
-- Virtual tables automatically return SQL errors - no need to pre-check
SELECT name, description FROM mcp_list_tools;
-- If not connected, query fails with error: "Not connected. Call mcp_connect() first"

-- Errors can be caught in application code:
-- C: sqlite3_errmsg(db)
-- Python: except sqlite3.Error as e
-- Node.js: try/catch with better-sqlite3
```

3. **Handle JSON function errors manually**:
```sql
-- JSON functions still return JSON with error field
SELECT
  CASE
    WHEN json_extract(result, '$.error') IS NOT NULL
    THEN 'Tool Error: ' || json_extract(result, '$.error')
    ELSE json_extract(result, '$.content[0].text')
  END as output
FROM (
  SELECT mcp_call_tool_json('browser_navigate', '{"url": "https://example.com"}') as result
);
```