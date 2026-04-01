// mcp.swift
// Provides the path to the mcp SQLite extension for use with sqlite3_load_extension.

import Foundation

public struct mcp {
    /// Returns the absolute path to the mcp dylib for use with sqlite3_load_extension.
    public static var path: String {
        #if os(macOS)
        return Bundle.main.bundlePath + "/Contents/Frameworks/mcp.framework/mcp"
        #else
        return Bundle.main.bundlePath + "/Frameworks/mcp.framework/mcp"
        #endif
    }
}
