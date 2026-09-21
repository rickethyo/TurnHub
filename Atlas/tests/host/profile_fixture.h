#pragma once
#include "profile_store.h"
#include <map>
namespace ProfileFixture {
struct Profile { String name, hash; TurnHubProfiles::ProfileStats stats; };
extern std::map<std::string,Profile> profiles;
extern std::map<std::string,String> bindings;
}
