# SQLite MCP Extension Usage

## Overview

The SQLite MCP extension supports connecting to MCP servers with optional custom HTTP headers in JSON format. This allows you to add multiple headers like Authorization, X-MCP-Readonly, or any custom headers your MCP server requires.

## SQL Function

### `mcp_connect(server_url, [legacy_sse], [headers_json], [use_json_ext])`

Connect to an MCP server with optional custom headers and JSON extension mode.

**Parameters:**
- `server_url` (required): URL of the MCP server (e.g., "http://localhost:8000/mcp")
- `legacy_sse` (optional): Transport type - 0 for streamable HTTP (default), 1 for SSE
- `headers_json` (optional): JSON string with custom headers (e.g., `{"Authorization": "Bearer token", "X-MCP-Readonly": "true"}`)
- `use_json_ext` (optional): JSON extension mode - 0 for regular text (default), 1 to return results as JSON type

**Returns:** JSON string with connection status (or JSON object if `use_json_ext` is enabled)

## SQL Examples

### Connect With Bearer Token (like GitHub Copilot)

```sql
-- Simple syntax with Authorization header
SELECT mcp_connect(
    'https://api.githubcopilot.com/mcp/',
    0,  -- Use streamable HTTP
    '{"Authorization": "Bearer ghp_your_github_personal_access_token"}'
);
```

### Connect With Multiple Headers

```sql
-- Add multiple custom headers
SELECT mcp_connect(
    'https://api.githubcopilot.com/mcp/',
    0,
    '{"Authorization": "Bearer ghp_token", "X-MCP-Readonly": "true", "X-Custom": "value"}'
);
```

### Connect Without Authentication

```sql
-- No auth needed - just omit the third parameter
SELECT mcp_connect('http://localhost:8000/mcp');

-- Or explicitly specify transport
SELECT mcp_connect('http://localhost:8000/mcp', 0);
```

## Security Best Practices

1. **Never hardcode tokens** in SQL scripts
2. Store tokens in secure files with restricted permissions
3. Use HTTPS in production to encrypt tokens
4. Rotate API tokens regularly

Example loading token from environment variable in your application code:

```c
// C code example
char headers_json[512];
const char* token = getenv("API_TOKEN");
snprintf(headers_json, sizeof(headers_json),
         "{\"Authorization\": \"Bearer %s\"}", token);
// Pass headers_json to mcp_connect
```

## Header Format

Headers must be provided as a valid JSON object string with header names as keys and header values as values:

```json
{
  "Authorization": "Bearer your_token",
  "X-MCP-Readonly": "true",
  "X-Custom-Header": "custom_value"
}
```

When passing to SQL, escape quotes properly:

```sql
SELECT mcp_connect('http://localhost:8000/mcp', 0, '{"Authorization": "Bearer token"}');
```

## Using JSON Extension Mode

The 4th parameter `use_json_ext` enables SQLite's JSON extension mode for all MCP results:

```sql
-- Enable JSON extension mode (4th parameter = 1)
SELECT mcp_connect('http://localhost:8000/mcp', 0, NULL, 1);

-- With custom headers AND JSON extension mode
SELECT mcp_connect(
    'https://api.githubcopilot.com/mcp/',
    0,
    '{"Authorization": "Bearer ghp_token"}',
    1
);
```

### JSON Extension Benefits

When JSON extension mode is enabled:
- Direct use of `json_extract()` without wrapping in `json()`
- Better type safety and performance
- Seamless integration with SQLite JSON functions

### JSON Extension Examples

```sql
-- Connect with JSON extension mode
SELECT mcp_connect('http://localhost:8000/mcp', 0, NULL, 1);

-- Extract specific fields from connection result
SELECT json_extract(
  mcp_connect('http://localhost:8000/mcp', 0, NULL, 1),
  '$.status'
) as connection_status;

-- List all tools with json_each
SELECT
  json_extract(value, '$.name') as tool_name,
  json_extract(value, '$.description') as description
FROM json_each((SELECT mcp_list_tools()), '$.tools');

-- Extract tool result
SELECT json_extract(
  mcp_call_tool('search', '{"query": "test"}'),
  '$.result.content[0].text'
) as result;
```

## Using Virtual Tables

Virtual tables provide a simpler way to work with MCP data by automatically parsing JSON into structured rows.

### How Virtual Tables Work

Virtual tables **always** extract and parse the JSON response using SQLite's `json_each()` function, regardless of whether JSON extension mode is enabled:

- `mcp_tools_table` - Extracts the `$.tools` array and returns each tool as a row with columns: name, title, description, inputSchema, outputSchema, annotations
- `mcp_call_tool_table` - Extracts the `$.result.content` array and returns each `type="text"` item as a row

The JSON extension mode setting only affects **scalar functions** (like `mcp_list_tools()` and `mcp_call_tool()`), which return the complete JSON response with or without the JSON subtype.

### Query Tools as Rows

Instead of manually parsing JSON with `json_each()`, use the `mcp_tools_table` virtual table:

```sql
-- Connect first
SELECT mcp_connect('http://localhost:8000/mcp');

-- Get all tools as rows
SELECT name, description FROM mcp_tools_table;

-- Filter tools
SELECT name, description
FROM mcp_tools_table
WHERE name LIKE 'search%';

-- Extract inputSchema for a specific tool
SELECT name, inputSchema
FROM mcp_tools_table
WHERE name = 'airbnb_search';
```

### Get Tool Results as Rows

The `mcp_call_tool_table` virtual table extracts text results automatically from the `result.content` array:

```sql
-- Call a tool and get text results
SELECT text FROM mcp_call_tool_table
WHERE tool_name = 'search'
AND arguments = '{"query": "SQLite"}';

-- Multiple text results become multiple rows
SELECT text FROM mcp_call_tool_table
WHERE tool_name = 'analyze_data'
AND arguments = '{"dataset": "sales"}';
```

**Expected result structure from MCP:**
```json
{
  "result": {
    "content": [
      {"type": "text", "text": "First result"},
      {"type": "text", "text": "Second result"}
    ]
  }
}
```

### Comparison: Virtual Tables vs JSON Functions

**Using JSON functions (more complex):**
```sql
SELECT json_extract(value, '$.name') as name
FROM json_each((SELECT mcp_list_tools()), '$.tools')
WHERE json_extract(value, '$.name') LIKE 'search%';
```

**Using virtual tables (simpler):**
```sql
SELECT name FROM mcp_tools_table
WHERE name LIKE 'search%';
```

### When to Use Virtual Tables

**Use virtual tables when:**
- You need to filter results with WHERE clauses
- You want to join MCP data with other tables
- You're processing multiple tools or results
- You prefer SQL syntax over JSON manipulation

**Use scalar functions when:**
- You need the complete JSON response
- You're passing results to another system
- You need to preserve the exact response structure
- You want to use `json_extract()` to access specific fields
