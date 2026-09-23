# Perfboard build sheet

24 × 18 holes on 2.54 mm pitch — 61 × 46 mm. A stock 70 × 50 mm
perfboard takes it with room for mounting holes.

Columns are lettered A–X left to right, rows numbered 1–18 top to bottom.
Row 1 is the USB end of the ESP32.

## Parts

| Ref | Part | Holes |
|---|---|---|
| ESP-L | ESP32 DevKit V1 · 1×15 female | B1 – B15 |
| ESP-R | (same module) | L1 – L15 |
| J1 | A4988 board | O1 – O4 |
| J2 | Feeder SG90 | O6 – O8 |
| J3 | Drill SG90 | O10 – O12 |
| J4 | Drill MOSFET | O14 – O15 |
| J5 | 0.96" OLED | S1 – S4 |
| J6 | Limit switch | S6 – S7 |
| J7 | 5 V in (screw) | U9 – U11 |
| C1 | 470 µF 10 V | U13 – U15 |

## Bus rails

Bare tinned wire soldered along the row.

| Net | Run |
|---|---|
| +3V3 | L16 → T16 |
| +5V | A17 → X17 |
| GND | A18 → X18 |

## Wires

| From | To | Net | |
|---|---|---|---|
| B1 | B17 | +5V | ESP-L.VIN → rail |
| B2 | B18 | GND | ESP-L.GND → rail |
| L2 | K18 | GND | ESP-R.GND → rail |
| L1 | L16 | +3V3 | ESP-R.3V3 → rail |
| B4 | O1 | STEP | ESP-L.D12 → J1.STEP |
| B5 | O2 | DIR | ESP-L.D14 → J1.DIR |
| B7 | O3 | EN | ESP-L.D26 → J1.EN |
| O4 | N18 | GND | J1.GND → rail |
| L9 | O8 | SERVO_FEED | ESP-R.D18 → J2.SIG |
| O6 | P18 | GND | J2.GND → rail |
| O7 | P17 | +5V | J2.+5V → rail |
| L10 | O12 | SERVO_DRILL | ESP-R.D19 → J3.SIG |
| O10 | Q18 | GND | J3.GND → rail |
| O11 | Q17 | +5V | J3.+5V → rail |
| B8 | O14 | DRILL_SIG | ESP-L.D25 → J4.SIG |
| O15 | R18 | GND | J4.GND → rail |
| L11 | S3 | SDA | ESP-R.D21 → J5.SDA |
| L14 | S4 | SCL | ESP-R.D22 → J5.SCL |
| S2 | S16 | +3V3 | J5.3V3 → rail |
| S1 | T18 | GND | J5.GND → rail |
| B6 | S6 | LIMIT | ESP-L.D27 → J6.LIM |
| S7 | U18 | GND | J6.GND → rail |
| U9 | W17 | +5V | J7.+5V → rail |
| U11 | W18 | GND | J7.GND → rail |
| U13 | V17 | +5V | C1.+ → rail |
| U15 | V18 | GND | C1.− → rail |
