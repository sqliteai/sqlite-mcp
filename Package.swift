//
//  Package.swift
//  sqlitemcp
//
//  Created by Gioele Cantoni on 05/11/25.
//

// swift-tools-version: 6.1
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription

let package = Package(
    name: "mcp",
    platforms: [.macOS(.v11), .iOS(.v11)],
    products: [
        // Products can be used to vend plugins, making them visible to other packages.
        .plugin(
            name: "mcpPlugin",
            targets: ["mcpPlugin"]),
        .library(
            name: "mcp",
            targets: ["mcp"])
    ],
    targets: [
        // Build tool plugin that invokes the Makefile
        .plugin(
            name: "mcpPlugin",
            capability: .buildTool(),
            path: "packages/swift/plugin"
        ),
        // mcp library target
        .target(
            name: "mcp",
            dependencies: [],
            path: "packages/swift/extension",
            plugins: ["mcpPlugin"]
        ),
    ]
)
