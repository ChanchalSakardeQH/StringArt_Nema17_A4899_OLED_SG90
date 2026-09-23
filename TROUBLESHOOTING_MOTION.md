# The motor shakes instead of turning

A stepper that buzzes, hums or twitches on the spot without rotating is one of
the most common faults on a machine like this, and it has a short list of
causes. Work down the list in order — the first three cost nothing to check and
account for most cases.

This page also covers **homing failing** (`the switch never closed`), because
on a machine that is not actually rotating, homing cannot possibly succeed. Fix
the rotation first; the homing error is usually a symptom, not a separate
problem.

---

## 1. Coil pairing

**The single most common cause.** A bipolar stepper has two coils and four
wires. The driver must get one whole coil on A1/A2 and the other whole coil on
B1/B2. Pair one wire from each coil together and the motor will buzz, vibrate
and hold, but never turn — which looks exactly like a firmware problem and is
not.

Find the pairs with a multimeter on its lowest resistance range, with the motor
unplugged from everything:

- Two wires that read a few ohms between them are **one coil**.
- Two wires that read open circuit are from **different coils**.

Common (not guaranteed) colour groupings: black+green together and red+blue
together, or on some motors black+green and red+yellow. Do not trust colours —
measure. Then wire one measured pair to A1/A2 and the other to B1/B2.

Wiring a pair the wrong way round is harmless: it just reverses the direction,
which the firmware's **Reverse rotation direction** switch fixes.

---

## 2. Driver current (Vref)

An A4988 delivers whatever current its little potentiometer is set to. Too low
and the motor has no torque — it will buzz and stall, especially at the start of
a move where it has to break static friction and accelerate a disc.

With the motor connected and the board powered but idle, measure between the
pot's metal screw and GND:

```
Vref = Imax x 8 x Rsense
```

Most A4988 boards use 0.1 Ω sense resistors, giving `Vref = Imax x 0.8`. For a
typical 1.5 A NEMA17 run at 1.2 A, that is about **0.96 V**. Turn the pot a few
degrees at a time and re-measure; the adjustment range is small.

Too *high* is its own failure: the driver overheats and cuts out intermittently,
which reads as the motor randomly stopping mid-job.

---

## 3. Heatsink and power

- **Fit the heatsink to the A4988.** Without one it will thermally shut down
  within a minute of continuous running. The symptom is a machine that works
  and then stops, works and then stops.
- **The motor supply must be 12 V at a couple of amps**, separate from the
  ESP32's USB power. USB alone cannot turn a NEMA17.
- **Grounds must be common** between the ESP32 and the driver board.
- Never unplug the motor with the driver powered — it destroys A4988s.

---

## 4. Acceleration

A stepper has a *start-stop rate*: the fastest it can be commanded from
standstill without the rotor losing sync. Command anything above that from rest
and it buzzes in place. A disc bolted straight to the motor shaft carries enough
inertia to put that limit well below the running rate.

The firmware ramps every move up to speed and back down, so this should not
bite. If it still stalls on starting, go to **Advanced → Motor**:

| Setting | Try |
|---|---|
| Start pulse | raise it — a bigger number is a **slower** start |
| Ramp length | raise it — more steps spent getting up to speed |
| Homing pulse | raise it — homing is the move most likely to stall |

The line beneath those fields tells you what the numbers mean in steps per
second, so you do not have to do the arithmetic.

Setting **Ramp length** to 0 turns the ramp off entirely and every move starts
at full speed. That is the old behaviour, and it is there for machines that do
not need a ramp — not as a default.

---

## 5. Microstepping

Full stepping (no MS jumpers fitted) is the roughest, loudest, most
resonance-prone way to drive a stepper. There is a band of speeds — very often
right where a machine like this runs — where a full-stepped motor will growl
and can stall outright.

Fitting the MS1/MS2/MS3 jumpers for **1/8 or 1/16 microstepping** transforms the
smoothness. It costs nothing but three jumpers.

If you do, tell the firmware: **Advanced → Motor → A4988 microstepping**. The
nail positions are recomputed from it, so getting this wrong moves every nail.

| MS1 | MS2 | MS3 | Mode | Set "microstepping" to |
|:---:|:---:|:---:|---|---|
| L | L | L | Full step | 1 |
| H | L | L | Half step | 2 |
| L | H | L | Quarter | 4 |
| H | H | L | Eighth | 8 |
| H | H | H | Sixteenth | 16 |

---

## 6. Mechanical binding

Disconnect the motor from the disc and run it on its own. If it turns freely
uncoupled and stalls when coupled, the problem is not electrical:

- The disc is rubbing on the baseboard or on a standoff.
- The hub grub screw is loose, so the shaft turns inside it. This one looks
  exactly like a stall: the motor really is turning and nothing moves.
- A gear train is over-meshed and binding.

---

# Homing fails: "the switch never closed"

Homing drives the disc until the limit switch closes, then calls that position
zero. It gives up after the number of turns set in **Advanced → Motor → Give up
homing after**.

Check, in this order:

1. **Is the disc actually turning?** If not, everything above applies. Homing
   is not the problem.
