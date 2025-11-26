//
//  mcp.swift
//  sqlitemcp
//
//  Created by Gioele Cantoni on 05/11/25.
//

// This file serves as a placeholder for the mcp target.
// The actual SQLite extension is built using the Makefile through the build plugin.

import Foundation

/// Placeholder structure for mcp
public struct mcp {
    /// Returns the path to the built mcp dylib inside the XCFramework
    public static var path: String {
        #if os(macOS)
        return "mcp.xcframework/macos-arm64_x86_64/mcp.framework/mcp"
        #elseif targetEnvironment(simulator)
        return "mcp.xcframework/ios-arm64_x86_64-simulator/mcp.framework/mcp"
        #else
        return "mcp.xcframework/ios-arm64/mcp.framework/mcp"
        #endif
    }
}
