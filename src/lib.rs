//
//  lib.rs (Fixed Windows-compatible version)
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
use rmcp::transport::streamable_http_client::StreamableHttpClientTransportConfig;
use rmcp::{ServiceExt, RoleClient, serve_client};
use rmcp::model::{ClientInfo, ClientCapabilities, Implementation, ProtocolVersion, CallToolRequestParam, ListToolsRequest};
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

/// Type alias for a running MCP service
type RunningClient = rmcp::service::RunningService<RoleClient, ()>;

/// Global MCP client state
static GLOBAL_CLIENT: Mutex<Option<RunningClient>> = Mutex::new(None);

/// Simple client service (no-op implementation since we only use client features)
struct SimpleClient;

impl rmcp::Service<RoleClient> for SimpleClient {
    async fn handle_request(
        &self,
        _request: rmcp::model::ServerRequest,
        _context: rmcp::service::RequestContext<RoleClient>,
    ) -> Result<rmcp::model::ClientResult, rmcp::ErrorData> {
        // Client typically doesn't handle requests from server
        Err(rmcp::ErrorData::new(
            (-32601).into(),
            "Method not found".to_string().into(),
            None,
        ))
    }

    async fn handle_notification(
        &self,
        _notification: rmcp::model::ServerNotification,
        _context: rmcp::service::NotificationContext<RoleClient>,
    ) -> Result<(), rmcp::ErrorData> {
        // Client can ignore server notifications
        Ok(())
    }

    fn get_info(&self) -> ClientInfo {
        ClientInfo {
            protocol_version: ProtocolVersion::default(),
            capabilities: ClientCapabilities::default(),
            client_info: Implementation {
                name: "sqlite-mcp".to_string(),
                title: None,
                version: "0.1.4".to_string(),
                icons: None,
                website_url: None,
            },
        }
    }
}

impl GlobalRuntime {
    /// Initialize the global runtime
    fn init() -> &'static GlobalRuntime {
        GLOBAL_RUNTIME.get_or_init(|| {
            let (sender, receiver) = mpsc::channel::<RuntimeCommand>();
            
            let worker_handle = thread::spawn(move || {
                let rt = tokio::runtime::Builder::new_current_thread()
                    .enable_all()
                    .build()
                    .expect("Failed to create Tokio runtime");
                
                rt.block_on(async {
                    Self::worker_loop(receiver).await;
                });
            });
            
            GlobalRuntime {
                sender,
                _worker_handle: worker_handle,
            }
        })
    }
    
    /// Worker loop that runs in the dedicated thread
    async fn worker_loop(receiver: Receiver<RuntimeCommand>) {
        let mut client: Option<RunningClient> = None;
        
        while let Ok(command) = receiver.recv() {
            match command {
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
                    let result = Self::handle_disconnect(&mut client).await;
                    let _ = response.send(result);
                }
            }
        }
    }
    
    async fn handle_connect(
        client: &mut Option<RunningClient>,
        url: String,
        headers: Option<String>,
        legacy_sse: bool,
    ) -> Result<String, String> {
        // Disconnect existing client
        if let Some(existing_client) = client.take() {
            let _ = existing_client.cancel().await;
        }
        
        // Parse headers if provided
        let headers_map = if let Some(headers_str) = headers {
            serde_json::from_str::<std::collections::HashMap<String, String>>(&headers_str)
                .map_err(|e| format!("Invalid headers JSON: {}", e))?
        } else {
            std::collections::HashMap::new()
        };
        
        let new_client = if legacy_sse {
            Self::connect_sse(url, if headers_map.is_empty() { None } else { Some(headers_map) }).await?
        } else {
            Self::connect_streamable_http(url, if headers_map.is_empty() { None } else { Some(headers_map) }).await?
        };
        
        *client = Some(new_client);
        Ok("null".to_string()) // Return null for success (matches existing API)
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
            if !header_map.is_empty() {
                client_builder = client_builder.default_headers(header_map);
            }
        }
        
        let http_client = client_builder.build().map_err(|e| e.to_string())?;
        
        let config = rmcp::transport::sse_client::SseClientConfig {
            sse_endpoint: url.into(),
            ..Default::default()
        };
        
        let transport = SseClientTransport::start_with_client(http_client, config).await
            .map_err(|e| e.to_string())?;
        
        let service = SimpleClient;
        service.serve(transport).await
            .map_err(|e| format!("Failed to connect to MCP server: {}", e))
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
        let config = StreamableHttpClientTransportConfig {
            uri: url.into(),
            auth_header: auth_header_value,
            ..Default::default()
        };
        let transport = StreamableHttpClientTransport::with_client(http_client, config);
        
        let service = SimpleClient;
        service.serve(transport).await
            .map_err(|e| format!("Failed to connect to MCP server: {}", e))
    }
    
    async fn handle_list_tools(client: &Option<RunningClient>) -> Result<String, String> {
        let service = client.as_ref().ok_or("Not connected. Call mcp_connect() first")?;
        
        let request = ListToolsRequest::default();
        let response = service.send_request(request.into()).await
            .map_err(|e| format!("Failed to list tools: {}", e))?;
        
        if let rmcp::model::ServerResult::ListToolsResult(tools_result) = response {
            serde_json::to_string(&tools_result)
                .map_err(|e| format!("Failed to serialize tools: {}", e))
        } else {
            Err("Unexpected response type".to_string())
        }
    }
    
    async fn handle_call_tool(
        client: &Option<RunningClient>,
        name: String,
        args: String,
    ) -> Result<String, String> {
        let service = client.as_ref().ok_or("Not connected. Call mcp_connect() first")?;
        
        let arguments = if args.trim().is_empty() {
            None
        } else {
            Some(serde_json::from_str(&args)
                .map_err(|e| format!("Invalid JSON arguments: {}", e))?)
        };
        
        let param = CallToolRequestParam {
            name: name.into(),
            arguments,
        };
        
        let request = rmcp::model::CallToolRequest { params: param };
        let response = service.send_request(request.into()).await
            .map_err(|e| format!("Failed to call tool: {}", e))?;
        
        if let rmcp::model::ServerResult::CallToolResult(tool_result) = response {
            serde_json::to_string(&tool_result)
                .map_err(|e| format!("Failed to serialize tool result: {}", e))
        } else {
            Err("Unexpected response type".to_string())
        }
    }
    
    async fn handle_disconnect(client: &mut Option<RunningClient>) -> Result<(), String> {
        if let Some(existing_client) = client.take() {
            existing_client.cancel().await
                .map_err(|e| format!("Failed to disconnect: {:?}", e))?;
        }
        Ok(())
    }
    
    /// Send a command to the worker thread and wait for response
    fn send_command<T>(command: RuntimeCommand) -> Result<T, String>
    where
        T: Send + 'static,
        RuntimeCommand: 'static,
    {
        let runtime = Self::init();
        
        // This is a simplified version - in real implementation you'd need proper type handling
        // For now, we'll implement each function separately
        runtime.sender.send(command)
            .map_err(|_| "Runtime worker thread has died".to_string())?;
            
        // The actual response handling is done in each specific function
        unreachable!() // This won't be reached in practice due to function-specific implementations
    }
}

