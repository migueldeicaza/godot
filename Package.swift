// swift-tools-version: 5.9
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription

let package = Package(
    name: "Godot",
    products: [
        // Products define the executables and libraries a package produces, making them visible to other packages.
        .library(
            name: "Godot",
            targets: ["Godot"])
    ],
    targets: [
        // Targets are the basic building blocks of a package, defining a module or a test suite.
        // Targets can depend on other targets in this package and products from dependencies.
        .target(
            name: "Godot",
            dependencies: ["CGodot"],
            path: "swift/Sources/Godot",
            cSettings: [.headerSearchPath("include")],
            swiftSettings: [.interoperabilityMode(.Cxx)]),
        .target(
            name: "CGodot",
            path: "swift/Sources/CGodot",
            cSettings: [
                .headerSearchPath("include"), 
                .unsafeFlags(
                    ["-Wno-c++11-extensions"])
            ],

            cxxSettings: [
                .headerSearchPath("include/platform/macos"),
                .headerSearchPath("include"), 
                .unsafeFlags(
                    ["-Wno-c++11-extensions"])
            ]
        ),
        .testTarget(
            name: "GodotTests",
            dependencies: ["Godot"]),
    ],
    cxxLanguageStandard: .gnucxx17

)
