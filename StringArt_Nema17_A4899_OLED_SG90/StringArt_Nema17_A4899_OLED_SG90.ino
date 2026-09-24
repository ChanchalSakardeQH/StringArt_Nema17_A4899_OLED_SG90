// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 CHANCHAL SAKARDE
//
// This file is part of String Art CNC.
//
// String Art CNC is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option) any
// later version.
//
// String Art CNC is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// this program. If not, see <https://www.gnu.org/licenses/>.

/*
  String Art CNC -- ESP32 / NEMA17 / A4988
  ========================================
  Drives a NEMA17 through an A4988 that rotates a disc of nails, a feeder
  servo that lays the thread, a drill servo and drill motor for making the
  holes, and a limit switch that gives the disc an absolute zero.

  This firmware is the ESP32 "String Art Studio" build written onto the
  ESP32 hardware: same web interface, same in-browser generator, same
  calibration and learning tools, driving this machine's motion instead.

  WHAT CAME ACROSS FROM THE ESP32 BUILD
    - the whole String Art Studio page (web_page.h): crop, generate,
      preview, download steps/SVG/pins.txt/greeting-card PDF, and the
      printable base-template designer
    - absolute nail<->step positioning, so any nail count works instead of
      only the hardcoded 200 x 12 = 2400 an earlier design assumed
    - the wrap cycle: overshoot, swing out, sweep back across the nail,
      swing in, land -- a real loop around the nail rather than a sweep
      that encloses nothing
    - persistent settings, sequence and progress in LittleFS, so a power
      cut can be resumed instead of restarting the piece
    - calibration (jog, re-sync, measured steps-per-turn) and the
      checkpoint/learning run
    - the optional SSD1306 status display

  WHAT IS NEW HERE, BECAUSE THE MACHINE IS DIFFERENT
    - drilling phase with its own configurable servo angles and timings
    - pause/resume and stop, kept from the original ESP32 sketch
    - A4988 enable handling, with an option to release the motor when idle
    - motion runs in a FreeRTOS task pinned to core 0 so long jobs never
      block the web server on core 1

  ----------------------------------------------------------------------
  WIRING
    A4988 STEP          -> GPIO 12
    A4988 DIR           -> GPIO 14
    A4988 EN            -> GPIO 26   (active LOW, driven by firmware --
                                      remove any jumper to GND)
    A4988 RESET + SLEEP -> tied together, to 3V3
    A4988 MS1/MS2/MS3   -> set your microstepping, then tell the web UI
                           about it under Advanced settings > Motor
    Feeder SG90 signal  -> GPIO 18
    Drill SG90 signal   -> GPIO 19
    Drill motor driver  -> GPIO 25
    Limit switch        -> GPIO 27 and GND (INPUT_PULLUP, closes to GND)

    OLED 0.96" SSD1306, I2C (optional):
      VCC -> 3V3, GND -> GND, SDA -> GPIO 21, SCL -> GPIO 22
    Detected automatically at 0x3C or 0x3D. With nothing connected the
    firmware notices at boot and carries on without it.

    Servos and the drill motor take their power from an external 5 V
    supply, not from the ESP32's regulator. Share the ground.

  BEFORE FLASHING
    1. Board Manager: "esp32" by Espressif Systems.
    2. Tools > Board: "ESP32 Dev Module" (WROOM-32).
    3. Tools > Partition Scheme: the page is about 160 kB of flash, so if
       the default scheme overflows pick "Minimal SPIFFS (1.9MB APP with
       OTA/190KB SPIFFS)" or "No OTA (2MB APP/2MB SPIFFS)". A scheme with
       *some* SPIFFS/LittleFS space is required -- that is where the
       settings, the sequence and the resume point are kept.
    4. Library Manager: "ESP32Servo" by Kevin
       Harrington. Everything else ships with the ESP32 core.
    5. Keep all three files together in the sketch folder:
         StringArt_Nema17_GUI.ino, web_page.h, oled_display.h

  A clean start, if settings ever look wrong:
    python -m esptool -p COM21 --chip esp32 erase_flash
  ----------------------------------------------------------------------
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>    // saved Wi-Fi credentials, in NVS
#include <ESP32Servo.h>
#include <LittleFS.h>
#include <Wire.h>
#include <vector>
#include <utility>
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "esp_system.h"
#include "machine_types.h"   // enums used in function signatures -- see the note there
#include "oled_display.h"

// ---------------------------------------------------------------------------
// Explicit prototypes. Arduino normally generates these by scanning the .ino;
// declaring them by hand costs nothing and means the build no longer depends
// on that scan succeeding -- the other half of the fix that moved the web page
// into web_page.h.
// ---------------------------------------------------------------------------
long stepsPerRev();
long normalizeStep(long s);
long nailBaseStep(uint16_t nail);
long nailToStep(uint16_t nail);
long autoLeadMag();
long autoSweepMag();
void applyAutoWrapGeometry();
long wrapLeadMag();
long wrapSweepMag();
uint16_t previewNail();
long discOffsetFromNail();
void driverEnable(bool on);
void stepPulse(int8_t dir);
bool moveSteps(long delta);
bool moveToStep(long target);
bool homeMachine();
void servoSlewFeeder(uint8_t target);
void servoSlewDrill(uint8_t target);
void feedCycle(bool settleFirst);
void wrapCycle(uint16_t nail, int8_t approachDir);
void drillHole();
void runDrillingPhase();
void runStringingPhase();
void runDryRunPhase();
void waitWhilePaused();
bool jobInterrupted();
void servicePending();
bool runLine(int i, bool allowVerify);
void motorTask(void *param);
void noteLineDone();
void learnReset();
void learnFit();
void learnApplySpr(long newX100, uint16_t atFeeder);
void verifyAnswer(long v);
void saveState();
void loadState();
void saveConfig();
void loadConfig();
long readConfigLine(File &f, long def);
void saveSequence();
void loadSequenceFromFile();
bool parseSequenceBody(const String &body, bool pairsExpected);
String autoBlock();
void dashTick();
void dashBoot(const char *l1, const char *l2, const char *l3);
String currentIp();
void sendCorsHeaders();
void handleCorsPreflight();
void handleRoot();
void handleStatus();
void handlePinsPost();
void handlePinsGet();
void handleUpload();
void handleConfig();
void handleAction();
void handleDrillStart();
void handleStringStart();
void handleStop();
void handlePause();
void handleResume();
void handleWifiSetup();
size_t photoSize();
void photoClear();
void handlePhotoGet();
void handlePhotoPost();
void handlePhotoUpload();

// The two helpers the HTTP handlers use to hand work to the motor task. They
// take the enums from machine_types.h, which is exactly why that file exists.
static bool queueCmd(PendCmd c, long v, String &why);
static bool startJob(JobReq r, String &why);
void handleNotFound();
void setupWiFi();
bool joinRouter(const String &ssid, const String &pass, bool announce);

// ---------------- Pins ----------------
#define STEP_PIN 12
#define DIR_PIN 14
#define FEEDER_SERVO_PIN 18
#define DRILL_SERVO_PIN 19
#define DRILL_MOTOR_PIN 25
#define ENABLE_PIN 26          // A4988 EN -- active LOW
#define LIMIT_SWITCH_PIN 27

// The ESP32's default I2C pair. Nothing else here uses them.
const uint8_t PIN_OLED_SDA = 21;
const uint8_t PIN_OLED_SCL = 22;

// The switch is wired to GND and read with the internal pull-up, so an
// unfitted switch reads HIGH (open) rather than floating. Set false if your
// build genuinely has no switch: homing is then refused up front instead of
// spinning a turn and a quarter before reporting failure.
const bool HAS_LIMIT_SWITCH = true;

bool limitTriggered() {
  return HAS_LIMIT_SWITCH && digitalRead(LIMIT_SWITCH_PIN) == LOW;
}

// ---------------- WiFi ----------------
// On boot the machine ALWAYS raises its own hotspot and then, in the
// background, tries to join whatever router was last saved. There is no
// captive setup portal: the Studio page is the setup page, and it is reachable
// at http://192.168.4.1 from the moment the ESP32 is up.
//
// The hotspot stays on even after the router connection succeeds (WIFI_AP_STA).
// That costs a little RAM and is worth it: entering router details over the
// hotspot would otherwise drop the very connection you are typing into.
// If none is saved (or it can't connect) it hosts the "StringArtMachine"
// hotspot plus a captive setup page, so a phone can point it at a router
// without re-flashing. To reconfigure later, use the Wi-Fi setup button under
// Advanced settings > Network, or POST /wifi-setup.
const char *AP_SSID = "StringArtMachine";
const char *AP_PASSWORD = "";          // 8+ characters if you set one
const unsigned long WIFI_JOIN_TIMEOUT_MS = 15000;

Preferences wifiPrefs;
String staSsid = "";            // router we are trying to join, "" if none
bool staJoined = false;
WebServer server(80);
Servo feederServo;
Servo drillServo;

// ---------------- Motor profile ----------------
//
// Everything downstream -- the nail->step maths, the status JSON, and the
// base-template designer in the web page -- derives from these three numbers,
// so change them here (or from the web UI) and nothing else needs touching.
//
//   steps per disc turn = motorFullSteps x microstep x gearRatio
//
// The defaults reproduce an earlier design exactly: 200 full steps, no
// microstepping, 12:1 to the disc = 2400 steps per turn, which at 200 nails
// is the 12 steps per nail the old STEPS_PER_NAIL constant hardcoded. Unlike
// that constant, these work for any nail count.
constexpr long DEF_MOTOR_FULL_STEPS = 200;
constexpr long DEF_MICROSTEP = 8;          // A4988 MS1+MS2 on, MS3 off
constexpr long DEF_GEAR_RATIO_X100000 = 100000L;    // 1.00000 : 1, disc on the shaft

long motorFullSteps = DEF_MOTOR_FULL_STEPS;
long microstep = DEF_MICROSTEP;
long gearRatioX100000 = DEF_GEAR_RATIO_X100000;

// Steps per disc revolution, x100 so the fractional part survives without
// floating point. A belt or a printed gear rarely lands on a whole number.
long geometryStepsPerRevX100() {
  long v = motorFullSteps * microstep * gearRatioX100000 / 1000L;
  return v > 0 ? v : 240000L;
}

// Runtime figure. Starts from the geometry above; the calibration routine and
// the checkpoint learner measure the real one and write it back.
long stepsPerRevX100 = 240000L;
long stepsPerRev() { return (stepsPerRevX100 + 50L) / 100L; }

// Fixed offset between nail numbering and disc position, in steps. Re-synced
// from the UI when the nail at the feeder is not the one the firmware thinks
// it is -- which is what missed steps eventually cause.
long nailOffsetSteps = 0;

// Which way the disc turns to present a nail to the fixed feeder.
//   +1 = disc turns with the nail numbering (an earlier design's behaviour)
//   -1 = disc turns against it
// Flip it live from the web UI; re-home afterwards.
int8_t dirSign = 1;

// ---------------- Adjustable defaults ----------------
// What the machine boots with before /config.txt is read, and what "Restore
// defaults" writes back.
constexpr uint16_t DEF_NUM_NAILS        = 360;
constexpr uint16_t DEF_STEP_PULSE_US    = 3000;  // per half-cycle, default
constexpr uint32_t DEF_AUTO_MS          = 500;   // dwell between lines
constexpr uint8_t  DEF_FEEDER_REST      = 180;
constexpr uint8_t  DEF_FEEDER_FEED      = 20;
constexpr uint16_t DEF_FEEDER_PULSE_MS  = 300;
constexpr uint16_t DEF_FEEDER_SETTLE_MS = 500;
constexpr uint16_t DEF_FEEDER_RECOVER_MS= 400;
constexpr uint8_t  DEF_DRILL_REST       = 20;
constexpr uint8_t  DEF_DRILL_DOWN       = 180;
constexpr uint16_t DEF_DRILL_SPIN_MS    = 300;
constexpr uint16_t DEF_DRILL_DWELL_MS   = 400;
constexpr uint8_t  DEF_DRILL_SLEW_MS    = 15;    // ms per degree, default
constexpr uint8_t  DEF_SERVO_SLEW_DEG   = 2;
constexpr uint16_t DEF_SERVO_SLEW_MS    = 20;
constexpr uint16_t DEF_WRAP_HOLD_IN_MS  = 400;
constexpr uint16_t DEF_WRAP_HOLD_SWEEP_MS = 400;

// ---- Acceleration -------------------------------------------------------
// A stepper has a "start-stop rate": the fastest it can be commanded from
// standstill without the rotor losing sync. Above that it has to be ramped up
// to speed. A disc bolted straight to the motor shaft carries enough inertia
// to put that limit well below the running rate, and commanding full speed
// from rest then makes the motor buzz and sit still instead of turning --
// which also makes homing fail, because the switch never arrives.
//
// So every move now starts at DEF_START_PULSE_US and ramps to stepPulseUs
// over accelSteps, then ramps back down before it stops.
constexpr uint16_t DEF_START_PULSE_US   = 9000;  // half-period at rest: ~55 steps/s
constexpr uint16_t DEF_ACCEL_STEPS      = 240;   // steps taken to reach full speed
constexpr uint16_t DEF_HOME_PULSE_US    = 6000;  // homing runs at half speed
constexpr uint8_t  DEF_ENABLE_SETTLE_MS = 5;     // A4988 needs a moment after EN
constexpr uint8_t  DEF_HOME_MAX_TURNS   = 2;     // give up after this many turns

// How long STEP is held high. The A4988 latches on the rising edge and needs
// only 1 us; the *period* between edges is what sets the speed. An earlier design
// held STEP high for a whole half-period, which works but leaves nowhere to
// put a ramp.
constexpr uint16_t STEP_HIGH_US = 10;

uint16_t startPulseUs = DEF_START_PULSE_US;
uint16_t accelSteps = DEF_ACCEL_STEPS;
uint16_t homePulseUs = DEF_HOME_PULSE_US;
uint8_t enableSettleMs = DEF_ENABLE_SETTLE_MS;
uint8_t homeMaxTurns = DEF_HOME_MAX_TURNS;
bool driverOn = false;

uint16_t numNails = DEF_NUM_NAILS;
uint16_t stepPulseUs = DEF_STEP_PULSE_US;
uint32_t autoAdvanceMs = DEF_AUTO_MS;

uint8_t feederRestAngle = DEF_FEEDER_REST;
uint8_t feederFeedAngle = DEF_FEEDER_FEED;
uint16_t feederPulseMs = DEF_FEEDER_PULSE_MS;
uint16_t feederSettleMs = DEF_FEEDER_SETTLE_MS;
uint16_t feederRecoverMs = DEF_FEEDER_RECOVER_MS;
bool feederAutoFeed = true;      // lay thread on every line of a run

uint8_t drillRestAngle = DEF_DRILL_REST;
uint8_t drillDownAngle = DEF_DRILL_DOWN;
uint16_t drillSpinMs = DEF_DRILL_SPIN_MS;
uint16_t drillDwellMs = DEF_DRILL_DWELL_MS;
uint8_t drillSlewMs = DEF_DRILL_SLEW_MS;

uint8_t servoSlewDeg = DEF_SERVO_SLEW_DEG;
uint16_t servoSlewMs = DEF_SERVO_SLEW_MS;

bool homeBeforeJob = true;       // home against the switch before drill/string

// Where the current step-zero came from. Nail numbering hangs off
// nailOffsetSteps, which is measured from step zero -- so if zero was set by
// hand and the machine later homes against the switch, the whole ring shifts
// by the angle between the two and every nail is wrong. Knowing which it was
// is the only way to catch that before a job runs.
bool zeroFromSwitch = false;

// ---- Dry run ------------------------------------------------------------
// The first few lines of the real pattern, run before the thread goes on. It
// is the only check that exercises everything at once -- homing, the nail
// maths, the wrap clearance, the feeder travel -- against the actual job
// rather than against a test move. Nothing is committed until you say it
// looked right.
uint16_t dryRunLines = 5;
volatile bool dryRunPending = false;   // finished, waiting for your verdict

// Set the first time homing actually succeeds. Without it, a machine whose
// switch does not work would be refused every job forever by the check below
// and have no way out except finding a setting -- a deadlock dressed up as a
// safety feature. If the switch has never worked, let the job start and let
// homing fail with its own message, which says what to check.
bool switchProven = false;
bool autoHomeOnBoot = false;     // home at power-up and return to the saved line
bool holdWhenIdle = false;       // keep the A4988 enabled between moves
uint16_t rehomeEvery = 0;        // re-home every N lines; 0 = off
uint16_t sinceRehome = 0;

// ---------------- Wrap cycle ----------------
//
// The tube tip cannot wrap a nail by moving out and back along one line --
// that encloses nothing. It has to trace a closed loop around the nail, and
// with only one servo axis the disc has to supply the other half of it:
//
//   1 APPROACH  disc moves so the tube sits half a nail BEFORE the target
//   2 SETTLE    let the disc stop ringing
//   3 OUT       servo swings the tube outside the nail ring, clear of nails
//               because it is midway between two of them
//   4 SWEEP     disc rotates one whole nail pitch while the tube stays out,
//               carrying the thread around the far side of the target nail
//   5 IN        servo brings the tube back inside, again midway between nails
//   6 LAND      disc backs up half a pitch to sit exactly on the target
//
// Steps 3 to 5 are the loop: out on one side, across the back, in on the
// other. Step 6 does not undo it -- the tube stays inside the ring, so it
// never re-crosses the thread.
bool wrapMode = true;            // false = the simple sweep the sketch shipped with
uint16_t wrapSteps = 0;          // overshoot past the nail, in steps
uint16_t wrapSweep = 0;          // how far the disc carries the thread while out
int8_t wrapDir = 1;              // global flip, if wraps shed
int8_t wrapApproachDir = 1;      // which way the disc travelled to reach the nail
uint16_t wrapHoldInMs = DEF_WRAP_HOLD_IN_MS;
uint16_t wrapHoldSweepMs = DEF_WRAP_HOLD_SWEEP_MS;

// ---------------- Checkpoints and learning ----------------
//
// Off by default. When on, every Nth line the disc stops ON the nail -- before
// wrapping -- and asks whether that nail really is in front of the feeder. The
// answer does two things:
//
//  1. Fixes the run now. A wrong answer re-syncs the numbering, then carries on.
//  2. Learns. Every answer is a data point: the total correction applied so
//     far, against the net steps the disc has turned since the last reference.
//     Positioning is absolute, so only two kinds of error can be learned:
//        a constant offset             -> shows up as the intercept
//        a wrong steps-per-turn figure -> an error that grows with net travel
//     A least-squares line through the points separates them. Missed steps are
//     random and cannot be learned, so they show up as a poor fit instead.
//
// The correction is steps/turn x (1 + slope). The obvious-looking
// steps/turn / (1 + slope) is WRONG here: corrections are applied to the
// offset, which flips the sign, and that version doubles the drift instead of
// removing it.
uint16_t verifyEvery = 0;
bool learnAutoSpr = true;
uint16_t sinceVerify = 0;
volatile bool verifyPending = false;
uint16_t verifyNail = 0;
int8_t verifyDir = 1;
long netSteps = 0;               // signed, unwrapped steps since the last reference

const uint8_t LEARN_MAX = 24;
long learnX[LEARN_MAX];
long learnE[LEARN_MAX];
uint8_t learnN = 0;
long learnCum = 0;
long learnBaseSprX100 = 0;
double learnA = 0, learnB = 0, learnRms = 0, learnSpanTurns = 0;
long learnSuggestX100 = 0;
bool learnConfident = false;
uint16_t learnChecks = 0, learnFixes = 0;
String learnNote = "";

// ---------------- Persistent files ----------------
const char *SEQ_FILE = "/sequence.csv";
const char *STATE_FILE = "/state.txt";
const char *CONFIG_FILE = "/config.txt";

SemaphoreHandle_t fsMutex = nullptr;   // LittleFS is touched from both cores
struct FsLock {
  FsLock()  { if (fsMutex) xSemaphoreTake(fsMutex, portMAX_DELAY); }
  ~FsLock() { if (fsMutex) xSemaphoreGive(fsMutex); }
};

// ---------------- Machine state ----------------
// MachineState, JobReq and PendCmd are defined in machine_types.h. They have
// to be, because Arduino generates prototypes for the functions that take them
// and puts those prototypes above this point -- see the note in that file.
volatile MachineState machineState = STATE_IDLE;
MachineState stateBeforePause = STATE_IDLE;
volatile bool stopRequested = false;
volatile bool pauseRequested = false;

volatile JobReq jobReq = JOB_NONE;

volatile PendCmd pendCmd = PC_NONE;
volatile long pendVal = 0;

volatile int progressCurrent = 0;    // index into pinSequence, or hole number
volatile int progressTotal = 0;
volatile int currentFromNail = -1;
volatile int currentToNail = -1;
volatile bool moving = false;
volatile bool feederBusy = false;
volatile bool wrapBusy = false;
volatile bool homing = false;
bool homeError = false;
bool positionKnown = true;
bool resumeAfterHome = false;
String lastError = "";
String autoNote = "";
const char *phaseText = "";

long currentStep = 0;                 // absolute step position of the disc
uint8_t servoCurrent = DEF_FEEDER_REST;
uint8_t drillServoCurrent = DEF_DRILL_REST;

// Calibration spin
bool calRunning = false;

// Index of the line being worked on right now, or -1 between lines. Distinct
// from progressCurrent, which counts lines *finished*: during line i these
// differ by one, so reading "the next line" off progressCurrent gave the
// current line while moving and the next one while idle -- the display
// flickered between the two depending on when the poll landed.
volatile int activeLine = -1;

// ---- Guided "which pin actually arrived?" check -------------------------
// caltest records the move it asked for; calresult is told what really turned
// up and solves for the true steps per disc turn. Keeping the commanded step
// delta rather than recomputing it means the answer does not depend on the
// setting being corrected.
long calTestSteps = 0;        // signed steps commanded by the last check
int  calTestFrom = -1;        // nail at the feeder when it started
int  calTestTarget = -1;      // nail it was asked for
String calTestNote = "";
int calRevs = 0;

// Run timing. Measured, not predicted: the theoretical cycle time ignores disc
// travel, which varies with how far apart consecutive nails are.
unsigned long lastLineAt = 0;
uint32_t avgNailMs = 0;
uint32_t runElapsedMs = 0;
unsigned long elapsedTickAt = 0;

// The sequence, as consecutive from,to pairs. Uploading a plain nail list
// turns it into pairs; both describe the same thread path.
std::vector<std::pair<int, int>> pinSequence;

// OLED
bool oledFound = false;
bool oledFlip = false;
unsigned long dashNextAt = 0;
unsigned long bootScreenUntil = 0;
uint32_t dashLastHash = 0;

#include "web_page.h"

// ===========================================================================
//  Nail <-> step geometry
// ===========================================================================

long normalizeStep(long s) {
  long spr = stepsPerRev();
  if (spr <= 0) return 0;
  s %= spr;
  if (s < 0) s += spr;
  return s;
}

// Where nail N sits before the calibration offset is applied. Rounded to the
// nearest step, signed by dirSign, wrapped to one revolution.
long nailBaseStep(uint16_t nail) {
  if (numNails == 0) return 0;
  long n = ((long)nail % (long)numNails + (long)numNails) % (long)numNails;
  long s = (n * stepsPerRevX100 + (long)numNails * 50L) / ((long)numNails * 100L);
  return normalizeStep((long)dirSign * s);
}

long nailToStep(uint16_t nail) {
  return normalizeStep(nailBaseStep(nail) + nailOffsetSteps);
}

// Half a nail pitch: that puts the ring crossing exactly midway between two
// nails, so the tube passes through a gap rather than into a nail, and the
// swept loop encloses the target nail and nothing else.
long autoLeadMag() {
  if (numNails == 0) return 1;
  long half = (stepsPerRevX100 / (long)numNails / 2 + 50L) / 100L;
  return half < 1 ? 1 : half;
}

// Twice the lead, deliberately, rather than the pitch rounded on its own.
// Rounding the two independently leaves the loop sitting off centre.
long autoSweepMag() { return autoLeadMag() * 2; }

void applyAutoWrapGeometry() {
  wrapSteps = (uint16_t)autoLeadMag();
  wrapSweep = (uint16_t)autoSweepMag();
}

long wrapLeadMag()  { return wrapSteps > 0 ? (long)wrapSteps : autoLeadMag(); }
long wrapSweepMag() { return wrapSweep > 0 ? (long)wrapSweep : autoSweepMag(); }

// The loop is handed off the direction of travel, with wrapDir as a global
// flip if the thread runs the other way round your tube.
long wrapSignedLead()  { return wrapLeadMag()  * wrapApproachDir * wrapDir; }
long wrapSignedSweep() { return wrapSweepMag() * wrapApproachDir * wrapDir; }

// The nail the wrap preview works around: the one the sequence is on, or 0.
uint16_t previewNail() {
  if (!pinSequence.empty()) {
    int i = progressCurrent;
    if (i < 0) i = 0;
    if (i >= (int)pinSequence.size()) i = (int)pinSequence.size() - 1;
    return (uint16_t)pinSequence[i].second;
  }
  return 0;
}

// Where the disc sits relative to the preview nail, in steps, shortest way.
long discOffsetFromNail() {
  long spr = stepsPerRev();
  long d = currentStep - nailToStep(previewNail());
  while (d >  spr / 2) d -= spr;
  while (d < -spr / 2) d += spr;
  return d;
}

// ===========================================================================
//  Low-level motion. Everything here runs in the motor task on core 0.
// ===========================================================================

// The A4988's EN pin is active LOW. Holding the motor costs current and heat
// and is only worth it if something can nudge the disc between moves.
void driverEnable(bool on) {
  bool was = driverOn;
  digitalWrite(ENABLE_PIN, on ? LOW : HIGH);
  driverOn = on;
  // The A4988 energises its outputs a moment after EN goes low. Stepping into
  // that moment throws away the first pulses, which at the start of a move is
  // exactly when the rotor is least able to afford it.
  if (on && !was && enableSettleMs) vTaskDelay(pdMS_TO_TICKS(enableSettleMs));
}

// Waits until an absolute deadline on the micros() clock. Returns true if it
// gave time back to FreeRTOS on the way.
//
// Busy-waiting milliseconds at a time starves core 0's idle task, so long
// waits go to vTaskDelay and only the last fraction of a millisecond is spun
// out. Waiting to an *absolute* deadline is what makes that safe: vTaskDelay
// only resolves to whole ticks, but the error is absorbed by the busy-wait at
// the end rather than accumulating into drift from one step to the next.
static bool waitUntilUs(uint32_t deadlineUs) {
  bool yielded = false;
  for (;;) {
    int32_t left = (int32_t)(deadlineUs - micros());
    if (left <= 0) return yielded;
    if (left > 3000) {
      vTaskDelay(pdMS_TO_TICKS((uint32_t)(left - 2000) / 1000U));
      yielded = true;
    } else if (left > 60) {
      delayMicroseconds((uint32_t)left - 50);
    } else {
      while ((int32_t)(deadlineUs - micros()) > 0) { }
      return yielded;
    }
  }
}

// The step period for step `i` of a move `n` steps long: ramped up from
// startPulseUs at the beginning, ramped back down at the end, flat in the
// middle. A move shorter than two ramps becomes a triangle and never reaches
// full speed, which is correct -- there is no time to.
//
// The interpolation is linear in *period* rather than in speed. That is not
// the textbook constant-acceleration curve, but it is monotonic, cheap in
// integer arithmetic, and close enough over the few hundred steps involved.
static uint32_t rampPeriodUs(long i, long n, uint32_t runPeriodUs) {
  uint32_t start = (uint32_t)startPulseUs * 2U;
  if (start <= runPeriodUs || accelSteps == 0 || n <= 1) return runPeriodUs;

  long a = accelSteps;
  if (a > n / 2) a = n / 2;
  if (a <= 0) return runPeriodUs;

  long into = i;              // steps taken since starting
  long left = n - 1 - i;      // steps still to go
  long d = into < left ? into : left;
  if (d >= a) return runPeriodUs;

  return start - (uint32_t)(((uint64_t)(start - runPeriodUs) * (uint64_t)d) / (uint64_t)a);
}

// A single step, used where there is no ramp to speak of.
void stepPulse(int8_t dir) {
  digitalWrite(DIR_PIN, dir > 0 ? HIGH : LOW);
  delayMicroseconds(5);
  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(STEP_HIGH_US);
  digitalWrite(STEP_PIN, LOW);
  delayMicroseconds((uint32_t)stepPulseUs * 2U);
}

// True when the current job should give up: a stop was requested, or the job
// was abandoned some other way.
bool jobInterrupted() { return stopRequested; }

// Blocks in place while pauseRequested is set, holding the motor exactly where
// it is. Restores whatever state the machine was in before pausing once
// /resume clears the flag. Returns immediately if not paused.
void waitWhilePaused() {
  if (!pauseRequested) return;
  stateBeforePause = machineState;
  machineState = STATE_PAUSED;
  Serial.println("Paused -- waiting for /resume.");
  while (pauseRequested && !stopRequested) {
    servicePending();               // jogging is still allowed while paused
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  if (!stopRequested) {
    machineState = stateBeforePause;
    Serial.println("Resumed.");
  }
}

// Steps `delta` (signed), ramping up to speed and back down. Returns false if
// interrupted. Time is given back to FreeRTOS inside the inter-step wait, so
// core 0 housekeeping is never starved -- an earlier design could trip the
// watchdog on a long move, and its every-tenth-step vTaskDelay bought that
// safety at the cost of a 1 ms hiccup in one step out of ten.
bool moveSteps(long delta) {
  if (delta == 0) return true;
  int8_t dir = delta > 0 ? 1 : -1;
  long n = delta > 0 ? delta : -delta;

  moving = true;
  driverEnable(true);
  digitalWrite(DIR_PIN, dir > 0 ? HIGH : LOW);
  delayMicroseconds(10);          // DIR setup; the A4988 wants 200 ns

  const uint32_t runPeriod = (uint32_t)stepPulseUs * 2U;
  uint32_t next = micros();
  uint32_t lastYieldMs = millis();

  for (long i = 0; i < n; i++) {
    if (jobInterrupted()) { moving = false; return false; }
    if (pauseRequested) {
      waitWhilePaused();
      if (jobInterrupted()) { moving = false; return false; }
      next = micros();            // do not try to make up the paused time
    }

    if (waitUntilUs(next)) lastYieldMs = millis();

    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(STEP_HIGH_US);
    digitalWrite(STEP_PIN, LOW);

    next += rampPeriodUs(i, n, runPeriod);
    currentStep += dir;
    netSteps += dir;

    // Backstop for short periods, where the wait above never blocks. Rebasing
    // the deadline afterwards matters: catching up would fire the next few
    // steps back to back, which is precisely the jolt the ramp exists to
    // avoid.
    if ((uint32_t)(millis() - lastYieldMs) > 20U) {
      vTaskDelay(pdMS_TO_TICKS(1));
      lastYieldMs = millis();
      next = micros() + rampPeriodUs(i, n, runPeriod);
    }
  }
  currentStep = normalizeStep(currentStep);
  moving = false;
  if (!holdWhenIdle) driverEnable(false);
  return true;
}

// Absolute move, shortest way round. Planning every move from an absolute
// target is what stops rounding error accumulating over thousands of chords.
bool moveToStep(long target) {
  long spr = stepsPerRev();
  target = normalizeStep(target);
  long delta = target - currentStep;
  while (delta >  spr / 2) delta -= spr;
  while (delta <= -spr / 2) delta += spr;
  bool ok = moveSteps(delta);
  if (ok) currentStep = target;
  return ok;
}

// Slow open-loop seek toward the limit switch, then declare that step zero.
bool homeMachine() {
  if (!HAS_LIMIT_SWITCH) {
    homeError = true;
    lastError = "No limit switch fitted, so the disc cannot be homed.";
    return false;
  }
  Serial.println("Homing: seeking the limit switch...");
  homing = true;
  moving = true;
  driverEnable(true);
  phaseText = "homing";

  long cap = (long)stepsPerRev() * (homeMaxTurns ? homeMaxTurns : 1);
  long taken = 0;
  int8_t dir = (int8_t)(-dirSign);                // toward the switch

  digitalWrite(DIR_PIN, dir > 0 ? HIGH : LOW);
  delayMicroseconds(10);

  // Homing is the one move that starts cold, against static friction, with no
  // idea how far it has to go -- so it runs at half speed and still ramps in.
  const uint32_t runPeriod = (uint32_t)homePulseUs * 2U;
  uint32_t next = micros();
  uint32_t lastYieldMs = millis();

  while (!limitTriggered() && taken < cap) {
    if (jobInterrupted()) { homing = false; moving = false; return false; }
    if (pauseRequested) {
      waitWhilePaused();
      if (jobInterrupted()) { homing = false; moving = false; return false; }
      next = micros();
    }

    if (waitUntilUs(next)) lastYieldMs = millis();

    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(STEP_HIGH_US);
    digitalWrite(STEP_PIN, LOW);

    // Ramp in only. There is no deceleration because the stopping point is
    // whenever the switch closes, and it is slow enough not to need one.
    uint32_t period = runPeriod;
    if (accelSteps && taken < (long)accelSteps) {
      uint32_t start = (uint32_t)startPulseUs * 2U;
      if (start > runPeriod) {
        period = start - (uint32_t)(((uint64_t)(start - runPeriod) *
                                     (uint64_t)taken) / (uint64_t)accelSteps);
      }
    }
    next += period;
    taken++;

    if ((uint32_t)(millis() - lastYieldMs) > 20U) {
      vTaskDelay(pdMS_TO_TICKS(1));
      lastYieldMs = millis();
      next = micros() + period;
    }
  }

  homing = false;
  moving = false;
  if (!holdWhenIdle) driverEnable(false);
  vTaskDelay(pdMS_TO_TICKS(300));

  if (limitTriggered()) {
    currentStep = 0;
    positionKnown = true;          // the switch is an absolute reference
    zeroFromSwitch = true;
    switchProven = true;
    homeError = false;
    verifyPending = false;
    learnReset();
    saveState();
    Serial.printf("Homing complete after %ld steps.\n", taken);
    return true;
  }

  homeError = true;
  phaseText = "";            // or the chip reads "idle - homing" for ever
  // The switch not closing has three common causes and the machine cannot
  // tell them apart, so the message names all three rather than guessing.
  lastError = String("Homing failed: the switch never closed after ") +
              String(taken) + " steps (" + String(homeMaxTurns ? homeMaxTurns : 1) +
              " turns). Check that the switch is mounted where a flag on the "
              "disc reaches it, that the disc actually turned, and that the "
              "gear ratio is right.";
  Serial.printf("Homing FAILED after %ld steps.\n", taken);
  return false;
}

// ===========================================================================
//  Servos
// ===========================================================================
//
// Servo.write() commands a position and an SG90 slams to it as fast as its
// gearing allows; there is no speed parameter. To move it slowly the angle has
// to be walked in small increments, which is what these do. Travel time is
// (degrees to cover / servoSlewDeg) x servoSlewMs.

void servoSlewFeeder(uint8_t target) {
  int stepDeg = servoSlewDeg < 1 ? 1 : servoSlewDeg;
  while (servoCurrent != target) {
    if (jobInterrupted()) return;
    int diff = (int)target - (int)servoCurrent;
    if (diff >= -stepDeg && diff <= stepDeg) servoCurrent = target;
    else servoCurrent = (uint8_t)((int)servoCurrent + (diff > 0 ? stepDeg : -stepDeg));
    feederServo.write(servoCurrent);
    vTaskDelay(pdMS_TO_TICKS(servoSlewMs ? servoSlewMs : 1));
  }
}

void servoSlewDrill(uint8_t target) {
  while (drillServoCurrent != target) {
    if (jobInterrupted()) return;
    drillServoCurrent = (uint8_t)((int)drillServoCurrent +
                                  (target > drillServoCurrent ? 1 : -1));
    drillServo.write(drillServoCurrent);
    if (drillSlewMs) vTaskDelay(pdMS_TO_TICKS(drillSlewMs));
  }
}

// The simple pay-out pulse: settle, swing to the feed angle, hold, come back,
// recover. settleFirst is false for a manual "Feed now" press, when nothing is
// moving and there is nothing to settle.
void feedCycle(bool settleFirst) {
  feederBusy = true;
  phaseText = "feed";
  if (settleFirst && feederSettleMs) vTaskDelay(pdMS_TO_TICKS(feederSettleMs));
  servoSlewFeeder(feederFeedAngle);
  if (feederPulseMs) vTaskDelay(pdMS_TO_TICKS(feederPulseMs));
  servoSlewFeeder(feederRestAngle);
  if (feederRecoverMs) vTaskDelay(pdMS_TO_TICKS(feederRecoverMs));
  feederBusy = false;
  phaseText = "";
}

// The real wrap: approach past the nail, swing out, sweep back across it,
// swing in, land on it. approachDir is which way the disc was travelling when
// it came to this nail; the loop is handed off that, so the tube always passes
// on the far side from the thread trailing behind.
void wrapCycle(uint16_t nail, int8_t approachDir) {
  if (approachDir != 0) wrapApproachDir = approachDir;
  wrapBusy = true;
  long nailStep = nailToStep(nail);

  phaseText = "wrap approach";
  if (!moveToStep(normalizeStep(nailStep + wrapSignedLead()))) { wrapBusy = false; return; }
  if (feederSettleMs) vTaskDelay(pdMS_TO_TICKS(feederSettleMs));

  phaseText = "wrap out";
  servoSlewFeeder(feederFeedAngle);
  if (wrapHoldInMs) vTaskDelay(pdMS_TO_TICKS(wrapHoldInMs));

  phaseText = "wrap sweep";
  if (!moveToStep(normalizeStep(nailStep + wrapSignedLead() - wrapSignedSweep()))) {
    servoSlewFeeder(feederRestAngle);   // never leave the tube out over the nails
    wrapBusy = false;
    return;
  }
  if (wrapHoldSweepMs) vTaskDelay(pdMS_TO_TICKS(wrapHoldSweepMs));

  phaseText = "wrap in";
  servoSlewFeeder(feederRestAngle);
  if (feederRecoverMs) vTaskDelay(pdMS_TO_TICKS(feederRecoverMs));

  phaseText = "wrap land";
  moveToStep(nailStep);
  wrapBusy = false;
  phaseText = "";
}

// The behaviour the plain sweep, kept for when wrapMode is
// off: sweep the feeder across while nudging the disc twelve steps each way.
void simpleFeedSweep() {
  feederBusy = true;
  phaseText = "feed";
  // Half a nail pitch each way, not a fixed 12 steps --
  // 12 was one nail on a 200-nail, 2400-step machine and is nearly three
  // nails on a finer one. It nets to zero either way, so this is about the
  // tube clearing the right gap, not about position.
  long sweep = stepsPerRev() / (numNails ? (long)numNails * 2 : 400);
  if (sweep < 1) sweep = 1;
  servoSlewFeeder(feederFeedAngle);
  vTaskDelay(pdMS_TO_TICKS(100));
  moveSteps(sweep);
  servoSlewFeeder(feederRestAngle);
  vTaskDelay(pdMS_TO_TICKS(100));
  moveSteps(-sweep);
  if (feederRecoverMs) vTaskDelay(pdMS_TO_TICKS(feederRecoverMs));
  feederBusy = false;
  phaseText = "";
}

void drillHole() {
  phaseText = "drill";
  digitalWrite(DRILL_MOTOR_PIN, HIGH);
  if (drillSpinMs) vTaskDelay(pdMS_TO_TICKS(drillSpinMs));
  servoSlewDrill(drillDownAngle);
  if (drillDwellMs) vTaskDelay(pdMS_TO_TICKS(drillDwellMs));
  servoSlewDrill(drillRestAngle);
  vTaskDelay(pdMS_TO_TICKS(200));
  digitalWrite(DRILL_MOTOR_PIN, LOW);
  phaseText = "";
}

// ===========================================================================
//  Run timing and learning
// ===========================================================================

// Folds one completed cycle into the average. Wildly short or long samples are
// dropped: a jog or a pause would otherwise poison the estimate.
void noteLineDone() {
  unsigned long now = millis();
  if (lastLineAt != 0) {
    unsigned long dt = now - lastLineAt;
    if (dt > 200 && dt < 120000UL) {
      avgNailMs = (avgNailMs == 0) ? (uint32_t)dt
                                   : (uint32_t)((avgNailMs * 3UL + dt) / 4UL);
    }
  }
  lastLineAt = now;
}

// A new absolute reference (a home, or a manual re-sync) starts a new session:
// old points were measured against a different zero and cannot be mixed in.
void learnReset() {
  learnN = 0; learnCum = 0; netSteps = 0;
  learnA = learnB = learnRms = learnSpanTurns = 0;
  learnSuggestX100 = 0; learnConfident = false;
  learnChecks = learnFixes = 0;
}

void learnFit() {
  learnSuggestX100 = 0;
  learnConfident = false;
  if (learnN < 2) { learnA = learnN ? learnE[0] : 0; learnB = 0; learnRms = 0; return; }

  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  long xmin = learnX[0], xmax = learnX[0];
  for (uint8_t i = 0; i < learnN; i++) {
    double x = learnX[i], y = learnE[i];
    sx += x; sy += y; sxx += x * x; sxy += x * y;
    if (learnX[i] < xmin) xmin = learnX[i];
    if (learnX[i] > xmax) xmax = learnX[i];
  }
  double n = learnN, den = n * sxx - sx * sx;
  learnSpanTurns = (xmax - xmin) / (double)stepsPerRev();
  if (den <= 0 || learnSpanTurns < 0.5) {
    // Not enough spread in travel to see drift; everything is offset.
    learnA = sy / n; learnB = 0;
  } else {
    learnB = (n * sxy - sx * sy) / den;
    learnA = (sy - learnB * sx) / n;
  }
  double ss = 0;
  for (uint8_t i = 0; i < learnN; i++) {
    double r = learnE[i] - (learnA + learnB * learnX[i]);
    ss += r * r;
  }
  learnRms = sqrt(ss / n);

  // Only trust the slope when all of these hold. Answers come in whole nails,
  // so a couple of points can suggest a trend that is really just rounding.
  double pitch = stepsPerRevX100 / 100.0 / numNails;
  bool enough = learnN >= 5 && learnSpanTurns >= 2.0;
  bool fits   = learnRms <= 0.5 * pitch;                 // the line explains the answers
  bool real   = fabs(learnB) * (xmax - xmin) >= 2 * pitch;
  bool sane   = fabs(learnB) <= 0.03;                    // a 3% error is a fault, not a ratio
  if (enough && fits && real && sane) {
    learnSuggestX100 = lround(stepsPerRevX100 * (1.0 + learnB));
    learnConfident = true;
  }
}

// Adopt a steps/turn figure while nail `atFeeder` is known to be in front of
// the feeder, keeping the numbering consistent across the change.
void learnApplySpr(long newX100, uint16_t atFeeder) {
  if (learnBaseSprX100 == 0) learnBaseSprX100 = stepsPerRevX100;
  stepsPerRevX100 = newX100;
  currentStep = normalizeStep(currentStep);
  nailOffsetSteps = normalizeStep(currentStep - nailBaseStep(atFeeder));
  learnReset();
  saveConfig();
  saveState();
}

// Answer to a checkpoint. v = the nail actually in front of the feeder;
// -1 = "yes, it's right"; -2 = skip this one without learning from it.
void verifyAnswer(long v) {
  if (v == -2) {
    learnNote = "Skipped checkpoint at nail " + String(verifyNail) + ".";
    verifyPending = false;
    return;
  }

  uint16_t seen = (v < 0) ? verifyNail : (uint16_t)(v % numNails);

  // Always re-sync to where the disc is NOW. If you nudged it to centre the
  // nail before answering "yes", that nudge is the correction, measured to the
  // step. That precision is what lets it learn: a whole-nail answer alone can
  // leave the disc up to half a nail off.
  long spr = stepsPerRev();
  long was = nailOffsetSteps;
  nailOffsetSteps = normalizeStep(currentStep - nailBaseStep(seen));
  long c = nailOffsetSteps - was;
  while (c >  spr / 2) c -= spr;
  while (c < -spr / 2) c += spr;
  learnCum += c;
  if (c != 0) learnFixes++;
  learnChecks++;

  if (learnN < LEARN_MAX) {
    learnX[learnN] = netSteps;
    learnE[learnN] = learnCum;
    learnN++;
  } else {
    // Keep the most recent window; the oldest point is the least relevant.
    for (uint8_t i = 1; i < LEARN_MAX; i++) {
      learnX[i - 1] = learnX[i];
      learnE[i - 1] = learnE[i];
    }
    learnX[LEARN_MAX - 1] = netSteps;
    learnE[LEARN_MAX - 1] = learnCum;
  }
  learnFit();

  learnNote = (c == 0) ? "Checkpoint at nail " + String(seen) + " was correct."
                       : "Corrected " + String(c) + " steps at nail " + String(seen) + ".";

  if (learnAutoSpr && learnConfident && learnSuggestX100 > 0 &&
      learnSuggestX100 != stepsPerRevX100) {
    long before = stepsPerRevX100;
    learnApplySpr(learnSuggestX100, seen);
    learnNote += " Learned steps/turn: " + String(before / 100.0, 2) +
                 " -> " + String(stepsPerRevX100 / 100.0, 2) + ".";
  }
  saveConfig();
  saveState();
  verifyPending = false;
}

// Why the run is not moving. Every reason must be visible on the page, or a
// waiting machine looks exactly like a broken button.
String autoBlock() {
  if (machineState == STATE_PAUSED) return "Paused. Press Resume to carry on.";
  if (verifyPending) return "Paused: waiting for your checkpoint answer.";
  if (machineState == STATE_HOMING) return "Finding home.";
  if (calRunning) return "Calibration spin in progress.";
  if (machineState == STATE_ERROR) return lastError;
  return "";
}

// ===========================================================================
//  Pending one-shot commands
// ===========================================================================
//
// The web server runs on core 1 and the motor on core 0. Everything that
// touches the stepper or a servo is queued here and performed by the motor
// task, so the two cores can never drive the same pin at once.

volatile bool servicingPending = false;

static uint8_t clampAngle(long v) {
  if (v < 0) return 0;
  if (v > 180) return 180;
  return (uint8_t)v;
}

void servicePending() {
  if (servicingPending) return;
  PendCmd c = (PendCmd)pendCmd;
  if (c == PC_NONE) return;
  long v = pendVal;
  pendCmd = PC_NONE;
  servicingPending = true;      // also tells waitWhilePaused to stand aside,
                                // so a jog requested while paused can finish

  switch (c) {
    case PC_JOG:
      // Nudge the disc without touching the nail numbering or progress. Used
      // to line a nail up with the feeder before re-syncing.
      moveToStep(normalizeStep(currentStep + v));
      saveState();
      break;

    case PC_GOTO:
      moveToStep(nailToStep((uint16_t)((v % numNails + numNails) % numNails)));
      break;

    case PC_SERVOTEST:
      servoSlewFeeder(clampAngle(v));
      break;

    case PC_DRILLSERVOTEST:
      servoSlewDrill(clampAngle(v));
      break;

    case PC_FEED:
      feedCycle(false);         // manual press: nothing is moving to settle
      break;

    case PC_WRAPTEST:
      wrapCycle(previewNail(), (int8_t)(v != 0 ? v : wrapApproachDir));
      break;

    case PC_WRAPPREVIEW: {
      // Park the disc a signed number of steps from the nail at the feeder, so
      // the ahead and behind positions of the wrap can be set by eye.
      long cap = stepsPerRev() / 4;
      if (v > cap) v = cap;
      if (v < -cap) v = -cap;
      moveToStep(normalizeStep(nailToStep(previewNail()) + v));
      break;
    }

    case PC_CALMOVE: {
      // Step one of measuring the real steps-per-revolution: drive a whole
      // number of turns and stop. Whatever nail comes back to the feeder tells
      // us how wrong our figure is.
      if (v < 1) v = 1;
      if (v > 50) v = 50;
      calRevs = (int)v;
      calRunning = true;
      phaseText = "calibration spin";
      moveSteps((long)calRevs * stepsPerRev() * (long)dirSign);
      calRunning = false;
      phaseText = "";
      break;
    }

    case PC_DRILLTEST:
      drillHole();
      break;

    // Hand-stepping. Next runs one real line, wrap and feed included, so the
    // result on the board is indistinguishable from the automatic run.
    case PC_NEXTLINE: {
      int i = progressCurrent;
      if (i >= 0 && i < (int)pinSequence.size()) {
        MachineState was = machineState;
        machineState = STATE_STRINGING;
        elapsedTickAt = millis();
        phaseText = "manual";
        runLine(i, false);          // no checkpoint prompt when hand-stepping
        phaseText = "";
        machineState = (progressCurrent >= (int)pinSequence.size())
                       ? STATE_DONE
                       : (was == STATE_DONE ? STATE_IDLE : was);
      }
      break;
    }

    // Previous only walks the disc back to where that line started. It does
    // not unwind thread -- nothing in software can -- so it is a way to redo a
    // line you have unwound by hand, not an undo.
    case PC_PREVLINE: {
      int i = progressCurrent - 1;
      if (i >= 0 && i < (int)pinSequence.size()) {
        phaseText = "manual";
        moveToStep(nailToStep((uint16_t)pinSequence[i].first));
        progressCurrent = i;
        currentFromNail = pinSequence[i].first;
        currentToNail = pinSequence[i].second;
        if (machineState == STATE_DONE) machineState = STATE_IDLE;
        phaseText = "";
        saveState();
      }
      break;
    }

    default:
      break;
  }

  servicingPending = false;
}

// ===========================================================================
//  Job phases
// ===========================================================================

void runDrillingPhase() {
  progressCurrent = 0;
  progressTotal = numNails;
  currentFromNail = -1;
  currentToNail = -1;
  lastLineAt = 0;
  runElapsedMs = 0;
  elapsedTickAt = millis();
  machineState = STATE_DRILLING;
  Serial.printf("Drilling %u holes.\n", numNails);

  for (uint16_t i = 0; i < numNails; i++) {
    if (jobInterrupted()) break;
    waitWhilePaused();
    if (jobInterrupted()) break;

    currentToNail = (int)i;
    phaseText = "index";
    if (!moveToStep(nailToStep(i))) break;
    drillHole();

    progressCurrent = i + 1;
    noteLineDone();
    saveState();
    vTaskDelay(pdMS_TO_TICKS(2));    // let the status poll in
  }

  currentFromNail = -1;
  currentToNail = -1;
  phaseText = "";
  machineState = stopRequested ? STATE_IDLE : STATE_DONE;
  Serial.println(stopRequested ? "Drilling stopped." : "Drilling complete.");
}

// Runs exactly one line of the sequence: work out which way the disc will
// travel, optionally stop for a checkpoint, then wrap the far nail and feed.
// Advances progressCurrent on success.
//
// The stringing job and the manual Next button both go through here, so a
// hand-stepped line is not an approximation of an automatic one -- it is the
// same code, same wrap, same feed, same bookkeeping.
bool runLine(int i, bool allowVerify) {
  if (i < 0 || i >= (int)pinSequence.size()) return false;

  int fromPos = pinSequence[i].first;
  int toPos = pinSequence[i].second;
  currentFromNail = fromPos;
  currentToNail = toPos;
  activeLine = i;

  // Which way the disc is about to travel, worked out before moving, so the
  // wrap loop can be handed that direction.
  long spr = stepsPerRev();
  long delta = nailToStep((uint16_t)toPos) - currentStep;
  while (delta >  spr / 2) delta -= spr;
  while (delta < -spr / 2) delta += spr;
  int8_t travelDir = (delta == 0) ? wrapApproachDir : (delta > 0 ? 1 : -1);

  // Checkpoint: stop ON the nail and ask, before wrapping, so the question
  // is about the nail actually at the feeder.
  if (allowVerify && verifyEvery > 0 && ++sinceVerify >= verifyEvery) {
    sinceVerify = 0;
    verifyNail = (uint16_t)toPos;
    verifyDir = travelDir;
    phaseText = "checkpoint";
    if (!moveToStep(nailToStep((uint16_t)toPos))) return false;
    verifyPending = true;
    while (verifyPending && !stopRequested) {
      servicePending();              // jogging to centre the nail is allowed
      waitWhilePaused();
      vTaskDelay(pdMS_TO_TICKS(50));
    }
    phaseText = "";
    if (jobInterrupted()) return false;
    travelDir = verifyDir;
  }

  if (feederAutoFeed && wrapMode) {
    wrapCycle((uint16_t)toPos, travelDir);
  } else {
    phaseText = "index";
    if (!moveToStep(nailToStep((uint16_t)toPos))) return false;
    phaseText = "";
    if (feederAutoFeed) simpleFeedSweep();
  }
  if (jobInterrupted()) return false;

  progressCurrent = i + 1;
  activeLine = -1;
  noteLineDone();
  saveState();
  return true;
}

// The first dryRunLines lines, exactly as the real job would run them, then
// stop and wait. Deliberately the same code path: a dry run that behaved
// differently from the real thing would be worse than no dry run at all.
void runDryRunPhase() {
  int n = (int)pinSequence.size();
  int upto = (int)dryRunLines;
  if (upto > n) upto = n;

  progressCurrent = 0;
  progressTotal = n;
  dryRunPending = false;
  lastLineAt = 0;
  elapsedTickAt = millis();
  machineState = STATE_STRINGING;
  phaseText = "dry run";
  Serial.printf("Dry run: %d of %d lines.\n", upto, n);

  bool finished = true;
  for (int i = 0; i < upto; i++) {
    if (jobInterrupted()) { finished = false; break; }
    waitWhilePaused();
    if (jobInterrupted()) { finished = false; break; }
    if (!runLine(i, false)) { finished = false; break; }
    if (autoAdvanceMs) vTaskDelay(pdMS_TO_TICKS(autoAdvanceMs));
  }

  currentFromNail = -1;
  currentToNail = -1;
  activeLine = -1;
  phaseText = "";
  // Progress goes back to zero either way: a dry run must never be mistaken
  // for work already done, or the real run would start partway through.
  progressCurrent = 0;
  saveState();
  dryRunPending = finished;
  machineState = STATE_IDLE;
  Serial.println(finished ? "Dry run finished -- waiting for your verdict."
                          : "Dry run stopped.");
}

void runStringingPhase() {
  progressTotal = (int)pinSequence.size();
  if (progressCurrent >= progressTotal) progressCurrent = 0;   // finished job: start again
  if (progressCurrent > 0) autoNote = "Resuming from line " + String(progressCurrent) + ".";

  lastLineAt = 0;
  elapsedTickAt = millis();
  machineState = STATE_STRINGING;
  Serial.printf("Stringing started: %d pin pairs, from line %d. Free heap=%u\n",
                progressTotal, progressCurrent, (unsigned)ESP.getFreeHeap());

  for (int i = progressCurrent; i < progressTotal; i++) {
    if (jobInterrupted()) break;
    waitWhilePaused();
    if (jobInterrupted()) break;

    if (!runLine(i, true)) break;

    // Periodic re-home wipes accumulated missed steps. Progress is kept, so it
    // picks straight back up on the same chord.
    if (rehomeEvery > 0 && HAS_LIMIT_SWITCH && ++sinceRehome >= rehomeEvery) {
      sinceRehome = 0;
      MachineState was = machineState;
      machineState = STATE_HOMING;
      homeMachine();
      machineState = was;
    }

    if (autoAdvanceMs) vTaskDelay(pdMS_TO_TICKS(autoAdvanceMs));
    else vTaskDelay(pdMS_TO_TICKS(2));
  }

  currentFromNail = -1;
  currentToNail = -1;
  activeLine = -1;
  phaseText = "";
  machineState = stopRequested ? STATE_IDLE : STATE_DONE;
  Serial.println(stopRequested ? "Stringing stopped." : "Stringing complete.");
}

// ===========================================================================
//  Motor task (core 0)
// ===========================================================================

void motorTask(void *param) {
  for (;;) {
    JobReq req = (JobReq)jobReq;

    if (req != JOB_NONE) {
      jobReq = JOB_NONE;
      stopRequested = false;
      lastError = "";
      homeError = false;

      bool homed = true;
      if (req == JOB_HOME || req == JOB_HOME_RESUME ||
          (homeBeforeJob && HAS_LIMIT_SWITCH &&
       (req == JOB_DRILL || req == JOB_STRING || req == JOB_DRYRUN))) {
        machineState = STATE_HOMING;
        homed = homeMachine();
      }

      if (stopRequested) {
        machineState = STATE_IDLE;
      } else if (!homed) {
        machineState = STATE_ERROR;
      } else if (req == JOB_DRILL) {
        saveState();
        runDrillingPhase();
      } else if (req == JOB_STRING) {
        saveState();
        runStringingPhase();
      } else if (req == JOB_DRYRUN) {
        saveState();
        runDryRunPhase();
      } else if (req == JOB_HOME_RESUME) {
        // Boot-time recovery: the switch has given us a known zero, so drive
        // back to the line the job was interrupted on. Progress is kept, and
        // the machine is deliberately NOT set running again.
        if (!pinSequence.empty() && progressCurrent < (int)pinSequence.size()) {
          moveToStep(nailToStep((uint16_t)pinSequence[progressCurrent].first));
        }
        machineState = STATE_IDLE;
      } else {
        machineState = STATE_IDLE;
      }

      phaseText = "";
      // A finished or abandoned job releases the motor and closes the state
      // file's "running" flag, so the next boot knows the position is good.
      if (!holdWhenIdle) driverEnable(false);
      saveState();
      stopRequested = false;
      pauseRequested = false;
    } else {
      servicePending();
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ===========================================================================
//  Persistence (LittleFS)
// ===========================================================================

void saveConfig() {
  FsLock lock;
  File f = LittleFS.open(CONFIG_FILE, "w");
  if (!f) return;
  f.printf("%u\n%u\n%lu\n%d\n%ld\n%ld\n%ld\n%ld\n%ld\n",
           numNails, stepPulseUs, (unsigned long)autoAdvanceMs,
           (int)dirSign, (long)motorFullSteps, (long)microstep,
           (long)gearRatioX100000, stepsPerRevX100, nailOffsetSteps);
  f.printf("%u\n%u\n%u\n%u\n%u\n%u\n",
           feederRestAngle, feederFeedAngle, feederPulseMs,
           feederSettleMs, feederRecoverMs, feederAutoFeed ? 1u : 0u);
  f.printf("%u\n%u\n%u\n%u\n%u\n",
           drillRestAngle, drillDownAngle, drillSpinMs, drillDwellMs, drillSlewMs);
  f.printf("%u\n%u\n%u\n%d\n%u\n%u\n%u\n%u\n",
           wrapMode ? 1u : 0u, wrapSteps, wrapSweep, (int)wrapDir,
           wrapHoldInMs, wrapHoldSweepMs, servoSlewDeg, servoSlewMs);
  f.printf("%u\n%u\n%u\n%u\n%u\n%u\n%u\n%ld\n",
           homeBeforeJob ? 1u : 0u, autoHomeOnBoot ? 1u : 0u,
           holdWhenIdle ? 1u : 0u, rehomeEvery, oledFlip ? 1u : 0u,
           verifyEvery, learnAutoSpr ? 1u : 0u, learnBaseSprX100);
  // Appended, never inserted: the file is positional, and readConfigLine
  // leaves a missing trailing line at its default, so a config written by an
  // older build still loads.
  f.printf("%u\n%u\n%u\n%u\n%u\n",
           startPulseUs, accelSteps, homePulseUs, enableSettleMs, homeMaxTurns);
  f.printf("%u\n", dryRunLines);
  f.close();
}

// Reads one line and returns `def` (instead of 0) when the line is missing or
// empty, so a config file written by an older firmware version leaves the new
// fields at whatever sane default the caller passes.
long readConfigLine(File &f, long def) {
  if (!f.available()) return def;
  String line = f.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return def;
  return line.toInt();
}

void loadConfig() {
  FsLock lock;
  File f = LittleFS.open(CONFIG_FILE, "r");
  if (!f) return;
  numNails         = (uint16_t)readConfigLine(f, numNails);
  stepPulseUs      = (uint16_t)readConfigLine(f, stepPulseUs);
  autoAdvanceMs    = (uint32_t)readConfigLine(f, autoAdvanceMs);
  dirSign          = (readConfigLine(f, dirSign) < 0) ? -1 : 1;
  motorFullSteps   = readConfigLine(f, motorFullSteps);
  microstep        = readConfigLine(f, microstep);
  gearRatioX100000 = readConfigLine(f, gearRatioX100000);
  stepsPerRevX100  = readConfigLine(f, stepsPerRevX100);
  nailOffsetSteps  = readConfigLine(f, nailOffsetSteps);

  feederRestAngle  = (uint8_t)readConfigLine(f, feederRestAngle);
  feederFeedAngle  = (uint8_t)readConfigLine(f, feederFeedAngle);
  feederPulseMs    = (uint16_t)readConfigLine(f, feederPulseMs);
  feederSettleMs   = (uint16_t)readConfigLine(f, feederSettleMs);
  feederRecoverMs  = (uint16_t)readConfigLine(f, feederRecoverMs);
  feederAutoFeed   = readConfigLine(f, feederAutoFeed ? 1 : 0) != 0;

  drillRestAngle   = (uint8_t)readConfigLine(f, drillRestAngle);
  drillDownAngle   = (uint8_t)readConfigLine(f, drillDownAngle);
  drillSpinMs      = (uint16_t)readConfigLine(f, drillSpinMs);
  drillDwellMs     = (uint16_t)readConfigLine(f, drillDwellMs);
  drillSlewMs      = (uint8_t)readConfigLine(f, drillSlewMs);

  wrapMode         = readConfigLine(f, wrapMode ? 1 : 0) != 0;
  wrapSteps        = (uint16_t)readConfigLine(f, wrapSteps);
  wrapSweep        = (uint16_t)readConfigLine(f, wrapSweep);
  wrapDir          = (readConfigLine(f, wrapDir) < 0) ? -1 : 1;
  wrapHoldInMs     = (uint16_t)readConfigLine(f, wrapHoldInMs);
  wrapHoldSweepMs  = (uint16_t)readConfigLine(f, wrapHoldSweepMs);
  servoSlewDeg     = (uint8_t)readConfigLine(f, servoSlewDeg);
  servoSlewMs      = (uint16_t)readConfigLine(f, servoSlewMs);

  homeBeforeJob    = readConfigLine(f, homeBeforeJob ? 1 : 0) != 0;
  autoHomeOnBoot   = readConfigLine(f, autoHomeOnBoot ? 1 : 0) != 0;
  holdWhenIdle     = readConfigLine(f, holdWhenIdle ? 1 : 0) != 0;
  rehomeEvery      = (uint16_t)readConfigLine(f, rehomeEvery);
  oledFlip         = readConfigLine(f, oledFlip ? 1 : 0) != 0;
  verifyEvery      = (uint16_t)readConfigLine(f, verifyEvery);
  learnAutoSpr     = readConfigLine(f, learnAutoSpr ? 1 : 0) != 0;
  learnBaseSprX100 = readConfigLine(f, learnBaseSprX100);

  startPulseUs     = (uint16_t)readConfigLine(f, startPulseUs);
  accelSteps       = (uint16_t)readConfigLine(f, accelSteps);
  homePulseUs      = (uint16_t)readConfigLine(f, homePulseUs);
  enableSettleMs   = (uint8_t)readConfigLine(f, enableSettleMs);
  homeMaxTurns     = (uint8_t)readConfigLine(f, homeMaxTurns);
  dryRunLines      = (uint16_t)readConfigLine(f, dryRunLines);
  f.close();

  if (numNails == 0) numNails = DEF_NUM_NAILS;
  if (stepPulseUs < 100) stepPulseUs = DEF_STEP_PULSE_US;
  if (servoSlewDeg < 1) servoSlewDeg = 1;
  if (motorFullSteps < 1) motorFullSteps = DEF_MOTOR_FULL_STEPS;
  if (microstep < 1) microstep = DEF_MICROSTEP;
  if (gearRatioX100000 < 1) gearRatioX100000 = DEF_GEAR_RATIO_X100000;
  if (stepsPerRevX100 < 1000) stepsPerRevX100 = geometryStepsPerRevX100();

  // A start period faster than the running period would be a ramp that slows
  // down, so the ramp is simply switched off rather than applied backwards.
  if (startPulseUs < stepPulseUs) startPulseUs = stepPulseUs;
  if (homePulseUs < 100) homePulseUs = DEF_HOME_PULSE_US;
  if (homeMaxTurns == 0) homeMaxTurns = DEF_HOME_MAX_TURNS;
}

// State is written at every line boundary, and the "running" flag tells the
// next boot whether power was cut mid-job -- in which case the disc may be
// parked between nails and the position cannot be trusted until it is homed.
void saveState() {
  FsLock lock;
  File f = LittleFS.open(STATE_FILE, "w");
  if (!f) return;
  bool running = (machineState == STATE_HOMING || machineState == STATE_DRILLING ||
                  machineState == STATE_STRINGING || machineState == STATE_PAUSED);
  f.printf("%d\n%ld\n%d\n%lu\n%lu\n",
           progressCurrent, currentStep, running ? 1 : 0,
           (unsigned long)runElapsedMs, (unsigned long)avgNailMs);
  f.close();
}

void loadState() {
  FsLock lock;
  File f = LittleFS.open(STATE_FILE, "r");
  if (!f) return;
  progressCurrent = (int)readConfigLine(f, 0);
  currentStep = normalizeStep(readConfigLine(f, 0));
  bool wasRunning = readConfigLine(f, 0) != 0;
  runElapsedMs = (uint32_t)readConfigLine(f, 0);
  avgNailMs = (uint32_t)readConfigLine(f, 0);
  f.close();
  positionKnown = !wasRunning;
}

void saveSequence() {
  FsLock lock;
  File f = LittleFS.open(SEQ_FILE, "w");
  if (!f) return;
  for (size_t i = 0; i < pinSequence.size(); i++) {
    f.printf("%d,%d\n", pinSequence[i].first, pinSequence[i].second);
  }
  f.close();
}

void loadSequenceFromFile() {
  FsLock lock;
  pinSequence.clear();
  File f = LittleFS.open(SEQ_FILE, "r");
  if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    int sep = line.indexOf(',');
    if (sep <= 0) continue;
    pinSequence.push_back({line.substring(0, sep).toInt(),
                           line.substring(sep + 1).toInt()});
  }
  f.close();
}

// Parses a sequence body into pinSequence.
//
//   pairsExpected  true  -- "76,148" per line, the /pins and pins.txt format.
//                           The first two numbers on each line are one line of
//                           thread; anything after them on that line is
//                           ignored.
//                  false -- a flat nail list, the format the studio's Send
//                           button uploads to /upload. Numbers are read right
//                           across the body regardless of how they are broken
//                           into lines -- the studio sends all 3000 of them on
//                           one line -- and consecutive stops become the
//                           from,to pairs the stringing phase runs on.
//
// Lines beginning with a letter or '#' are skipped either way, so a file with
// a header row loads without hand-editing.
bool parseSequenceBody(const String &body, bool pairsExpected) {
  std::vector<std::pair<int, int>> pairs;
  std::vector<int> flat;
  const int len = (int)body.length();
  int lineStart = 0;

  while (lineStart < len) {
    int lineEnd = body.indexOf('\n', lineStart);
    if (lineEnd == -1) lineEnd = len;
    String line = body.substring(lineStart, lineEnd);
    lineStart = lineEnd + 1;
    line.trim();
    if (line.length() == 0) continue;
    if (isAlpha(line[0]) || line[0] == '#') continue;   // headers and comments

    int onThisLine = 0;
    int first = 0;
    int i = 0;
    const int n = (int)line.length();
    while (i < n) {
      // Skip to the next number, allowing a leading minus sign.
      while (i < n && !isDigit(line[i])) i++;
      if (i >= n) break;
      int start = i;
      if (start > 0 && line[start - 1] == '-') start--;
      while (i < n && isDigit(line[i])) i++;
      long v = line.substring(start, i).toInt();

      if (pairsExpected) {
        if (onThisLine == 0) first = (int)v;
        else if (onThisLine == 1) pairs.push_back({first, (int)v});
        // Further numbers on a pair line are ignored.
        onThisLine++;
      } else {
        flat.push_back((int)v);
      }
    }
  }

  if (!pairsExpected) {
    // Consecutive nail stops become the lines of thread between them.
    for (size_t k = 0; k + 1 < flat.size(); k++) {
      pairs.push_back({flat[k], flat[k + 1]});
    }
  }

  if (pairs.empty()) return false;

  // Anything outside the ring would be a move to nowhere; drop those lines
  // rather than sending the disc to a nail that does not exist.
  for (size_t k = 0; k < pairs.size(); k++) {
    if (pairs[k].first < 0 || pairs[k].first >= (int)numNails ||
        pairs[k].second < 0 || pairs[k].second >= (int)numNails) {
      return false;
    }
  }

  pinSequence = pairs;
  progressCurrent = 0;
  progressTotal = (int)pinSequence.size();
  sinceVerify = 0;
  sinceRehome = 0;
  runElapsedMs = 0;
  avgNailMs = 0;
  lastLineAt = 0;
  saveSequence();
  saveState();
  return true;
}

// ===========================================================================
//  OLED dashboard (optional)
// ===========================================================================

String currentIp() {
  bool sta = (WiFi.status() == WL_CONNECTED);
  return sta ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
}

void dashBoot(const char *l1, const char *l2, const char *l3) {
  if (!oledFound) return;
  oled::renderBoot(l1, l2, l3);
  oled::flushAll();
}

// Redraws at most twice a second and only sends the frame when something on it
// actually changed -- the SSD1306 does not need 1 KB over I2C just to show the
// same numbers again.
void dashTick() {
  if (!oledFound) return;
  if (!oled::flushing() && millis() >= bootScreenUntil &&
      (long)(millis() - dashNextAt) >= 0) {
    dashNextAt = millis() + 500;

    oled::Dash d;
    d.alert = false;
    switch (machineState) {
      case STATE_HOMING:    d.status = "HOMING"; break;
      case STATE_DRILLING:  d.status = "DRILL"; break;
      case STATE_STRINGING: d.status = wrapBusy ? "WRAP" : (feederBusy ? "FEED" : "RUN"); break;
      case STATE_PAUSED:    d.status = "PAUSED"; d.alert = true; break;
      case STATE_DONE:      d.status = "DONE"; break;
      case STATE_ERROR:     d.status = "ERROR"; d.alert = true; break;
      default:              d.status = pinSequence.empty() ? "READY" : "IDLE"; break;
    }
    if (verifyPending) { d.status = "CONFIRM?"; d.alert = true; }
    else if (calRunning) d.status = "CALIBRATE";
    else if (!positionKnown && machineState == STATE_IDLE) { d.status = "CHECK POS"; d.alert = true; }

    d.total = progressTotal;
    d.index = progressCurrent > 0 ? progressCurrent - 1 : 0;
    d.current = currentToNail;
    d.next = -1;
    if (machineState == STATE_DRILLING) {
      d.current = progressCurrent;
      d.next = (progressCurrent + 1 < progressTotal) ? progressCurrent + 1 : -1;
    } else if (!pinSequence.empty()) {
      int i = progressCurrent;
      if (d.current < 0 && i < (int)pinSequence.size()) d.current = pinSequence[i].first;
      if (i < (int)pinSequence.size()) d.next = pinSequence[i].second;
    }

    // A true countdown while running: lines left at the measured rate, less
    // however long the current one has already been going.
    d.remainSec = -1;
    if (avgNailMs > 0 && d.total > 0) {
      long left = (long)(d.total - progressCurrent);
      long ms = left * (long)avgNailMs;
      if (lastLineAt) ms -= (long)(millis() - lastLineAt);
      d.remainSec = ms > 0 ? ms / 1000 : 0;
    }

    String ip = currentIp();
    d.ip = ip.c_str();
    oled::renderDash(d);

    uint32_t h = 2166136261u;                 // FNV-1a over the frame
    for (uint16_t i = 0; i < sizeof(oled::buf); i++) h = (h ^ oled::buf[i]) * 16777619u;
    if (h != dashLastHash) { dashLastHash = h; oled::markDirty(); }
  }
  oled::service();
}

// ===========================================================================
//  HTTP
// ===========================================================================

// The machine's own page is always same-origin, but a generator page hosted
// elsewhere on the LAN is a different origin and needs these to fetch() the
// API directly.
void sendCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void handleCorsPreflight() {
  sendCorsHeaders();
  server.send(204);
}

static String jsonEscape(const String &s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else if (c == '\r') { /* dropped */ }
    else if ((uint8_t)c < 0x20) out += ' ';
    else out += c;
  }
  return out;
}

