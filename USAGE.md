# SQLite MCP Extension Usage

## Overview

The SQLite MCP extension supports connecting to MCP servers with optional custom HTTP headers in JSON format. This allows you to add multiple headers like Authorization, X-MCP-Readonly, or any custom headers your MCP server requires.

## SQL Function

### `mcp_connect(server_url, [legacy_sse], [headers_json])`

Connect to an MCP server with optional custom headers.

**Parameters:**
- `server_url` (required): URL of the MCP server (e.g., "http://localhost:8000/mcp")
- `legacy_sse` (optional): Transport type - 0 for streamable HTTP (default), 1 for SSE
- `headers_json` (optional): JSON string with custom headers (e.g., `{"Authorization": "Bearer token", "X-MCP-Readonly": "true"}`)

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
