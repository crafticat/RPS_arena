#!/usr/bin/env python3
"""Builds the arena and every bot in bots/ — macOS, Linux, or Windows.

No make required: this script finds a C++ compiler by itself.

    python3 build.py            # build what changed
    python3 build.py --test     # also build + run the rules unit tests
    python3 build.py --clean    # delete the build/ directory
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)

# Windows consoles often default to cp1252, which can't print ✓/✗.
for stream in (sys.stdout, sys.stderr):
    if hasattr(stream, "reconfigure"):
        stream.reconfigure(encoding="utf-8", errors="replace")

WIN = os.name == "nt"
EXE = ".exe" if WIN else ""
SDK = os.path.join("sdk", "rps.h")

INSTALL_HELP = """error: no C++ compiler found on PATH.

Install one of:
  macOS:    xcode-select --install
  Linux:    sudo apt install build-essential      (or your distro's equivalent)
  Windows:  MinGW-w64 g++  — easiest is https://winlibs.com or MSYS2 —
            or Visual Studio Build Tools (then run from a "Developer" prompt
            so that cl.exe is on PATH).
"""


def find_compiler():
    candidates = []
    if os.environ.get("CXX"):
        candidates.append(os.environ["CXX"])
    candidates += ["clang++", "g++", "cl"]
    for c in candidates:
        if shutil.which(c):
            return c
    sys.exit(INSTALL_HELP)


def is_msvc(cc):
    return os.path.basename(cc).lower().replace(".exe", "") == "cl"


def compile_cmd(cc, src, out):
    if is_msvc(cc):
        objdir = os.path.join("build", "obj")
        os.makedirs(objdir, exist_ok=True)
        return [cc, "/nologo", "/std:c++17", "/O2", "/EHsc", "/W3", "/Isdk",
                src, "/Fe:" + out, "/Fo" + objdir + os.sep]
    cmd = [cc, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Isdk", src, "-o", out]
    if WIN:
        cmd.append("-static")  # MinGW: students shouldn't chase missing DLLs
    return cmd


def stale(src, out):
    if not os.path.exists(out):
        return True
    built = os.path.getmtime(out)
    return built < os.path.getmtime(src) or built < os.path.getmtime(SDK)


def build_one(cc, src, out, label):
    if not stale(src, out):
        return (True, label, None)
    # encoding= matters: compilers emit UTF-8 (g++ curly quotes), which the
    # Windows locale codec can't decode — that kills the capture thread.
    r = subprocess.run(compile_cmd(cc, src, out), capture_output=True,
                       encoding="utf-8", errors="replace")
    output = ((r.stdout or "") + (r.stderr or "")).strip()
    return (r.returncode == 0, label, output or None)


def main():
    ap = argparse.ArgumentParser(description="build the RPS arena + bots")
    ap.add_argument("--test", action="store_true", help="also build and run the rules tests")
    ap.add_argument("--clean", action="store_true", help="delete build/ and exit")
    args = ap.parse_args()

    if args.clean:
        shutil.rmtree("build", ignore_errors=True)
        print("removed build/")
        return 0

    cc = find_compiler()
    os.makedirs(os.path.join("build", "bots"), exist_ok=True)

    jobs = [("engine/arena.cpp", os.path.join("build", "arena" + EXE), "arena")]
    for src in sorted(glob.glob(os.path.join("bots", "*.cpp"))):
        name = os.path.splitext(os.path.basename(src))[0]
        jobs.append((src, os.path.join("build", "bots", name + EXE), "bots/" + name))
    if args.test:
        jobs.append(("tests/test_rules.cpp", os.path.join("build", "test_rules" + EXE), "tests"))

    print(f"compiler: {cc}")
    failed = False
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        for ok, label, output in pool.map(lambda j: build_one(cc, *j), jobs):
            print(("  ✓ " if ok else "  ✗ ") + label)
            if output:
                print("    " + "\n    ".join(output.splitlines()))
            failed = failed or not ok

    if failed:
        print("\nbuild FAILED")
        return 1

    if args.test:
        r = subprocess.run([os.path.join("build", "test_rules" + EXE)])
        if r.returncode != 0:
            return r.returncode

    print("build OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