// The page is about 160 kB of PROGMEM -- far more than one TCP write, and more
// than WebServer would happily buffer. Send it in chunks, yielding between
// them so WiFi housekeeping keeps up.
void handleRoot() {
  sendCorsHeaders();
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  size_t len = strlen_P(INDEX_HTML);
  server.setContentLength(len);
  server.send(200, "text/html", "");

  const size_t CHUNK = 1024;
  char buf[CHUNK];
  size_t sent = 0;
  while (sent < len) {
    size_t n = (len - sent > CHUNK) ? CHUNK : (len - sent);
    memcpy_P(buf, INDEX_HTML + sent, n);
    server.sendContent(buf, n);
    sent += n;
    delay(0);
  }
}

void handleStatus() {
  const char *stateStr = "idle";
  switch (machineState) {
    case STATE_IDLE:      stateStr = "idle"; break;
    case STATE_HOMING:    stateStr = "homing"; break;
    case STATE_DRILLING:  stateStr = "drilling"; break;
    case STATE_STRINGING: stateStr = "stringing"; break;
    case STATE_PAUSED:    stateStr = "paused"; break;
    case STATE_DONE:      stateStr = "done"; break;
    case STATE_ERROR:     stateStr = "error"; break;
  }
  bool onRouter = (WiFi.status() == WL_CONNECTED);
  String ip = onRouter ? WiFi.localIP().toString() : WiFi.softAPIP().toString();

  String json;
  json.reserve(2400);
  json  = "{";
  json += "\"state\":\"" + String(stateStr) + "\",";
  json += "\"phase\":\"" + String(phaseText) + "\",";
  json += "\"progress\":" + String(progressCurrent) + ",";
  json += "\"total\":" + String(progressTotal) + ",";
  json += "\"pinsLoaded\":" + String((unsigned)pinSequence.size()) + ",";
  json += "\"currentFrom\":" + String(currentFromNail) + ",";
  // Three pin numbers the page shows directly, worked out here so it does not
  // have to hold a copy of the sequence or guess which counter to read.
  //
  //   prevPin  the nail the thread is already hooked on
  //   curPin   the nail arriving at the feeder
  //   nextPin  the nail after that
  //
  // The line to describe is the one running, or -- between lines -- the one
  // about to run. nextFrom/nextTo stay as the line the Next button would do,
  // which is always indexed by progressCurrent.
  {
    int n = (int)pinSequence.size();
    int li = (activeLine >= 0) ? activeLine : progressCurrent;
    if (li >= n) li = n - 1;                 // finished: keep showing the last
    int pp = -1, cp = -1, np = -1;
    if (li >= 0 && li < n) {
      pp = pinSequence[li].first;
      cp = pinSequence[li].second;
      if (li + 1 < n) np = pinSequence[li + 1].second;
    }
    json += "\"prevPin\":" + String(pp) + ",";
    json += "\"curPin\":" + String(cp) + ",";
    json += "\"nextPin\":" + String(np) + ",";

    int nx = progressCurrent;
    int nf = -1, nt = -1;
    if (nx >= 0 && nx < n) { nf = pinSequence[nx].first; nt = pinSequence[nx].second; }
    json += "\"nextFrom\":" + String(nf) + ",";
    json += "\"nextTo\":" + String(nt) + ",";
  }
  json += "\"currentTo\":" + String(currentToNail) + ",";
  json += "\"moving\":" + String(moving ? "true" : "false") + ",";
  json += "\"feederBusy\":" + String(feederBusy ? "true" : "false") + ",";
  json += "\"wrapBusy\":" + String(wrapBusy ? "true" : "false") + ",";

  json += "\"numNails\":" + String(numNails) + ",";
  json += "\"calTestFrom\":" + String(calTestFrom) + ",";
  json += "\"calTestTarget\":" + String(calTestTarget) + ",";
  json += "\"calTestNote\":\"" + jsonEscape(calTestNote) + "\",";
  json += "\"photoBytes\":" + String((unsigned)photoSize()) + ",";
  json += "\"dryRunPending\":" + String(dryRunPending ? "true" : "false") + ",";
  json += "\"dryRunLines\":" + String(dryRunLines) + ",";
  json += "\"stepPulseUs\":" + String(stepPulseUs) + ",";
  json += "\"startPulseUs\":" + String(startPulseUs) + ",";
  json += "\"accelSteps\":" + String(accelSteps) + ",";
  json += "\"homePulseUs\":" + String(homePulseUs) + ",";
  json += "\"homeMaxTurns\":" + String(homeMaxTurns) + ",";
  json += "\"enableSettleMs\":" + String(enableSettleMs) + ",";
  json += "\"autoMs\":" + String(autoAdvanceMs) + ",";
  json += "\"dirSign\":" + String((int)dirSign) + ",";
  json += "\"motorFullSteps\":" + String(motorFullSteps) + ",";
  json += "\"microstep\":" + String(microstep) + ",";
  json += "\"gearRatioX100000\":" + String(gearRatioX100000) + ",";
  json += "\"stepsPerRevX100\":" + String(stepsPerRevX100) + ",";
  json += "\"stepsPerRevGeomX100\":" + String(geometryStepsPerRevX100()) + ",";
  json += "\"nailOffsetSteps\":" + String(nailOffsetSteps) + ",";
  json += "\"previewNail\":" + String(previewNail()) + ",";
  json += "\"discOffset\":" + String(discOffsetFromNail()) + ",";

  json += "\"hasLimitSwitch\":" + String(HAS_LIMIT_SWITCH ? "true" : "false") + ",";
  json += "\"switchTriggered\":" + String(limitTriggered() ? "true" : "false") + ",";
  json += "\"homeError\":" + String(homeError ? "true" : "false") + ",";
  json += "\"positionKnown\":" + String(positionKnown ? "true" : "false") + ",";
  json += "\"homeBeforeJob\":" + String(homeBeforeJob ? "true" : "false") + ",";
  json += "\"zeroFromSwitch\":" + String(zeroFromSwitch ? "true" : "false") + ",";
  json += "\"autoHomeOnBoot\":" + String(autoHomeOnBoot ? "true" : "false") + ",";
  json += "\"holdWhenIdle\":" + String(holdWhenIdle ? "true" : "false") + ",";
  json += "\"rehomeEvery\":" + String(rehomeEvery) + ",";

  json += "\"feederRestAngle\":" + String(feederRestAngle) + ",";
  json += "\"feederFeedAngle\":" + String(feederFeedAngle) + ",";
  json += "\"feederPulseMs\":" + String(feederPulseMs) + ",";
  json += "\"feederSettleMs\":" + String(feederSettleMs) + ",";
  json += "\"feederRecoverMs\":" + String(feederRecoverMs) + ",";
  json += "\"feederAutoFeed\":" + String(feederAutoFeed ? "true" : "false") + ",";
  json += "\"servoAngle\":" + String(servoCurrent) + ",";
  json += "\"servoSlewDeg\":" + String(servoSlewDeg) + ",";
  json += "\"servoSlewMs\":" + String(servoSlewMs) + ",";

  json += "\"drillRestAngle\":" + String(drillRestAngle) + ",";
  json += "\"drillDownAngle\":" + String(drillDownAngle) + ",";
  json += "\"drillSpinMs\":" + String(drillSpinMs) + ",";
  json += "\"drillDwellMs\":" + String(drillDwellMs) + ",";
  json += "\"drillSlewMs\":" + String(drillSlewMs) + ",";
  json += "\"drillServoAngle\":" + String(drillServoCurrent) + ",";

  json += "\"wrapMode\":" + String(wrapMode ? "true" : "false") + ",";
  json += "\"wrapSteps\":" + String(wrapSteps) + ",";
  json += "\"wrapSweep\":" + String(wrapSweep) + ",";
  json += "\"wrapDir\":" + String((int)wrapDir) + ",";
  json += "\"wrapApproachDir\":" + String((int)wrapApproachDir) + ",";
  json += "\"wrapAutoSteps\":" + String(wrapLeadMag()) + ",";
  json += "\"wrapAutoSweep\":" + String(wrapSweepMag()) + ",";
  json += "\"autoLead\":" + String(autoLeadMag()) + ",";
  json += "\"autoSweep\":" + String(autoSweepMag()) + ",";
  json += "\"wrapHoldInMs\":" + String(wrapHoldInMs) + ",";
  json += "\"wrapHoldSweepMs\":" + String(wrapHoldSweepMs) + ",";

  json += "\"verifyEvery\":" + String(verifyEvery) + ",";
  json += "\"verifyPending\":" + String(verifyPending ? "true" : "false") + ",";
  json += "\"verifyNail\":" + String(verifyNail) + ",";
  json += "\"learnAutoSpr\":" + String(learnAutoSpr ? "true" : "false") + ",";
  json += "\"learnPoints\":" + String(learnN) + ",";
  json += "\"learnChecks\":" + String(learnChecks) + ",";
  json += "\"learnFixes\":" + String(learnFixes) + ",";
  json += "\"learnOffset\":" + String(learnA, 2) + ",";
  json += "\"learnDriftPpm\":" + String(learnB * 1e6, 0) + ",";
  json += "\"learnRms\":" + String(learnRms, 2) + ",";
  json += "\"learnSpanTurns\":" + String(learnSpanTurns, 2) + ",";
  json += "\"learnSuggestX100\":" + String(learnSuggestX100) + ",";
  json += "\"learnConfident\":" + String(learnConfident ? "true" : "false") + ",";
  json += "\"learnBaseSprX100\":" + String(learnBaseSprX100) + ",";
  json += "\"learnNote\":\"" + jsonEscape(learnNote) + "\",";

  json += "\"calRunning\":" + String(calRunning ? "true" : "false") + ",";
  json += "\"calAwaiting\":" + String((calRevs > 0 && !calRunning) ? "true" : "false") + ",";
  json += "\"avgNailMs\":" + String(avgNailMs) + ",";
  json += "\"elapsedMs\":" + String(runElapsedMs) + ",";

  json += "\"oledAddr\":" + String(oledFound ? (int)oled::addr : 0) + ",";
  json += "\"oledFlip\":" + String(oledFlip ? "true" : "false") + ",";
  json += "\"mode\":\"" + String(onRouter ? "router" : "hotspot") + "\",";
  json += "\"ip\":\"" + ip + "\",";
  json += "\"apIp\":\"" + WiFi.softAPIP().toString() + "\",";
  json += "\"staSsid\":\"" + jsonEscape(staSsid) + "\",";
  json += "\"staJoined\":" + String(staJoined ? "true" : "false") + ",";
  json += "\"heap\":" + String((unsigned)ESP.getFreeHeap()) + ",";
  json += "\"autoNote\":\"" + jsonEscape(autoNote) + "\",";
  json += "\"autoBlock\":\"" + jsonEscape(autoBlock()) + "\",";
  json += "\"error\":\"" + jsonEscape(lastError) + "\"";
  json += "}";

  sendCorsHeaders();
  server.send(200, "application/json", json);
}

