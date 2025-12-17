//
//  lib.rs (New Windows-compatible version)
//  sqlitemcp
//
//  Single global Tokio runtime architecture to fix Windows issues
//

use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::ptr;
use std::sync::{Arc, Mutex, OnceLock};
use std::sync::mpsc::{self, Sender, Receiver};
use std::thread;

use rmcp::transport::{SseClientTransport, StreamableHttpClientTransport};
use rmcp::{ServiceExt, RoleClient};
use rmcp::model::{ClientInfo, ClientCapabilities, Implementation, ProtocolVersion};
use serde_json;

// === SINGLE GLOBAL RUNTIME ARCHITECTURE ===
// This is the ONLY Tokio runtime in the entire process

static GLOBAL_RUNTIME: OnceLock<GlobalRuntime> = OnceLock::new();

/// Single global runtime with dedicated worker thread
/// This ensures ALL async operations happen on the same runtime
struct GlobalRuntime {
    sender: Sender<RuntimeCommand>,
    _worker_handle: thread::JoinHandle<()>,
}

/// Commands sent to the runtime worker thread
enum RuntimeCommand {
    Connect { 
        url: String, 
        headers: Option<String>,
        legacy_sse: bool,
        response: Sender<Result<String, String>> 
    },
    ListTools { 
        response: Sender<Result<String, String>> 
    },
    CallTool { 
        name: String, 
        args: String,
        response: Sender<Result<String, String>> 
    },
    Disconnect { 
        response: Sender<Result<(), String>> 
    },
}

impl GlobalRuntime {
    fn new() -> Self {
        let (sender, receiver) = mpsc::channel();
        
        let worker_handle = thread::spawn(move || {
            // Create the ONE AND ONLY Tokio runtime
            let rt = tokio::runtime::Builder::new_multi_thread()
                .enable_all()
                .build()
                .expect("Failed to create Tokio runtime");
            
            // This thread will run ALL async operations
            rt.block_on(async move {
                let mut client: Option<RunningClient> = None;
                
                while let Ok(cmd) = receiver.recv() {
                    match cmd {
                        RuntimeCommand::Connect { url, headers, legacy_sse, response } => {
                            let result = Self::handle_connect(&mut client, url, headers, legacy_sse).await;
                            let _ = response.send(result);
                        }
                        RuntimeCommand::ListTools { response } => {
                            let result = Self::handle_list_tools(&client).await;
                            let _ = response.send(result);
                        }
                        RuntimeCommand::CallTool { name, args, response } => {
                            let result = Self::handle_call_tool(&client, name, args).await;
                            let _ = response.send(result);
                        }
                        RuntimeCommand::Disconnect { response } => {
                            client = None;
                            let _ = response.send(Ok(()));
                        }
                    }
                }
            });
        });
        
        Self {
            sender,
            _worker_handle: worker_handle,
        }
    }
    
    async fn handle_connect(
        client: &mut Option<RunningClient>,
        url: String,
        headers: Option<String>,
        legacy_sse: bool,
    ) -> Result<String, String> {
        // Disconnect existing client
        *client = None;
        
        // Parse headers if provided
        let headers_map: Option<std::collections::HashMap<String, String>> = if let Some(headers_str) = headers {
            match serde_json::from_str(&headers_str) {
                Ok(map) => Some(map),
                Err(_) => return Err("Invalid headers JSON".to_string()),
            }
        } else {
            None
        };
        
        let result = if legacy_sse {
            Self::connect_sse(url, headers_map).await
        } else {
            Self::connect_streamable_http(url, headers_map).await
        };
        
        match result {
            Ok(service) => {
                *client = Some(service);
                Ok("connected".to_string())
            }
            Err(e) => Err(format!("Failed to connect to MCP server: {}", e)),
        }
    }
    
    async fn connect_sse(
        url: String,
        headers_map: Option<std::collections::HashMap<String, String>>,
    ) -> Result<RunningClient, String> {
        let mut client_builder = reqwest::Client::builder();
        if let Some(ref headers_map) = headers_map {
            let mut header_map = reqwest::header::HeaderMap::new();
            for (key, value) in headers_map {
                if let (Ok(header_name), Ok(header_value)) = (
                    reqwest::header::HeaderName::from_bytes(key.as_bytes()),
                    reqwest::header::HeaderValue::from_str(value),
                ) {
                    header_map.insert(header_name, header_value);
                }
            }
            client_builder = client_builder.default_headers(header_map);
        }
        
        let http_client = client_builder.build().map_err(|e| e.to_string())?;
        let transport = Box::new(SseClientTransport::new(http_client));
        
        let client_info = ClientInfo {
            protocol_version: rmcp::model::ProtocolVersion::default(),
            capabilities: ClientCapabilities::default(),
            client_info: Implementation {
                name: "sqlite-mcp".to_string(),
                title: None,
                version: "0.1.4".to_string(),
                icons: None,
                website_url: None,
            },
        };
        
        let client_caps = ClientCapabilities::default();
        
        rmcp::client::connect(transport, client_info, client_caps, &url)
            .await
            .map_err(|e| e.to_string())
    }
    
