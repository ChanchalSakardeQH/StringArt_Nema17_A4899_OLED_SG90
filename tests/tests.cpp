// Host-side unit tests for the pure logic in StringArt_Nema17_GUI.ino.
// Built against the shim headers in ../shims, so it runs on a PC with no
// ESP32 toolchain.  Checks the three things that would be painful to debug
// on the machine itself: the sequence parser, the nail<->step maths, and the
// least-squares drift fit.
#include <Arduino.h>
#include <freertos_shim.h>
#include "sketch_body.h"
#include <cstdio>

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...) do {                                   \
    checks++;                                                   \
    if (!(cond)) { failures++;                                  \
      printf("FAIL %s:%d  ", __FILE__, __LINE__);               \
      printf(__VA_ARGS__); printf("\n"); }                      \
  } while (0)

static void resetGeom(uint16_t nails, long sprX100, int8_t dir, long offset) {
  numNails = nails;
  stepsPerRevX100 = sprX100;
  dirSign = dir;
  nailOffsetSteps = offset;
}

// ---------------------------------------------------------------------------
//  normalizeStep / nailBaseStep / nailToStep
// ---------------------------------------------------------------------------
static void testGeometry() {
  resetGeom(200, 240000, 1, 0);          // 2400 steps/rev, 12 steps/nail

  CHECK(stepsPerRev() == 2400, "stepsPerRev %ld", stepsPerRev());

  // Wrapping, both directions and well beyond one turn.
  CHECK(normalizeStep(0) == 0, "norm 0");
  CHECK(normalizeStep(2399) == 2399, "norm 2399");
  CHECK(normalizeStep(2400) == 0, "norm 2400 -> %ld", normalizeStep(2400));
  CHECK(normalizeStep(2401) == 1, "norm 2401");
  CHECK(normalizeStep(-1) == 2399, "norm -1 -> %ld", normalizeStep(-1));
  CHECK(normalizeStep(-2400) == 0, "norm -2400");
  CHECK(normalizeStep(-2401) == 2399, "norm -2401");
  CHECK(normalizeStep(7 * 2400 + 5) == 5, "norm many turns");

  // The old sketch's fixed STEPS_PER_NAIL 12 must come out of the general
  // maths unchanged, or the port silently moves every machine already built.
  for (uint16_t n = 0; n < 200; n++) {
    CHECK(nailBaseStep(n) == (long)n * 12, "nail %u base %ld", n, nailBaseStep(n));
  }
  CHECK(nailBaseStep(200) == 0, "nail 200 wraps to 0");
  CHECK(nailBaseStep(201) == 12, "nail 201 wraps to nail 1");

  // Offset shifts the whole ring and wraps.
  resetGeom(200, 240000, 1, 2395);
  CHECK(nailToStep(0) == 2395, "offset nail0 %ld", nailToStep(0));
  CHECK(nailToStep(1) == 7, "offset nail1 wraps %ld", nailToStep(1));

  // Reversed direction mirrors the ring about zero.
  resetGeom(200, 240000, -1, 0);
  CHECK(nailToStep(0) == 0, "rev nail0");
  CHECK(nailToStep(1) == 2388, "rev nail1 %ld", nailToStep(1));
  CHECK(nailToStep(199) == 12, "rev nail199 %ld", nailToStep(199));

  // A nail count that does not divide the revolution: positions must stay
  // monotonic, evenly spread, and never land twice on the same step.
  resetGeom(288, 240000, 1, 0);           // 8.333 steps per nail
  long prev = -1;
  int repeats = 0;
  for (uint16_t n = 0; n < 288; n++) {
    long s = nailBaseStep(n);
    CHECK(s >= 0 && s < 2400, "nail %u out of range %ld", n, s);
    if (s == prev) repeats++;
    CHECK(s >= prev, "nail %u went backwards %ld after %ld", n, s, prev);
    prev = s;
  }
  CHECK(repeats == 0, "%d nails shared a step", repeats);
  CHECK(nailBaseStep(144) == 1200, "half way round %ld", nailBaseStep(144));
  // Rounding must be to nearest, not truncation: nail 1 sits at 8.33 -> 8.
  CHECK(nailBaseStep(1) == 8, "nail 1 of 288 -> %ld", nailBaseStep(1));
  CHECK(nailBaseStep(2) == 17, "nail 2 of 288 -> %ld", nailBaseStep(2));

  // A calibrated, non-round steps/turn figure (learning adopts these).
  resetGeom(300, 240731, 1, 0);
  CHECK(stepsPerRev() == 2407, "rounded spr %ld", stepsPerRev());
  CHECK(nailBaseStep(0) == 0, "cal nail0");
  CHECK(nailBaseStep(150) == 1204, "cal half way %ld", nailBaseStep(150));

  // Zero nails must not divide by zero.
  resetGeom(0, 240000, 1, 0);
  CHECK(nailBaseStep(5) == 0, "no nails -> 0");
  resetGeom(200, 0, 1, 0);
  CHECK(normalizeStep(50) == 0, "zero spr -> 0");
}

