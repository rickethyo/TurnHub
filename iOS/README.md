# TurnHub for iOS

Native SwiftUI foundation for iPhone and iPad. **Status: foundation only**;
the app shows a development landing screen. It does not yet play a game,
connect to Atlas or persist player data.

Architecture, milestones and acceptance criteria live in the
[development plan](../Documentation/engineering/IOS_DEVELOPMENT.md).

## Requirements and build

- A Mac with Xcode 16 or newer and the iOS Simulator runtime installed.
- Deployment baseline: iOS/iPadOS 17. Swift 6 language mode.
- No third-party packages or project generator. SwiftPM is bundled with Xcode.

Open `iOS/TurnHub.xcodeproj`, select the **TurnHub** scheme and an iPhone or
iPad simulator, then Run. Simulator builds do not require an Apple Developer
membership or a signing team. Use a current Xcode release for physical devices
running a newer OS. The bundle identifier `com.turnhub.ios` is provisional;
confirm ownership before registering an App Store app.

From the repository root:

```sh
swift test --package-path iOS/Core
xcodebuild -project iOS/TurnHub.xcodeproj -scheme TurnHub \
  -configuration Debug -sdk iphonesimulator \
  -destination 'generic/platform=iOS Simulator' \
  -derivedDataPath iOS/build CODE_SIGNING_ALLOWED=NO build
```

The package tests read the canonical metadata fixture directly from
[`protocol/examples/`](../protocol/examples); run them in this repository.
The package has no UIKit or SwiftUI dependency. Swift tests can also run on a
Linux host with Swift 6 installed; building the app requires macOS/Xcode.

## Source layout

| Path | Purpose |
| --- | --- |
| `App/` | SwiftUI entry and development landing screen |
| `Config/` | Deployment, build identity and Info.plist |
| `Core/Package.swift` | Local `TurnHubCore` Swift package; no remote dependencies |
| `Core/Sources/TurnHubCore/Protocol/` | Typed metadata and compatibility checks |
| `Core/Sources/TurnHubCore/Connection/` | Validated Atlas endpoint value |
| `Core/Tests/TurnHubCoreTests/` | Shared-fixture and endpoint boundary tests |
| `TurnHub.xcodeproj/` | Checked-in Xcode project and shared scheme |

Add sources to the Xcode app target when extending `App/`. SwiftPM discovers
core sources/tests automatically. Keep game rules, storage and network code
out of SwiftUI views; follow the plan's ownership and dependency boundaries.
