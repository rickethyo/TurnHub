// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "TurnHubCore",
    platforms: [.iOS(.v17), .macOS(.v14)],
    products: [.library(name: "TurnHubCore", targets: ["TurnHubCore"])],
    targets: [
        .target(name: "TurnHubCore"),
        .testTarget(name: "TurnHubCoreTests", dependencies: ["TurnHubCore"])
    ]
)
