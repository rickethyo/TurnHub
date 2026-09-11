// ============================================================
// TurnHub - ESP32 Module 0
//
// Module 0 = ESP32 = WHITE wiring
//
// The ESP32 does NOT own game state.
// The Raspberry Pi is authoritative.
//
// SHORT BUTTON = PASS TURN
// TALL BUTTON  = ACTION
//
// Pins:
//   Blue LED   -> GPIO 27
//   Red LED    -> GPIO 14
//   Green LED  -> GPIO 13
//   Pass       -> GPIO 26 (SHORT button)
//   Action     -> GPIO 25 (TALL button)
//
// Serial baud: 9600
//
// ESP32 -> Pi:
//
//   READY|0
//   PASS|0
//
//   ACTION|0|DOWN
//   ACTION|0|UP
//   ACTION|0|SHORT
//   ACTION|0|LONG
//   ACTION|0|WIN
//
// Pi -> ESP32:
//
//   BLUE|0|<0-255>
//   RED|0|<0|1>
//   GREEN|0|<0|1>
//   OFF
//
// ============================================================


const int MODULE_ID = 0;


// ============================================================
// Pins
// ============================================================

const int BLUE_LED  = 27;
const int RED_LED   = 14;
const int GREEN_LED = 13;

const int PASS_BUTTON   = 26;
const int ACTION_BUTTON = 25;


// ============================================================
// Timing
// ============================================================

const unsigned long DEBOUNCE_TIME   = 30;
const unsigned long LONG_PRESS_TIME = 2000;
const unsigned long WIN_HOLD_TIME   = 5000;


// ============================================================
// Button State
// ============================================================

struct ButtonState {
  int pin;

  bool rawState;
  bool stableState;

  unsigned long lastDebounceTime;
  unsigned long pressStartTime;

  bool longSent;
  bool winSent;
};


ButtonState passButton = {
  PASS_BUTTON,
  HIGH,
  HIGH,
  0,
  0,
  false,
  false
};


ButtonState actionButton = {
  ACTION_BUTTON,
  HIGH,
  HIGH,
  0,
  0,
  false,
  false
};


// ============================================================
// Setup
// ============================================================

void setup() {

  Serial.begin(9600);

  pinMode(BLUE_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);

  pinMode(PASS_BUTTON, INPUT_PULLUP);
  pinMode(ACTION_BUTTON, INPUT_PULLUP);

  allOff();

  delay(100);

  Serial.print("READY|");
  Serial.println(MODULE_ID);
}


// ============================================================
// Main Loop
// ============================================================

void loop() {

  updatePassButton();
  updateActionButton();

  readSerialCommands();
}


// ============================================================
// PASS BUTTON
//
// Short physical button.
// PASS occurs after a complete press/release.
// ============================================================

void updatePassButton() {

  bool reading = digitalRead(passButton.pin);

  if (reading != passButton.rawState) {

    passButton.rawState = reading;
    passButton.lastDebounceTime = millis();
  }


  if (
    millis() - passButton.lastDebounceTime
    >= DEBOUNCE_TIME
  ) {

    if (
      passButton.stableState
      != passButton.rawState
    ) {

      passButton.stableState =
        passButton.rawState;


      // PASS on release
      if (passButton.stableState == HIGH) {

        Serial.print("PASS|");
        Serial.println(MODULE_ID);
      }
    }
  }
}


// ============================================================
// ACTION BUTTON
//
// Tall physical button.
// ============================================================