// ---------------------------------------------------------------------------
//  parseSequenceBody
// ---------------------------------------------------------------------------
static void testParser() {
  resetGeom(200, 240000, 1, 0);

  // Pairs, one per line, with a header row and a comment.
  pinSequence.clear();
  CHECK(parseSequenceBody("from,to\n0,10\n10,25\n# note\n25,3\n", true), "pairs rejected");
  CHECK(pinSequence.size() == 3, "pairs size %d", (int)pinSequence.size());
  CHECK(pinSequence[0].first == 0 && pinSequence[0].second == 10, "pair 0");
  CHECK(pinSequence[2].first == 25 && pinSequence[2].second == 3, "pair 2");
  CHECK(progressTotal == 3, "progressTotal %d", progressTotal);
  CHECK(progressCurrent == 0, "progressCurrent %d", progressCurrent);

  // Whitespace and tab separated pairs, CRLF line endings.
  pinSequence.clear();
  CHECK(parseSequenceBody("1 2\r\n3\t4\r\n", true), "ws pairs rejected");
  CHECK(pinSequence.size() == 2, "ws pairs size %d", (int)pinSequence.size());
  CHECK(pinSequence[1].first == 3 && pinSequence[1].second == 4, "ws pair 1");

  // Extra columns on a pair line are ignored, not treated as more nails.
  pinSequence.clear();
  CHECK(parseSequenceBody("0,10,999,888\n", true), "extra cols rejected");
  CHECK(pinSequence.size() == 1, "extra cols size %d", (int)pinSequence.size());
  CHECK(pinSequence[0].second == 10, "extra cols pair");

  // Flat list: consecutive stops become lines.  This is what the Studio's
  // Send button posts, and it arrives as ONE very long comma-separated line.
  pinSequence.clear();
  CHECK(parseSequenceBody("0,10,25,3", false), "flat rejected");
  CHECK(pinSequence.size() == 3, "flat size %d", (int)pinSequence.size());
  CHECK(pinSequence[0].first == 0 && pinSequence[0].second == 10, "flat 0");
  CHECK(pinSequence[1].first == 10 && pinSequence[1].second == 25, "flat 1");
  CHECK(pinSequence[2].first == 25 && pinSequence[2].second == 3, "flat 2");

  // The regression the old eight-numbers-per-line parser hit: a 3000-stop
  // sequence on a single line must survive whole.
  {
    std::string big;
    const int stops = 3000;
    for (int i = 0; i < stops; i++) {
      if (i) big += ",";
      big += std::to_string(i % 200);
    }
    pinSequence.clear();
    CHECK(parseSequenceBody(String(big), false), "long flat rejected");
    CHECK((int)pinSequence.size() == stops - 1, "long flat size %d", (int)pinSequence.size());
    CHECK(pinSequence[0].first == 0 && pinSequence[0].second == 1, "long flat head");
    CHECK(pinSequence.back().second == (stops - 1) % 200, "long flat tail");
  }

  // Flat list spread over many lines streams across the line breaks.
  pinSequence.clear();
  CHECK(parseSequenceBody("nails\n0\n10\n25\n", false), "multiline flat rejected");
  CHECK(pinSequence.size() == 2, "multiline flat size %d", (int)pinSequence.size());
  CHECK(pinSequence[1].first == 10 && pinSequence[1].second == 25, "multiline flat 1");

  // Out of range for the configured ring is refused, not clamped.
  {
    auto before = pinSequence;
    CHECK(!parseSequenceBody("0,200\n", true), "nail 200 of 200 accepted");
    CHECK(!parseSequenceBody("0,-5\n", true), "negative nail accepted");
    CHECK(pinSequence.size() == before.size(), "refused parse still overwrote sequence");
  }
  CHECK(parseSequenceBody("0,199\n", true), "top nail refused");

  // Nothing usable is a failure, so the caller can answer 400 instead of
  // wiping a good sequence with an empty one.
  CHECK(!parseSequenceBody("", true), "empty body accepted");
  CHECK(!parseSequenceBody("from,to\n\n# only comments\n", true), "header-only accepted");
  CHECK(!parseSequenceBody("42", false), "single stop accepted as a line");
}

