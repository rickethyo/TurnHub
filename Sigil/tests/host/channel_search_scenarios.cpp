// Finding Atlas on its table channel (channel_search.h): the saved channel
// first, then 1-11 in turn, one dwell each, stopping once Atlas answers.
#include "channel_search.h"
#include <cassert>
#include <iostream>

using TurnHubProtocol::CHANNEL_DWELL_MS;
using TurnHubSigil::ChannelSearch;

int main() {
  ChannelSearch search;
  search.begin(0);  // Nothing saved: the default channel.
  assert(search.channel() == TurnHubProtocol::DEFAULT_TABLE_CHANNEL);
  search.begin(12);  // Out of range: the default too.
  assert(search.channel() == 6);

  search.begin(10);
  assert(!search.step(1000, false));
  // The first dwell stays on the saved channel.
  assert(!search.step(1000, true) && search.channel() == 10);
  assert(!search.step(1000 + CHANNEL_DWELL_MS - 1, true));
  assert(search.step(1000 + CHANNEL_DWELL_MS, true) && search.channel() == 11);
  // After 11 it wraps to 1.
  assert(search.step(1000 + 2 * CHANNEL_DWELL_MS, true) && search.channel() == 1);
  // Found: it stays, and a later search starts with a dwell there.
  assert(!search.step(1000 + 3 * CHANNEL_DWELL_MS, false) && search.channel() == 1);
  assert(!search.step(5000, true) && search.channel() == 1);
  assert(search.step(5000 + CHANNEL_DWELL_MS, true) && search.channel() == 2);

  // Every table channel is reached within one sweep.
  bool seen[TurnHubProtocol::MAX_TABLE_CHANNEL + 1] = {};
  uint32_t now = 10000;
  search.step(now, false);
  search.step(now, true);
  for (uint8_t i = 0; i < TurnHubProtocol::MAX_TABLE_CHANNEL; ++i) {
    now += CHANNEL_DWELL_MS;
    assert(search.step(now, true));
    seen[search.channel()] = true;
  }
  for (uint8_t c = TurnHubProtocol::MIN_TABLE_CHANNEL; c <= TurnHubProtocol::MAX_TABLE_CHANNEL; ++c) assert(seen[c]);
  assert(!TurnHubProtocol::validTableChannel(0) && !TurnHubProtocol::validTableChannel(12));

  std::cout << "PASS channel search: saved channel first, sweep 1-11, stops where Atlas answers\n";
  return 0;
}
