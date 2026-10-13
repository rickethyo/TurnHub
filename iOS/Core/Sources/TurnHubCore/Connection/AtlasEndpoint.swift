import Foundation

/// An HTTP(S) origin, never a URL containing credentials or an API path.
public struct AtlasEndpoint: Equatable, Sendable {
    public let origin: URL

    public enum ValidationError: Error, Equatable {
        case invalidOrigin
    }

    public init(_ input: String) throws {
        guard var parts = URLComponents(string: input),
              let scheme = parts.scheme?.lowercased(),
              ["http", "https"].contains(scheme),
              let host = parts.host, !host.isEmpty,
              parts.user == nil, parts.password == nil,
              parts.query == nil, parts.fragment == nil,
              parts.path.isEmpty || parts.path == "/",
              parts.port.map({ (1...65535).contains($0) }) ?? true else {
            throw ValidationError.invalidOrigin
        }
        parts.scheme = scheme
        parts.path = ""
        guard let url = parts.url else { throw ValidationError.invalidOrigin }
        origin = url
    }

    /// Atlas's default access-point address. Constructed through the same validator.
    public static let defaultAtlas = try! AtlasEndpoint("http://192.168.4.1")
}
