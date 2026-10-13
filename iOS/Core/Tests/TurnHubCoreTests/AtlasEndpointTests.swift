import XCTest
@testable import TurnHubCore

final class AtlasEndpointTests: XCTestCase {
    func testDefaultAndOriginNormalization() throws {
        XCTAssertEqual(AtlasEndpoint.defaultAtlas.origin.absoluteString, "http://192.168.4.1")
        XCTAssertEqual(try AtlasEndpoint("http://192.168.4.1/"), .defaultAtlas)
        XCTAssertEqual(try AtlasEndpoint("https://atlas.local:8443/").origin.absoluteString,
                       "https://atlas.local:8443")
    }

    func testRejectsCredentialsAPIPathsAndAmbiguousOrigins() {
        for input in [
            "", "192.168.4.1", "file:///tmp/atlas", "ftp://atlas.local", "http://",
            "http://player@192.168.4.1", "http://player:secret@192.168.4.1",
            "http://192.168.4.1/api/v1/info", "http://192.168.4.1//",
            "http://192.168.4.1?game=2", "http://192.168.4.1#table",
            "http://192.168.4.1:0", "http://192.168.4.1:65536"
        ] {
            XCTAssertThrowsError(try AtlasEndpoint(input), input) { error in
                XCTAssertEqual(error as? AtlasEndpoint.ValidationError, .invalidOrigin)
            }
        }
    }
}
