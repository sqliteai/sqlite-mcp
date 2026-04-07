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
            url: "https://github.com/sqliteai/sqlite-mcp/releases/download/0.1.8/mcp-apple-xcframework-0.1.8.zip",
            checksum: "e7260c98e929b262e4e39f44a88a5e755b2bd94c12bac67ff36d19660a2793e8"
        ),
        .target(
            name: "mcp",
            dependencies: ["mcpBinary"],
            path: "packages/swift"
        ),
    ]
)