// ---------------------------------------------------------------------------
//  learnFit
// ---------------------------------------------------------------------------
static void feedLearn(const long *x, const long *e, int n) {
  learnN = 0;
  for (int i = 0; i < n && i < (int)(sizeof(learnX) / sizeof(learnX[0])); i++) {
    learnX[learnN] = x[i];
    learnE[learnN] = e[i];
    learnN++;
  }
  learnFit();
}

static void testLearnFit() {
  resetGeom(200, 240000, 1, 0);          // pitch = 12 steps

  // No data: no suggestion, no crash.
  learnN = 0; learnFit();
  CHECK(!learnConfident, "confident with no data");
  CHECK(learnSuggestX100 == 0, "suggestion with no data");

  // A pure offset with no travel-dependent growth: the fit must read that as
  // "your zero is out", not "your steps per turn are wrong".
  {
    long x[6], e[6];
    for (int i = 0; i < 6; i++) { x[i] = (long)i * 2400 * 2; e[i] = 36; }
    feedLearn(x, e, 6);
    CHECK(fabs(learnA - 36.0) < 1e-6, "offset intercept %f", learnA);
    CHECK(fabs(learnB) < 1e-9, "offset slope %f", learnB);
    CHECK(!learnConfident, "flat error suggested a ratio change");
  }

  // A real 0.5% shortfall over ten turns, measured at six checkpoints.
  {
    const double drift = 0.005;
    long x[6], e[6];
    for (int i = 0; i < 6; i++) { x[i] = (long)i * 2400 * 2; e[i] = lround(x[i] * drift); }
    feedLearn(x, e, 6);
    CHECK(fabs(learnB - drift) < 1e-6, "slope %f", learnB);
    CHECK(learnSpanTurns > 9.9 && learnSpanTurns < 10.1, "span %f", learnSpanTurns);
    CHECK(learnConfident, "clear drift not trusted");
    CHECK(learnSuggestX100 == lround(240000 * (1.0 + drift)), "suggest %ld", learnSuggestX100);
    CHECK(learnRms < 1e-6, "rms %f", learnRms);
  }

  // Same drift but only three checkpoints over half a turn: too little
  // evidence, so it must stay a measurement and not become a setting.
  {
    long x[3], e[3];
    for (int i = 0; i < 3; i++) { x[i] = (long)i * 600; e[i] = lround(x[i] * 0.005); }
    feedLearn(x, e, 3);
    CHECK(!learnConfident, "thin evidence trusted (span %f, n %u)", learnSpanTurns, learnN);
  }

  // Noise larger than half a nail pitch: the line does not explain the data.
  {
    long x[6], e[6];
    const long noise[6] = { 0, 9, -9, 9, -9, 9 };
    for (int i = 0; i < 6; i++) { x[i] = (long)i * 2400 * 2; e[i] = lround(x[i] * 0.005) + noise[i]; }
    feedLearn(x, e, 6);
    CHECK(!learnConfident, "noisy data trusted, rms %f", learnRms);
  }

  // An implausible 10% slope is a mechanical fault (slipping belt, wrong
  // microstep jumper), not a ratio to adopt.
  {
    long x[6], e[6];
    for (int i = 0; i < 6; i++) { x[i] = (long)i * 2400 * 2; e[i] = lround(x[i] * 0.10); }
    feedLearn(x, e, 6);
    CHECK(fabs(learnB - 0.10) < 1e-6, "fault slope %f", learnB);
    CHECK(!learnConfident, "10%% error adopted as a ratio");
  }

  // A tiny but perfectly consistent drift: real, yet below one nail pitch of
  // total effect, so it is not worth changing the machine over.
  {
    long x[6], e[6];
    for (int i = 0; i < 6; i++) { x[i] = (long)i * 2400 * 2; e[i] = (long)i * 2; }
    feedLearn(x, e, 6);
    CHECK(fabs(learnB) > 1e-6, "sub-pitch slope lost %f", learnB);
    CHECK(!learnConfident, "sub-pitch drift adopted");
  }
}

