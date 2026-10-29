// Touch Sigil (E32R28T stand-in): calibration math and the on-screen keys.
#include <assert.h>
#include <stdio.h>

#include "sigil_touch_pad.h"
#include "touch_calibration.h"
#include "table_snapshot.h"
#include "received_packet.h"

using namespace TurnHubSigil;

int main() {
  // Raw readings at the four inset crosses on the first board (2026-10-28):
  // raw Y rises along screen x, raw X falls down screen y.
  const uint16_t rawX[TOUCH_CAL_POINTS] = {3500, 3500, 380, 380};
  const uint16_t rawY[TOUCH_CAL_POINTS] = {720, 3470, 3470, 720};
  TouchCalibration cal;
  assert(solveTouchCalibration(rawX, rawY, SIGIL_SCREEN_W, SIGIL_SCREEN_H, cal));
  assert(cal.swapXY == 1 && cal.xAtFirst < cal.xAtLast && cal.yAtFirst > cal.yAtLast);
  int16_t x = 0, y = 0;
  mapTouch(cal, 1940, 2095, SIGIL_SCREEN_W, SIGIL_SCREEN_H, x, y);  // The centre.
  assert(x > 110 && x < 130 && y > 150 && y < 170);
  // Presses that do not separate the axes are refused.
  const uint16_t flat[TOUCH_CAL_POINTS] = {2000, 2010, 2020, 2000};
  assert(!solveTouchCalibration(flat, flat, SIGIL_SCREEN_W, SIGIL_SCREEN_H, cal));

  // Blank space and gaps are inert. A drag or semantic screen change must
  // never turn a held contact into another action, even at the same position.
  TouchMap map;
  map.add({10,112,52,56},2,100); map.add({178,112,52,56},3,101);
  assert(map.at(120,140)==nullptr && map.at(10,168)==nullptr);
  TouchContact contact;
  assert(contact.sample(map,true,30,140)==2);
  assert(contact.sample(map,true,31,142)==2);
  assert(contact.sample(map,true,200,140)==TOUCH_NONE);
  assert(contact.sample(map,true,30,140)==TOUCH_NONE);
  contact.sample(map,false,0,0);
  assert(contact.sample(map,true,30,140)==2);
  map.regions[0].meaning=200;
  assert(contact.sample(map,true,30,140)==TOUCH_NONE);
  contact.sample(map,false,0,0);
  assert(contact.sample(map,true,120,140)==TOUCH_NONE);
  assert(contact.sample(map,true,200,140)==TOUCH_NONE);
  contact.sample(map,false,0,0);
  assert(contact.sample(map,true,200,140)==3);

  using namespace TurnHubProtocol;
  TableDisplayPacket packet{};
  packet.version=VERSION;packet.type=PacketType::TableDisplay;packet.sigilId=0;
  packet.game=1;
  packet.mode=static_cast<uint8_t>(DisplayMode::Running);packet.total=16;packet.revision=1;
  for(uint8_t i=0;i<TABLE_PAGE_ROWS;++i) { packet.rows[i].player=i+1;packet.rows[i].life=40; }
  TableSnapshot roster;
  assert(validTableDisplay(packet) && !roster.accept(packet) && !roster.valid);
  packet.page=1;
  for(uint8_t i=0;i<TABLE_PAGE_ROWS;++i) packet.rows[i].player=i+9;
  assert(roster.accept(packet) && roster.valid && roster.total==16 && roster.rows[15].life==40);
  assert(!roster.accept(packet));
  // A new page cannot replace half of a published roster.
  packet.revision=2;packet.rows[0].life=17;
  assert(!roster.accept(packet) && roster.rows[8].life==40);
  packet.page=0;
  for(uint8_t i=0;i<TABLE_PAGE_ROWS;++i) packet.rows[i].player=i+1;
  assert(roster.accept(packet) && roster.rows[8].life==17);
  packet.rows[0].player=0;assert(!validTableDisplay(packet));
  packet.rows[0].player=1;packet.total=17;assert(!validTableDisplay(packet));
  packet.total=16;packet.rows[0].life=1000001;assert(!validTableDisplay(packet));
  packet={}; packet.version=VERSION;packet.type=PacketType::TableDisplay;
  packet.revision=3;packet.mode=static_cast<uint8_t>(DisplayMode::Lobby);
  packet.game=2;
  assert(roster.accept(packet) && roster.total==0 && roster.rows[15].player==0);
  assert(ReceivedPacket::acceptedSize(sizeof(packet)+TurnHubSecureLink::SECURE_OVERHEAD));
  uint8_t sealed[sizeof(packet)+TurnHubSecureLink::SECURE_OVERHEAD]={};
  uint8_t sender[6]={}; ReceivedPacket queued;
  assert(queued.assign(sender,sealed,sizeof(sealed)) && queued.length==sizeof(sealed));
  puts("PASS touch: calibration, semantic hit regions, held-contact cancellation and atomic 16-player rosters");
  return 0;
}
