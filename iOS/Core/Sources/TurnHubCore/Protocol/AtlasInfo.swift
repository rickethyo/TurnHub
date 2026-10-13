import Foundation

/// Mirrors protocol/info-v1.schema.json; radio version is informational to apps.
public struct AtlasInfo: Codable, Equatable, Sendable {
    public let product: String
    public let deviceType: String
    public let atlasId: String
    public let firmwareVersion: String
    public let apiVersion: String
    public let protocolVersion: String
    public let radioProtocolVersion: UInt32
    public let bootId: String
    public let revision: UInt32
    public let capabilities: Capabilities

    public struct Capabilities: Codable, Equatable, Sendable {
        public let stateSnapshot: Bool
        public let sessionControls: Bool
        public let intentEnvelope: Bool
        public let events: Bool
        public let requestDeduplication: Bool
    }
}

public enum AtlasCompatibilityError: Error, Equatable, Sendable {
    case invalidMetadata
    case unsupportedAPI(String)
    case unsupportedProtocol(String)
    case snapshotsUnavailable
}

public enum AtlasCompatibility {
    public static let apiVersion = "1"
    public static let protocolVersion = "0.1"

    /// Decode and validate before exposing a successful connection to the UI.
    public static func decodeInfo(_ data: Data) throws -> AtlasInfo {
        let info = try JSONDecoder().decode(AtlasInfo.self, from: data)
        try requireCompatible(info)
        return info
    }

    public static func requireCompatible(_ info: AtlasInfo) throws {
        let hexDigits = Set("0123456789ABCDEF".unicodeScalars)
        guard info.product == "TurnHub", info.deviceType == "Atlas",
              !info.atlasId.isEmpty, info.radioProtocolVersion >= 1,
              info.bootId.unicodeScalars.count == 32,
              info.bootId.unicodeScalars.allSatisfy(hexDigits.contains) else {
            throw AtlasCompatibilityError.invalidMetadata
        }
        guard info.apiVersion == apiVersion else {
            throw AtlasCompatibilityError.unsupportedAPI(info.apiVersion)
        }
        guard info.protocolVersion == protocolVersion else {
            throw AtlasCompatibilityError.unsupportedProtocol(info.protocolVersion)
        }
        guard info.capabilities.stateSnapshot else {
            throw AtlasCompatibilityError.snapshotsUnavailable
        }
    }
}