2. **Is the switch mounted, and is there something to press it?** The switch
   has to be fixed where the disc passes it, with a screw head, a printed flag
   or a tab on the disc that closes it once per revolution. A switch sitting
   loose on the bench can never be triggered.
3. **Is it wired and reading?** The Run panel shows `limit switch: open` or
   `triggered` live. Press the lever with a finger and watch it change. If it
   never changes, check the wiring: the switch goes between **GPIO 27 and GND**,
   using the internal pull-up, so an unpressed switch reads HIGH. On a RAMPS
   endstop board, use the **C (common)** and **NO** terminals — wiring it to NC
   reads as permanently triggered instead.
4. **Is the gear ratio right?** Homing gives up after a set number of *disc*
   turns, and the firmware works out a turn from the gear ratio. If it thinks
   the reduction is 12:1 when the disc is bolted straight to the shaft, one
   "turn" is twelve times too many steps — and every nail move is twelve times
   too far as well. See below.
5. **Is it seeking the wrong way?** Homing drives opposite to the normal
   direction. If the flag sits just past the switch in that direction it will
   take almost a full turn to arrive, which is fine — but combined with a low
   give-up limit it can look like a failure. Raise the limit, or flip
   **Reverse rotation direction** and re-home.

---

# Getting the gear ratio right

This is worth stating plainly because it is invisible until the machine moves
twelve times too far:

```
steps per disc turn = motor full steps x microstepping x reduction to the disc
```

| Your drive | Reduction to the disc |
|---|---|
| Disc bolted straight to the motor shaft | **1** |
| Pinion on the motor, ring gear on the disc | ring teeth ÷ pinion teeth |
| Belt | disc pulley teeth ÷ motor pulley teeth |

A printed ring gear on the disc means nothing on its own — what matters is
whether a pinion on the motor shaft is actually meshing with it. If the motor
sits at the centre with the disc on its shaft, the reduction is **1**, however
many teeth the rim has.

Set it in **Advanced → Motor → Reduction to the disc**. The line underneath
shows the resulting steps per turn, and the steps per nail, so you can sanity
check it before running anything.

Once it is right: **Home**, then put a known nail in front of the feeder and
press **Set nail**. Then use **Calibrate → spin N turns** to measure the real
figure and correct for gear tolerance.


---

# The wrong nail arrives at the feeder

If the machine reliably brings up *a* nail but not the one you asked for, the
mechanism is working and the arithmetic is wrong. Almost always that means the
configured steps per disc turn does not match the real one.

## Measuring it from what you saw

You do not need a special tool for this — a handful of manual **Next** presses
is enough, as long as you write down both numbers each time.

```
real steps per turn  =  configured steps per turn  x  (commanded travel / actual travel)
```

Travel is measured in nails, and it is a *difference*, not a position: how far
the disc was asked to move versus how far it went. Work in signed differences
wrapped to ±half a turn, because the firmware always takes the shortest way
round.

Worked example, on a 360-nail disc configured for 2400 steps per turn:

| Asked for | Commanded travel | Nail that arrived | Actual travel |
|---|---|---|---|
| 263 | −97 | 140 | +140 |
| 355 | +92 | 9 | −131 |
| 259 | −96 | 152 | +143 |
| 346 | +87 | 22 | −130 |
| 253 | −93 | 161 | +139 |

Every actual travel is about **1.5×** the commanded one, so the disc is turning
half again as far as the firmware intends. The real figure is
`2400 ÷ 1.5 = 1600` steps per turn, and the fix is to make
`full steps × microstepping × reduction` come to 1600 in
**Advanced → Motor**.

Two things worth noticing in that table:

- **The ratio is the same for every row.** A constant ratio is a scale error —
  a configuration problem. If the ratio wandered instead, the fault would be
  mechanical: slipping, binding or lost steps.
- **Every actual travel is slightly *under* 1.5×**, never over. A consistent
  shortfall on top of a clean scale error is missed steps, and points back to
  the stalling section above. Fix the scale first, then look at that.

The signs alternate because of how the numbers are read, not necessarily
because the direction is wrong. Do not flip **Reverse rotation direction** on
the strength of this table. Use **Run direction test** instead: it jogs one
nail and tells you which way the numbering went.

## Doing it properly

**Calibrate → spin N turns** is the unambiguous version and takes a minute. It
spins a whole number of turns and asks where the disc actually finished, which
removes the guesswork about whether a move went the short way or the long way
round.

## Then check you have resolution to spare

```
steps per nail = steps per disc turn ÷ nail count
```

Below about eight steps between nails there is nothing left to aim with:
rounding alone can land the feeder on the wrong nail, and the wrap overshoot
has no room to clear the gap. The steps-per-turn line in **Advanced → Motor**
now warns when this happens.

1600 steps per turn across 360 nails is only 4.4 steps per nail — not enough.
Fitting the A4988 MS jumpers for 1/16 takes the same mechanism to 25600 steps
per turn and 71 steps per nail, which is comfortable. It costs three jumpers
and one settings change.

While you are counting: 360 nails on a 7.2 cm radius puts them **1.26 mm
apart**. That is very tight for real nails and thread — worth checking the
physical build can actually support the nail count you are generating for,
independently of anything in the firmware.