// === FFI FUNCTIONS ===

/// Create a new MCP client connection
/// Returns NULL on success, or JSON error string on failure
#[no_mangle]
pub extern "C" fn mcp_connect(
    server_url: *const c_char,
    headers: *const c_char,
    legacy_sse: i32,
) -> *mut c_char {
    let url = match unsafe { CStr::from_ptr(server_url).to_str() } {
        Ok(s) => s.to_string(),
        Err(_) => return CString::new("{\"error\": \"Invalid server URL\"}").unwrap().into_raw(),
    };
    
    let headers_opt = if headers.is_null() {
        None
    } else {
        match unsafe { CStr::from_ptr(headers).to_str() } {
            Ok(s) if s.trim().is_empty() => None,
            Ok(s) => Some(s.to_string()),
            Err(_) => return CString::new("{\"error\": \"Invalid headers\"}").unwrap().into_raw(),
        }
    };
    
    let runtime = GlobalRuntime::init();
    let (tx, rx) = mpsc::channel();
    
    let command = RuntimeCommand::Connect {
        url,
        headers: headers_opt,
        legacy_sse: legacy_sse != 0,
        response: tx,
    };
    
    if runtime.sender.send(command).is_err() {
        return CString::new("{\"error\": \"Runtime worker thread has died\"}").unwrap().into_raw();
    }
    
    match rx.recv() {
        Ok(Ok(result)) => {
            if result == "null" {
                ptr::null_mut() // Success
            } else {
                CString::new(result).unwrap().into_raw()
            }
        }
        Ok(Err(err)) => {
            let error_json = format!("{{\"error\": \"{}\"}}", err.replace('"', "\\\""));
            CString::new(error_json).unwrap().into_raw()
        }
        Err(_) => CString::new("{\"error\": \"Failed to receive response from runtime\"}").unwrap().into_raw(),
    }
}

