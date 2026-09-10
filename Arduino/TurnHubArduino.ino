// TurnHub Arduino I/O Controller
//
// The Arduino does NOT own game state.
// It only:
// - Reads player buttons
// - Reads the warning potentiometer
// - Reports events to the Raspberry Pi
// - Controls LEDs
// - Controls the piezo buzzer
//
// Serial baud: 9600
//
// Arduino -> Pi:
// READY
// POT|<warning_ms>
// BUTTON|1|DOWN
// BUTTON|1|UP
// BUTTON|1|SHORT
// BUTTON|1|LONG
// BUTTON|1|WIN
// BUTTON|2|...
//
// Pi -> Arduino:
// BLUE|<module>|<0-255>
// RED|<module>|<0|1>
// GREEN|<module>|<0|1>
// SOUND|<frequency>|<duration_ms>
// OFF


// ============================================================
// Pin Assignments
// ============================================================

// Module 1
const int M1_BLUE = 9;
const int M1_RED = 10;
const int M1_GREEN = 2;
const int M1_BUTTON = 4;

// Module 2
const int M2_BLUE = 5;
const int M2_RED = 6;
const int M2_GREEN = 3;
const int M2_BUTTON = 7;

// Shared hardware
const int BUZZER = 8;
const int POT = A0;


// ============================================================
// Timing
// ============================================================

const unsigned long LONG_PRESS_TIME = 2000;
const unsigned long WIN_HOLD_TIME = 5000;
const unsigned long DEBOUNCE_TIME = 30;

const unsigned long POT_REPORT_INTERVAL = 250;


// ============================================================
// Potentiometer Range
// ============================================================

// IMPORTANT:
// The Pi now treats these values as raw pot-position values.
//
// The Arduino still reports a quantized range from:
//
// 5 seconds -> 5 minutes
//
// The Pi then remaps that into:
//
// lowest position = warning OFF
// rest of range = 1-10 minutes

const unsigned long WARNING_MIN = 5000;
const unsigned long WARNING_MAX = 300000;
const unsigned long WARNING_STEP = 5000;


// ============================================================
// Button State Structure
// ============================================================

struct ButtonState {
  int pin;

  bool rawState;
  bool stableState;
  bool previousStableState;

  unsigned long lastDebounceTime;
  unsigned long pressStartTime;

  bool longSent;
  bool winSent;
};


// ============================================================
// Button Instances
// ============================================================

ButtonState button1 = {
  M1_BUTTON,
  HIGH,
  HIGH,
  HIGH,
  0,
  0,
  false,
  false
};

ButtonState button2 = {
  M2_BUTTON,
  HIGH,
  HIGH,
  HIGH,
  0,
  0,
  false,
  false
};


// ============================================================
// Pot State
// ============================================================

unsigned long lastPotReport = 0;
unsigned long lastPotValue = 0;


// ============================================================
// Setup
// ============================================================

void setup() {
  Serial.begin(9600);

  pinMode(M1_BLUE, OUTPUT);
  pinMode(M1_RED, OUTPUT);
  pinMode(M1_GREEN, OUTPUT);

  pinMode(M2_BLUE, OUTPUT);
  pinMode(M2_RED, OUTPUT);
  pinMode(M2_GREEN, OUTPUT);

  pinMode(M1_BUTTON, INPUT_PULLUP);
  pinMode(M2_BUTTON, INPUT_PULLUP);

  pinMode(BUZZER, OUTPUT);

  allOff();

  delay(100);

  Serial.println("READY");

  // Send initial pot value immediately.
  lastPotValue = readQuantizedPot();
  reportPot(lastPotValue);
}


// ============================================================
// Main Loop
// ============================================================

void loop() {
  updateButton(button1, 1);
  updateButton(button2, 2);

  updatePot();
  readSerialCommands();
}


// ============================================================
// Button Handling
// ============================================================

void updateButton(ButtonState &button, int module) {
  bool reading = digitalRead(button.pin);

  // Raw state changed, restart debounce timer.
  if (reading != button.rawState) {
    button.rawState = reading;
    button.lastDebounceTime = millis();
  }

  // Debounce period has elapsed.
  if ((millis() - button.lastDebounceTime) >= DEBOUNCE_TIME) {

    if (button.stableState != button.rawState) {
      button.previousStableState = button.stableState;
      button.stableState = button.rawState;

      // Button pressed.
      if (button.stableState == LOW) {
        button.pressStartTime = millis();
        button.longSent = false;
        button.winSent = false;

        sendButtonEvent(module, "DOWN");
      }

      // Button released.
      else {
        sendButtonEvent(module, "UP");

        // SHORT only happens if the 2-second LONG event was never reached.
        if (!button.longSent) {
          sendButtonEvent(module, "SHORT");
        }

        button.pressStartTime = 0;
        button.longSent = false;
        button.winSent = false;
      }
    }
  }

  // Button is currently held.
  if (button.stableState == LOW) {
    unsigned long heldTime = millis() - button.pressStartTime;

    if (!button.longSent && heldTime >= LONG_PRESS_TIME) {
      button.longSent = true;
      sendButtonEvent(module, "LONG");
    }

    if (!button.winSent && heldTime >= WIN_HOLD_TIME) {
      button.winSent = true;
      sendButtonEvent(module, "WIN");
    }
  }
}


