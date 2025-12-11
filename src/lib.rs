//
//  lib.rs
//  sqlitemcp
//
//  Created by Gioele Cantoni on 05/11/25.
//

use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::ptr;
use std::sync::{Mutex, OnceLock};

use rmcp::transport::{SseClientTransport, StreamableHttpClientTransport};
use rmcp::{ServiceExt, RoleClient};
use rmcp::model::{ClientInfo, ClientCapabilities, Implementation};

// Global client instance - one client per process
static GLOBAL_CLIENT: OnceLock<Mutex<Option<McpClient>>> = OnceLock::new();

/// Initialize the MCP library
/// Returns 0 on success, non-zero on error
#[no_mangle]
pub extern "C" fn mcp_init() -> i32 {
    0
}

/// Get the version string of the MCP library
/// Returns a null-terminated string that must be freed with mcp_free_string
#[no_mangle]
pub extern "C" fn mcp_get_version() -> *mut c_char {
    let version = env!("CARGO_PKG_VERSION");
    match CString::new(version) {
        Ok(c_str) => c_str.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}

/// Free a string allocated by the MCP library
#[no_mangle]
pub extern "C" fn mcp_free_string(s: *mut c_char) {
    if s.is_null() {
        return;
    }
    unsafe {
        let _ = CString::from_raw(s);
    }
}

/// Parse tools JSON and extract structured data for virtual table
/// Returns number of tools found, or 0 on error
#[no_mangle]
pub extern "C" fn mcp_parse_tools_json(json_str: *const c_char) -> usize {
    if json_str.is_null() {
        return 0;
    }

    let json_string = unsafe {
        match CStr::from_ptr(json_str).to_str() {
            Ok(s) => s,
            Err(_) => return 0,
        }
    };

    // Parse JSON using serde_json
    match serde_json::from_str::<serde_json::Value>(json_string) {
        Ok(json) => {
            if let Some(tools) = json.get("tools").and_then(|v| v.as_array()) {
                tools.len()
            } else {
                0
            }
        }
        Err(_) => 0,
    }
}

/// Extract tool data by index for virtual table
/// Returns allocated string that must be freed, or NULL if index out of bounds
#[no_mangle]
pub extern "C" fn mcp_get_tool_field(
    json_str: *const c_char,
    tool_index: usize,
    field_name: *const c_char,
) -> *mut c_char {
    if json_str.is_null() || field_name.is_null() {
        return ptr::null_mut();
    }

    let json_string = unsafe {
        match CStr::from_ptr(json_str).to_str() {
            Ok(s) => s,
            Err(_) => return ptr::null_mut(),
        }
    };

    let field = unsafe {
        match CStr::from_ptr(field_name).to_str() {
            Ok(s) => s,
            Err(_) => return ptr::null_mut(),
        }
    };

    // Parse JSON using serde_json
    match serde_json::from_str::<serde_json::Value>(json_string) {
        Ok(json) => {
            // Check if this is a streaming result (single tool object) or batch result (tools array)
            let tool = if let Some(tools) = json.get("tools").and_then(|v| v.as_array()) {
                // Batch result: {"tools": [...]}, get tool by index
                tools.get(tool_index)
            } else if tool_index == 0 {
                // Streaming result: single tool object, only valid for index 0
                Some(&json)
            } else {
                None
            };

            if let Some(tool) = tool {
                let value = match field {
                    "name" => tool.get("name"),
                    "title" => tool.get("title"),
                    "description" => tool.get("description"),
                    "inputSchema" => tool.get("inputSchema"),
                    "outputSchema" => tool.get("outputSchema"),
                    "annotations" => tool.get("annotations"),
                    _ => None,
                };

                if let Some(v) = value {
                    let result = if v.is_string() {
                        v.as_str().unwrap_or("").to_string()
                    } else {
                        // For complex objects, serialize to JSON
                        serde_json::to_string(v).unwrap_or_else(|_| "".to_string())
                    };

                    return match CString::new(result) {
                        Ok(c_str) => c_str.into_raw(),
                        Err(_) => ptr::null_mut(),
                    };
                }
            }
        }
        Err(_) => {}
    }

    // Return empty string if field not found
    match CString::new("") {
        Ok(c_str) => c_str.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}

/// Parse call tool result JSON and extract text content  
/// Returns number of text results found, or 0 on error
#[no_mangle]
pub extern "C" fn mcp_parse_call_result_json(json_str: *const c_char) -> usize {
    if json_str.is_null() {
        return 0;
    }

    let json_string = unsafe {
        match CStr::from_ptr(json_str).to_str() {
            Ok(s) => s,
            Err(_) => return 0,
        }
    };

    // Parse JSON using serde_json
    match serde_json::from_str::<serde_json::Value>(json_string) {
        Ok(json) => {
            // Try both direct content and nested result.content
            let content_array = json.get("content").and_then(|v| v.as_array())
                .or_else(|| json.get("result").and_then(|r| r.get("content").and_then(|v| v.as_array())));
                
            if let Some(content) = content_array {
                content.len()
            } else {
                0
            }
        }
        Err(_) => 0,
    }
}

/// Extract call result text by index for virtual table
/// Returns allocated string that must be freed, or NULL if index out of bounds
#[no_mangle]
pub extern "C" fn mcp_get_call_result_text(
    json_str: *const c_char,
    content_index: usize,
) -> *mut c_char {
    if json_str.is_null() {
        return ptr::null_mut();
    }

    let json_string = unsafe {
        match CStr::from_ptr(json_str).to_str() {
            Ok(s) => s,
            Err(_) => return ptr::null_mut(),
        }
    };

    // Parse JSON using serde_json
    match serde_json::from_str::<serde_json::Value>(json_string) {
        Ok(json) => {
            // Try both direct content and nested result.content
            let content_array = json.get("content").and_then(|v| v.as_array())
                .or_else(|| json.get("result").and_then(|r| r.get("content").and_then(|v| v.as_array())));
                
            if let Some(content) = content_array {
                if let Some(item) = content.get(content_index) {
                    if let Some(text) = item.get("text").and_then(|v| v.as_str()) {
                        return match CString::new(text) {
                            Ok(c_str) => c_str.into_raw(),
                            Err(_) => ptr::null_mut(),
                        };
                    }
                }
            }
        }
        Err(_) => {}
    }

    // Return empty string if not found
    match CString::new("") {
        Ok(c_str) => c_str.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}

type RunningClient = rmcp::service::RunningService<RoleClient, ClientInfo>;

/// Opaque handle for MCP client
pub struct McpClient {
    runtime: tokio::runtime::Runtime,
    service: Arc<TokioMutex<Option<RunningClient>>>,
    server_url: Mutex<Option<String>>,
}

/// Create a new MCP client
/// Returns NULL on error
#[no_mangle]
pub extern "C" fn mcp_client_new() -> *mut McpClient {
    match tokio::runtime::Runtime::new() {
        Ok(runtime) => {
            let client = Box::new(McpClient {
                runtime,
                service: Arc::new(TokioMutex::new(None)),
                server_url: Mutex::new(None),
            });
            Box::into_raw(client)
        }
        Err(_) => ptr::null_mut(),
    }
}

/// Free an MCP client
#[no_mangle]
pub extern "C" fn mcp_client_free(client: *mut McpClient) {
    if client.is_null() {
        return;
    }
    unsafe {
        let _ = Box::from_raw(client);
    }
}

/// Connect to an MCP server with optional custom headers
/// server_url: URL of the MCP server (e.g., "http://localhost:8931/sse")
/// headers_json: Optional JSON string with custom headers (e.g., '{"Authorization": "Bearer token", "X-MCP-Readonly": "true"}'), can be NULL
/// legacy_sse: 1 to use SSE transport (legacy), 0 to use streamable HTTP transport (default)
/// Returns: NULL on success, error string on failure (must be freed with mcp_free_string)
#[no_mangle]
pub extern "C" fn mcp_connect(
    _client_ptr: *mut McpClient,
    server_url: *const c_char,
    headers_json: *const c_char,
    legacy_sse: i32
) -> *mut c_char {
    if server_url.is_null() {
        let error = r#"{"error": "Invalid arguments"}"#;
        return CString::new(error).unwrap_or_default().into_raw();
    }

    let server_url_str = unsafe {
        match CStr::from_ptr(server_url).to_str() {
            Ok(s) => s.to_string(),
            Err(_) => {
                let error = r#"{"error": "Invalid server URL"}"#;
                return CString::new(error).unwrap_or_default().into_raw();
            }
        }
    };

    // Parse optional headers_json (can be NULL or a JSON object)
    let headers_map: Option<std::collections::HashMap<String, String>> = if headers_json.is_null() {
        None
    } else {
        unsafe {
            match CStr::from_ptr(headers_json).to_str() {
                Ok(json_str) => {
                    // Try to parse as JSON object
                    match serde_json::from_str::<std::collections::HashMap<String, String>>(json_str) {
                        Ok(map) => {
                            Some(map)
                        },
                        Err(_e) => {
                            let error = r#"{"error": "Invalid headers JSON format. Expected: {\"Header-Name\": \"value\"}"}"#;
                            return CString::new(error).unwrap_or_default().into_raw();
                        }
                    }
                }
                Err(_) => {
                    let error = r#"{"error": "Invalid headers string"}"#;
                    return CString::new(error).unwrap_or_default().into_raw();
                }
            }
        }
    };

    // Create a new McpClient with runtime
    let new_client = McpClient {
        runtime: match tokio::runtime::Runtime::new() {
            Ok(r) => r,
            Err(e) => {
                let error = format!(r#"{{"error": "Failed to create runtime: {}"}}"#, e);
                return CString::new(error).unwrap_or_default().into_raw();
            }
        },
        service: Arc::new(TokioMutex::new(None)),
        server_url: Mutex::new(None),
    };

    let use_sse = legacy_sse != 0;

    let (result, maybe_service) = if use_sse {
        // Use SSE transport (legacy) with optional custom headers
        new_client.runtime.block_on(async {
            // Create HTTP client with optional custom headers
            let mut client_builder = reqwest::Client::builder();
            if let Some(ref headers_map) = headers_map {
                use reqwest::header::{HeaderMap, HeaderValue, HeaderName};
                let mut headers = HeaderMap::new();

                for (key, value) in headers_map {
                    match (HeaderName::from_bytes(key.as_bytes()), HeaderValue::from_str(value)) {
                        (Ok(header_name), Ok(header_value)) => {
                            headers.insert(header_name, header_value);
                        }
                        _ => {
                            let error = format!(r#"{{"error": "Invalid header format: {}: {}"}}"#, key, value);
                            return (error, None);
                        }
                    }
                }

                client_builder = client_builder.default_headers(headers);
            }

            let http_client = match client_builder.build() {
                Ok(c) => c,
                Err(e) => {
                    let error = format!(r#"{{"error": "Failed to create HTTP client: {}"}}"#, e);
                    return (error, None);
                }
            };

            // Build SSE transport with custom HTTP client
            let sse_config = rmcp::transport::sse_client::SseClientConfig {
                sse_endpoint: server_url_str.clone().into(),
                ..Default::default()
            };

            let transport = match SseClientTransport::start_with_client(http_client, sse_config).await {
                Ok(t) => t,
                Err(e) => {
                    let error = format!(r#"{{"error": "Failed to connect to MCP server: {}"}}"#, e);
                    return (error, None);
                }
            };

            // Create client info
            let client_info = ClientInfo {
                protocol_version: Default::default(),
                capabilities: ClientCapabilities::default(),
                client_info: Implementation {
                    name: "sqlite-mcp".to_string(),
                    title: None,
                    version: env!("CARGO_PKG_VERSION").to_string(),
                    website_url: None,
                    icons: None,
                },
            };

            // Create service from transport
            let service = match client_info.serve(transport).await {
                Ok(s) => s,
                Err(e) => {
                    let error = format!(r#"{{"error": "Failed to initialize service: {}"}}"#, e);
                    return (error, None);
                }
            };

            // Get server info
            let info = service.peer_info();
            let (server_name, server_version) = if let Some(info) = info {
                (
                    serde_json::to_string(&info.server_info.name).unwrap_or_else(|_| "\"unknown\"".to_string()),
                    serde_json::to_string(&info.server_info.version).unwrap_or_else(|_| "\"0.0.0\"".to_string())
                )
            } else {
                ("\"unknown\"".to_string(), "\"0.0.0\"".to_string())
            };

            let result_msg = format!(
                r#"{{"status": "connected", "server": {}, "version": {}, "transport": "sse"}}"#,
                server_name, server_version
            );

            (result_msg, Some((service, server_url_str)))
        })
    } else {
        // Use streamable HTTP transport (default) with optional custom headers
        new_client.runtime.block_on(async {
            // For Streamable HTTP, we need to extract the Authorization header specifically
            // since it has a dedicated field, and we'll use a custom HTTP client for other headers
            let auth_header_value = headers_map.as_ref().and_then(|m| m.get("Authorization")).map(|s| s.clone());

            // Build streamable HTTP transport config
            let config = rmcp::transport::streamable_http_client::StreamableHttpClientTransportConfig {
                uri: server_url_str.clone().into(),
                auth_header: auth_header_value.map(|s| s.into()),
                ..Default::default()
            };

            // If there are other custom headers besides Authorization, create a custom HTTP client
            let transport = if let Some(ref headers_map) = headers_map {
                let non_auth_headers: std::collections::HashMap<String, String> = headers_map.iter()
                    .filter(|(k, _)| k.as_str() != "Authorization")
                    .map(|(k, v)| (k.clone(), v.clone()))
                    .collect();

                if !non_auth_headers.is_empty() {
                    // Create HTTP client with custom headers
                    use reqwest::header::{HeaderMap, HeaderValue, HeaderName};
                    let mut headers = HeaderMap::new();

                    for (key, value) in non_auth_headers {
                        match (HeaderName::from_bytes(key.as_bytes()), HeaderValue::from_str(&value)) {
                            (Ok(header_name), Ok(header_value)) => {
                                headers.insert(header_name, header_value);
                            }
                            _ => {
                                let error = format!(r#"{{"error": "Invalid header format: {}: {}"}}"#, key, value);
                                return (error, None);
                            }
                        }
                    }

                    let http_client = match reqwest::Client::builder()
                        .default_headers(headers)
                        .build() {
                        Ok(c) => c,
                        Err(e) => {
                            let error = format!(r#"{{"error": "Failed to create HTTP client: {}"}}"#, e);
                            return (error, None);
                        }
                    };

                    StreamableHttpClientTransport::with_client(http_client, config)
                } else {
                    StreamableHttpClientTransport::from_config(config)
                }
            } else {
                // No custom headers
                StreamableHttpClientTransport::from_config(config)
            };

            // Create client info
            let client_info = ClientInfo {
                protocol_version: Default::default(),
                capabilities: ClientCapabilities::default(),
                client_info: Implementation {
                    name: "sqlite-mcp".to_string(),
                    title: None,
                    version: env!("CARGO_PKG_VERSION").to_string(),
                    website_url: None,
                    icons: None,
                },
            };

            // Create service from transport
            let service = match client_info.serve(transport).await {
                Ok(s) => s,
                Err(e) => {
                    let error = format!(r#"{{"error": "Failed to connect to MCP server: {}"}}"#, e);
                    return (error, None);
                }
            };

            // Get server info
            let info = service.peer_info();
            let (server_name, server_version) = if let Some(info) = info {
                (
                    serde_json::to_string(&info.server_info.name).unwrap_or_else(|_| "\"unknown\"".to_string()),
                    serde_json::to_string(&info.server_info.version).unwrap_or_else(|_| "\"0.0.0\"".to_string())
                )
            } else {
                ("\"unknown\"".to_string(), "\"0.0.0\"".to_string())
            };

            let result_msg = format!(
                r#"{{"status": "connected", "server": {}, "version": {}, "transport": "streamable-http"}}"#,
                server_name, server_version
            );

            (result_msg, Some((service, server_url_str)))
        })
    };

    // Store service and URL if connection succeeded
    if let Some((service, url)) = maybe_service {
        new_client.runtime.block_on(async {
            *new_client.service.lock().await = Some(service);
        });
        *new_client.server_url.lock().unwrap() = Some(url);

        // Store the client globally
        let global_client = GLOBAL_CLIENT.get_or_init(|| Mutex::new(None));
        *global_client.lock().unwrap() = Some(new_client);
    }

    // Parse the JSON response using serde_json to check status
    match serde_json::from_str::<serde_json::Value>(&result) {
        Ok(json) => {
            if let Some(status) = json.get("status").and_then(|v| v.as_str()) {
                if status == "connected" {
                    // Return NULL on successful connection
                    ptr::null_mut()
                } else {
                    // Return error if status is not "connected"
                    match CString::new(result) {
                        Ok(c_str) => c_str.into_raw(),
                        Err(_) => ptr::null_mut(),
                    }
                }
            } else {
                // No status field found, return as error
                match CString::new(result) {
                    Ok(c_str) => c_str.into_raw(),
                    Err(_) => ptr::null_mut(),
                }
            }
        }
        Err(_) => {
            // Invalid JSON, return as error
            match CString::new(result) {
                Ok(c_str) => c_str.into_raw(),
                Err(_) => ptr::null_mut(),
            }
        }
    }
}

/// List tools available on the connected MCP server (returns raw JSON)
/// Returns: JSON string with tools list (must be freed with mcp_free_string)
#[no_mangle]
pub extern "C" fn mcp_list_tools_json(_client_ptr: *mut McpClient) -> *mut c_char {
    // Get global client
    let global_client_guard = GLOBAL_CLIENT.get()
        .and_then(|c| Some(c.lock().unwrap()));
    let client = match global_client_guard.as_ref().and_then(|g| g.as_ref()) {
        Some(c) => c,
        None => {
            let error = r#"{"error": "Not connected. Call mcp_connect() first"}"#;
            return CString::new(error).unwrap_or_default().into_raw();
        }
    };

    let result = client.runtime.block_on(async {
        let service_guard = client.service.lock().await;
        let service = match service_guard.as_ref() {
            Some(s) => s,
            None => {
                return r#"{"error": "Not connected to server"}"#.to_string();
            }
        };

        match service.list_tools(Default::default()).await {
            Ok(tools_response) => {
                let tools_json: Vec<serde_json::Value> = tools_response
                    .tools
                    .iter()
                    .map(|tool| {
                        serde_json::json!({
                            "name": tool.name,
                            "description": tool.description,
                            "inputSchema": tool.input_schema
                        })
                    })
                    .collect();

                match serde_json::to_string(&serde_json::json!({
                    "tools": tools_json
                })) {
                    Ok(json) => json,
                    Err(e) => format!(r#"{{"error": "Serialization failed: {}"}}"#, e),
                }
            }
            Err(e) => format!(r#"{{"error": "Failed to list tools: {}"}}"#, e),
        }
    });

    match CString::new(result) {
        Ok(c_str) => c_str.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}

/// Call a tool on the connected MCP server (returns raw JSON)
/// tool_name: Name of the tool to call
/// arguments_json: JSON string with tool arguments
/// Returns: JSON string with tool result (must be freed with mcp_free_string)
#[no_mangle]
pub extern "C" fn mcp_call_tool_json(
    _client_ptr: *mut McpClient,
    tool_name: *const c_char,
    arguments_json: *const c_char,
) -> *mut c_char {
    if tool_name.is_null() || arguments_json.is_null() {
        let error = r#"{"error": "Invalid arguments"}"#;
        return CString::new(error).unwrap_or_default().into_raw();
    }

    let tool_name_str = unsafe {
        match CStr::from_ptr(tool_name).to_str() {
            Ok(s) => s.to_string(),
            Err(_) => {
                let error = r#"{"error": "Invalid tool name"}"#;
                return CString::new(error).unwrap_or_default().into_raw();
            }
        }
    };

    let arguments_str = unsafe {
        match CStr::from_ptr(arguments_json).to_str() {
            Ok(s) => s,
            Err(_) => {
                let error = r#"{"error": "Invalid arguments JSON"}"#;
                return CString::new(error).unwrap_or_default().into_raw();
            }
        }
    };

    let arguments: serde_json::Value = match serde_json::from_str(arguments_str) {
        Ok(v) => v,
        Err(e) => {
            let error = format!(r#"{{"error": "Invalid JSON: {}"}}"#, e);
            return CString::new(error).unwrap_or_default().into_raw();
        }
    };

    // Get global client
    let global_client_guard = GLOBAL_CLIENT.get()
        .and_then(|c| Some(c.lock().unwrap()));
    let client = match global_client_guard.as_ref().and_then(|g| g.as_ref()) {
        Some(c) => c,
        None => {
            let error = r#"{"error": "Not connected. Call mcp_connect() first"}"#;
            return CString::new(error).unwrap_or_default().into_raw();
        }
    };

    let result = client.runtime.block_on(async {
        let service_guard = client.service.lock().await;
        let service = match service_guard.as_ref() {
            Some(s) => s,
            None => {
                return r#"{"error": "Not connected to server"}"#.to_string();
            }
        };

        let call_param = rmcp::model::CallToolRequestParam {
            name: std::borrow::Cow::Owned(tool_name_str),
            arguments: arguments.as_object().cloned(),
        };

        match service.call_tool(call_param).await {
            Ok(result) => {
                match serde_json::to_string(&serde_json::json!({
                    "result": result
                })) {
                    Ok(json) => json,
                    Err(e) => format!(r#"{{"error": "Serialization failed: {}"}}"#, e),
                }
            }
            Err(e) => format!(r#"{{"error": "Tool call failed: {}"}}"#, e),
        }
    });

    match CString::new(result) {
        Ok(c_str) => c_str.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}

// Streaming API
use std::sync::Arc;
use std::collections::HashMap;
use tokio::sync::Mutex as TokioMutex;

lazy_static::lazy_static! {
    static ref STREAM_CHANNELS: Arc<TokioMutex<HashMap<usize, tokio::sync::mpsc::UnboundedReceiver<StreamChunk>>>> =
        Arc::new(TokioMutex::new(HashMap::new()));
    static ref STREAM_COUNTER: Mutex<usize> = Mutex::new(0);
}

// FFI-compatible StreamResult struct (must match C definition)
#[repr(C)]
pub struct StreamResult {
    pub result_type: i32,  // 0=tool, 1=text, 2=error, 3=done
    pub data: *mut c_char,
}

// Stream type constants (must match C)
const STREAM_TYPE_TOOL: i32 = 0;
const STREAM_TYPE_TEXT: i32 = 1;
const STREAM_TYPE_ERROR: i32 = 2;
const STREAM_TYPE_DONE: i32 = 3;

// Internal stream chunk enum
#[derive(Debug, Clone)]
enum StreamChunk {
    Tool(serde_json::Value),
    Text(String),
    Error(String),
    Done,
}

/// Initialize a new list_tools stream
/// Returns a stream ID that can be used to fetch results
#[no_mangle]
pub extern "C" fn mcp_list_tools_init() -> usize {
    // Get next stream ID
    let stream_id = {
        let mut counter = STREAM_COUNTER.lock().unwrap();
        *counter += 1;
        *counter
    };

    // Create unbounded channel for streaming
    let (tx, rx) = tokio::sync::mpsc::unbounded_channel();

    // Get the global client
    let client_mutex = GLOBAL_CLIENT.get_or_init(|| Mutex::new(None));

    // Spawn the async task
    {
        let client_opt = client_mutex.lock().unwrap();
        if let Some(client) = client_opt.as_ref() {
            // Clone the Arc to share the service across async boundaries
            let service_arc = client.service.clone();

            // Use the client's runtime to spawn the task
            client.runtime.spawn(async move {
                let service_guard = service_arc.lock().await;
                if let Some(service) = service_guard.as_ref() {
                    match service.list_tools(None).await {
                        Ok(response) => {
                            // Send each tool as a separate chunk
                            for tool in response.tools {
                                if let Ok(tool_json) = serde_json::to_value(&tool) {
                                    let _ = tx.send(StreamChunk::Tool(tool_json));
                                }
                            }
                            let _ = tx.send(StreamChunk::Done);
                        }
                        Err(e) => {
                            let _ = tx.send(StreamChunk::Error(format!("Failed to list tools: {}", e)));
                            let _ = tx.send(StreamChunk::Done);
                        }
                    }
                } else {
                    // No service connected
                    let _ = tx.send(StreamChunk::Error("Not connected. Call mcp_connect() first".to_string()));
                    let _ = tx.send(StreamChunk::Done);
                }
            });
        } else {
            // No client initialized
            let _ = tx.send(StreamChunk::Error("Client not initialized".to_string()));
            let _ = tx.send(StreamChunk::Done);
        }
    } // Release the lock here

    // Store the receiver in global storage (now safe to acquire lock again)
    let client_opt = client_mutex.lock().unwrap();
    if let Some(client) = client_opt.as_ref() {
        client.runtime.block_on(async {
            let mut channels = STREAM_CHANNELS.lock().await;
            channels.insert(stream_id, rx);
        });
    }

    stream_id
}

/// Initialize a stream for calling a tool and retrieving results
/// Returns a stream ID that can be used with mcp_stream_next/wait/cleanup
#[no_mangle]
pub extern "C" fn mcp_call_tool_init(tool_name: *const c_char, arguments: *const c_char) -> usize {
    let tool_name_str = unsafe {
        if tool_name.is_null() {
            return 0;
        }
        match CStr::from_ptr(tool_name).to_str() {
            Ok(s) => s.to_string(),
            Err(_) => return 0,
        }
    };

    let arguments_str = unsafe {
        if arguments.is_null() {
            return 0;
        }
        match CStr::from_ptr(arguments).to_str() {
            Ok(s) => s.to_string(),
            Err(_) => return 0,
        }
    };

    // Generate unique stream ID
    let stream_id = {
        let mut counter = STREAM_COUNTER.lock().unwrap();
        *counter += 1;
        *counter
    };

    // Create unbounded channel for streaming
    let (tx, rx) = tokio::sync::mpsc::unbounded_channel();

    // Get the global client
    let client_mutex = GLOBAL_CLIENT.get_or_init(|| Mutex::new(None));

    // Spawn the async task
    {
        let client_opt = client_mutex.lock().unwrap();
        if let Some(client) = client_opt.as_ref() {
            let service_arc = client.service.clone();

            // Use the client's runtime to spawn the task
            client.runtime.spawn(async move {
                let service_guard = service_arc.lock().await;
                if let Some(service) = service_guard.as_ref() {
                    // Parse arguments
                    let arguments_json: serde_json::Value = match serde_json::from_str(&arguments_str) {
                        Ok(v) => v,
                        Err(e) => {
                            let _ = tx.send(StreamChunk::Error(format!("Invalid JSON arguments: {}", e)));
                            let _ = tx.send(StreamChunk::Done);
                            return;
                        }
                    };

                    // Create the call tool parameter
                    let call_param = rmcp::model::CallToolRequestParam {
                        name: std::borrow::Cow::Owned(tool_name_str),
                        arguments: arguments_json.as_object().cloned(),
                    };

                    // Call the tool
                    match service.call_tool(call_param).await {
                        Ok(result) => {
                            // Serialize the result to JSON and extract text content
                            if let Ok(result_json) = serde_json::to_value(&result) {
                                if let Some(content_array) = result_json.get("content").and_then(|v| v.as_array()) {
                                    for item in content_array {
                                        if let Some(item_type) = item.get("type").and_then(|v| v.as_str()) {
                                            if item_type == "text" {
                                                if let Some(text) = item.get("text").and_then(|v| v.as_str()) {
                                                    let _ = tx.send(StreamChunk::Text(text.to_string()));
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            let _ = tx.send(StreamChunk::Done);
                        }
                        Err(e) => {
                            let _ = tx.send(StreamChunk::Error(format!("Failed to call tool: {}", e)));
                            let _ = tx.send(StreamChunk::Done);
                        }
                    }
                } else {
                    let _ = tx.send(StreamChunk::Error("Not connected. Call mcp_connect() first".to_string()));
                    let _ = tx.send(StreamChunk::Done);
                }
            });
        } else {
            let _ = tx.send(StreamChunk::Error("Client not initialized".to_string()));
            let _ = tx.send(StreamChunk::Done);
        }
    }

    // Store the receiver
    let client_opt = client_mutex.lock().unwrap();
    if let Some(client) = client_opt.as_ref() {
        client.runtime.block_on(async {
            let mut channels = STREAM_CHANNELS.lock().await;
            channels.insert(stream_id, rx);
        });
    }

    stream_id
}

/// Try to get the next chunk from a stream (non-blocking)
/// Returns NULL if no data is available
#[no_mangle]
pub extern "C" fn mcp_stream_next(stream_id: usize) -> *mut StreamResult {
    let client_mutex = GLOBAL_CLIENT.get_or_init(|| Mutex::new(None));
    let client_opt = client_mutex.lock().unwrap();

    if let Some(client) = client_opt.as_ref() {
        client.runtime.block_on(async {
            let mut channels = STREAM_CHANNELS.lock().await;
            if let Some(rx) = channels.get_mut(&stream_id) {
                match rx.try_recv() {
                    Ok(chunk) => {
                        Box::into_raw(Box::new(chunk_to_stream_result(chunk)))
                    }
                    Err(_) => ptr::null_mut(),
                }
            } else {
                ptr::null_mut()
            }
        })
    } else {
        ptr::null_mut()
    }
}

/// Wait for the next chunk from a stream (blocking with timeout)
/// timeout_ms: Maximum milliseconds to wait
/// Returns NULL if timeout occurs or stream is closed
#[no_mangle]
pub extern "C" fn mcp_stream_wait(stream_id: usize, timeout_ms: u64) -> *mut StreamResult {
    let client_mutex = GLOBAL_CLIENT.get_or_init(|| Mutex::new(None));
    let client_opt = client_mutex.lock().unwrap();

    if let Some(client) = client_opt.as_ref() {
        client.runtime.block_on(async {
            let mut channels = STREAM_CHANNELS.lock().await;
            if let Some(rx) = channels.get_mut(&stream_id) {
                let timeout = tokio::time::Duration::from_millis(timeout_ms);
                match tokio::time::timeout(timeout, rx.recv()).await {
                    Ok(Some(chunk)) => {
                        Box::into_raw(Box::new(chunk_to_stream_result(chunk)))
                    }
                    _ => ptr::null_mut(),
                }
            } else {
                ptr::null_mut()
            }
        })
    } else {
        ptr::null_mut()
    }
}

/// Clean up a stream and free its resources
#[no_mangle]
pub extern "C" fn mcp_stream_cleanup(stream_id: usize) {
    let client_mutex = GLOBAL_CLIENT.get_or_init(|| Mutex::new(None));
    let client_opt = client_mutex.lock().unwrap();

    if let Some(client) = client_opt.as_ref() {
        client.runtime.block_on(async {
            let mut channels = STREAM_CHANNELS.lock().await;
            channels.remove(&stream_id);
        });
    }
}

/// Free a StreamResult returned by mcp_stream_next or mcp_stream_wait
#[no_mangle]
pub extern "C" fn mcp_stream_free_result(result: *mut StreamResult) {
    if result.is_null() {
        return;
    }
    unsafe {
        let result = Box::from_raw(result);
        if !result.data.is_null() {
            let _ = CString::from_raw(result.data);
        }
    }
}

// Helper function to convert StreamChunk to StreamResult
fn chunk_to_stream_result(chunk: StreamChunk) -> StreamResult {
    match chunk {
        StreamChunk::Tool(tool_json) => {
            let json_str = serde_json::to_string(&tool_json).unwrap_or_else(|_| "{}".to_string());
            let c_str = CString::new(json_str).unwrap_or_else(|_| CString::new("{}").unwrap());
            StreamResult {
                result_type: STREAM_TYPE_TOOL,
                data: c_str.into_raw(),
            }
        }
        StreamChunk::Text(text) => {
            let c_str = CString::new(text).unwrap_or_else(|_| CString::new("").unwrap());
            StreamResult {
                result_type: STREAM_TYPE_TEXT,
                data: c_str.into_raw(),
            }
        }
        StreamChunk::Error(error) => {
            let c_str = CString::new(error).unwrap_or_else(|_| CString::new("Unknown error").unwrap());
            StreamResult {
                result_type: STREAM_TYPE_ERROR,
                data: c_str.into_raw(),
            }
        }
        StreamChunk::Done => {
            StreamResult {
                result_type: STREAM_TYPE_DONE,
                data: ptr::null_mut(),
            }
        }
    }
}

