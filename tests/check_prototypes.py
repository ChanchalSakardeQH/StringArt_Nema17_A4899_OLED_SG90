#!/usr/bin/env python3
"""
Catch the one mistake a plain host compile cannot see.

Before compiling, the Arduino build scans the .ino for function definitions and
writes a forward declaration for each one, inserting them all near the top of
the file. It does not move type definitions. So a function whose signature
mentions a type defined later in the .ino gets a prototype that names a type
which does not exist yet, and the build fails with

    error: 'PendCmd' was not declared in this scope

pointed at the line the function is *defined* on -- which looks like the
definition is wrong when the real problem is the generated prototype above it.

A host compile of the same file passes, because g++ never inserts prototypes.
That is a blind spot, and this script is the patch for it: any type defined in
the .ino must not appear in a top-level function signature. Move it to a header
instead (see machine_types.h). Types used only inside function bodies are fine.
"""

import re
import sys
import pathlib

SKETCH = (pathlib.Path(__file__).resolve().parent.parent
          / "StringArt_Nema17_GUI" / "StringArt_Nema17_GUI.ino")

# enum Foo {, struct Foo {, class Foo {, typedef ... Foo;
TYPE_DEF = re.compile(r"^\s*(?:enum|struct|class)\s+(?:class\s+)?([A-Za-z_]\w*)\s*[{:]")
TYPEDEF = re.compile(r"^\s*typedef\s+.*?\b([A-Za-z_]\w*)\s*;")

# A function definition starting at column 0 or after `static`/`inline`:
#   [static ]ReturnType name(args) {
FUNC_DEF = re.compile(
    r"^(?:static\s+|inline\s+)*"          # storage class
    r"(?:[A-Za-z_]\w*[\s*&]+)+"           # return type
    r"([A-Za-z_]\w*)\s*"                  # name
    r"\(([^;{]*)\)\s*(?:const\s*)?\{"     # args, then an opening brace
)


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def main():
    raw = SKETCH.read_text(encoding="utf-8", errors="replace")
    clean = strip_comments(raw)
    lines = clean.split("\n")

    local_types = set()
    for line in lines:
        m = TYPE_DEF.match(line) or TYPEDEF.match(line)
        if m:
            local_types.add(m.group(1))

    if not local_types:
        print("no types defined in the sketch; nothing to check")
        return 0

    problems = []
    for i, line in enumerate(lines, start=1):
        m = FUNC_DEF.match(line)
        if not m:
            continue
        name, args = m.group(1), m.group(2)
        if name in ("if", "for", "while", "switch", "return", "else", "do"):
            continue
        signature = line[: line.index("{")]
        for t in sorted(local_types):
            if re.search(r"\b%s\b" % re.escape(t), signature):
                problems.append((i, name, t))

    if problems:
        print("Types defined in the .ino must not appear in function signatures.")
        print("Arduino's generated prototypes are inserted above the definition,")
        print("so they will not be able to see the type. Move it to a header.\n")
        for line_no, name, t in problems:
            print("  %s:%d  %s() takes '%s'" % (SKETCH.name, line_no, name, t))
        print("\n%d problem(s)." % len(problems))
        return 1

    print("ok -- %d sketch-local type(s), none used in a signature" % len(local_types))
    print("     (%s)" % ", ".join(sorted(local_types)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
