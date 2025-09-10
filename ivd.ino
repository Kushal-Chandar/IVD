#include <Arduino.h>
#include "DRV8834.h"

// ─────────────────────────────────────────────────────────────
// Pin Configuration
// ─────────────────────────────────────────────────────────────
constexpr uint8_t BUTTON_PIN       = 3;
constexpr uint8_t DIR_PIN          = 4;
constexpr uint8_t STEP_PIN         = 6;
constexpr uint8_t SLEEP_PIN        = 12;
constexpr uint8_t MICROSTEP_PIN_0  = 9;
constexpr uint8_t MICROSTEP_PIN_1  = 10;
constexpr uint8_t GK152_PIN        = 2;  // Optical switch (GK152 black wire)

// ─────────────────────────────────────────────────────────────
// Motion Configuration
// ─────────────────────────────────────────────────────────────
constexpr uint8_t DEBOUNCE_MS      = 50;
constexpr int MOTOR_STEPS          = 200;

constexpr float PRE_SCAN_RPM       = 300;
constexpr int SCAN_RPM             = 18;

constexpr uint8_t MICROSTEP_LEVEL  = 16;
constexpr int BACKOFF_STEPS        = 1375;  // Steps to back off after hitting sensor
constexpr int SCAN_START_POSITION  = 800;
constexpr int SCAN_STEPS           = 275;


DRV8834 stepper(MOTOR_STEPS, DIR_PIN, STEP_PIN, SLEEP_PIN, MICROSTEP_PIN_0, MICROSTEP_PIN_1);

// ─────────────────────────────────────────────────────────────
// GK152 Optical Switch Logic
// ─────────────────────────────────────────────────────────────
const bool GK152_ACTIVE_HIGH = true;

inline bool gkBlockedRaw() {
  int val = digitalRead(GK152_PIN);
  return GK152_ACTIVE_HIGH ? (val == HIGH) : (val == LOW);
}

// ─────────────────────────────────────────────────────────────
// Debounce & Interrupts
// ─────────────────────────────────────────────────────────────
bool          lastRawState    = HIGH;
bool          stableButton    = HIGH;
unsigned long lastChangeMs    = 0;

volatile bool stopMotor = false;

void handleGK152Interrupt() {
  stopMotor = true;
}

// ─────────────────────────────────────────────────────────────
// Homing Routine Using GK152
// ─────────────────────────────────────────────────────────────
void homeToGK152(bool cwTowardSensor = true, float rpm = PRE_SCAN_RPM, bool backOff = true) {
  Serial.println("Starting homing...");

  stepper.enable();
  stepper.setMicrostep(MICROSTEP_LEVEL);
  stepper.setRPM(rpm);

  int direction = cwTowardSensor ? 1 : -1;

  bool lastState = gkBlockedRaw();
  bool triggered = false;

  for (int i = 0; i < 5000; ++i) {
    bool nowState = gkBlockedRaw();

    if (!lastState && nowState) {
      Serial.println("Sensor triggered (CLEAR → BLOCKED)");
      triggered = true;
      break;
    }

    lastState = nowState;
    stepper.rotate(direction);  // rotate one step
  }

  if (!triggered) {
    Serial.println("Homing FAILED: Sensor never triggered.");
    stepper.disable();
    return;
  }
  
  if (backOff) {
    Serial.println("Backing off from sensor...");
    stepper.rotate(-BACKOFF_STEPS * direction);
  }

  delay(100);
  Serial.println("Homing complete. Motor at zero position.");
  stepper.disable();
}

// ─────────────────────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(GK152_PIN, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(GK152_PIN), handleGK152Interrupt, CHANGE);

  stepper.begin(PRE_SCAN_RPM);
  stepper.setMicrostep(MICROSTEP_LEVEL);
  stepper.disable();
  

  homeToGK152();
}

// ─────────────────────────────────────────────────────────────
// Debounced Button Read
// ─────────────────────────────────────────────────────────────
bool readButtonPressed() {
  bool raw = digitalRead(BUTTON_PIN);

  if (raw != lastRawState) {
    lastChangeMs = millis();
  }

  if (millis() - lastChangeMs >= DEBOUNCE_MS) {
    if (raw != stableButton) {
      stableButton = raw;
      if (stableButton == LOW) {
        lastRawState = raw;
        return true;
      }
    }
  }

  lastRawState = raw;
  return false;
}

// ─────────────────────────────────────────────────────────────
// Motion Sequence with Re-Homing at Start
// ─────────────────────────────────────────────────────────────
void runStepperSequence() {
  stepper.enable();

  Serial.println("Go to Scan Start Position");
  delay(100);
  stepper.setRPM(PRE_SCAN_RPM);
  stepper.rotate(SCAN_START_POSITION);

  Serial.println("SCANNING...");
  delay(100);
  stepper.setRPM(SCAN_RPM);
  stepper.rotate(SCAN_STEPS);

  homeToGK152();  
}

// ─────────────────────────────────────────────────────────────
// Main Loop
// ─────────────────────────────────────────────────────────────
void loop() {
  if (readButtonPressed()) {
    Serial.println("Button pressed!");
    runStepperSequence();
  }
}