static bool machineBusy() {
  return machineState == STATE_HOMING || machineState == STATE_DRILLING ||
         machineState == STATE_STRINGING || machineState == STATE_PAUSED ||
         jobReq != JOB_NONE;
}

// Body: one "from,to" pair per line, e.g. "76,148\n148,194\n..."
void handlePinsPost() {
  sendCorsHeaders();
  if (machineBusy()) {
    server.send(409, "text/plain", "Machine is busy, send /stop first.");
    return;
  }
  if (!parseSequenceBody(server.arg("plain"), true)) {
    server.send(400, "text/plain", "No valid pin pairs in the body, or a nail number outside 0.." + String(numNails - 1) + ". Check the nail count matches.");
    return;
  }
  server.send(200, "text/plain", "Received " + String((unsigned)pinSequence.size()) + " pin pairs.");
}

// Export whatever is loaded, in the same format /pins accepts.
void handlePinsGet() {
  sendCorsHeaders();
  String out;
  out.reserve(pinSequence.size() * 9 + 16);
  for (size_t i = 0; i < pinSequence.size(); i++) {
    out += String(pinSequence[i].first) + "," + String(pinSequence[i].second) + "\n";
  }
  server.sendHeader("Content-Disposition", "attachment; filename=pins.txt");
  server.send(200, "text/plain", out);
}