// ============================================================
// Button Event Output
// ============================================================

void sendButtonEvent(int module, const char *eventName) {
  Serial.print("BUTTON|");
  Serial.print(module);
  Serial.print("|");
  Serial.println(eventName);
}


// ============================================================
// Potentiometer
// ============================================================

void updatePot() {
  if ((millis() - lastPotReport) < POT_REPORT_INTERVAL) {
    return;
  }

  lastPotReport = millis();

  unsigned long value = readQuantizedPot();

  if (value != lastPotValue) {
    lastPotValue = value;
    reportPot(value);
  }
}


unsigned long readQuantizedPot() {
  int raw = analogRead(POT);

  unsigned long mappedValue = map(
    raw,
    0,
    1023,
    WARNING_MIN,
    WARNING_MAX
  );

  // Quantize into 5-second steps.
  mappedValue =
    ((mappedValue + (WARNING_STEP / 2)) / WARNING_STEP)
    * WARNING_STEP;

  // Clamp just in case.
  if (mappedValue < WARNING_MIN) {
    mappedValue = WARNING_MIN;
  }

  if (mappedValue > WARNING_MAX) {
    mappedValue = WARNING_MAX;
  }

  return mappedValue;
}


void reportPot(unsigned long value) {
  Serial.print("POT|");
  Serial.println(value);
}


// ============================================================
// Serial Input
// ============================================================

void readSerialCommands() {
  while (Serial.available()) {
    String command = Serial.readStringUntil('\n');
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
    noTone(BUZZER);
    return;
  }


  // ----------------------------------------------------------
  // BLUE|module|brightness
  // ----------------------------------------------------------

  if (command.startsWith("BLUE|")) {
    int first = command.indexOf('|');
    int second = command.indexOf('|', first + 1);

    if (second == -1) {
      return;
    }

    int module = command.substring(
      first + 1,
      second
    ).toInt();

    int brightness = command.substring(
      second + 1
    ).toInt();

    brightness = constrain(
      brightness,
      0,
      255
    );

    setBlue(module, brightness);

    return;
  }


  // ----------------------------------------------------------
  // RED|module|state
  // ----------------------------------------------------------

  if (command.startsWith("RED|")) {
    int first = command.indexOf('|');
    int second = command.indexOf('|', first + 1);

    if (second == -1) {
      return;
    }

    int module = command.substring(
      first + 1,
      second
    ).toInt();

    int state = command.substring(
      second + 1
    ).toInt();

    setRed(
      module,
      state != 0
    );

    return;
  }


  // ----------------------------------------------------------
  // GREEN|module|state
  // ----------------------------------------------------------

  if (command.startsWith("GREEN|")) {
    int first = command.indexOf('|');
    int second = command.indexOf('|', first + 1);

    if (second == -1) {
      return;
    }

    int module = command.substring(
      first + 1,
      second
    ).toInt();

    int state = command.substring(
      second + 1
    ).toInt();

    setGreen(
      module,
      state != 0
    );

    return;
  }


  // ----------------------------------------------------------
  // SOUND|frequency|duration
  // ----------------------------------------------------------

  if (command.startsWith("SOUND|")) {
    int first = command.indexOf('|');
    int second = command.indexOf('|', first + 1);

    if (second == -1) {
      return;
    }

    int frequency = command.substring(
      first + 1,
      second
    ).toInt();

    int duration = command.substring(
      second + 1
    ).toInt();

    if (frequency <= 0 || duration <= 0) {
      noTone(BUZZER);
      return;
    }

    tone(
      BUZZER,
      frequency,
      duration
    );

    return;
  }
}


// ============================================================
// LED Helpers
// ============================================================

void setBlue(int module, int brightness) {
  if (module == 1) {
    analogWrite(
      M1_BLUE,
      brightness
    );
  }

  else if (module == 2) {
    analogWrite(
      M2_BLUE,
      brightness
    );
  }
}


void setRed(int module, bool state) {
  if (module == 1) {
    digitalWrite(
      M1_RED,
      state ? HIGH : LOW
    );
  }

  else if (module == 2) {
    digitalWrite(
      M2_RED,
      state ? HIGH : LOW
    );
  }
}


void setGreen(int module, bool state) {
  if (module == 1) {
    digitalWrite(
      M1_GREEN,
      state ? HIGH : LOW
    );
  }

  else if (module == 2) {
    digitalWrite(
      M2_GREEN,
      state ? HIGH : LOW
    );
  }
}


void allOff() {
  analogWrite(M1_BLUE, 0);
  digitalWrite(M1_RED, LOW);
  digitalWrite(M1_GREEN, LOW);

  analogWrite(M2_BLUE, 0);
  digitalWrite(M2_RED, LOW);
  digitalWrite(M2_GREEN, LOW);
}