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

### `mcp_connect(server_url, [legacy_sse], [headers_json], [use_json_ext])`

Connects to an MCP server using either Streamable HTTP (default) or SSE transport, with optional custom HTTP headers and JSON extension mode.

**Syntax:**
```sql
SELECT mcp_connect(server_url);
SELECT mcp_connect(server_url, legacy_sse);
SELECT mcp_connect(server_url, legacy_sse, headers_json);
SELECT mcp_connect(server_url, legacy_sse, headers_json, use_json_ext);
```

**Parameters:**
- `server_url` (TEXT) - URL of the MCP server (e.g., "http://localhost:8000/mcp")
- `legacy_sse` (INTEGER, optional) - 1 to use SSE transport (legacy), 0 for Streamable HTTP (default)
- `headers_json` (TEXT, optional) - JSON string with custom HTTP headers (e.g., `{"Authorization": "Bearer token"}`)
- `use_json_ext` (INTEGER, optional) - 1 to enable JSON extension mode (returns results as JSON type), 0 for regular text mode (default)

**Returns:**

SQLite return codes from `sqlite3_step()`:
- **On success**: `SQLITE_ROW`
  - Regular mode (`use_json_ext=0`): Row contains JSON object with connection status
  - JSON mode (`use_json_ext=1`): Row contains NULL (silent success)
- **On failure**: `SQLITE_ERROR` - Error message set via `sqlite3_result_error()`

**Note:** SQLite scalar functions always return exactly one row on success. They cannot return `SQLITE_DONE` (zero rows).

**Examples:**
```sql
-- Connect using Streamable HTTP (default)
SELECT mcp_connect('http://localhost:8000/mcp');

-- Connect using legacy SSE transport
SELECT mcp_connect('http://localhost:8931/sse', 1);

-- Connect with authorization header (GitHub Copilot)
SELECT mcp_connect(
  'https://api.githubcopilot.com/mcp/',
  0,
  '{"Authorization": "Bearer ghp_your_token", "X-MCP-Readonly": "true"}'
);

-- Connect with JSON extension mode enabled
SELECT mcp_connect('http://localhost:8000/mcp', 0, NULL, 1);

-- Connect with both custom headers and JSON extension mode
SELECT mcp_connect(
  'http://localhost:8000/mcp',
  0,
  '{"Authorization": "Bearer token"}',
  1
);
```

**Response:**
```json
{"status": "connected", "transport": "streamable_http"}
```

See [USAGE.md](USAGE.md) for more examples of using custom headers.

---

### `mcp_list_tools()`

Lists all tools available on the connected MCP server with their complete signatures.

**Syntax:**
```sql
SELECT mcp_list_tools();
```

**Returns:** `TEXT` - JSON array of tool definitions including:
- Tool name
- Description
- Complete input schema with all parameters
- Required vs optional parameters