void updateActionButton() {

  bool reading =
    digitalRead(actionButton.pin);


  // ----------------------------------------------------------  
  // Debounce
  // ----------------------------------------------------------

  if (reading != actionButton.rawState) {

    actionButton.rawState = reading;

    actionButton.lastDebounceTime =
      millis();
  }


  if (
    millis() - actionButton.lastDebounceTime
    >= DEBOUNCE_TIME
  ) {

    if (
      actionButton.stableState
      != actionButton.rawState
    ) {

      actionButton.stableState =
        actionButton.rawState;


      // ------------------------------------------------------
      // Pressed
      // ------------------------------------------------------

      if (actionButton.stableState == LOW) {

        actionButton.pressStartTime =
          millis();

        actionButton.longSent = false;
        actionButton.winSent = false;

        sendActionEvent("DOWN");
      }


      // ------------------------------------------------------
      // Released
      // ------------------------------------------------------

      else {

        sendActionEvent("UP");

        if (!actionButton.longSent) {

          sendActionEvent("SHORT");
        }

        actionButton.pressStartTime = 0;

        actionButton.longSent = false;
        actionButton.winSent = false;
      }
    }
  }


  // ==========================================================
  // Held Action Button
  // ==========================================================

  if (actionButton.stableState == LOW) {

    unsigned long heldTime =
      millis() - actionButton.pressStartTime;


    // 2 second hold
    if (
      !actionButton.longSent &&
      heldTime >= LONG_PRESS_TIME
    ) {

      actionButton.longSent = true;

      sendActionEvent("LONG");
    }


    // 5 second hold
    if (
      !actionButton.winSent &&
      heldTime >= WIN_HOLD_TIME
    ) {

      actionButton.winSent = true;

      sendActionEvent("WIN");
    }
  }
}


// ============================================================
// Action Events
// ============================================================

void sendActionEvent(
  const char *eventName
) {

  Serial.print("ACTION|");

  Serial.print(MODULE_ID);
  Serial.print("|");

  Serial.println(eventName);
}


// ============================================================
// Serial Input
// ============================================================

void readSerialCommands() {

  while (Serial.available()) {

    String command =
      Serial.readStringUntil('\n');

    command.trim();

    if (command.length() == 0) {
      continue;
    }

    handleCommand(command);
  }
}


// ============================================================
// Command Parser
// ============================================================

void handleCommand(String command) {


  // ----------------------------------------------------------
  // OFF
  // ----------------------------------------------------------

  if (command == "OFF") {

    allOff();

    return;
  }


  // ----------------------------------------------------------
  // BLUE|module|brightness
  // ----------------------------------------------------------

  if (command.startsWith("BLUE|")) {

    int first =
      command.indexOf('|');

    int second =
      command.indexOf('|', first + 1);

    if (second == -1) {
      return;
    }

    int module =
      command.substring(
        first + 1,
        second
      ).toInt();

    if (module != MODULE_ID) {
      return;
    }

    int brightness =
      command.substring(
        second + 1
      ).toInt();

    brightness =
      constrain(
        brightness,
        0,
        255
      );

    analogWrite(
      BLUE_LED,
      brightness
    );

    return;
  }


  // ----------------------------------------------------------
  // RED|module|state
  // ----------------------------------------------------------

  if (command.startsWith("RED|")) {

    int first =
      command.indexOf('|');

    int second =
      command.indexOf('|', first + 1);

    if (second == -1) {
      return;
    }

    int module =
      command.substring(
        first + 1,
        second
      ).toInt();

    if (module != MODULE_ID) {
      return;
    }

    int state =
      command.substring(
        second + 1
      ).toInt();

    digitalWrite(
      RED_LED,
      state ? HIGH : LOW
    );

    return;
  }


  // ----------------------------------------------------------
  // GREEN|module|state
  // ----------------------------------------------------------

  if (command.startsWith("GREEN|")) {

    int first =
      command.indexOf('|');

    int second =
      command.indexOf('|', first + 1);

    if (second == -1) {
      return;
    }

    int module =
      command.substring(
        first + 1,
        second
      ).toInt();

    if (module != MODULE_ID) {
      return;
    }

    int state =
      command.substring(
        second + 1
      ).toInt();

    digitalWrite(
      GREEN_LED,
      state ? HIGH : LOW
    );

    return;
  }
}


// ============================================================
// LEDs Off
// ============================================================

void allOff() {

  analogWrite(
    BLUE_LED,
    0
  );

  digitalWrite(
    RED_LED,
    LOW
  );

  digitalWrite(
    GREEN_LED,
    LOW
  );
}