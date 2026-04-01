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
            url: "https://github.com/sqliteai/sqlite-mcp/releases/download/0.1.7/mcp-apple-xcframework-0.1.7.zip",
            checksum: "3274e630367746309e92a280ba4b9ea5335c15ed4f2dd7c065a02f97b2788a7c"
        ),
        .target(
            name: "mcp",
            dependencies: ["mcpBinary"],
            path: "packages/swift"
        ),
    ]
)
