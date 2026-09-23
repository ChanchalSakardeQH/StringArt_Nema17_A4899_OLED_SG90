// SPDX-License-Identifier: GPL-3.0-or-later AND BSD-2-Clause
// Copyright (C) 2026 CHANCHAL SAKARDE
//
// This file is part of String Art CNC.
//
// String Art CNC is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option) any
// later version.
//
// String Art CNC is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// this program. If not, see <https://www.gnu.org/licenses/>.
//
// Carried across unchanged from this design; Wire and pgm_read_byte
// behave the same on the ESP32, so only the pins differ (GPIO21/22 here).
//
// The 5x7 font table below is the "glcdfont" from Adafruit-GFX-Library,
// Copyright (c) 2012 Adafruit Industries, under the BSD 2-Clause licence.
// The driver around it is GPL. See THIRD_PARTY_NOTICES.md.

// =============================================================================
//  oled_display.h -- 0.96" SSD1306 128x64 I2C dashboard, no libraries
// =============================================================================
//
//  A small driver written for this project instead of pulling in
//  Adafruit_SSD1306 + Adafruit_GFX, so the sketch needs nothing beyond the
//  ESP32 core (Wire is bundled). About 250 lines: an init sequence, a 1 KB framebuffer, a
//  flush that sends one page per call so it never blocks the stepper for long,
//  and the standard 5x7 font below.
//
//  The drawing code has no hardware dependencies. Compile with
//  -DOLED_HOST_TEST and it builds on a PC, which is how the dashboard layout
//  was checked before it ever touched a real display.
// =============================================================================

#pragma once
#include <Arduino.h>
#ifndef OLED_HOST_TEST
#include <Wire.h>
#endif

namespace oled {

const uint8_t W = 128, H = 64;
uint8_t buf[W * H / 8];

// ---------------------------------------------------------------- font ------
// Standard ASCII 5x7 font, 32..126. One byte per column, bit 0 at top.
// This glyph table is not original to this project: it is the widely used
// "glcdfont" as distributed in Adafruit-GFX (BSD licence), which in turn
// descends from older LCD libraries. See the Credits section of the README.
const uint8_t FONT[] PROGMEM = {
0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x5F,0x00,0x00, 0x00,0x07,0x00,0x07,0x00, 0x14,0x7F,0x14,0x7F,0x14,
0x24,0x2A,0x7F,0x2A,0x12, 0x23,0x13,0x08,0x64,0x62, 0x36,0x49,0x56,0x20,0x50, 0x00,0x08,0x07,0x03,0x00,
0x00,0x1C,0x22,0x41,0x00, 0x00,0x41,0x22,0x1C,0x00, 0x2A,0x1C,0x7F,0x1C,0x2A, 0x08,0x08,0x3E,0x08,0x08,
0x00,0x80,0x70,0x30,0x00, 0x08,0x08,0x08,0x08,0x08, 0x00,0x00,0x60,0x60,0x00, 0x20,0x10,0x08,0x04,0x02,
0x3E,0x51,0x49,0x45,0x3E, 0x00,0x42,0x7F,0x40,0x00, 0x72,0x49,0x49,0x49,0x46, 0x21,0x41,0x49,0x4D,0x33,
0x18,0x14,0x12,0x7F,0x10, 0x27,0x45,0x45,0x45,0x39, 0x3C,0x4A,0x49,0x49,0x31, 0x41,0x21,0x11,0x09,0x07,
0x36,0x49,0x49,0x49,0x36, 0x46,0x49,0x49,0x29,0x1E, 0x00,0x00,0x14,0x00,0x00, 0x00,0x40,0x34,0x00,0x00,
0x00,0x08,0x14,0x22,0x41, 0x14,0x14,0x14,0x14,0x14, 0x00,0x41,0x22,0x14,0x08, 0x02,0x01,0x59,0x09,0x06,
0x3E,0x41,0x5D,0x59,0x4E, 0x7C,0x12,0x11,0x12,0x7C, 0x7F,0x49,0x49,0x49,0x36, 0x3E,0x41,0x41,0x41,0x22,
0x7F,0x41,0x41,0x41,0x3E, 0x7F,0x49,0x49,0x49,0x41, 0x7F,0x09,0x09,0x09,0x01, 0x3E,0x41,0x41,0x51,0x73,
0x7F,0x08,0x08,0x08,0x7F, 0x00,0x41,0x7F,0x41,0x00, 0x20,0x40,0x41,0x3F,0x01, 0x7F,0x08,0x14,0x22,0x41,
0x7F,0x40,0x40,0x40,0x40, 0x7F,0x02,0x1C,0x02,0x7F, 0x7F,0x04,0x08,0x10,0x7F, 0x3E,0x41,0x41,0x41,0x3E,
0x7F,0x09,0x09,0x09,0x06, 0x3E,0x41,0x51,0x21,0x5E, 0x7F,0x09,0x19,0x29,0x46, 0x26,0x49,0x49,0x49,0x32,
0x03,0x01,0x7F,0x01,0x03, 0x3F,0x40,0x40,0x40,0x3F, 0x1F,0x20,0x40,0x20,0x1F, 0x3F,0x40,0x38,0x40,0x3F,
0x63,0x14,0x08,0x14,0x63, 0x03,0x04,0x78,0x04,0x03, 0x61,0x59,0x49,0x4D,0x43, 0x00,0x7F,0x41,0x41,0x41,
0x02,0x04,0x08,0x10,0x20, 0x00,0x41,0x41,0x41,0x7F, 0x04,0x02,0x01,0x02,0x04, 0x40,0x40,0x40,0x40,0x40,
0x00,0x03,0x07,0x08,0x00, 0x20,0x54,0x54,0x78,0x40, 0x7F,0x28,0x44,0x44,0x38, 0x38,0x44,0x44,0x44,0x28,
0x38,0x44,0x44,0x28,0x7F, 0x38,0x54,0x54,0x54,0x18, 0x00,0x08,0x7E,0x09,0x02, 0x18,0xA4,0xA4,0x9C,0x78,
0x7F,0x08,0x04,0x04,0x78, 0x00,0x44,0x7D,0x40,0x00, 0x20,0x40,0x40,0x3D,0x00, 0x7F,0x10,0x28,0x44,0x00,
0x00,0x41,0x7F,0x40,0x00, 0x7C,0x04,0x78,0x04,0x78, 0x7C,0x08,0x04,0x04,0x78, 0x38,0x44,0x44,0x44,0x38,
0xFC,0x18,0x24,0x24,0x18, 0x18,0x24,0x24,0x18,0xFC, 0x7C,0x08,0x04,0x04,0x08, 0x48,0x54,0x54,0x54,0x24,
0x04,0x04,0x3F,0x44,0x24, 0x3C,0x40,0x40,0x20,0x7C, 0x1C,0x20,0x40,0x20,0x1C, 0x3C,0x40,0x30,0x40,0x3C,
0x44,0x28,0x10,0x28,0x44, 0x4C,0x90,0x90,0x90,0x7C, 0x44,0x64,0x54,0x4C,0x44, 0x00,0x08,0x36,0x41,0x00,
0x00,0x00,0x77,0x00,0x00, 0x00,0x41,0x36,0x08,0x00, 0x02,0x01,0x02,0x04,0x02,
};

// ------------------------------------------------------------ drawing -------
void clear() { memset(buf, 0, sizeof(buf)); }

void pixel(int x, int y, bool on = true) {
  if (x < 0 || y < 0 || x >= W || y >= H) return;
  uint8_t &b = buf[(y >> 3) * W + x];
  if (on) b |= (1 << (y & 7)); else b &= ~(1 << (y & 7));
}

void fillRect(int x, int y, int w, int h, bool on = true) {
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) pixel(x + i, y + j, on);
}