// Body: the nail sequence the studio generates, "0,148,76,..." -- turned into
// consecutive from,to pairs here.
void handleUpload() {
  sendCorsHeaders();
  if (machineBusy()) {
    server.send(409, "text/plain", "Machine is busy, stop the current job first.");
    return;
  }
  if (!parseSequenceBody(server.arg("plain"), false)) {
    server.send(400, "text/plain", "No usable nail numbers in the body, or a nail number outside 0.." + String(numNails - 1) + ". Check the nail count matches.");
    return;
  }
  server.send(200, "text/plain", "ok");
}

void handleConfig() {
  // "reset=1" restores every adjustable setting to the compiled-in defaults.
  // Done server-side so the page never carries its own copy of the defaults
  // and drifts out of step with the firmware.
  if (server.hasArg("reset")) {
    numNails = DEF_NUM_NAILS;
    stepPulseUs = DEF_STEP_PULSE_US;
    startPulseUs = DEF_START_PULSE_US;
    accelSteps = DEF_ACCEL_STEPS;
    homePulseUs = DEF_HOME_PULSE_US;
    enableSettleMs = DEF_ENABLE_SETTLE_MS;
    homeMaxTurns = DEF_HOME_MAX_TURNS;
    dryRunLines = 5;
    autoAdvanceMs = DEF_AUTO_MS;
    dirSign = 1;
    motorFullSteps = DEF_MOTOR_FULL_STEPS;
    microstep = DEF_MICROSTEP;
    gearRatioX100000 = DEF_GEAR_RATIO_X100000;
    stepsPerRevX100 = geometryStepsPerRevX100();
    nailOffsetSteps = 0;
    feederRestAngle = DEF_FEEDER_REST;
    feederFeedAngle = DEF_FEEDER_FEED;
    feederPulseMs = DEF_FEEDER_PULSE_MS;
    feederSettleMs = DEF_FEEDER_SETTLE_MS;
    feederRecoverMs = DEF_FEEDER_RECOVER_MS;
    feederAutoFeed = true;
    drillRestAngle = DEF_DRILL_REST;
    drillDownAngle = DEF_DRILL_DOWN;
    drillSpinMs = DEF_DRILL_SPIN_MS;
    drillDwellMs = DEF_DRILL_DWELL_MS;
    drillSlewMs = DEF_DRILL_SLEW_MS;
    servoSlewDeg = DEF_SERVO_SLEW_DEG;
    servoSlewMs = DEF_SERVO_SLEW_MS;
    wrapMode = true;
    wrapDir = 1;
    wrapHoldInMs = DEF_WRAP_HOLD_IN_MS;
    wrapHoldSweepMs = DEF_WRAP_HOLD_SWEEP_MS;
    homeBeforeJob = true;
    autoHomeOnBoot = false;
    holdWhenIdle = false;
    rehomeEvery = 0;
    verifyEvery = 0;
    learnAutoSpr = true;
    learnReset();
    applyAutoWrapGeometry();
    saveConfig();
    sendCorsHeaders();
    server.send(200, "text/plain", "ok");
    return;
  }

  if (server.hasArg("numNails")) {
    uint16_t was = numNails;
    long v = server.arg("numNails").toInt();
    if (v >= 2 && v <= 2000) numNails = (uint16_t)v;
    // Step counts are meaningless against a different nail pitch, so rebuild
    // them. Changing the nail count invalidates the sequence anyway.
    if (numNails != was) applyAutoWrapGeometry();
  }
  if (server.hasArg("recalcWrap")) applyAutoWrapGeometry();

  bool geomChanged = false;
  if (server.hasArg("motorFullSteps")) {
    long v = server.arg("motorFullSteps").toInt();
    if (v >= 1 && v <= 10000) { motorFullSteps = v; geomChanged = true; }
  }
  if (server.hasArg("microstep")) {
    long v = server.arg("microstep").toInt();
    if (v >= 1 && v <= 256) { microstep = v; geomChanged = true; }
  }
  if (server.hasArg("gearRatioX100000")) {
    long v = server.arg("gearRatioX100000").toInt();
    if (v >= 1) { gearRatioX100000 = v; geomChanged = true; }
  }
  // Editing the mechanical figures supersedes anything calibration had
  // measured: the old number described a different machine.
  if (geomChanged) {
    stepsPerRevX100 = geometryStepsPerRevX100();
    learnReset();
    learnBaseSprX100 = 0;
    applyAutoWrapGeometry();
  }

  if (server.hasArg("stepPulseUs")) {
    long v = server.arg("stepPulseUs").toInt();
    if (v >= 100 && v <= 20000) stepPulseUs = (uint16_t)v;
  }
  if (server.hasArg("startPulseUs")) {
    long v = server.arg("startPulseUs").toInt();
    if (v >= 100 && v <= 60000) startPulseUs = (uint16_t)v;
  }
  if (server.hasArg("accelSteps")) {
    long v = server.arg("accelSteps").toInt();
    if (v >= 0 && v <= 20000) accelSteps = (uint16_t)v;
  }
  if (server.hasArg("homePulseUs")) {
    long v = server.arg("homePulseUs").toInt();
    if (v >= 100 && v <= 60000) homePulseUs = (uint16_t)v;
  }
  if (server.hasArg("enableSettleMs")) {
    long v = server.arg("enableSettleMs").toInt();
    if (v >= 0 && v <= 200) enableSettleMs = (uint8_t)v;
  }
  if (server.hasArg("dryRunLines")) {
    long v = server.arg("dryRunLines").toInt();
    if (v >= 1 && v <= 500) dryRunLines = (uint16_t)v;
  }
  if (server.hasArg("homeMaxTurns")) {
    long v = server.arg("homeMaxTurns").toInt();
    if (v >= 1 && v <= 50) homeMaxTurns = (uint8_t)v;
  }
  // A start period faster than the running period is a ramp that slows down.
  if (startPulseUs < stepPulseUs) startPulseUs = stepPulseUs;
  if (server.hasArg("autoMs")) autoAdvanceMs = (uint32_t)server.arg("autoMs").toInt();
  if (server.hasArg("dirSign")) dirSign = (server.arg("dirSign").toInt() < 0) ? -1 : 1;

  if (server.hasArg("feederRestAngle")) feederRestAngle = clampAngle(server.arg("feederRestAngle").toInt());
  if (server.hasArg("feederFeedAngle")) feederFeedAngle = clampAngle(server.arg("feederFeedAngle").toInt());
  if (server.hasArg("feederPulseMs")) feederPulseMs = (uint16_t)server.arg("feederPulseMs").toInt();
  if (server.hasArg("feederSettleMs")) feederSettleMs = (uint16_t)server.arg("feederSettleMs").toInt();
  if (server.hasArg("feederRecoverMs")) feederRecoverMs = (uint16_t)server.arg("feederRecoverMs").toInt();
  if (server.hasArg("feederAutoFeed")) feederAutoFeed = server.arg("feederAutoFeed").toInt() != 0;

  if (server.hasArg("drillRestAngle")) drillRestAngle = clampAngle(server.arg("drillRestAngle").toInt());
  if (server.hasArg("drillDownAngle")) drillDownAngle = clampAngle(server.arg("drillDownAngle").toInt());
  if (server.hasArg("drillSpinMs")) drillSpinMs = (uint16_t)server.arg("drillSpinMs").toInt();
  if (server.hasArg("drillDwellMs")) drillDwellMs = (uint16_t)server.arg("drillDwellMs").toInt();
  if (server.hasArg("drillSlewMs")) drillSlewMs = (uint8_t)server.arg("drillSlewMs").toInt();

  if (server.hasArg("wrapMode")) wrapMode = server.arg("wrapMode").toInt() != 0;
  if (server.hasArg("wrapSteps")) wrapSteps = (uint16_t)server.arg("wrapSteps").toInt();
  if (server.hasArg("wrapSweep")) wrapSweep = (uint16_t)server.arg("wrapSweep").toInt();
  if (server.hasArg("wrapDir")) wrapDir = (server.arg("wrapDir").toInt() < 0) ? -1 : 1;
  if (server.hasArg("wrapHoldInMs")) wrapHoldInMs = (uint16_t)server.arg("wrapHoldInMs").toInt();
  if (server.hasArg("wrapHoldSweepMs")) wrapHoldSweepMs = (uint16_t)server.arg("wrapHoldSweepMs").toInt();
  if (server.hasArg("servoSlewDeg")) {
    servoSlewDeg = (uint8_t)server.arg("servoSlewDeg").toInt();
    if (servoSlewDeg < 1) servoSlewDeg = 1;
  }
  if (server.hasArg("servoSlewMs")) servoSlewMs = (uint16_t)server.arg("servoSlewMs").toInt();

  if (server.hasArg("homeBeforeJob")) homeBeforeJob = server.arg("homeBeforeJob").toInt() != 0;
  if (server.hasArg("autoHomeOnBoot")) autoHomeOnBoot = server.arg("autoHomeOnBoot").toInt() != 0;
  if (server.hasArg("holdWhenIdle")) {
    holdWhenIdle = server.arg("holdWhenIdle").toInt() != 0;
    if (!machineBusy()) driverEnable(holdWhenIdle);
  }
  if (server.hasArg("rehomeEvery")) rehomeEvery = (uint16_t)server.arg("rehomeEvery").toInt();

  if (server.hasArg("verifyEvery")) {
    verifyEvery = (uint16_t)server.arg("verifyEvery").toInt();
    sinceVerify = 0;
    // Turning it off mid-question must still let the run continue; just
    // dropping the question would leave the motor task waiting forever.
    if (verifyEvery == 0 && verifyPending) verifyPending = false;
  }
  if (server.hasArg("learnAutoSpr")) learnAutoSpr = server.arg("learnAutoSpr").toInt() != 0;

  if (server.hasArg("oledFlip")) {
    oledFlip = server.arg("oledFlip").toInt() != 0;
    if (oledFound) oled::setFlip(oledFlip);
    dashLastHash = 0;          // force a redraw the right way up
  }

  saveConfig();
  sendCorsHeaders();
  server.send(200, "text/plain", "ok");
}