**Example:**
```sql
sqlite> SELECT mcp_connect('http://localhost:8000/mcp');
sqlite> SELECT mcp_list_tools();
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

### `mcp_call_tool(tool_name, arguments_json)`

Calls a tool on the connected MCP server.

**Syntax:**
```sql
SELECT mcp_call_tool(tool_name, arguments_json);
```

**Parameters:**
- `tool_name` (TEXT) - Name of the tool to call
- `arguments_json` (TEXT) - JSON object containing tool arguments

**Returns:** `TEXT` - JSON response from the tool

**Example:**
```sql
SELECT mcp_call_tool(
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
SELECT mcp_call_tool('test', '{}');
-- {"error": "Not connected. Call mcp_connect() first"}
```

---

## Virtual Tables

The extension provides virtual tables that automatically parse MCP responses into structured rows. These are ideal for SQL queries that need to process multiple tools or results.

### `mcp_tools_table`

A virtual table that returns each tool as a row with structured columns.

**Syntax:**
```sql
SELECT * FROM mcp_tools_table;
SELECT name, description FROM mcp_tools_table;
SELECT * FROM mcp_tools_table WHERE name LIKE 'airbnb%';
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
FROM mcp_tools_table
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

### `mcp_call_tool_table`

A virtual table that extracts text results from tool calls. Returns one row for each `type="text"` content item in the result.

**Syntax:**
```sql
SELECT text FROM mcp_call_tool_table
WHERE tool_name = '<tool>' AND arguments = '<json>';
```

**Columns:**
- `tool_name` (TEXT, hidden) - Name of tool to call (required in WHERE clause)
- `arguments` (TEXT, hidden) - JSON arguments for the tool (required in WHERE clause)
- `text` (TEXT) - Text content from each result item

**Example:**
```sql
-- Call tool and get text results as rows
SELECT text FROM mcp_call_tool_table
WHERE tool_name = 'airbnb_search'
AND arguments = '{"location": "Rome", "maxPrice": 100}';
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
- The `tool_name` and `arguments` columns are "hidden" parameters that must be provided via the WHERE clause
- This table automatically extracts only `type="text"` results from the MCP response
- Each text item in the response becomes a separate row

---

## Function Variants

The extension provides multiple ways to access MCP functionality:

### Scalar Functions (Return JSON strings)

- `mcp_list_tools()` - Returns JSON string of all tools
- `mcp_call_tool(tool_name, arguments)` - Returns JSON string of tool result
- `mcp_tools_json()` - Alias for `mcp_list_tools()`
- `mcp_call_tool_json(tool_name, arguments)` - Alias for `mcp_call_tool()`

**Behavior:**
- Returns the complete JSON response from MCP
- When JSON extension mode is enabled (`use_json_ext=1`), results are marked with JSON subtype for seamless use with SQLite JSON functions
- When JSON extension mode is disabled, results are returned as plain text

Use these when you need the raw JSON response or want to process with `json_extract()`.

### Virtual Tables (Return structured rows)

- `mcp_tools_table` - Returns tools as rows with named columns
- `mcp_call_tool_table` - Returns text results as rows

**Behavior:**
- Always extract and parse the JSON response using SQLite's `json_each()` function
- Return structured rows regardless of JSON extension mode setting
- `mcp_tools_table` extracts the `$.tools` array and returns each tool as a row
- `mcp_call_tool_table` extracts the `$.result.content` array and returns each `type="text"` item as a row

Use these when you need to process multiple items, filter with WHERE clauses, or join with other tables.

**Example comparing approaches:**

```sql
-- Scalar function approach
SELECT json_extract(value, '$.name')
FROM json_each((SELECT mcp_list_tools()), '$.tools')
WHERE json_extract(value, '$.name') LIKE 'airbnb%';

-- Virtual table approach (simpler)
SELECT name
FROM mcp_tools_table
WHERE name LIKE 'airbnb%';
```

---

## JSON Extension Mode

### Benefits

- **Direct JSON Operations**: Use `json_extract()`, `json_each()`, `json_array_length()` and other SQLite JSON functions directly on results
- **Type Safety**: SQLite recognizes results as JSON type, not plain text
- **Cleaner Queries**: No need to wrap results in `json()` function calls
- **Better Performance**: SQLite can optimize JSON operations when the type is known

### Usage Examples

**Without JSON Extension Mode (default):**
```sql
-- Connect in regular text mode
SELECT mcp_connect('http://localhost:8000/mcp', 0, NULL, 0);

-- Need to explicitly parse as JSON
SELECT json_extract(json(mcp_list_tools()), '$.tools[0].name');
```

**With JSON Extension Mode:**
```sql
-- Connect with JSON extension mode enabled
SELECT mcp_connect('http://localhost:8000/mcp', 0, NULL, 1);

-- Results are automatically recognized as JSON
SELECT json_extract(mcp_list_tools(), '$.tools[0].name');

-- Extract connection status
SELECT json_extract(
  mcp_connect('http://localhost:8000/mcp', 0, NULL, 1),
  '$.status'
) as status;

-- List all available tools using json_each
SELECT
  json_extract(value, '$.name') as tool_name,
  json_extract(value, '$.description') as description
FROM json_each((SELECT mcp_list_tools()), '$.tools');

-- Extract tool result content
SELECT json_extract(
  mcp_call_tool('airbnb_search', '{"location": "Rome"}'),
  '$.result.content[0].text'
) as result_text;
```

### When to Use JSON Extension Mode

**Use JSON extension mode when:**
- You need to extract specific fields from results using `json_extract()`
- You want to iterate over arrays in results using `json_each()`
- You're building queries that process JSON results
- You want type safety for JSON operations

**Use regular text mode when:**
- You just need the raw JSON string
- You're displaying results to users without processing
- You're integrating with systems that expect text output

---

## Transport Protocols

The extension supports two MCP transport protocols:

### Streamable HTTP (Default)
Modern streaming HTTP transport for MCP servers.

```sql
SELECT mcp_connect('http://localhost:8000/mcp');
SELECT mcp_connect('http://localhost:8000/mcp', 0);  -- Explicit
```

### SSE (Legacy)
Server-Sent Events transport for compatibility with older MCP servers.

```sql
SELECT mcp_connect('http://localhost:8931/sse', 1);
```

---

## Error Handling

All functions return JSON with error information on failure:

```json
{"error": "Connection failed: timeout"}
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
FROM (SELECT mcp_call_tool('test', '{}') as result);
```