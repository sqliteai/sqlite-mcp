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

### `mcp_connect(server_url, [legacy_sse], [headers_json])`

Connects to an MCP server using either Streamable HTTP (default) or SSE transport, with optional custom HTTP headers.

**Syntax:**
```sql
SELECT mcp_connect(server_url);
SELECT mcp_connect(server_url, legacy_sse);
SELECT mcp_connect(server_url, legacy_sse, headers_json);
```

**Parameters:**
- `server_url` (TEXT) - URL of the MCP server (e.g., "http://localhost:8000/mcp")
- `legacy_sse` (INTEGER, optional) - 1 to use SSE transport (legacy), 0 for Streamable HTTP (default)
- `headers_json` (TEXT, optional) - JSON string with custom HTTP headers (e.g., `{"Authorization": "Bearer token"}`)

**Returns:** `TEXT` - JSON object with connection status

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