// Queue a one-shot command for the motor task. Refused while a job is running
// so a jog can never fight the stringing loop for the stepper.
static bool queueCmd(PendCmd c, long v, String &why) {
  if (machineBusy() && !verifyPending) {
    why = "Machine is busy -- stop or pause the job first.";
    return false;
  }
  if (pendCmd != PC_NONE) {
    why = "Still working on the last request.";
    return false;
  }
  pendVal = v;
  pendCmd = c;
  return true;
}

static bool startJob(JobReq r, String &why) {
  // Homing moves step zero to the switch. If the nail numbering was anchored
  // by hand somewhere else, that shift silently rotates the whole ring, and
  // the job strings a picture onto the wrong nails from the very first line.
  // Cheaper to refuse here than to discover it 200 lines in.
  if (homeBeforeJob && HAS_LIMIT_SWITCH && switchProven && !zeroFromSwitch &&
      (r == JOB_DRILL || r == JOB_STRING || r == JOB_DRYRUN)) {
    why = "Nail 0 was set by hand, but this job would home against the limit "
          "switch first -- which puts step zero somewhere else and rotates "
          "every nail. Press Find home, then set nail 0 again (or re-run the "
          "pin check) so the numbering is measured from the switch. If the "
          "machine has no usable switch, turn off \"Home before every job\".";
    autoNote = why;          // so it survives the next status poll
    return false;
  }
  if (machineBusy()) { why = "Machine is busy."; autoNote = why; return false; }
  stopRequested = false;
  pauseRequested = false;
  verifyPending = false;
  lastError = "";
  autoNote = "";
  sinceVerify = 0;
  sinceRehome = 0;
  jobReq = r;
  return true;
}

