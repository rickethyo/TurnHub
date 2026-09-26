#pragma once
#include "profile_store.h"
#include <map>
namespace ProfileFixture {
struct Profile { String name, hash; TurnHubProfiles::ProfileStats stats; TurnHubProfiles::ModerationStats moderation; TurnHubProfiles::ProfilePolicy policy; bool policyReadable = true; TurnHubProfiles::AccessibilityPrefs accessibility; };
extern std::map<std::string,Profile> profiles;
extern std::map<std::string,String> bindings;
extern bool gameSettingsWritable;
extern void (*afterStatsSave)();  // Fault injection after a durable profile write.
String key(const uint8_t *mac,uint8_t slot);
}
