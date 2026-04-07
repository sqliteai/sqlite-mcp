// swift-tools-version: 6.1
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription

let package = Package(
    name: "mcp",
    platforms: [.macOS(.v11), .iOS(.v11)],
    products: [
        .library(
            name: "mcp",
            targets: ["mcp"])
    ],
    targets: [
        .binaryTarget(
            name: "mcpBinary",
            url: "https://github.com/sqliteai/sqlite-mcp/releases/download/0.1.9/mcp-apple-xcframework-0.1.9.zip",
            checksum: "bd78a37409d191f59b366a90f1c2cca83875ff9bc870cee121e66ccdc6bfc319"
        ),
        .target(
            name: "mcp",
            dependencies: ["mcpBinary"],
            path: "packages/swift"
        ),
    ]
)
