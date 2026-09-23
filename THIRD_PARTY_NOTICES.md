# Third-party notices

This project is GPL-3.0-or-later. It bundles or depends on the following work
by other people, under their own licences. Their notices are reproduced here in
full and are also kept in the source files themselves.

---

## Cropper.js 1.6.1 — Chen Fengyuan

**Bundled**, unmodified, inside `web_page.h` (CSS and JavaScript), with its
original banner comment intact. Used for the photo crop modal.

Licence: **MIT**

```
Copyright (c) 2015-present Chen Fengyuan

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## 5×7 "glcdfont" — Adafruit Industries

**Bundled** inside `oled_display.h` as the `FONT[]` table. It is the font
distributed with Adafruit-GFX-Library, which in turn descends from older LCD
libraries. The SSD1306 driver code around it was written for this project and
is GPL-3.0-or-later.

Licence: **BSD 2-Clause**

```
Software License Agreement (BSD License)

Copyright (c) 2012 Adafruit Industries. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

---

## Libraries used but not bundled

These are installed through the Arduino Library Manager and are not
redistributed with this project. Their licences apply to their own code.

| Library | Author | Licence |
|---|---|---|
| WiFiManager | tzapu | MIT |
| ESP32Servo | Kevin Harrington | LGPL-2.1-or-later |
| Arduino core for ESP32 (`WebServer`, `LittleFS`, `Wire`, FreeRTOS) | Espressif / Arduino | LGPL-2.1-or-later, Apache-2.0 |

---

## Everything else

All remaining code — the firmware, the String Art Studio page, the generator,
the base template designer, the PDF writer and the SSD1306 driver — is
Copyright (C) 2026 Chanchal Sakarde, under GPL-3.0-or-later.