    async fn connect_streamable_http(
        url: String,
        headers_map: Option<std::collections::HashMap<String, String>>,
    ) -> Result<RunningClient, String> {
        let auth_header_value = headers_map.as_ref()
            .and_then(|m| m.get("Authorization"))
            .map(|s| s.clone());
        
        let mut client_builder = reqwest::Client::builder();
        if let Some(ref headers_map) = headers_map {
            let mut header_map = reqwest::header::HeaderMap::new();
            for (key, value) in headers_map {
                if key != "Authorization" {
                    if let (Ok(header_name), Ok(header_value)) = (
                        reqwest::header::HeaderName::from_bytes(key.as_bytes()),
                        reqwest::header::HeaderValue::from_str(value),
                    ) {
                        header_map.insert(header_name, header_value);
                    }
                }
            }
            if !header_map.is_empty() {
                client_builder = client_builder.default_headers(header_map);
            }
        }
        
        let http_client = client_builder.build().map_err(|e| e.to_string())?;
        let transport = Box::new(StreamableHttpClientTransport::new(http_client, auth_header_value));
        
        let client_info = ClientInfo {
            protocol_version: rmcp::model::ProtocolVersion::default(),
            capabilities: ClientCapabilities::default(),
            client_info: Implementation {
                name: "sqlite-mcp".to_string(),
                title: None,
                version: "0.1.4".to_string(),
                icons: None,
                website_url: None,
            },
        };
        
        let client_caps = ClientCapabilities::default();
        
        rmcp::client::connect(transport, client_info, client_caps, &url)
            .await
            .map_err(|e| e.to_string())
    }
    
    async fn handle_list_tools(client: &Option<RunningClient>) -> Result<String, String> {
        let service = client.as_ref().ok_or("Not connected. Call mcp_connect() first")?;
        
        match service.list_tools(None).await {
            Ok(response) => {
                serde_json::to_string(&response).map_err(|e| e.to_string())
            }
            Err(e) => Err(e.to_string()),
        }
    }
    
    async fn handle_call_tool(
        client: &Option<RunningClient>,
        name: String,
        args: String,
    ) -> Result<String, String> {
        let service = client.as_ref().ok_or("Not connected. Call mcp_connect() first")?;
        
        let arguments: serde_json::Value = serde_json::from_str(&args)
            .map_err(|e| format!("Invalid JSON arguments: {}", e))?;
        
        match service.call_tool(&name, arguments).await {
            Ok(response) => {
                serde_json::to_string(&response).map_err(|e| e.to_string())
            }
            Err(e) => Err(e.to_string()),
        }
    }
    
    fn get() -> &'static GlobalRuntime {
        GLOBAL_RUNTIME.get_or_init(|| GlobalRuntime::new())
    }
}

// Type alias for the running client - use dynamic trait object to support both transport types  
type RunningClient = rmcp::client::RunningClient<
    Box<dyn rmcp::transport::ClientTransport + Send + Sync>,
>;

/// Extract error message from JSON error response
/// Returns the error message string if found, or the original JSON if not found
fn extract_error_message(json_str: &str) -> String {
    match serde_json::from_str::<serde_json::Value>(json_str) {
        Ok(json) => {
            if let Some(error) = json.get("error").and_then(|e| e.as_str()) {
                error.to_string()
            } else {
                json_str.to_string()
            }
        }
        Err(_) => json_str.to_string(),
    }
}

