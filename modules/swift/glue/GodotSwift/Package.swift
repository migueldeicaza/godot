// swift-tools-version:5.2
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription
import Foundation
let dirs = [
        "-I../../../..", 
        "-I../../../../../..", 
        "-I/Users/miguel/cvs/godot/platform/osx", 
        "-I/Users/miguel/cvs/godot/"
        ];

let cdeps = [
    CXXSetting.unsafeFlags(dirs)
]

let package = Package(
    name: "GodotSwift",
    products: [
        // Products define the executables and libraries produced by a package, and make them visible to other packages.
        .library(
            name: "GodotSwift",
            targets: ["GodotSwift"]),
    ],
    dependencies: [
        // Dependencies declare other packages that this package depends on.
        // .package(url: /* package url */, from: "1.0.0"),
    ],
    targets: [
        // Targets are the basic building blocks of a package. A target can define a module or a test suite.
        // Targets can depend on other targets in this package, and on products in packages which this package depends on.
        .target(
            name: "GodotSwift",
            dependencies: ["CApiGodotSwift"],
            cxxSettings: cdeps,
            swiftSettings: [SwiftSetting.unsafeFlags(dirs)]),
	.target(
 	    name: "CApiGodotSwift",
        dependencies: [],
        cxxSettings: cdeps),
        .testTarget(
            name: "GodotSwiftTests",
            dependencies: ["GodotSwift"]),
    ]
)
