#pragma once

// Atlas's secure-link crypto (shared/include/secure_link_mbedtls.h).
// Firmware-only: it needs mbedTLS; test_globals.cpp stubs it for host tests.

namespace TurnHubAtlas {

// Runs the known-answer self-test (RFC vectors, then a fresh key agreement
// and one sealed packet) and logs ATLAS|SECURE_LINK|SELF_TEST|PASS|<ms> or
// FAIL|<step>. The secure link must not be used unless it passed.
bool runSecureLinkSelfTest();

}  // namespace TurnHubAtlas
