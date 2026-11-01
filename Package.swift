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
            url: "https://github.com/sqliteai/sqlite-mcp/releases/download/0.2.1/mcp-apple-xcframework-0.2.1.zip",
            checksum: "4608d96f8d8bf0b8f56d472b195a0afa296b392114553b9f5f110492461bb1f6"
        ),
        .target(
            name: "mcp",
            dependencies: ["mcpBinary"],
            path: "packages/swift"
        ),
    ]
)
