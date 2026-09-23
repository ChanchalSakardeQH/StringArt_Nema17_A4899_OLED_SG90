# Host-side tests

These run on a PC with nothing but `g++`. No ESP32, no Arduino IDE, no board
attached.

```sh
./run.sh
```

Four things happen:

1. **Prototype lint** (`check_prototypes.py`). Arduino generates a forward
   declaration for every function in the `.ino` and inserts them all near the
   top of the file, above most of your code — but it does not move type
   definitions. A function whose signature names a type defined further down
   therefore gets a prototype that cannot see that type, and the build fails
   with something like `'PendCmd' was not declared in this scope`, pointed at
   the definition rather than at the generated prototype that caused it. A
   `g++` compile never inserts prototypes, so it happily passes a file the IDE
   cannot build. This lint closes that gap: it fails if any type defined in the
   `.ino` appears in a top-level function signature. Put such types in
   `machine_types.h`. Types used only inside function bodies are fine.
2. **Page check** (`check_page.py`). Pulls the page back out of `web_page.h`
   and checks three things: every script block parses, every element id the
   JavaScript looks up exists in the HTML, and every `/status` key the page
   reads is one the firmware actually emits. All three failure modes are
   silent on the machine — the page loads, looks right, and one control
   quietly does nothing, or a number renders as `undefined`.
3. **Syntax check.** The whole sketch is compiled with `-fsyntax-only` against
   the stub headers in `shims/`, which fake Arduino, WiFi, WebServer,
   WiFiManager, ESP32Servo, LittleFS, Wire and FreeRTOS. This catches typos and
   type errors in seconds instead of after a two-minute Arduino build.
4. **Unit tests.** 1683 assertions over the three pieces of pure logic that are
   painful to debug on the machine itself.

## What is covered

**`normalizeStep` / `nailBaseStep` / `nailToStep`** — wrapping in both
directions and past several turns; the assertion that 200 nails at 2400 steps
per turn still yields exactly 12 steps per nail, so this firmware moves like
an earlier design did; a nail count that does not divide the revolution (288 nails, 8.33
steps of pitch) staying monotonic, evenly spread and collision-free; rounding
to nearest rather than truncating; a calibrated fractional steps-per-turn; and
the zero-nails and zero-steps guards.

**`parseSequenceBody`** — `from,to` pairs and flat nail lists; CRLF and tab
separators; header rows and `#` comments; extra columns ignored on pair lines;
out-of-range nails refused without clobbering the loaded sequence; and a
3000-stop sequence arriving as one comma-separated line, which is what the
Studio's Send button actually posts and what an earlier version of this parser
silently truncated.

**`rampPeriodUs`** — the acceleration profile: reaching full speed and coming
back down, monotonic on the way up (a period that wandered would be a jolt),
symmetric between the two ends, the triangle case where a move is too short to
reach full speed, single- and two-step moves, and the three ways the ramp can
be switched off or configured backwards. A ramp that accelerates into the stop
would be worse than no ramp, so that clamp is tested directly.

**`learnFit`** — a pure offset must not be read as a ratio error; a real 0.5 %
drift over ten turns must be found and adopted; three points over half a turn
must not be trusted; noise larger than half a nail pitch must not be; a 10 %
slope is a mechanical fault, not a ratio; and a real but sub-pitch drift is
measured and left alone. These test the confidence gates, not just the
arithmetic — the gates are what stop the machine from talking itself into a
wrong calibration.

## What is not covered

Anything that touches hardware: stepping, servo timing, the limit switch, I²C,
LittleFS, the web server. The stubs make those compile, not work. Flash the
firmware and use the wrap test and the calibration tools for that.

## Adding a test

`tests.cpp` includes the generated `build/sketch_body.h`, so every global and
function in the sketch is directly reachable. Use `resetGeom()` to set the ring
up, then `CHECK(condition, "printf-style message")`. A failure prints the file,
line and message and makes `run.sh` exit non-zero.
