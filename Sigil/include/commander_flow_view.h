#pragma once
#include "protocol.h"
#include <stdio.h>
#include <string.h>
namespace TurnHubSigil {
struct CommanderFlowView { char title[22] = {}; char lines[6][32] = {}; };
inline CommanderFlowView commanderFlowView(const TurnHubProtocol::CommanderFlowPacket &p) {
  using TurnHubProtocol::CommanderStage;
  CommanderFlowView v;
  snprintf(v.title,sizeof(v.title),"%s",p.stage == CommanderStage::UndoConfirm ? "UNDO HIT?" : "COMMANDER DAMAGE");
  snprintf(v.lines[0],sizeof(v.lines[0]),"To: %s (%c)",p.recipientName,p.recipientSlot == 2 ? 'B' : 'A');
  snprintf(v.lines[1],sizeof(v.lines[1]),"From: %s",p.sourceName);
  if (p.stage == CommanderStage::Source) {
    snprintf(v.lines[2],sizeof(v.lines[2]),"Player %u",static_cast<unsigned>(p.source));
    strcpy(v.lines[3],"Left/right: player"); strcpy(v.lines[4],"Click: next"); strcpy(v.lines[5],"Up/Down: cancel");
  } else if (p.stage == CommanderStage::Commander) {
    snprintf(v.lines[2],sizeof(v.lines[2]),"Commander %u",static_cast<unsigned>(p.commander));
    strcpy(v.lines[3],"Left/right: 1 or 2"); strcpy(v.lines[4],"2 for partner"); strcpy(v.lines[5],"Click next, Up back");
  } else if (p.stage == CommanderStage::Amount) {
    snprintf(v.lines[2],sizeof(v.lines[2]),"C%u: %ld damage",static_cast<unsigned>(p.commander),static_cast<long>(p.amount));
    strcpy(v.lines[3],"Left/right: amount"); strcpy(v.lines[4],"Hold to repeat"); strcpy(v.lines[5],"Click next, Up back");
  } else if (p.stage == CommanderStage::Confirm || p.stage == CommanderStage::UndoConfirm) {
    const int32_t amount = p.stage == CommanderStage::UndoConfirm ? -p.amount : p.amount;
    snprintf(v.lines[1],sizeof(v.lines[1]),"%s C%u: %ld",p.sourceName,static_cast<unsigned>(p.commander),static_cast<long>(p.amount));
    char totals[48];
    snprintf(totals,sizeof(totals),"Life %ld -> %ld",static_cast<long>(p.life),static_cast<long>(p.life-amount));
    if (strlen(totals)<22) snprintf(v.lines[2],sizeof(v.lines[2]),"%.21s",totals);
    // Keep numeric bounds readable on the 21-column OLED.
    if (strlen(totals)>=22) snprintf(v.lines[2],sizeof(v.lines[2]),"Life after: %ld",static_cast<long>(p.life-amount));
    snprintf(totals,sizeof(totals),"Cmd %ld -> %ld",static_cast<long>(p.damage),static_cast<long>(p.damage+amount));
    if (strlen(totals)<22) snprintf(v.lines[3],sizeof(v.lines[3]),"%.21s",totals);
    else snprintf(v.lines[3],sizeof(v.lines[3]),"Cmd after: %ld",static_cast<long>(p.damage+amount));
    strcpy(v.lines[4],p.stage == CommanderStage::UndoConfirm ? "Click: undo hit" : "Click: apply hit");
    strcpy(v.lines[5],"Up back, Down cancel");
  } else if (p.stage == CommanderStage::Result) {
    snprintf(v.lines[2],sizeof(v.lines[2]),"%.21s",p.notice);
    snprintf(v.lines[3],sizeof(v.lines[3]),"%.21s",strlen(p.notice)>21 ? p.notice+21 : "");
    strcpy(v.lines[5],"Click: close");
  }
  return v;
}
}
