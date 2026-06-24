// Motor_Cont  -- closed-loop lead-screw position controller
//
// Target : Seeed XIAO ESP32-C3  (Arduino-ESP32 core 2.x, channel-based LEDC)
// Driver : DRV8833, both H-bridges PARALLELED
//            AIN1+BIN1 tied -> D2 (IN1)
//            AIN2+BIN2 tied -> D3 (IN2)
//            AOUT1+BOUT1 tied, AOUT2+BOUT2 tied -> motor
//            nSLEEP must be HIGH (tie to 3V3 or drive from a spare GPIO)
// Motor  : NFP-GM12-N20-EN, 6V A06, 100:1, M4 lead-screw shaft
//            3 PPR base encoder x 100 gear, x4 quadrature = 1200 counts / output rev
// Sensors: AB encoder  -> D0 (A), D1 (B)   (3V3, built-in pull-ups, direct connect)
//          limit switch -> D4  (to GND, active LOW, internal pull-up)
//
// Protocol over the command/telemetry UART (D6=TX, D7=RX, 115200 8N1):
//   RX commands:  "M <pos>"  set desired position (mm)
//                 "H"        re-run homing
//                 "?"        print one telemetry line now
//   TX telemetry: "<current>, <desired>\r\n"  (mm) at a fixed rate
 
#include <Arduino.h>
 
// ----------------------------- Pin map (XIAO ESP32-C3) -----------------------
#define ENC_A    D0    // GPIO2   encoder channel A  (NOTE: GPIO2 is a strapping pin)
#define ENC_B    D1    // GPIO3   encoder channel B
#define MOT_IN1  D2    // GPIO4   DRV8833 IN1 (AIN1+BIN1)
#define MOT_IN2  D3    // GPIO5   DRV8833 IN2 (AIN2+BIN2)
#define LIMIT    D4    // GPIO6   limit switch, active LOW
 
// Command + telemetry link. UART1 on D6/D7 (board-to-board link).
// For bench testing over USB-C instead, change CTRL to Serial and delete the
// CtrlSerial.begin() line in setup().
HardwareSerial CtrlSerial(1);
#define CTRL CtrlSerial
 
// ----------------------------- Mechanics calibration -------------------------
const float GEAR_RATIO     = 100.0f;                       // 100:1
const float ENCODER_PPR    = 3.0f;                         // base pulses/rev/channel
const float COUNTS_PER_REV = ENCODER_PPR * GEAR_RATIO * 4; // x4 quad = 1200 / output rev
const float LEAD_MM        = 0.7f;                         // M4 coarse pitch (CHANGE if different)
const float COUNTS_PER_MM  = COUNTS_PER_REV / LEAD_MM;     // ~1714 counts/mm
const float MAX_TRAVEL_MM  = 50.0f;                        // usable stroke (set to your mechanism)
 
// ----------------------------- Control tuning --------------------------------
const int   PWM_FREQ   = 20000;   // 20 kHz, above audible
const int   PWM_RES    = 8;       // 0..255
// LEDC channels (Arduino-ESP32 core 2.x uses channel-based PWM). One per pin.
const int   PWM_CH_IN1 = 0;       // channel bound to MOT_IN1
const int   PWM_CH_IN2 = 1;       // channel bound to MOT_IN2
const float KP         = 0.12f;   // PWM per count of error. ~full speed beyond ~1mm,
                                  //   ramps down inside the last mm. Raise for stiffer.
const float KD         = 0.0f;    // optional damping (PWM per count/sec). Start at 0.
const int   MAX_PWM    = 230;     // cap (protects a 6V motor if VM > 6V)
const int   MIN_PWM    = 70;      // stiction floor: smallest move that actually turns
const float POS_TOL_MM = 0.10f;   // deadband: "close enough", then coast & hold
 
// Homing
const int           HOMING_PWM        = 110;    // homing speed
const int           HOMING_DIR        = -1;     // sign that drives TOWARD the switch (flip if wrong)
const unsigned long HOMING_TIMEOUT_MS = 8000;
 
// Loop timing
const unsigned long CTRL_MS  = 10;    // control update period
const unsigned long TELEM_MS = 250;   // telemetry print period
 
// ----------------------------- State ----------------------------------------
volatile int32_t encoderCount = 0;
volatile uint8_t encPrev      = 0;
int32_t  desiredCounts = 0;
float    lastError     = 0.0f;
bool     homed         = false;
 
const int32_t POS_TOL_COUNTS = (int32_t)(POS_TOL_MM * COUNTS_PER_MM);
const int32_t MAX_COUNTS     = (int32_t)(MAX_TRAVEL_MM * COUNTS_PER_MM);
 
// ----------------------------- Encoder (x4 quadrature) -----------------------
// Lookup table indexed by (prevAB << 2) | currAB. Value = +1 / -1 / 0.
static const int8_t QUAD_LUT[16] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};
 
