# SQLite MCP Extension Usage

## Overview

The SQLite MCP extension supports connecting to MCP servers with optional custom HTTP headers in JSON format. This allows you to add multiple headers like Authorization, X-MCP-Readonly, or any custom headers your MCP server requires.

## SQL Function

### `mcp_connect(server_url, [headers_json], [legacy_sse])`

Connect to an MCP server with optional custom headers.

**Parameters:**
- `server_url` (required): URL of the MCP server (e.g., "http://localhost:8000/mcp")
- `headers_json` (optional): JSON string with custom headers (e.g., `{"Authorization": "Bearer token", "X-MCP-Readonly": "true"}`)
- `legacy_sse` (optional): Transport type - 0 for streamable HTTP (default), 1 for SSE

**Returns:** JSON string with connection status

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
-- No auth needed 
SELECT mcp_connect('http://localhost:8000/mcp');

-- Or explicitly specify parameters
SELECT mcp_connect('http://localhost:8000/mcp', NULL, 0);
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

### Query Tools as Rows

Instead of manually parsing JSON with `json_each()`, use the `mcp_list_tools_respond` virtual table:

```sql
-- Connect first
SELECT mcp_connect('http://localhost:8000/mcp');

-- Get all tools as rows
SELECT name, description FROM mcp_list_tools_respond;

-- Filter tools
SELECT name, description
FROM mcp_list_tools_respond
WHERE name LIKE 'search%';

-- Extract inputSchema for a specific tool
SELECT name, inputSchema
FROM mcp_list_tools_respond
WHERE name = 'airbnb_search';
```

### Get Tool Results as Rows

The `mcp_call_tool_respond` virtual table extracts text results automatically from the `result.content` array:

```sql
-- Call a tool and get text results
SELECT text FROM mcp_call_tool_respond('search', '{"query": "SQLite"}');

-- Multiple text results become multiple rows
SELECT text FROM mcp_call_tool_respond('analyze_data', '{"dataset": "sales"}');
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

### Stream Tool Results in Real-Time

The `mcp_call_tool` virtual table provides streaming access to tool results, delivering text chunks as they arrive instead of waiting for the complete response. This is ideal for long-running operations or when you need immediate feedback.

**Basic streaming syntax:**
```sql
-- Stream results from a tool call
SELECT text FROM mcp_call_tool('browser_navigate', '{"url": "https://sqlite.ai"}');

-- Stream search results
SELECT text FROM mcp_call_tool('search', '{"query": "SQLite MCP"}');
```

**When to use streaming:**
- Long-running tool operations (web scraping, large data processing)
- Real-time feedback needed (progress updates, partial results)
- Memory-efficient processing of large responses
- Interactive applications requiring immediate output

**Comparison: Streaming vs Non-Streaming**

| Feature | `mcp_call_tool` (streaming) | `mcp_call_tool_respond` (cached) |
|---------|----------------------|---------------------|
| Response delivery | Real-time chunks | Complete response |
| Memory usage | Low (streaming) | Higher (full result) |
| Use case | Long operations | Quick queries |
| Latency | Immediate first chunk | Wait for completion |

**Example - Processing large dataset:**
```sql
-- Streaming approach - get results as they arrive
SELECT text FROM mcp_call_tool('analyze_logs', '{"file": "large.log"}');

-- Non-streaming approach - wait for complete analysis
SELECT text FROM mcp_call_tool_respond('analyze_logs', '{"file": "large.log"}');
```

### Comparison: Virtual Tables vs JSON Functions

**Using JSON functions (more complex):**
```sql
SELECT json_extract(value, '$.name') as name
FROM json_each((SELECT mcp_list_tools_json()), '$.tools')
WHERE json_extract(value, '$.name') LIKE 'search%';
```

**Using virtual tables (simpler):**
```sql
SELECT name FROM mcp_list_tools_respond
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
