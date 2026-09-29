// Included by scenarios.cpp to reuse the real HTTP/auth/application fixture.
#include "firmware_signing_key.h"
#include "test_package_crypto.h"
void fixtureConnectOta(TurnHub::SigilRecord &r);

static std::vector<uint8_t> otaTestPackage(uint8_t product) {
  using namespace TurnHubFirmwarePackage;
  Header h{}; memcpy(h.magic, PACKAGE_MAGIC, 8); h.format=HEADER_FORMAT;
  h.product=product; h.version={0,9,0}; h.radioProtocol=2; h.imageSize=4096;
  memcpy(h.keyId, KEY_ID, KEY_ID_BYTES);
  std::vector<uint8_t> image(h.imageSize, 0xE9);
  TurnHubTest::Sha256::digest(image.data(), image.size(), h.imageHash);
  uint8_t digest[HASH_BYTES]; TurnHubTest::Sha256::digest(reinterpret_cast<uint8_t *>(&h), SIGNED_BYTES, digest);
  TurnHubTest::TestPackageCrypto::sign(PUBLIC_KEY, digest, h.signature);
  std::vector<uint8_t> out(reinterpret_cast<uint8_t *>(&h), reinterpret_cast<uint8_t *>(&h)+sizeof(h));
  out.insert(out.end(), image.begin(), image.end()); return out;
}

static void otaServiceScenarios() {
  using namespace TurnHub;
  using namespace TurnHubProtocol;
  freshLobby(2); enterEmptyLobby(); resetPresence();
  TurnHubWebApi::configureDevices(manageDevices, [](){return pairingWindowMs;});
  TurnHubWebApi::configurePresence(presenceHooks());
  beginSigilUpdates("OTA test AP", "test-password");
  String adminId, playerId;
  const String admin=registerPhone("OTA admin",adminId), player=registerPhone("OTA player",playerId);
  TurnHubAccounts::Account account; account.permissions=TurnHubAccounts::Admin;
  assert(TurnHubAccounts::save(adminId,account));
  auto upload=[&](const String &token, std::vector<uint8_t> bytes) {
    server.headers.clear(); server.headers["X-TurnHub-Token"]=token; server.arguments.clear();
    auto &u=server.uploadState; u.status=UPLOAD_FILE_START; server.uploads["/api/sigil-firmware"]();
    u.status=UPLOAD_FILE_WRITE;u.buf=bytes.data();u.currentSize=bytes.size();server.uploads["/api/sigil-firmware"]();
    u.status=UPLOAD_FILE_END;server.uploads["/api/sigil-firmware"]();
    server.routes["1/api/sigil-firmware"](); return server.status;
  };
  assert(upload(player,otaTestPackage(3))==403);
  assert(upload(admin,otaTestPackage(3))==403); // Not verified yet.
  verifyAtTable(admin);
  assert(upload(admin,otaTestPackage(1))==400); // Atlas image rejected.
  auto bad=otaTestPackage(3);bad.back()^=1;assert(upload(admin,bad)==400);
  assert(upload(admin,otaTestPackage(2))==200);
  auto &r=fixtureRecords[0];r.helloInfoValid=true;r.capabilities=CAPABILITY_DISPLAY_OLED;
  r.firmwareMajor=0;r.firmwareMinor=8;r.firmwarePatch=0;
  fixtureConnectOta(r);r.sessionGeneration=1;r.confirmedSessionGeneration=1;
  assert(request("/api/sigil-update",admin,{{"module","0"}})==409); // E-ink cannot go to OLED.
  assert(upload(admin,otaTestPackage(3))==200);
  r.capabilities=0;assert(request("/api/sigil-update",admin,{{"module","0"}})==409); // Nor vice versa.
  r.capabilities=CAPABILITY_DISPLAY_OLED|CAPABILITY_HARNESS;
  assert(request("/api/sigil-update",admin,{{"module","0"}})==409);
  r.capabilities=CAPABILITY_DISPLAY_OLED;
  assert(request("/api/sigil-update",admin,{{"module","abc"}})==400);
  hubState=HubState::Running;assert(request("/api/sigil-update",admin,{{"module","0"}})==409);hubState=HubState::Lobby;
  assert(request("/api/sigil-update",admin,{{"module","0"}})==200 && sigilUpdatesBusy());
  serviceSigilUpdates(testNow);
  assert(strcmp(fixtureUpdateOffer.ssid, "OTA test AP")==0);
  assert(strcmp(fixtureUpdateOffer.password, "test-password")==0);
  assert(fixtureUpdateOffer.product==3 && fixtureUpdateOffer.packageSize==otaTestPackage(3).size());
  assert(upload(admin,otaTestPackage(2))==403); // Cannot replace an active package.
  Intent start;start.type=IntentType::StartGame;assert(intents.dispatch(start).status==IntentStatus::Conflict);
  char token[33];for(size_t i=0;i<16;++i)snprintf(token+2*i,3,"%02x",fixtureUpdateOffer.token[i]);
  assert(request("/api/sigil-package","",{{"token","bad"}},HTTP_GET)==403);
  assert(request("/api/sigil-package","",{{"token",token}},HTTP_GET)==200);
  assert(request("/api/sigil-package","",{{"token",token}},HTTP_GET)==403); // Single use.
  noteSigilUpdateStatus(0,encodeUpdateStatus(UpdateStage::Installed,100,0,fixtureUpdateOffer.token[0]),testNow);
  testNow+=4000; serviceSigilUpdates(testNow); assert(sigilUpdatesBusy()); // Cached old Hello cannot fail job.
  r.firmwareMinor=9;serviceSigilUpdates(testNow);assert(sigilUpdatesBusy()); // Even cached target version cannot succeed.
  ++r.sessionGeneration;serviceSigilUpdates(testNow);assert(sigilUpdatesBusy()); // Handshake alone is insufficient.
  r.confirmedSessionGeneration=r.sessionGeneration;serviceSigilUpdates(testNow);assert(!sigilUpdatesBusy());
  assert(request("/api/sigil-firmware",admin,{},HTTP_GET)==200 && server.body.find("\"stage\":\"done\"")!=std::string::npos);
  fixtureRadio=false; resetPresence();
}