void rect(int x, int y, int w, int h) {
  for (int i = 0; i < w; i++) { pixel(x + i, y); pixel(x + i, y + h - 1); }
  for (int j = 0; j < h; j++) { pixel(x, y + j); pixel(x + w - 1, y + j); }
}

// Characters are 6 px wide at scale 1 (5 of glyph, 1 of gap); scale doubles
// each pixel into a block, which is how the big nail number is drawn.
void glyph(int x, int y, char c, int s = 1, bool on = true) {
  if (c < 32 || c > 126) c = '?';
  const uint8_t *g = FONT + (c - 32) * 5;
  for (int col = 0; col < 5; col++) {
    uint8_t bits = pgm_read_byte(g + col);
    for (int row = 0; row < 8; row++)
      if (bits >> row & 1) fillRect(x + col * s, y + row * s, s, s, on);
  }
}

int textW(const char *t, int s = 1) {
  int n = strlen(t);
  return n ? n * 6 * s - s : 0;
}

void text(int x, int y, const char *t, int s = 1, bool on = true) {
  for (; *t; t++, x += 6 * s) glyph(x, y, *t, s, on);
}

void textRight(int xr, int y, const char *t, int s = 1, bool on = true) {
  text(xr - textW(t, s), y, t, s, on);
}

void textCentre(int y, const char *t, int s = 1) {
  text((W - textW(t, s)) / 2, y, t, s);
}

// ---------------------------------------------------------- dashboard -------

struct Dash {
  const char *status;   // RUN, WRAP, DRILL, PAUSED, READY ...
  bool alert;           // invert the header -- position unverified, homing error
  int current;          // nail at the feeder, -1 if nothing loaded
  int next;             // -1 if at the end
  int index, total;     // progress through the sequence
  long remainSec;       // -1 while the estimate is still being measured
  const char *ip;       // shown when nothing is loaded
};

void fmtTime(char *out, long sec) {
  if (sec < 0) { strcpy(out, "--:--"); return; }
  if (sec > 99L * 3600 + 3599) sec = 99L * 3600 + 3599;   // 99:59:59 fits the line
  int h = sec / 3600, m = (sec / 60) % 60, s = sec % 60;
  if (h) snprintf(out, 16, "%d:%02d:%02d", h, m, s);
  else   snprintf(out, 16, "%d:%02d", m, s);
}

