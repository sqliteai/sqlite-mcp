//
//  mcp.swift
//  sqlitemcp
//
//  Created by Gioele Cantoni on 05/11/25.
//

import PackagePlugin
import Foundation

@main
struct mcp: BuildToolPlugin {
    /// Entry point for creating build commands for targets in Swift packages.
    func createBuildCommands(context: PluginContext, target: Target) async throws -> [Command] {
        let packageDirectory = context.package.directoryURL
        let outputDirectory = context.pluginWorkDirectoryURL
        return createmcpBuildCommands(packageDirectory: packageDirectory, outputDirectory: outputDirectory)
    }
}

#if canImport(XcodeProjectPlugin)
import XcodeProjectPlugin

extension mcp: XcodeBuildToolPlugin {
    // Entry point for creating build commands for targets in Xcode projects.
    func createBuildCommands(context: XcodePluginContext, target: XcodeTarget) throws -> [Command] {
        let outputDirectory = context.pluginWorkDirectoryURL
        return createmcpBuildCommands(packageDirectory: nil, outputDirectory: outputDirectory)
    }
}

#endif

/// Shared function to create mcp build commands
func createmcpBuildCommands(packageDirectory: URL?, outputDirectory: URL) -> [Command] {

    // For Xcode projects, use current directory; for Swift packages, use provided packageDirectory
    let workingDirectory = packageDirectory?.path ?? "$(pwd)"
    let packageDirInfo = packageDirectory != nil ? "Package directory: \(packageDirectory!.path)" : "Working directory: $(pwd)"

    return [
        .prebuildCommand(
            displayName: "Building mcp XCFramework",
            executable: URL(fileURLWithPath: "/bin/bash"),
            arguments: [
                "-c",
                """
                set -e
                echo "Starting mcp XCFramework prebuild..."
                echo "\(packageDirInfo)"

                # Clean and create output directory
                rm -rf "\(outputDirectory.path)"
                mkdir -p "\(outputDirectory.path)"

                # Build directly from source directory with custom output paths
                cd "\(workingDirectory)" && \
                echo "Building XCFramework..." && \
                make xcframework DIST_DIR="\(outputDirectory.path)" BUILD_DIR="\(outputDirectory.path)/build" && \
                rm -rf "\(outputDirectory.path)/build" && \
                echo "XCFramework build completed successfully!"
                """
            ],
            outputFilesDirectory: outputDirectory
        )
    ]
}
