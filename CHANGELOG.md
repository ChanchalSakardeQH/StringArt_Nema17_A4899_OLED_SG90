# Changelog

All notable changes to the String Art CNC firmware and its documentation.

## [Unreleased]

### Planned

- Verify on real hardware. This release was written and tested without an ESP32 toolchain available; see [Testing](#testing) for exactly what was and was not checked.
- Hardware emergency-stop documentation and wiring.
- Optional gzip of `web_page.h` behind a build flag, for builds where flash is tighter than an editable page is useful.
- A second servo axis for the wrap, so the disc does not have to make half of each loop.
- Multi-job queueing and a job history held on the device.

---

## [1.3] — 2026-09-23

### Changed

#### The greeting card redesigned

- **Both portraits are now the same diameter on the same centre line.** Two pictures of different sizes at different heights is the single thing that made the card look homemade rather than made.
- **Framed panels**, identical on both halves, with a double rule broken at the corners.
- **Decoration drawn from the subject.** Each portrait is ringed by 72 small marks standing in for nails, and each corner carries a quarter-arc with chords struck across it — the same geometry the machine draws, at the size of a flourish. Generic clipart borders would have been easier and would have said nothing about what the card is.
- **Wood You Love It branding** on the cover: the logo and `www.woodyouloveit.com` beneath the title and count. The card reuses the logo element already decoded in the top bar rather than carrying a second copy of the bytes.
- Title on the cover gains a short accent rule; the inside repeats it smaller above the message, so the two halves read as one piece.
- When no photo is stored, the inside says so in the circle instead of leaving a blank panel.

Verified by rendering: the card code was extracted and run headlessly against node-canvas with a stand-in photo and pattern, and the output inspected — the layout was adjusted twice on the strength of what it showed, not on what it should have shown.

---

## [1.2] — 2026-09-23

### Fixed

#### A greeting card printed from a second device had no photo inside

Opening the machine on a phone mid-job rebuilds the artwork from `GET /pins`, which works because the pin sequence is all the artwork is. The photo is not: it only ever existed in the browser that cropped it. So the card printed from the phone had the string art on the front and a blank panel inside, with nothing to say why.

The photo now travels with the pattern. **Send to this machine** shrinks the crop to 480 px at quality 0.72 — the card prints it about 45 mm across, so that is already more resolution than the paper can show — and posts it to the new `POST /photo`. Any device that later rebuilds the pattern also pulls it back from `GET /photo`, and prints a complete card.

Three details that matter more than they look:

- **It is stored as a file and streamed both ways.** A JPEG will not survive being carried in an Arduino `String`, which stops at the first zero byte, and buffering one in RAM would cost more heap than the ESP32 can spare.
- **A pattern sent without a photo clears the stored one** (`POST /photo?clear=1`). Otherwise the next card carries a stranger's face beside your artwork, which is worse than carrying none.
- **Uploads are refused while a job runs.** The motor task shares this filesystem, and a second spent writing is a second it could spend blocked between lines. Send the photo before pressing start.

A failed photo upload never fails the pattern: the sequence loads, and the message says the card's inside picture is the only thing affected. Rebuilding on a device with no stored photo says so up front rather than letting you discover it in the PDF.

New status key `photoBytes`. Storage caps at 160 kB.

---

## [1.1] — 2026-09-23

### Added

- **Wood You Love It branding in the interface.** The logo sits in the top bar with `www.woodyouloveit.com` beside it, both linking to the site, and a credit line above the licence notice in the sidebar footer.

  The artwork is black with a single pink accent, so it disappears against the dark theme, and inverting it would turn the heart cyan. It sits on its own white chip instead, which keeps the brand colours exactly right in both themes. Embedded as a 440 × 73 base64 PNG quantised to 32 colours — about 8.6 KB, small enough not to matter against a 214 KB page, and it means the logo loads with no network and no second request.

  On narrow screens the URL text drops out and the mark shrinks, so the state pill keeps its place.

---

## [1.0] — 2026-09-23

First release. An ESP32 WROOM-32 drives a NEMA17 through an A4988, two SG90 servos and a drill motor, homes against a limit switch, and serves the whole String Art Studio interface from its own flash.

### The machine

- **Absolute pin ↔ step positioning for any pin count**, from `motorFullSteps × microstep × gearRatio`, carried to two decimal places because a printed gear or a belt rarely lands on a whole number and the error accumulates across a job.
- **Acceleration on every move.** A stepper has a start-stop rate: the fastest it can be commanded from standstill without the rotor losing sync. A disc bolted to the motor shaft carries enough inertia to put that limit well below the running rate, so moves ramp from `startPulseUs` to `stepPulseUs` over `accelSteps` and back down before stopping. A move shorter than two ramps becomes a triangle and never reaches full speed, which is correct — there is no room to get there and stop again.
- **Step timing against absolute per-step deadlines.** Long waits are handed back to FreeRTOS and only the last fraction of a millisecond is spun out, so the motor task cannot starve core 0 and the watchdog stays quiet. Using an absolute deadline is what makes that safe: tick-granularity error is absorbed rather than accumulating into drift.
- **Wrap cycle.** Overshoot the pin, swing the tube out, carry the disc back across it, swing in, land — a real loop around the pin. A plain sweep past a pin encloses nothing and leaves the thread lying against it. `wrapSteps` and `wrapSweep` default to half a pin pitch computed from the geometry, so the tube passes through the gap rather than into a pin. `wrapMode = false` gives the plain sweep instead.
- **Limit-switch homing** at its own slower seek speed with a ramp in, because it is the one move that starts cold against static friction with no idea how far it has to go. Optional home-before-job, optional re-home every N lines, and a boot-time recovery that re-establishes zero and drives back to the interrupted line without starting the job again.
- **Drill cycle** with configurable rest and down angles, spin-up, dwell and slew.
- **Servo slew limiting** on both servos, so an SG90 cannot slam and drag the disc.
- **Calibration**: jog, set-pin, an N-turn spin measurement, and a guided "which pin actually arrived?" check that solves for the true steps per disc turn.
- **Checkpoint verify with least-squares learning** of the real steps-per-turn, reporting drift in ppm, RMS residual and span in turns. It only suggests a change given at least five checkpoints over two turns, an RMS within half a pin pitch, a total effect of at least two pin pitches, and a slope under 3%. A 10% slope is a mechanical fault, not a ratio to adopt.
- **Dry run.** The first few lines of the real pattern, run before the thread goes on, through the same code path as the real job — a dry run that behaved differently would be worse than none. Progress returns to zero either way, so a dry run can never be mistaken for work already done.
- **LittleFS persistence** of settings, sequence and progress, every access under a FreeRTOS mutex since both cores touch the filesystem. A power cut loses at most the line in progress.
- **Pause, resume, stop**; an elapsed clock that ticks only while running, average time per line, and an ETA derived from it.
- **Optional SSD1306 dashboard** on the ESP32's default I²C pair.
- **Motor task pinned to core 0**, with everything that touches a pin queued to it, so the web server on core 1 stays responsive and the two cores can never drive the same pin.

### The interface

Served from flash as plain, editable text — about 200 KB, which is why the partition scheme matters.

- Photo crop (Cropper.js), square or circular framing.
- Greedy chord generator with minimum pin separation, recent-pin avoidance, residual darkening and a circular mask. It runs entirely in the browser; the ESP32 never receives an image.
- Live canvas preview as lines are chosen.
- Downloads: `steps.txt`, `pins.txt`, SVG, and an A4 greeting-card PDF written by a hand-rolled DCTDecode PDF writer.
- Printable base-template designer: pin ring, hole positions, centre marks, print scaling.
- **Open the machine's address on a second device mid-job and the artwork appears.** The picture only ever exists in the browser that generated it, but the machine holds the thing the picture is made of — the pin sequence — so a phone rebuilds the artwork from `GET /pins` rather than needing the original photo. The chords drawn are the chords being strung.
- **Run panel**: three pins in the order they happen — the pin the thread is hooked on, the pin arriving at the feeder (large, tinted while travelling), and the one after. Progress bar, ETA, and **Back** / **Next** for hand-stepping. Next runs a real line through the same shared code as the automatic run; Back only walks the disc to where the previous line started, because nothing in software can unwind thread, and it says so.
- **Setup panel** with the five settings a new machine needs, and a guided pin check that measures rather than asks.
- **Export and import of every setting** as JSON, built from `/status` so there is one list to keep correct rather than two that drift. The calibrated steps-per-turn travels with it; `nailOffsetSteps` is written to the file but never applied on import, because it is where one particular disc sits relative to one particular switch.
- A shared pin count across the generator, the template designer and the machine settings; an edit-guard so the status poll cannot overwrite a field being typed in; dark and light themes; a resizable control panel on desktop.
- GPLv3 notice in the sidebar footer, as the licence asks of interactive programs.

### Wi-Fi

The machine raises its own hotspot first and unconditionally, then tries the saved router in the background. There is no captive setup portal — the Studio page is the setup page, reachable at `http://192.168.4.1` from the moment the ESP32 is up.

The hotspot stays up after the router connection succeeds (`WIFI_AP_STA`). That costs a little RAM and is worth it: entering router details over the hotspot would otherwise drop the very connection you are typing into. Nothing reboots, and a password that did not work is not saved — remembering it only means failing the same way on every boot.

### HTTP API

```
GET  /              the page
GET  /status        state, progress, position, learn, errors
GET  /pins          the loaded sequence as from,to pairs
POST /pins          load a sequence as from,to pairs
POST /upload        load a flat pin list
POST /config        set any adjustable setting by name
POST /action        cmd=...
POST /drill /string /stop /pause /resume /wifi-setup
```

`/action` commands: `drill`, `string`, `dryrun`, `dryrunok`, `dryruncancel`, `findhome`, `home`, `stop`, `pause`, `resume`, `next`, `prev`, `gotostep`, `goto`, `jog`, `setnail`, `calmove`, `calreport`, `caltest`, `calresult`, `servotest`, `drillservotest`, `feed`, `drilltest`, `wraptest`, `wrappreview`, `verify`, `learnapply`, `learnforget`.

Sequence files may carry a header row or `#` comments. Pins outside the ring are refused with an explanation rather than clamped — a clamped pin is a move to the wrong place, which is worse than a refused upload. CORS preflight is answered on every POST route, so a `file://` copy of the page can drive the machine during development.

### Safety behaviour worth knowing

- **A job that would home against the switch while pin 0 was set by hand is refused.** Homing moves step zero to the switch; if the numbering was anchored somewhere else, that shift rotates the whole ring and the job strings onto the wrong pins from its first line. The refusal names both ways out. It applies only once homing has actually succeeded at least once, so a machine whose switch does not work is not deadlocked by a check it cannot satisfy.
- **Refusals are held in front of the status poll** for twelve seconds. Without that the 1 Hz poll rewrites the same line and the reason vanishes before it can be read — indistinguishable from a button that does nothing.
- **The pin check detects reversed numbering.** Which way the motor turns for a given step depends on how its coils are wired, and the firmware has no way to know. So "clockwise" is not a question it can act on. If the disc moves about the right distance but the wrong way round the numbering, the check says so specifically and changes nothing.
- **Stop and Pause are software controls.** Neither removes motor power, and both depend on the ESP32 and this firmware running correctly. Fit a hardware emergency stop.

### Testing

No ESP32 toolchain was available where this was written, so it has not been compiled for the target. `tests/run.sh` runs four passes on any PC with `g++`:

- **Prototype lint.** Arduino generates forward declarations for every function and inserts them above most of your code, without moving type definitions — so a type used in a function signature must live in a header. A plain host compile never inserts prototypes and happily passes a file the IDE cannot build, which is why this check exists.
- **Page check.** Every script block parses, every element id the JavaScript looks up exists in the HTML, and every `/status` key the page reads is one the firmware emits.
- **Host syntax check** of the whole sketch against stub headers.
- **Unit tests**, 1683 assertions, over the pure logic: pin ↔ step geometry, the sequence parser, the acceleration profile and the drift fit. They test the confidence gates as much as the arithmetic — the gates are what stop the machine talking itself into a wrong calibration.

Anything that touches hardware is out of scope for all of it: stepping, servo timing, the limit switch, I²C, LittleFS, the web server. Flash it, read the first-run checklist in the README, and keep a hand near the power switch on the first move.