void IRAM_ATTR encISR() {
  uint8_t s = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  encoderCount += QUAD_LUT[(encPrev << 2) | s];
  encPrev = s;
}
 
int32_t readCount() {
  noInterrupts();
  int32_t c = encoderCount;
  interrupts();
  return c;
}
 
// ----------------------------- Motor primitives ------------------------------
// duty: -255..255, sign = direction. (+) should drive AWAY from the limit/home end.
// If it goes the wrong way, swap the two motor wires OR negate here.
void motorRaw(int duty) {
  duty = constrain(duty, -MAX_PWM, MAX_PWM);
  if (duty >= 0) { ledcWrite(PWM_CH_IN1, duty);  ledcWrite(PWM_CH_IN2, 0); }
  else           { ledcWrite(PWM_CH_IN1, 0);     ledcWrite(PWM_CH_IN2, -duty); }
}
void motorCoast() { ledcWrite(PWM_CH_IN1, 0);   ledcWrite(PWM_CH_IN2, 0); }   // both LOW  = Hi-Z
void motorBrake() { ledcWrite(PWM_CH_IN1, 255); ledcWrite(PWM_CH_IN2, 255); } // both HIGH = brake
 
// ----------------------------- Helpers ---------------------------------------
bool limitPressed() { return digitalRead(LIMIT) == LOW; }   // active-low switch
 
float countsToMm(int32_t c) { return c / COUNTS_PER_MM; }
 
void setDesiredMm(float mm) {
  mm = constrain(mm, 0.0f, MAX_TRAVEL_MM);
  desiredCounts = (int32_t)lroundf(mm * COUNTS_PER_MM);
}
 
// ----------------------------- Homing ----------------------------------------
// Drive to the switch, back off to the release edge, call that zero.
bool homeAxis() {
  Serial.println("[home] seeking limit...");
  unsigned long t0 = millis();
 
  // Phase 1: approach the switch.
  motorRaw(HOMING_DIR * HOMING_PWM);
  while (!limitPressed()) {
    if (millis() - t0 > HOMING_TIMEOUT_MS) { motorCoast(); Serial.println("[home] TIMEOUT"); return false; }
    delay(2);
  }
  motorCoast();
  delay(150);
 
  // Phase 2: back off slowly until the switch releases -> repeatable zero edge.
  t0 = millis();
  motorRaw(-HOMING_DIR * (HOMING_PWM * 2 / 3));
  while (limitPressed()) {
    if (millis() - t0 > HOMING_TIMEOUT_MS) { motorCoast(); Serial.println("[home] TIMEOUT (backoff)"); return false; }
    delay(2);
  }
  motorCoast();
  delay(150);
 
  noInterrupts();
  encoderCount = 0;
  interrupts();
  desiredCounts = 0;
  lastError = 0;
  homed = true;
  Serial.println("[home] zeroed");
  return true;
}
 
// ----------------------------- Closed loop -----------------------------------
void updateControl(float dt) {
  int32_t pos   = readCount();
  int32_t error = desiredCounts - pos;
 
  // Hard safety: if we bump the home-end switch while driving toward it, stop & re-zero.
  if (limitPressed() && (HOMING_DIR * error) > 0) {
    motorCoast();
    noInterrupts(); encoderCount = 0; interrupts();
    desiredCounts = constrain(desiredCounts, (int32_t)0, MAX_COUNTS);
    return;
  }
 
  if (labs(error) <= POS_TOL_COUNTS) {  // in the deadband: let the self-locking screw hold
    motorCoast();
    lastError = error;
    return;
  }
 
  float dErr = (error - lastError) / dt;            // counts/sec
  lastError  = error;
 
  float out = KP * (float)error + KD * dErr;
  int   duty = (int)out;
 
  // stiction floor
  if (duty > 0 && duty < MIN_PWM)  duty = MIN_PWM;
  if (duty < 0 && duty > -MIN_PWM) duty = -MIN_PWM;
 
  motorRaw(duty);
}
 
// ----------------------------- Self-test sequence ----------------------------
// Only compiled into the Motor_Cont_Test environment (-D SELFTEST). Runs once,
// automatically, after homing at power-up -- no UART commands needed, so the
// motor board can be bench-tested by itself over USB.
//
// Non-blocking (a state machine, not delay()) so the closed loop keeps running
// and actually holds position during the 30 s dwell. Sequence:
//   1. drive to TEST_POS_MM
//   2. once within tolerance, dwell TEST_DWELL_MS
//   3. drive back to 0 mm, then finish (stays at 0, holding)
#ifdef SELFTEST
enum TestPhase { TEST_GO_OUT, TEST_DWELL, TEST_GO_HOME, TEST_DONE };
TestPhase           testPhase     = TEST_GO_OUT;
unsigned long       testTimer     = 0;
const float         TEST_POS_MM   = 30.0f;     // travel target for the test
const unsigned long TEST_DWELL_MS = 30000;     // hold time at TEST_POS_MM (30 s)