void handleAction() {
  String cmd = server.arg("cmd");
  long value = server.hasArg("value") ? server.arg("value").toInt() : 0;
  String why;
  String ok = "ok";
  int code = 200;

  if (cmd == "drill") {
    progressTotal = numNails;
    if (!startJob(JOB_DRILL, why)) { code = 409; ok = why; }
    else ok = "Homing, then drilling " + String(numNails) + " holes.";

  } else if (cmd == "string") {
    if (pinSequence.empty()) { code = 400; ok = "No sequence loaded. Send one from the Design panel first."; }
    else if (!startJob(JOB_STRING, why)) { code = 409; ok = why; }
    else ok = "Stringing " + String((unsigned)pinSequence.size()) + " lines.";

  } else if (cmd == "findhome") {
    if (!HAS_LIMIT_SWITCH) { code = 409; ok = "No limit switch fitted."; }
    else if (!startJob(JOB_HOME, why)) { code = 409; ok = why; }
    else ok = "Homing.";

  } else if (cmd == "home") {
    // "Set Home here": declare the disc's CURRENT physical position to be
    // nail 0, without moving it and without touching progress.
    if (machineBusy()) { code = 409; ok = "Machine is busy."; }
    else {
      currentStep = 0;
      nailOffsetSteps = 0;
      positionKnown = true;
      zeroFromSwitch = false;      // set by hand, not measured from the switch
      homeError = false;
      learnReset();
      saveConfig();
      saveState();
      ok = "This position is now nail 0.";
    }

  } else if (cmd == "stop") {
    stopRequested = true;
    pauseRequested = false;      // never leave the machine waiting to resume
    verifyPending = false;
    calRunning = false;
    calRevs = 0;
    autoNote = "";
    ok = "Stop requested.";

  } else if (cmd == "pause") {
    if (machineState == STATE_HOMING || machineState == STATE_DRILLING ||
        machineState == STATE_STRINGING) {
      pauseRequested = true;
      ok = "Pause requested -- the machine will halt at the next step boundary.";
    } else if (machineState == STATE_PAUSED) {
      ok = "Already paused.";
    } else { code = 409; ok = "Nothing running to pause."; }

  } else if (cmd == "resume") {
    if (machineState == STATE_PAUSED) { pauseRequested = false; ok = "Resuming."; }
    else { code = 409; ok = "Machine is not paused."; }

  } else if (cmd == "gotostep") {
    // Moves progress as well as the disc, so stringing carries on from there.
    if (machineBusy()) { code = 409; ok = "Machine is busy."; }
    else if (pinSequence.empty()) { code = 400; ok = "No sequence loaded."; }
    else {
      int v = (int)value;
      if (v < 0) v = 0;
      if (v >= (int)pinSequence.size()) v = (int)pinSequence.size() - 1;
      progressCurrent = v;
      progressTotal = (int)pinSequence.size();
      sinceVerify = 0;
      lastLineAt = 0;
      saveState();
      if (!queueCmd(PC_GOTO, pinSequence[v].first, why)) { code = 409; ok = why; }
      else ok = "Going to line " + String(v) + ".";
    }

  } else if (cmd == "dryrun") {
    if (pinSequence.empty()) { code = 400; ok = "No pattern loaded."; }
    else if (!startJob(JOB_DRYRUN, why)) { code = 409; ok = why; }
    else ok = "Dry run: the first " + String(dryRunLines) +
              " lines, with no thread on. Watch the pins.";

  } else if (cmd == "dryrunok") {
    // The verdict was yes: start the real thing from the first line.
    dryRunPending = false;
    progressCurrent = 0;
    sinceVerify = 0;
    sinceRehome = 0;
    saveState();
    if (!startJob(JOB_STRING, why)) { code = 409; ok = why; }
    else ok = "Starting the real run from line 0.";

  } else if (cmd == "dryruncancel") {
    dryRunPending = false;
    progressCurrent = 0;
    saveState();
    ok = "Dry run discarded. Nothing was strung.";

  } else if (cmd == "next") {
    // Hand-step one line forward: the real thing, wrap and feed included.
    if (machineBusy()) { code = 409; ok = "Machine is busy."; }
    else if (pinSequence.empty()) { code = 400; ok = "No sequence loaded."; }
    else if (progressCurrent >= (int)pinSequence.size()) {
      code = 409; ok = "Already at the last line.";
    } else if (!queueCmd(PC_NEXTLINE, 0, why)) { code = 409; ok = why; }
    else {
      int i = progressCurrent;
      ok = "Line " + String(i) + ": nail " + String(pinSequence[i].first) +
           " to " + String(pinSequence[i].second) + ".";
    }

  } else if (cmd == "prev") {
    // Walks the disc back to where the previous line began. It cannot unwind
    // thread, so this is for redoing a line you have unwound by hand.
    if (machineBusy()) { code = 409; ok = "Machine is busy."; }
    else if (pinSequence.empty()) { code = 400; ok = "No sequence loaded."; }
    else if (progressCurrent <= 0) { code = 409; ok = "Already at the first line."; }
    else if (!queueCmd(PC_PREVLINE, 0, why)) { code = 409; ok = why; }
    else ok = "Back to line " + String(progressCurrent - 1) + ".";

  } else if (cmd == "goto") {
    if (!queueCmd(PC_GOTO, value, why)) { code = 409; ok = why; }

  } else if (cmd == "jog") {
    if (!queueCmd(PC_JOG, value, why)) { code = 409; ok = why; }

  } else if (cmd == "setnail") {
    // "The nail at the feeder is actually N." Solves for the offset that makes
    // that true, so every future move lands right. Progress is untouched.
    if (machineBusy()) { code = 409; ok = "Machine is busy."; }
    else {
      nailOffsetSteps = normalizeStep(currentStep - nailBaseStep((uint16_t)value));
      learnReset();            // a hand re-sync is a new reference
      saveConfig();
      saveState();
      ok = "Re-synced.";
    }

  } else if (cmd == "calmove") {
    if (!queueCmd(PC_CALMOVE, value, why)) { code = 409; ok = why; }

  } else if (cmd == "caltest") {
    // Send the disc to a nail and remember exactly how far we asked it to go.
    if (machineBusy()) { code = 409; ok = "Machine is busy."; }
    else if (numNails == 0) { code = 400; ok = "Set the nail count first."; }
    else {
      long v = value % (long)numNails;
      if (v < 0) v += numNails;
      long spr = stepsPerRev();
      long d = nailToStep((uint16_t)v) - currentStep;
      while (d >  spr / 2) d -= spr;
      while (d <= -spr / 2) d += spr;

      if (!queueCmd(PC_GOTO, v, why)) { code = 409; ok = why; }
      else {
        // The nail the firmware believes is at the feeder right now.
        long base = normalizeStep(currentStep - nailOffsetSteps);
        long f = (spr > 0) ? ((base * (long)numNails + spr / 2) / spr) : 0;
        if (dirSign < 0) f = (long)numNails - f;
        calTestFrom = (int)(((f % numNails) + numNails) % numNails);
        calTestTarget = (int)v;
        calTestSteps = d;
        calTestNote = "";
        ok = "Going to pin " + String(v) + ". Watch the feeder.";
      }
    }

  } else if (cmd == "calresult") {
    // You say which pin actually arrived, and how many complete turns the disc
    // made on the way. Complete turns matter: without them a disc that is 50%
    // out looks nearly right, because only the remainder is visible.
    if (calTestTarget < 0) { code = 409; ok = "Run the check first."; }
    else if (machineBusy()) { code = 409; ok = "Machine is busy."; }
    else {
      long turns = server.hasArg("turns") ? server.arg("turns").toInt() : 0;
      if (turns < 0) turns = 0;
      long o = value % (long)numNails;
      if (o < 0) o += numNails;

      // A: how far the disc really turned, in nails, signed the same way as
      // the steps we commanded. Rotating the disc one way carries nails past
      // the feeder the other way, hence (from - arrived).
      long partial = ((long)calTestFrom - o) % (long)numNails;
      if (partial < 0) partial += numNails;
      long A = (calTestSteps >= 0)
               ? partial + turns * (long)numNails
               : partial - (turns + 1) * (long)numNails;

      // The same reading with the numbering mirrored. If the disc is turning
      // the right amount but the wrong way round the pins, this candidate
      // comes out close to the current setting while the straight one comes
      // out nonsense -- which is the only reliable way to tell, since the
      // firmware cannot know which way the motor turns for a given step.
      long partialM = (o - (long)calTestFrom) % (long)numNails;
      if (partialM < 0) partialM += numNails;
      long AM = (calTestSteps >= 0)
                ? partialM + turns * (long)numNails
                : partialM - (turns + 1) * (long)numNails;

      if (A == 0) { code = 400; ok = "That says the disc never moved."; }
      else if (AM != 0 && stepsPerRevX100 > 0 &&
               llabs(((int64_t)calTestSteps * numNails * 100LL) / AM - stepsPerRevX100) * 4 <
               llabs(((int64_t)calTestSteps * numNails * 100LL) / A - stepsPerRevX100)) {
        code = 409;
        ok = "The disc moved about the right distance but the wrong way round "
             "the pin numbering. Flip \"Pin numbering direction\" in Setup, "
             "then run this check again. (Nothing was changed.)";
      }
      else {
        // steps per turn = steps commanded / turns actually made
        int64_t fresh = ((int64_t)calTestSteps * (int64_t)numNails * 100LL) / (int64_t)A;
        if (fresh < (int64_t)stepsPerRevX100 / 5 ||
            fresh > (int64_t)stepsPerRevX100 * 5) {
          code = 400;
          ok = "That would change steps per turn by more than five times, so "
               "it is almost certainly a mis-read or a missing turn count. "
               "Check the pin number and how many full turns the disc made.";
        } else {
          long was = stepsPerRevX100;
          stepsPerRevX100 = (long)fresh;
          nailOffsetSteps = normalizeStep(currentStep - nailBaseStep((uint16_t)o));
          learnReset();
          applyAutoWrapGeometry();
          saveConfig();
          saveState();
          calTestNote = "Steps per disc turn: " + String(was / 100.0, 2) +
                        " -> " + String(stepsPerRevX100 / 100.0, 2) +
                        ". Run the check again to confirm.";
          ok = calTestNote;
        }
      }
    }

  } else if (cmd == "calreport") {
    // You say how many nails past (or short of) the start the disc actually
    // finished -- counting whole extra turns, not just the final position --
    // and it corrects steps-per-revolution.
    long errNails = value;
    if (calRevs > 0 && numNails > 0 && errNails != 0) {
      // 64-bit: on a finely microstepped machine stepsPerRevX100 runs into
      // the millions, and multiplying that by the nail count overflows a
      // 32-bit long. The result is a plausible-looking but wrong correction,
      // which is worse than no correction at all.
      int64_t asked = (int64_t)calRevs * (int64_t)numNails;
      int64_t went = asked + (int64_t)errNails;
      if (went > 0) {
        int64_t fresh = ((int64_t)stepsPerRevX100 * asked) / went;
        if (fresh > (int64_t)stepsPerRevX100 / 2 &&
            fresh < (int64_t)stepsPerRevX100 * 2) {
          stepsPerRevX100 = (long)fresh;
        }
      }
    }
    calRunning = false;
    calRevs = 0;
    saveConfig();

  } else if (cmd == "servotest") {
    if (!queueCmd(PC_SERVOTEST, value, why)) { code = 409; ok = why; }

  } else if (cmd == "drillservotest") {
    if (!queueCmd(PC_DRILLSERVOTEST, value, why)) { code = 409; ok = why; }

  } else if (cmd == "feed") {
    if (!queueCmd(PC_FEED, 0, why)) { code = 409; ok = why; }

  } else if (cmd == "drilltest") {
    if (!queueCmd(PC_DRILLTEST, 0, why)) { code = 409; ok = why; }

  } else if (cmd == "wraptest") {
    if (pinSequence.empty()) { code = 400; ok = "Load a sequence first."; }
    else if (!queueCmd(PC_WRAPTEST, value, why)) { code = 409; ok = why; }

  } else if (cmd == "wrappreview") {
    if (!queueCmd(PC_WRAPPREVIEW, value, why)) { code = 409; ok = why; }

  } else if (cmd == "verify") {
    if (verifyPending && !moving) verifyAnswer(value);
    else { code = 409; ok = "No checkpoint waiting."; }

  } else if (cmd == "learnapply") {
    // Use the suggested steps/turn now, while a checkpoint has just confirmed
    // where the disc is, so the numbering stays consistent.
    if (learnSuggestX100 > 0 && !moving && !pinSequence.empty()) {
      long was = stepsPerRevX100;
      learnApplySpr(learnSuggestX100, previewNail());
      learnNote = "Applied steps/turn " + String(was / 100.0, 2) + " -> " +
                  String(stepsPerRevX100 / 100.0, 2) + ".";
    } else { code = 409; ok = "Nothing to apply yet."; }

  } else if (cmd == "learnforget") {
    // Put steps/turn back to what it was before learning touched it. The
    // offset corrections stay: they describe where the disc really is.
    if (learnBaseSprX100 > 0 && !moving && !pinSequence.empty()) {
      learnApplySpr(learnBaseSprX100, previewNail());
      learnBaseSprX100 = 0;
    }
    learnReset();
    learnNote = "Forgot what it learned.";
    saveConfig();

  } else {
    code = 400;
    ok = "Unknown command.";
  }

  sendCorsHeaders();
  server.send(code, "text/plain", ok);
}

