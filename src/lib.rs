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

type RunningClient = rmcp::service::RunningService<RoleClient, ClientInfo>;

/// Opaque handle for MCP client
pub struct McpClient {
    runtime: tokio::runtime::Runtime,
    service: Mutex<Option<RunningClient>>,
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
                service: Mutex::new(None),
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
/// Returns: JSON string with status (must be freed with mcp_free_string)
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
        service: Mutex::new(None),
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
        *new_client.service.lock().unwrap() = Some(service);
        *new_client.server_url.lock().unwrap() = Some(url);

        // Store the client globally
        let global_client = GLOBAL_CLIENT.get_or_init(|| Mutex::new(None));
        *global_client.lock().unwrap() = Some(new_client);
    }

    match CString::new(result) {
        Ok(c_str) => c_str.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}

/// List tools available on the connected MCP server
/// Returns: JSON string with tools list (must be freed with mcp_free_string)
#[no_mangle]
pub extern "C" fn mcp_list_tools(_client_ptr: *mut McpClient) -> *mut c_char {
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
        let service_guard = client.service.lock().unwrap();
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

/// Call a tool on the connected MCP server
/// tool_name: Name of the tool to call
/// arguments_json: JSON string with tool arguments
/// Returns: JSON string with tool result (must be freed with mcp_free_string)
#[no_mangle]
pub extern "C" fn mcp_call_tool(
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
        let service_guard = client.service.lock().unwrap();
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

// Keep old compatibility functions
#[no_mangle]
pub extern "C" fn mcp_context_new() -> *mut McpClient {
    mcp_client_new()
}

#[no_mangle]
pub extern "C" fn mcp_context_free(ctx: *mut McpClient) {
    mcp_client_free(ctx);
}