// ---------------------------------------------------------------------------
//  rampPeriodUs  --  the acceleration profile
// ---------------------------------------------------------------------------
static void testRamp() {
  stepPulseUs = 3000;                   // run period 6000 us
  startPulseUs = 9000;                  // start period 18000 us
  accelSteps = 240;
  const uint32_t RUN = 6000, START = 18000;

  // A long move: starts slow, reaches full speed, ends slow again.
  {
    const long n = 2000;
    CHECK(rampPeriodUs(0, n, RUN) == START, "first step %u", rampPeriodUs(0, n, RUN));
    CHECK(rampPeriodUs(240, n, RUN) == RUN, "top of ramp %u", rampPeriodUs(240, n, RUN));
    CHECK(rampPeriodUs(1000, n, RUN) == RUN, "cruise %u", rampPeriodUs(1000, n, RUN));
    CHECK(rampPeriodUs(n - 1, n, RUN) == START, "last step %u", rampPeriodUs(n - 1, n, RUN));
    CHECK(rampPeriodUs(120, n, RUN) == 12000, "midway up %u", rampPeriodUs(120, n, RUN));

    // Never faster than the running period, never slower than the start, and
    // monotonic on the way up -- a period that wandered would be a jolt.
    uint32_t prev = 0;
    for (long i = 0; i <= 240; i++) {
      uint32_t p = rampPeriodUs(i, n, RUN);
      CHECK(p >= RUN && p <= START, "step %ld out of range %u", i, p);
      if (i) CHECK(p <= prev, "step %ld sped up then slowed %u after %u", i, p, prev);
      prev = p;
    }
    // Symmetric: step i from the start matches step i from the end.
    for (long i = 0; i < 240; i++) {
      CHECK(rampPeriodUs(i, n, RUN) == rampPeriodUs(n - 1 - i, n, RUN),
            "asymmetric at %ld", i);
    }
  }

  // A move shorter than two ramps becomes a triangle: it never reaches full
  // speed, which is correct -- there is no room to get there and stop again.
  {
    const long n = 100;               // 50 steps of ramp each way
    CHECK(rampPeriodUs(0, n, RUN) == START, "short first %u", rampPeriodUs(0, n, RUN));
    CHECK(rampPeriodUs(n - 1, n, RUN) == START, "short last %u", rampPeriodUs(n - 1, n, RUN));
    uint32_t fastest = START;
    for (long i = 0; i < n; i++) {
      uint32_t p = rampPeriodUs(i, n, RUN);
      if (p < fastest) fastest = p;
      CHECK(p >= RUN, "short move exceeded full speed at %ld: %u", i, p);
    }
    CHECK(fastest > RUN, "short move reached full speed (%u)", fastest);
  }

  // A single step is all ramp and no cruise.
  CHECK(rampPeriodUs(0, 1, RUN) == RUN, "one-step move %u", rampPeriodUs(0, 1, RUN));
  CHECK(rampPeriodUs(0, 2, RUN) == START, "two-step move %u", rampPeriodUs(0, 2, RUN));

  // Ramp switched off, or configured backwards, must fall back to a flat
  // profile rather than producing a ramp that speeds up into the stop.
  accelSteps = 0;
  CHECK(rampPeriodUs(0, 2000, RUN) == RUN, "accelSteps=0 not flat");
  accelSteps = 240;
  startPulseUs = 3000;                       // same as running
  CHECK(rampPeriodUs(0, 2000, RUN) == RUN, "equal start not flat");
  startPulseUs = 1000;                       // faster than running: nonsense
  CHECK(rampPeriodUs(0, 2000, RUN) == RUN, "inverted ramp not clamped");
  CHECK(rampPeriodUs(5, 2000, RUN) == RUN, "inverted ramp not clamped mid");

  startPulseUs = DEF_START_PULSE_US;
  accelSteps = DEF_ACCEL_STEPS;
  stepPulseUs = DEF_STEP_PULSE_US;
}

int main() {
  testGeometry();
  testParser();
  testLearnFit();
  testRamp();
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