// ---- Legacy endpoints, kept so the original dashboard and any scripts or
// ---- curl one-liners built against it still work unchanged.

void handleDrillStart() {
  sendCorsHeaders();
  if (server.hasArg("nails")) {
    long v = server.arg("nails").toInt();
    if (v >= 2 && v <= 2000) { numNails = (uint16_t)v; applyAutoWrapGeometry(); saveConfig(); }
  }
  String why;
  progressTotal = numNails;
  if (!startJob(JOB_DRILL, why)) server.send(409, "text/plain", why);
  else server.send(200, "text/plain", "Homing, then drilling " + String(numNails) + " holes.");
}

void handleStringStart() {
  sendCorsHeaders();
  Serial.printf("START STRING request: %u pin pairs loaded. Free heap=%u\n",
                (unsigned)pinSequence.size(), (unsigned)ESP.getFreeHeap());
  if (pinSequence.empty()) {
    server.send(400, "text/plain", "No pins loaded. POST to /pins first.");
    return;
  }
  String why;
  if (!startJob(JOB_STRING, why)) server.send(409, "text/plain", why);
  else server.send(200, "text/plain",
                   "Stringing " + String((unsigned)pinSequence.size()) + " lines.");
}

void handleStop() {
  sendCorsHeaders();
  stopRequested = true;
  pauseRequested = false;
  verifyPending = false;
  server.send(200, "text/plain", "Stop requested.");
}

