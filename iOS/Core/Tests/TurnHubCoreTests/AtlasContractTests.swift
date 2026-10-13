import Foundation
import XCTest
@testable import TurnHubCore

final class AtlasContractTests: XCTestCase {
    // Keep fixtures in protocol/examples, rather than a second iOS-owned copy.
    private func fixture() throws -> Data {
        var root = URL(fileURLWithPath: #filePath)
        for _ in 0..<5 { root.deleteLastPathComponent() }
        return try Data(contentsOf: root.appendingPathComponent("protocol/examples/info.response.json"))
    }

    private func changed(_ modify: (inout [String: Any]) -> Void) throws -> Data {
        var object = try XCTUnwrap(JSONSerialization.jsonObject(with: fixture()) as? [String: Any])
        modify(&object)
        return try JSONSerialization.data(withJSONObject: object)
    }

    func testCanonicalMetadataFixture() throws {
        let info = try AtlasCompatibility.decodeInfo(fixture())
        XCTAssertEqual(info.product, "TurnHub")
        XCTAssertEqual(info.deviceType, "Atlas")
        XCTAssertTrue(info.capabilities.stateSnapshot)
        XCTAssertEqual(try AtlasCompatibility.decodeInfo(JSONEncoder().encode(info)), info)
    }

    func testUnsignedRevisionBoundaries() throws {
        let data = try changed { $0["revision"] = UInt32.max }
        XCTAssertEqual(try AtlasCompatibility.decodeInfo(data).revision, UInt32.max)
        for value in [Int64(-1), Int64(4_294_967_296)] {
            let invalid = try changed { $0["revision"] = value }
            XCTAssertThrowsError(try AtlasCompatibility.decodeInfo(invalid))
        }
    }

    func testRejectsMissingAndMistypedRequiredFields() throws {
        for field in ["atlasId", "bootId", "revision", "capabilities"] {
            let invalid = try changed { $0.removeValue(forKey: field) }
            XCTAssertThrowsError(try AtlasCompatibility.decodeInfo(invalid), field)
        }
        let invalid = try changed { $0["revision"] = "42" }
        XCTAssertThrowsError(try AtlasCompatibility.decodeInfo(invalid))
    }

    func testRejectsInvalidIdentityAndBootEpoch() throws {
        let invalidFields: [(String, Any)] = [
            ("product", "Other"), ("deviceType", "Sigil"), ("atlasId", ""),
            ("bootId", "abc"), ("bootId", String(repeating: "a", count: 32)),
            ("radioProtocolVersion", 0)
        ]
        for (field, value) in invalidFields {
            let invalid = try changed { $0[field] = value }
            XCTAssertThrowsError(try AtlasCompatibility.decodeInfo(invalid)) { error in
                XCTAssertEqual(error as? AtlasCompatibilityError, .invalidMetadata, field)
            }
        }
    }

    func testReportsUnsupportedContracts() throws {
        let api = try changed { $0["apiVersion"] = "2" }
        XCTAssertThrowsError(try AtlasCompatibility.decodeInfo(api)) { error in
            XCTAssertEqual(error as? AtlasCompatibilityError, .unsupportedAPI("2"))
        }
        let logicalProtocol = try changed { $0["protocolVersion"] = "9" }
        XCTAssertThrowsError(try AtlasCompatibility.decodeInfo(logicalProtocol)) { error in
            XCTAssertEqual(error as? AtlasCompatibilityError, .unsupportedProtocol("9"))
        }
        let noSnapshots = try changed {
            var capabilities = $0["capabilities"] as! [String: Any]
            capabilities["stateSnapshot"] = false
            $0["capabilities"] = capabilities
        }
        XCTAssertThrowsError(try AtlasCompatibility.decodeInfo(noSnapshots)) { error in
            XCTAssertEqual(error as? AtlasCompatibilityError, .snapshotsUnavailable)
        }
    }

    func testRadioVersionDoesNotSelectAppProtocol() throws {
        let data = try changed { $0["radioProtocolVersion"] = 99 }
        XCTAssertNoThrow(try AtlasCompatibility.decodeInfo(data))
    }
}