/// List available tools from the connected MCP server
/// Returns JSON string with tools list, or JSON error on failure
#[no_mangle]
pub extern "C" fn mcp_list_tools_json() -> *mut c_char {
    let runtime = GlobalRuntime::init();
    let (tx, rx) = mpsc::channel();
    
    let command = RuntimeCommand::ListTools { response: tx };
    
    if runtime.sender.send(command).is_err() {
        let error_json = "{\"error\": \"Runtime worker thread has died\"}";
        return CString::new(error_json).unwrap().into_raw();
    }
    
    match rx.recv() {
        Ok(Ok(result)) => CString::new(result).unwrap().into_raw(),
        Ok(Err(err)) => {
            let error_json = format!("{{\"error\": \"{}\"}}", err.replace('"', "\\\""));
            CString::new(error_json).unwrap().into_raw()
        }
        Err(_) => {
            let error_json = "{\"error\": \"Failed to receive response from runtime\"}";
            CString::new(error_json).unwrap().into_raw()
        }
    }
}

/// Call a tool on the connected MCP server
/// Returns JSON string with result, or JSON error on failure
#[no_mangle]
pub extern "C" fn mcp_call_tool_json(tool_name: *const c_char, arguments: *const c_char) -> *mut c_char {
    let name = match unsafe { CStr::from_ptr(tool_name).to_str() } {
        Ok(s) => s.to_string(),
        Err(_) => {
            let error_json = "{\"error\": \"Invalid tool name\"}";
            return CString::new(error_json).unwrap().into_raw();
        }
    };
    
    let args = if arguments.is_null() {
        String::new()
    } else {
        match unsafe { CStr::from_ptr(arguments).to_str() } {
            Ok(s) => s.to_string(),
            Err(_) => {
                let error_json = "{\"error\": \"Invalid arguments\"}";
                return CString::new(error_json).unwrap().into_raw();
            }
        }
    };
    
    let runtime = GlobalRuntime::init();
    let (tx, rx) = mpsc::channel();
    
    let command = RuntimeCommand::CallTool {
        name,
        args,
        response: tx,
    };
    
    if runtime.sender.send(command).is_err() {
        let error_json = "{\"error\": \"Runtime worker thread has died\"}";
        return CString::new(error_json).unwrap().into_raw();
    }
    
    match rx.recv() {
        Ok(Ok(result)) => CString::new(result).unwrap().into_raw(),
        Ok(Err(err)) => {
            let error_json = format!("{{\"error\": \"{}\"}}", err.replace('"', "\\\""));
            CString::new(error_json).unwrap().into_raw()
        }
        Err(_) => {
            let error_json = "{\"error\": \"Failed to receive response from runtime\"}";
            CString::new(error_json).unwrap().into_raw()
        }
    }
}

/// Disconnect from the MCP server
/// Returns NULL on success, or JSON error on failure
#[no_mangle]
pub extern "C" fn mcp_disconnect() -> *mut c_char {
    let runtime = GlobalRuntime::init();
    let (tx, rx) = mpsc::channel();
    
    let command = RuntimeCommand::Disconnect { response: tx };
    
    if runtime.sender.send(command).is_err() {
        let error_json = "{\"error\": \"Runtime worker thread has died\"}";
        return CString::new(error_json).unwrap().into_raw();
    }
    
    match rx.recv() {
        Ok(Ok(())) => ptr::null_mut(), // Success
        Ok(Err(err)) => {
            let error_json = format!("{{\"error\": \"{}\"}}", err.replace('"', "\\\""));
            CString::new(error_json).unwrap().into_raw()
        }
        Err(_) => {
            let error_json = "{\"error\": \"Failed to receive response from runtime\"}";
            CString::new(error_json).unwrap().into_raw();
        }
    }
}

/// Get the version of the MCP extension
#[no_mangle]
pub extern "C" fn mcp_version() -> *mut c_char {
    CString::new("0.1.4").unwrap().into_raw()
}

/// Free a C string allocated by this library
#[no_mangle]
pub extern "C" fn mcp_free_string(ptr: *mut c_char) {
    if !ptr.is_null() {
        unsafe {
            drop(CString::from_raw(ptr));
        }
    }
}

// For the virtual table functions, we'll need to implement those separately
// since they require more complex state management

// TODO: Implement streaming and virtual table functions:
// - mcp_list_tools (virtual table)
// - mcp_call_tool (virtual table) 
// - mcp_list_tools_respond (virtual table)
// - mcp_call_tool_respond (virtual table)
// These will require storing the client state globally and implementing
// the SQLite virtual table interface