void handlePause() {
  sendCorsHeaders();
  if (machineState == STATE_HOMING || machineState == STATE_DRILLING ||
      machineState == STATE_STRINGING) {
    pauseRequested = true;
    server.send(200, "text/plain", "Pause requested.");
  } else if (machineState == STATE_PAUSED) {
    server.send(200, "text/plain", "Already paused.");
  } else {
    server.send(409, "text/plain", "Nothing running to pause.");
  }
}

void handleResume() {
  sendCorsHeaders();
  if (machineState == STATE_PAUSED) {
    pauseRequested = false;
    server.send(200, "text/plain", "Resuming.");
  } else {
    server.send(409, "text/plain", "Machine is not paused.");
  }
}

// ---------------------------------------------------------------------------
//  Source photo
// ---------------------------------------------------------------------------
// The picture a pattern came from lives only in the browser that cropped it,
// so a second device could rebuild the artwork from /pins but had no photo for
// the inside of the greeting card. Keeping the JPEG here makes the machine the
// whole job rather than half of it.
//
// It is stored as a file and streamed both ways: a JPEG will not survive being
// carried in an Arduino String, which stops at the first zero byte, and buffering
// one in RAM would cost more heap than the ESP32 can spare.

const char *PHOTO_PATH = "/photo.jpg";
const size_t PHOTO_MAX = 160 * 1024;

File photoUploadFile;
size_t photoUploadBytes = 0;
bool photoUploadFailed = false;

size_t photoSize() {
  FsLock lock;
  if (!LittleFS.exists(PHOTO_PATH)) return 0;
  File f = LittleFS.open(PHOTO_PATH, "r");
  if (!f) return 0;
  size_t n = f.size();
  f.close();
  return n;
}

void photoClear() {
  FsLock lock;
  if (LittleFS.exists(PHOTO_PATH)) LittleFS.remove(PHOTO_PATH);
}

void handlePhotoGet() {
  sendCorsHeaders();
  FsLock lock;
  if (!LittleFS.exists(PHOTO_PATH)) {
    server.send(404, "text/plain", "No photo stored.");
    return;
  }
  File f = LittleFS.open(PHOTO_PATH, "r");
  if (!f) { server.send(500, "text/plain", "Could not open the photo."); return; }
  server.sendHeader("Cache-Control", "no-cache");
  server.streamFile(f, "image/jpeg");
  f.close();
}

// Streamed to the filesystem a chunk at a time. Refused while a job runs: the
// motor task shares this filesystem, and a second of writing is a second it
// could spend blocked between lines.
void handlePhotoUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    photoUploadBytes = 0;
    photoUploadFailed = machineBusy();
    if (photoUploadFailed) return;
    FsLock lock;
    if (LittleFS.exists(PHOTO_PATH)) LittleFS.remove(PHOTO_PATH);
    photoUploadFile = LittleFS.open(PHOTO_PATH, "w");
    if (!photoUploadFile) photoUploadFailed = true;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (photoUploadFailed || !photoUploadFile) return;
    photoUploadBytes += up.currentSize;
    if (photoUploadBytes > PHOTO_MAX) { photoUploadFailed = true; return; }
    if (photoUploadFile.write(up.buf, up.currentSize) != up.currentSize) {
      photoUploadFailed = true;             // filesystem full
    }
  } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
    if (photoUploadFile) photoUploadFile.close();
    if (photoUploadFailed || up.status == UPLOAD_FILE_ABORTED) photoClear();
  }
}

void handlePhotoPost() {
  sendCorsHeaders();
  // A pattern sent without a photo must clear the old one. Otherwise the next
  // card carries a stranger's face beside your artwork, which is worse than
  // carrying none.
  if (server.hasArg("clear")) {
    photoClear();
    server.send(200, "text/plain", "Photo cleared.");
    return;
  }
  if (machineBusy()) {
    server.send(409, "text/plain", "Machine is busy \u2014 send the photo before starting a job.");
    return;
  }
  if (photoUploadFailed) {
    server.send(507, "text/plain",
                "Could not store the photo: it is too large, or the filesystem "
                "is full. The pattern is fine; only the card's inside picture "
                "is affected.");
    return;
  }
  server.send(200, "text/plain", "Photo stored (" + String(photoUploadBytes) + " bytes).");
}

// POST ssid=...&pass=...  joins that router and remembers it.
// POST forget=1           drops the saved router and stays on the hotspot.
// Neither reboots: the hotspot is up throughout, so the page you sent this
// from keeps working and can tell you what happened.
void handleWifiSetup() {
  sendCorsHeaders();

  if (server.arg("forget").toInt() == 1) {
    wifiPrefs.begin("stringart", false);
    wifiPrefs.remove("ssid");
    wifiPrefs.remove("pass");
    wifiPrefs.end();
    WiFi.disconnect(false, true);
    staSsid = "";
    staJoined = false;
    server.send(200, "text/plain",
                "Forgotten. The machine stays on its own hotspot at 192.168.4.1.");
    return;
  }

  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (!ssid.length()) {
    server.send(400, "text/plain", "Give a network name.");
    return;
  }

  if (joinRouter(ssid, pass, true)) {
    wifiPrefs.begin("stringart", false);
    wifiPrefs.putString("ssid", ssid);
    wifiPrefs.putString("pass", pass);
    wifiPrefs.end();
    server.send(200, "text/plain",
                "Joined " + ssid + ". Also reachable at http://" +
                WiFi.localIP().toString() +
                " -- the hotspot stays up, so this page keeps working.");
  } else {
    // Not saved: remembering a password that did not work only means failing
    // the same way on every future boot.
    server.send(200, "text/plain",
                "Could not join " + ssid +
                ". Check the name and password. Still on the hotspot.");
  }
}

void handleNotFound() {
  sendCorsHeaders();
  if (server.method() == HTTP_OPTIONS) server.send(204);
  else server.send(404, "text/plain", "Not found.");
}

// ===========================================================================
//  Setup / loop
// ===========================================================================

// Try to join a router without blocking the machine. Returns straight away if
// there is nothing saved; otherwise waits up to WIFI_JOIN_TIMEOUT_MS. Either
// way the hotspot is already up, so the interface is reachable throughout.
bool joinRouter(const String &ssid, const String &pass, bool announce) {
  if (!ssid.length()) return false;
  staSsid = ssid;
  staJoined = false;
  WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : nullptr);
  uint32_t until = millis() + WIFI_JOIN_TIMEOUT_MS;
  while (millis() < until && WiFi.status() != WL_CONNECTED) {
    delay(200);
  }
  staJoined = (WiFi.status() == WL_CONNECTED);
  if (announce) {
    if (staJoined) {
      Serial.print("Joined \"");
      Serial.print(ssid);
      Serial.print("\". Also reachable at http://");
      Serial.println(WiFi.localIP());
    } else {
      Serial.print("Could not join \"");
      Serial.print(ssid);
      Serial.println("\" -- staying on the hotspot.");
    }
  }
  return staJoined;
}

void setupWiFi() {
  // Hotspot first and unconditionally. A machine you cannot reach is worse
  // than a machine on the wrong network, and this way the address printed on
  // the OLED is true from the first second.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("Hotspot \"");
  Serial.print(AP_SSID);
  Serial.println("\" is up -- browse to http://192.168.4.1");

  wifiPrefs.begin("stringart", false);
  String ssid = wifiPrefs.getString("ssid", "");
  String pass = wifiPrefs.getString("pass", "");
  wifiPrefs.end();

  if (ssid.length()) joinRouter(ssid, pass, true);
  else Serial.println("No router saved. Set one from the interface if you want one.");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.print("ESP32 reset reason: ");
  Serial.println((int)esp_reset_reason());

  fsMutex = xSemaphoreCreateMutex();

  pinMode(DIR_PIN, OUTPUT);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);
  driverEnable(false);              // released until something needs to move
  pinMode(DRILL_MOTOR_PIN, OUTPUT);
  digitalWrite(DRILL_MOTOR_PIN, LOW);
  pinMode(LIMIT_SWITCH_PIN, INPUT_PULLUP);

  // ESP32Servo needs its LEDC timers claimed before the first attach().
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  if (!LittleFS.begin(true)) {      // true = format if the partition is blank
    Serial.println("LittleFS could not be mounted; settings will not persist.");
  }
  loadConfig();
  if (stepsPerRevX100 < 1000) stepsPerRevX100 = geometryStepsPerRevX100();
  if (wrapSteps == 0 || wrapSweep == 0) applyAutoWrapGeometry();

  // Display comes up straight after config, so the flip setting is known, and
  // before WiFi so there is something on screen during the join.
  oledFound = oled::begin(PIN_OLED_SDA, PIN_OLED_SCL, oledFlip);
  Serial.println(oledFound ? "OLED found"
                           : "No OLED on GPIO21/22 -- carrying on without it");
  dashBoot("Joining WiFi", "please wait", "");

  feederServo.setPeriodHertz(50);
  feederServo.attach(FEEDER_SERVO_PIN, 500, 2400);
  servoCurrent = feederRestAngle;
  feederServo.write(servoCurrent);

  drillServo.setPeriodHertz(50);
  drillServo.attach(DRILL_SERVO_PIN, 500, 2400);
  drillServoCurrent = drillRestAngle;
  drillServo.write(drillServoCurrent);
  delay(500);

  loadSequenceFromFile();
  loadState();
  if (progressCurrent > (int)pinSequence.size()) progressCurrent = 0;
  progressTotal = (int)pinSequence.size();
  Serial.printf("Resumed at line %d of %d, disc at %ld%s\n",
                progressCurrent, (int)pinSequence.size(), currentStep,
                positionKnown ? "" : "  (UNVERIFIED -- power was cut mid-job)");

  setupWiFi();
  {
    bool sta = (WiFi.status() == WL_CONNECTED);
    String ip = currentIp();
    dashBoot(sta ? "WiFi connected" : "Own network:",
             sta ? ip.c_str() : AP_SSID,
             sta ? "" : ip.c_str());
    bootScreenUntil = millis() + 8000;
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/status", HTTP_OPTIONS, handleCorsPreflight);
  server.on("/pins", HTTP_GET, handlePinsGet);
  server.on("/pins", HTTP_POST, handlePinsPost);
  server.on("/pins", HTTP_OPTIONS, handleCorsPreflight);
  server.on("/upload", HTTP_POST, handleUpload);
  server.on("/upload", HTTP_OPTIONS, handleCorsPreflight);
  server.on("/config", HTTP_POST, handleConfig);
  server.on("/config", HTTP_OPTIONS, handleCorsPreflight);
  server.on("/action", HTTP_POST, handleAction);
  server.on("/action", HTTP_OPTIONS, handleCorsPreflight);
  server.on("/drill", HTTP_POST, handleDrillStart);
  server.on("/string", HTTP_POST, handleStringStart);
  server.on("/stop", HTTP_POST, handleStop);
  server.on("/pause", HTTP_POST, handlePause);
  server.on("/resume", HTTP_POST, handleResume);
  server.on("/wifi-setup", HTTP_POST, handleWifiSetup);
  server.on("/photo", HTTP_GET, handlePhotoGet);
  server.on("/photo", HTTP_POST, handlePhotoPost, handlePhotoUpload);
  server.on("/photo", HTTP_OPTIONS, handleCorsPreflight);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("Web server started.");

  // Motion runs on core 0 so long jobs never block the web server on core 1.
  xTaskCreatePinnedToCore(motorTask, "motorTask", 8192, NULL, 1, NULL, 0);

  // Auto-recovery after a power cut. Only worth doing with a limit switch: it
  // re-establishes an absolute zero, then drives back to the saved line. The
  // machine is deliberately NOT set running again -- thread may be loose, and
  // starting an unattended machine on power-up is a bad default.
  if (autoHomeOnBoot && HAS_LIMIT_SWITCH && !pinSequence.empty()) {
    jobReq = JOB_HOME_RESUME;
  }
}

void loop() {
  server.handleClient();

  // Run clock: only ticks while a job is actually running, so pauses and idle
  // time do not inflate the elapsed figure.
  {
    unsigned long now = millis();
    bool running = (machineState == STATE_DRILLING || machineState == STATE_STRINGING);
    if (running && elapsedTickAt != 0) runElapsedMs += (uint32_t)(now - elapsedTickAt);
    elapsedTickAt = now;
  }

  dashTick();
  delay(1);
}