void renderDash(const Dash &d) {
  clear();
  char t[24];

  // header: status left, progress count right. The count is dropped rather
  // than allowed to collide with a long status -- overlapping text on an
  // inverted bar turns into an unreadable block.
  if (d.alert) fillRect(0, 0, W, 9);
  text(1, 1, d.status, 1, !d.alert);
  if (d.total > 0) {
    snprintf(t, sizeof t, "%d/%d", d.index + 1, d.total);
    if (textW(d.status) + textW(t) + 8 <= W)
      textRight(W - 1, 1, t, 1, !d.alert);
  }
  if (!d.alert) for (int x = 0; x < W; x++) pixel(x, 10);

  if (d.total <= 0 || d.current < 0) {
    textCentre(20, "No sequence");
    textCentre(34, "Open in a browser:");
    textCentre(48, d.ip);
    return;
  }

  // the nail at the feeder, as large as it will go
  text(0, 14, "NAIL", 1);
  snprintf(t, sizeof t, "%d", d.current);
  text(0, 24, t, 3);

  // what comes next, smaller, on the right
  textRight(W - 1, 14, "NEXT", 1);
  if (d.next >= 0) snprintf(t, sizeof t, "%d", d.next); else strcpy(t, "end");
  textRight(W - 1, 24, t, 2);

  // time left
  char tm[16];
  fmtTime(tm, d.remainSec);
  text(0, 47, "time left", 1);
  textRight(W - 1, 47, d.remainSec < 0 ? "measuring" : tm, 1);

  // progress bar across the bottom
  rect(0, 57, W, 7);
  if (d.total > 0) {
    long fill = (long)(W - 4) * (d.index + 1) / d.total;
    fillRect(2, 59, (int)fill, 3);
  }
}

void renderBoot(const char *line1, const char *line2, const char *line3) {
  clear();
  textCentre(4, "String Art", 2);
  for (int x = 14; x < W - 14; x++) pixel(x, 22);
  textCentre(30, line1);
  textCentre(42, line2);
  textCentre(54, line3);
}

// ------------------------------------------------------------ transport -----
#ifndef OLED_HOST_TEST

uint8_t addr = 0;        // 0 = no display found
bool dirty = false;
uint8_t page = 8;        // 8 = idle; 0..7 = flush in progress

bool present() { return addr != 0; }

static void cmd(uint8_t c) {
  Wire.beginTransmission(addr);
  Wire.write(0x00);
  Wire.write(c);
  Wire.endTransmission();
}

static bool probe(uint8_t a) {
  Wire.beginTransmission(a);
  return Wire.endTransmission() == 0;
}

void setFlip(bool flip) {
  if (!addr) return;
  cmd(flip ? 0xA0 : 0xA1);   // segment remap
  cmd(flip ? 0xC0 : 0xC8);   // COM scan direction
  dirty = true;
}

bool begin(uint8_t sda, uint8_t scl, bool flip) {
  Wire.begin(sda, scl);
  Wire.setClock(400000);
  if (probe(0x3C)) addr = 0x3C;
  else if (probe(0x3D)) addr = 0x3D;
  else return false;

  static const uint8_t init[] PROGMEM = {
    0xAE,             // display off
    0xD5, 0x80,       // clock divide
    0xA8, 0x3F,       // multiplex 64
    0xD3, 0x00,       // display offset
    0x40,             // start line 0
    0x8D, 0x14,       // charge pump on -- needed on the common 3.3 V modules
    0x20, 0x00,       // horizontal addressing
    0xDA, 0x12,       // COM pins
    0x81, 0xCF,       // contrast
    0xD9, 0xF1,       // precharge
    0xDB, 0x40,       // VCOMH
    0xA4, 0xA6,       // show RAM, not inverted
  };
  for (uint8_t i = 0; i < sizeof(init); i++) cmd(pgm_read_byte(&init[i]));
  setFlip(flip);
  cmd(0xAF);          // display on
  clear();
  dirty = true;
  return true;
}

bool flushing() { return page < 8; }

// Whole frame at once. Only for boot, while WiFi setup is blocking the loop
// anyway -- during a run, service() spreads the frame over eight loop passes.
void service();
void flushAll() {
  if (!addr) return;
  dirty = false;
  page = 0;
  while (page < 8) service();
}
void markDirty() { dirty = true; }

// Sends ONE 128-byte page per call, about 4 ms. A full frame spread over eight
// loop passes means the stepper is never held up for more than one page.
void service() {
  if (!addr) return;
  if (page >= 8) {
    if (!dirty) return;
    dirty = false;
    page = 0;
  }
  cmd(0x21); cmd(0); cmd(W - 1);
  cmd(0x22); cmd(page); cmd(page);
  const uint8_t *p = buf + page * W;
  for (uint8_t i = 0; i < W; i += 16) {
    Wire.beginTransmission(addr);
    Wire.write(0x40);
    Wire.write(p + i, 16);    // 17 bytes a go: inside the Wire buffer on every core
    Wire.endTransmission();
  }
  page++;
}

#endif
}  // namespace oled
