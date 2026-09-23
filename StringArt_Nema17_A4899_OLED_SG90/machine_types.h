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

#pragma once

/*
  Machine types.

  WHY THESE LIVE IN A HEADER RATHER THAN IN THE .INO

  Before compiling, the Arduino build scans the .ino and writes a forward
  declaration for every function it finds, inserting them all near the top of
  the file -- above most of your code. That is what lets a sketch call a
  function defined further down without declaring it first.

  The scan does not move type definitions. So a function like

      static bool queueCmd(PendCmd c, long v, String &why)

  gets a prototype generated at the top of the file, while `enum PendCmd`
  is still defined several hundred lines below it. The prototype names a type
  that does not exist yet, and the build fails with

      error: 'PendCmd' was not declared in this scope

  rewritten against the line the function is *defined* on, which makes it look
  like the definition is at fault when the real problem is the invisible
  prototype above it.

  Anything #included, on the other hand, is textually present before those
  prototypes are inserted. Putting the enums here means the generated
  declarations always see them, whatever the scanner decides to do.

  Rule of thumb for this sketch: any type used in a function *signature*
  belongs in this file. Types used only inside function bodies (FsLock, for
  instance) can stay in the .ino.
*/

// ---------------- Machine state ----------------
enum MachineState {
  STATE_IDLE, STATE_HOMING, STATE_DRILLING, STATE_STRINGING,
  STATE_PAUSED, STATE_DONE, STATE_ERROR
};

// A job the web server asks for and the motor task runs to completion.
enum JobReq { JOB_NONE, JOB_DRILL, JOB_STRING, JOB_HOME, JOB_HOME_RESUME, JOB_DRYRUN };

// One-shot commands the web server asks for and the motor task performs, so
// only one task ever drives the stepper and the servos.
enum PendCmd {
  PC_NONE, PC_JOG, PC_GOTO, PC_SERVOTEST, PC_DRILLSERVOTEST,
  PC_FEED, PC_WRAPTEST, PC_CALMOVE, PC_WRAPPREVIEW, PC_DRILLTEST,
  PC_NEXTLINE, PC_PREVLINE
};
