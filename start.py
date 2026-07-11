#!/usr/bin/env python3
"""One command: build everything, then serve the arena UI in your browser.

    python3 start.py [--port N] [--no-open]

(Students: just run start.sh on macOS/Linux or start.bat on Windows.)
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)

rc = subprocess.call([sys.executable, "build.py"])
if rc != 0:
    print("\nThe build failed — fix the compiler error above, then run this again.")
    sys.exit(rc)

args = sys.argv[1:]
if "--no-open" in args:
    args.remove("--no-open")
else:
    args.append("--open")

try:
    sys.exit(subprocess.call([sys.executable, "server.py"] + args))
except KeyboardInterrupt:
    sys.exit(0)