bool atTarget() { return labs(desiredCounts - readCount()) <= POS_TOL_COUNTS; }

void startTest() {
  Serial.printf("[test] -> %.1f mm\n", TEST_POS_MM);
  setDesiredMm(TEST_POS_MM);
  testPhase = TEST_GO_OUT;
}

void updateTest() {
  switch (testPhase) {
    case TEST_GO_OUT:                              // wait until we reach 30 mm
      if (atTarget()) {
        testTimer = millis();
        testPhase = TEST_DWELL;
        Serial.printf("[test] at %.1f mm, dwell %lu s\n",
                      TEST_POS_MM, TEST_DWELL_MS / 1000);
      }
      break;
    case TEST_DWELL:                               // hold position for the dwell
      if (millis() - testTimer >= TEST_DWELL_MS) {
        setDesiredMm(0.0f);
        testPhase = TEST_GO_HOME;
        Serial.println("[test] -> 0 mm");
      }
      break;
    case TEST_GO_HOME:                             // wait until we get back to 0
      if (atTarget()) {
        testPhase = TEST_DONE;
        Serial.println("[test] done (holding 0 mm)");
      }
      break;
    case TEST_DONE:
      break;                                       // finished; just hold position
  }
}
#endif  // SELFTEST

// ----------------------------- Command parser --------------------------------
void handleLine(char *line) {
  // trim leading spaces
  while (*line == ' ' || *line == '\t') line++;
 
  char c = line[0];
  if (c == 'M' || c == 'm') {
    float mm = atof(line + 1);     // accepts "M 25", "M25", "M 25.4"
    setDesiredMm(mm);
    Serial.printf("[cmd] target = %.2f mm\n", countsToMm(desiredCounts));
  } else if (c == 'H' || c == 'h') {
    homeAxis();
  } else if (c == '?') {
    CTRL.printf("%.2f, %.2f\r\n", countsToMm(readCount()), countsToMm(desiredCounts));
  }
}
 
void pollCommands() {
  static char buf[32];
  static size_t n = 0;
  while (CTRL.available()) {
    char ch = (char)CTRL.read();
    if (ch == '\n' || ch == '\r') {
      if (n > 0) { buf[n] = '\0'; handleLine(buf); n = 0; }
    } else if (n < sizeof(buf) - 1) {
      buf[n++] = ch;
    }
  }
}
 
// ----------------------------- Setup / loop ----------------------------------
void setup() {
  Serial.begin(115200);                                 // USB-CDC: boot/debug only
  CtrlSerial.begin(115200, SERIAL_8N1, D7, D6);         // command + telemetry (RX=D7, TX=D6)
 
  pinMode(LIMIT, INPUT_PULLUP);
  pinMode(ENC_A, INPUT);                                // encoder has its own pull-ups
  pinMode(ENC_B, INPUT);
 
  ledcSetup(PWM_CH_IN1, PWM_FREQ, PWM_RES);
  ledcSetup(PWM_CH_IN2, PWM_FREQ, PWM_RES);
  ledcAttachPin(MOT_IN1, PWM_CH_IN1);
  ledcAttachPin(MOT_IN2, PWM_CH_IN2);
  motorCoast();
 
  encPrev = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  attachInterrupt(digitalPinToInterrupt(ENC_A), encISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), encISR, CHANGE);
 
  Serial.println("[Motor_Cont] ready");
  homeAxis();

#ifdef SELFTEST
  // Standalone bench test: auto-run the move cycle once homing completes.
  if (homed) startTest();
  else       Serial.println("[test] homing failed; test not started");
#endif
}
 
void loop() {
  static unsigned long tCtrl = 0, tTelem = 0;
  unsigned long now = millis();
 
  pollCommands();

#ifdef SELFTEST
  updateTest();   // advance the auto move cycle (sets desiredCounts; loop drives it)
#endif

  if (now - tCtrl >= CTRL_MS) {
    float dt = (now - tCtrl) / 1000.0f;
    tCtrl = now;
    if (homed) updateControl(dt);
  }

  if (now - tTelem >= TELEM_MS) {
    tTelem = now;
    CTRL.printf("%.2f, %.2f\r\n", countsToMm(readCount()), countsToMm(desiredCounts));
#ifdef SELFTEST
    // Mirror telemetry to USB so the board can be watched on the serial monitor.
    Serial.printf("pos %.2f mm  target %.2f mm\n",
                  countsToMm(readCount()), countsToMm(desiredCounts));
#endif
  }
}
 