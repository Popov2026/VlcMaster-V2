// swift-tools-version:5.7
// Paquet de test du cœur de HomeRun Player (sans interface ni VLCKit) : `swift test`.
// L'appli elle-même est construite par xcodebuild à partir de project.yml (XcodeGen).
import PackageDescription

let package = Package(
    name: "HomeRunCore",
    platforms: [.macOS(.v11)],
    products: [.library(name: "HomeRunCore", targets: ["HomeRunCore"])],
    targets: [
        .target(name: "HomeRunCore", path: "Sources/Core"),
        .testTarget(name: "CoreTests", dependencies: ["HomeRunCore"], path: "Tests/CoreTests"),
    ]
)