/// Initialize the MCP library
/// Returns 0 on success, non-zero on error
#[no_mangle]
pub extern "C" fn mcp_init() -> i32 {
    0
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

/// Opaque handle for MCP client - now just a marker since we use global runtime
pub struct McpClient {
    _marker: u8,
}

/// Create a new MCP client
/// Returns NULL on error
#[no_mangle]
pub extern "C" fn mcp_client_new() -> *mut McpClient {
    // Initialize the global runtime on first use
    let _ = GlobalRuntime::get();
    // Just return a dummy pointer - all work is done by the global runtime  
    Box::into_raw(Box::new(McpClient { _marker: 0 }))
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
        let error = extract_error_message(r#"{"error": "Invalid arguments"}"#);
        return CString::new(error).unwrap_or_default().into_raw();
    }

    let server_url_str = unsafe {
        match CStr::from_ptr(server_url).to_str() {
            Ok(s) => s.to_string(),
            Err(_) => {
                let error = extract_error_message(r#"{"error": "Invalid server URL"}"#);
                return CString::new(error).unwrap_or_default().into_raw();
            }
        }
    };

    // Parse optional headers_json (can be NULL or a JSON object)
    let headers_str: Option<String> = if headers_json.is_null() {
        None
    } else {
        unsafe {
            match CStr::from_ptr(headers_json).to_str() {
                Ok(json_str) => Some(json_str.to_string()),
                Err(_) => {
                    let error = extract_error_message(r#"{"error": "Invalid headers string"}"#);
                    return CString::new(error).unwrap_or_default().into_raw();
                }
            }
        }
    };

    // Send connection request to global runtime
    let (response_tx, response_rx) = mpsc::channel();
    let runtime = GlobalRuntime::get();
    
    if runtime.sender.send(RuntimeCommand::Connect {
        url: server_url_str,
        headers: headers_str,
        legacy_sse: legacy_sse != 0,
        response: response_tx,
    }).is_err() {
        let error = extract_error_message(r#"{"error": "Failed to send command to runtime"}"#);
        return CString::new(error).unwrap_or_default().into_raw();
    }
    
    // Wait for response
    match response_rx.recv() {
        Ok(Ok(_)) => {
            // Success - return NULL (which means success in the API)
            ptr::null_mut()
        }
        Ok(Err(err)) => {
            // Connection failed
            let error = extract_error_message(&format!(r#"{{"error": "{}"}}"#, err));
            CString::new(error).unwrap_or_default().into_raw()
        }
        Err(_) => {
            // Channel error
            let error = extract_error_message(r#"{"error": "Runtime communication error"}"#);
            CString::new(error).unwrap_or_default().into_raw()
        }
    }
}

/// Disconnect from MCP server
/// Returns NULL on success
#[no_mangle]
pub extern "C" fn mcp_disconnect() -> *mut c_char {
    let (response_tx, response_rx) = mpsc::channel();
    let runtime = GlobalRuntime::get();
    
    if runtime.sender.send(RuntimeCommand::Disconnect {
        response: response_tx,
    }).is_err() {
        let error = extract_error_message(r#"{"error": "Failed to send command to runtime"}"#);
        return CString::new(error).unwrap_or_default().into_raw();
    }
    
    // Wait for response
    match response_rx.recv() {
        Ok(Ok(_)) => ptr::null_mut(), // Success
        Ok(Err(err)) => {
            let error = extract_error_message(&format!(r#"{{"error": "{}"}}"#, err));
            CString::new(error).unwrap_or_default().into_raw()
        }
        Err(_) => {
            let error = extract_error_message(r#"{"error": "Runtime communication error"}"#);
            CString::new(error).unwrap_or_default().into_raw()
        }
    }
}

/// List tools from MCP server
/// Returns JSON string with tools, or error string (must be freed with mcp_free_string)
#[no_mangle]
pub extern "C" fn mcp_list_tools_json(_client_ptr: *mut McpClient) -> *mut c_char {
    let (response_tx, response_rx) = mpsc::channel();
    let runtime = GlobalRuntime::get();
    
    if runtime.sender.send(RuntimeCommand::ListTools {
        response: response_tx,
    }).is_err() {
        let error = extract_error_message(r#"{"error": "Failed to send command to runtime"}"#);
        return CString::new(error).unwrap_or_default().into_raw();
    }
    
    // Wait for response
    match response_rx.recv() {
        Ok(Ok(json)) => {
            CString::new(json).unwrap_or_default().into_raw()
        }
        Ok(Err(err)) => {
            let error = extract_error_message(&format!(r#"{{"error": "{}"}}"#, err));
            CString::new(error).unwrap_or_default().into_raw()
        }
        Err(_) => {
            let error = extract_error_message(r#"{"error": "Runtime communication error"}"#);
            CString::new(error).unwrap_or_default().into_raw()
        }
    }
}

/// Call a tool on the MCP server
/// Returns JSON response, or error string (must be freed with mcp_free_string)
#[no_mangle]
pub extern "C" fn mcp_call_tool_json(
    _client_ptr: *mut McpClient,
    tool_name: *const c_char,
    arguments: *const c_char,
) -> *mut c_char {
    if tool_name.is_null() || arguments.is_null() {
        let error = extract_error_message(r#"{"error": "Invalid arguments"}"#);
        return CString::new(error).unwrap_or_default().into_raw();
    }

    let name = unsafe {
        match CStr::from_ptr(tool_name).to_str() {
            Ok(s) => s.to_string(),
            Err(_) => {
                let error = extract_error_message(r#"{"error": "Invalid tool name"}"#);
                return CString::new(error).unwrap_or_default().into_raw();
            }
        }
    };

    let args = unsafe {
        match CStr::from_ptr(arguments).to_str() {
            Ok(s) => s.to_string(),
            Err(_) => {
                let error = extract_error_message(r#"{"error": "Invalid arguments"}"#);
                return CString::new(error).unwrap_or_default().into_raw();
            }
        }
    };

    let (response_tx, response_rx) = mpsc::channel();
    let runtime = GlobalRuntime::get();
    
    if runtime.sender.send(RuntimeCommand::CallTool {
        name,
        args,
        response: response_tx,
    }).is_err() {
        let error = extract_error_message(r#"{"error": "Failed to send command to runtime"}"#);
        return CString::new(error).unwrap_or_default().into_raw();
    }
    
    // Wait for response
    match response_rx.recv() {
        Ok(Ok(json)) => {
            CString::new(json).unwrap_or_default().into_raw()
        }
        Ok(Err(err)) => {
            let error = extract_error_message(&format!(r#"{{"error": "{}"}}"#, err));
            CString::new(error).unwrap_or_default().into_raw()
        }
        Err(_) => {
            let error = extract_error_message(r#"{"error": "Runtime communication error"}"#);
            CString::new(error).unwrap_or_default().into_raw()
        }
    }
}