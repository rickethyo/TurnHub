// Experimental TurnHub hardware-in-the-loop test harness.
//
// This target intentionally does not initialize ESP-NOW or drive Atlas yet.
// It establishes the serial/test boundary and consumes the canonical shared
// protocol so later work does not grow a second copy of the radio contract.

#include <Arduino.h>

#include "protocol.h"

namespace {

constexpr uint32_t SERIAL_BAUD = 115200;

String inputLine;

void printHelp() {
  Serial.println("HARNESS|HELP|commands=help,status,run <scenario>");
  Serial.println("HARNESS|HELP|planned=smoke,pairing,standard-4p,reconnect,chaos,soak");
}

void printStatus() {
  Serial.printf(
      "HARNESS|STATUS|protocol=%u|maxSigils=%u|transport=not-initialized|scenarios=0\n",
      static_cast<unsigned>(TurnHubProtocol::VERSION),
      static_cast<unsigned>(TurnHubProtocol::MAX_SIGILS));
}

void handleCommand(String command) {
  command.trim();
  if (command.length() == 0) return;

  if (command == "help") {
    printHelp();
    return;
  }

  if (command == "status") {
    printStatus();
    return;
  }

  if (command.startsWith("run ")) {
    const String scenario = command.substring(4);
    Serial.print("HARNESS|NOT_IMPLEMENTED|scenario=");
    Serial.println(scenario);
    return;
  }

  Serial.print("HARNESS|UNKNOWN_COMMAND|");
  Serial.println(command);
}

void pollSerial() {
  while (Serial.available() > 0) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\r') continue;

    if (ch == '\n') {
      handleCommand(inputLine);
      inputLine = "";
      continue;
    }

    if (inputLine.length() < 96) {
      inputLine += ch;
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(250);

  Serial.println("HARNESS|BOOT|experimental-scaffold");
  printStatus();
  printHelp();
}

void loop() {
  pollSerial();
  delay(5);
}
