#!/usr/bin/env python3
"""Crafti RPS Arena — local web server.

Zero dependencies: Python stdlib only. All game logic lives in the C++
arena binary; this server just serves the UI and shells out to it.

    make serve            # build everything and start on http://localhost:8125
    python3 server.py --port 9000
"""
import argparse
import json
import os
import queue
import re
import subprocess
import sys
import threading
import time
import uuid
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)

# Windows consoles often default to cp1252, which can't print the banner glyphs.
for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

WIN = os.name == "nt"
EXE = ".exe" if WIN else ""
ARENA = os.path.join(ROOT, "build", "arena" + EXE)
BOTS_DIR = os.path.join(ROOT, "build", "bots")
WEB_DIR = os.path.join(ROOT, "web")

BOT_NAME_RE = re.compile(r"^[A-Za-z0-9_-]{1,64}$")
SESSION_IDLE_LIMIT = 30 * 60  # seconds

MIME = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".json": "application/json",
    ".svg": "image/svg+xml",
    ".png": "image/png",
    ".ico": "image/x-icon",
    ".ttf": "font/ttf",
    ".woff2": "font/woff2",
    ".txt": "text/plain; charset=utf-8",
}


def list_bots():
    """Bare bot names ("gambit"), platform-independent."""
    bots = []
    if os.path.isdir(BOTS_DIR):
        for name in sorted(os.listdir(BOTS_DIR)):
            path = os.path.join(BOTS_DIR, name)
            if name.startswith(".") or not os.path.isfile(path):
                continue
            if WIN:
                if name.lower().endswith(".exe"):
                    bots.append(name[:-4])
            elif os.access(path, os.X_OK):
                bots.append(name)
    return bots


# ---------------------------------------------------------------- sessions

class Session:
    """One live human-vs-bot game: a running `arena interactive` process."""

    def __init__(self, bot, human_side, time_ms, seed):
        cmd = [ARENA, "interactive", bot, "--human", human_side,
               "--time-ms", str(time_ms), "--seed", str(seed)]
        self.proc = subprocess.Popen(
            cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, text=True, bufsize=1)
        self.lock = threading.Lock()
        self.last_used = time.time()
        self.over = False
        # A pump thread avoids the select()-vs-buffered-readline deadlock.
        self.events = queue.Queue()
        threading.Thread(target=self._pump, daemon=True).start()

    def _pump(self):
        for line in self.proc.stdout:
            try:
                self.events.put(json.loads(line))
            except ValueError:
                pass
        self.events.put(None)  # EOF sentinel

    def read_event(self, timeout=10.0):
        """One JSON event from the engine, or None on timeout/EOF."""
        try:
            return self.events.get(timeout=timeout)
        except queue.Empty:
            return None

    def read_until_actionable(self):
        """Collect events until the engine expects input again (or the game ends)."""
        events = []
        while True:
            ev = self.read_event()
            if ev is None:
                events.append({"type": "error", "reason": "engine did not respond"})
                self.over = True
                return events
            events.append(ev)
            if ev["type"] in ("state", "illegal"):
                return events
            if ev["type"] == "end":
                self.over = True
                return events

    def send_move(self, move):
        self.last_used = time.time()
        self.proc.stdin.write("move " + move + "\n")
        self.proc.stdin.flush()
        return self.read_until_actionable()

    def close(self):
        try:
            if self.proc.poll() is None:
                self.proc.kill()
            self.proc.wait(timeout=5)
        except Exception:
            pass


SESSIONS = {}
SESSIONS_LOCK = threading.Lock()


def sweep_sessions():
    now = time.time()
    with SESSIONS_LOCK:
        stale = [k for k, s in SESSIONS.items()
                 if now - s.last_used > SESSION_IDLE_LIMIT or s.over]
        for k in stale:
            SESSIONS.pop(k).close()


# ----------------------------------------------------------------- handler

class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):  # quieter console
        sys.stderr.write("  %s\n" % (fmt % args))

    # -- helpers ------------------------------------------------------------
    def send_json(self, obj, status=200):
        body = json.dumps(obj).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_raw_json(self, text, status=200):
        body = text.encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def fail(self, msg, status=400):
        self.send_json({"error": msg}, status)

    def read_body(self):
        length = int(self.headers.get("Content-Length") or 0)
        if length > 1_000_000:
            raise ValueError("body too large")
        raw = self.rfile.read(length) if length else b"{}"
        return json.loads(raw or b"{}")

    def check_bot(self, name):
        if not isinstance(name, str) or not BOT_NAME_RE.match(name):
            raise ValueError("invalid bot name")
        if name not in list_bots():
            raise ValueError("unknown bot '%s' — did you run make?" % name)
        return name

    def run_arena(self, args, timeout=600):
        if not os.access(ARENA, os.X_OK):
            self.fail("arena binary missing — run: make", 500)
            return None
        r = subprocess.run([ARENA] + args, capture_output=True, text=True, timeout=timeout)
        if r.returncode != 0:
            self.fail("arena failed: " + r.stderr.strip()[:400], 500)
            return None
        return r.stdout

    # -- static -------------------------------------------------------------
    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/api/bots":
            return self.send_json({"bots": list_bots()})
        if path.startswith("/api/"):
            return self.fail("not found", 404)
        if path == "/":
            path = "/index.html"
        fs_path = os.path.realpath(os.path.join(WEB_DIR, path.lstrip("/")))
        if not fs_path.startswith(os.path.realpath(WEB_DIR)) or not os.path.isfile(fs_path):
            return self.fail("not found", 404)
        with open(fs_path, "rb") as f:
            body = f.read()
        self.send_response(200)
        ext = os.path.splitext(fs_path)[1]
        self.send_header("Content-Type", MIME.get(ext, "application/octet-stream"))
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    # -- api ----------------------------------------------------------------
    def do_POST(self):
        sweep_sessions()
        try:
            body = self.read_body()
        except ValueError as e:
            return self.fail(str(e))

        try:
            if self.path == "/api/build":
                r = subprocess.run([sys.executable, "build.py"], capture_output=True,
                                   text=True, timeout=300)
                return self.send_json({
                    "ok": r.returncode == 0,
                    "output": (r.stdout + r.stderr)[-8000:],
                    "bots": list_bots(),
                })

            if self.path == "/api/play":
                a, b = self.check_bot(body["a"]), self.check_bot(body["b"])
                args = ["play", a, b, "--json"] + self.common_flags(body)
                out = self.run_arena(args)
                return None if out is None else self.send_raw_json(out)

            if self.path == "/api/match":
                a, b = self.check_bot(body["a"]), self.check_bot(body["b"])
                n = max(1, min(500, int(body.get("n", 50))))
                args = ["match", a, b, "-n", str(n), "--json"] + self.common_flags(body)
                out = self.run_arena(args)
                return None if out is None else self.send_raw_json(out)

            if self.path == "/api/tournament":
                games = max(1, min(50, int(body.get("games", 10))))
                args = ["tournament", "--games", str(games), "--json"] + self.common_flags(body)
                out = self.run_arena(args)
                return None if out is None else self.send_raw_json(out)

            if self.path == "/api/session":
                bot = self.check_bot(body["bot"])
                side = body.get("humanSide", "a")
                if side not in ("a", "b"):
                    return self.fail("humanSide must be 'a' or 'b'")
                time_ms = max(50, min(5000, int(body.get("timeMs", 300))))
                seed = int(body.get("seed", time.time_ns() % 1_000_000))
                s = Session(bot, side, time_ms, seed)
                events = s.read_until_actionable()
                sid = uuid.uuid4().hex[:12]
                if not s.over:
                    with SESSIONS_LOCK:
                        SESSIONS[sid] = s
                else:
                    s.close()
                return self.send_json({"id": sid, "events": events})

            m = re.match(r"^/api/session/([0-9a-f]{12})/move$", self.path)
            if m:
                with SESSIONS_LOCK:
                    s = SESSIONS.get(m.group(1))
                if s is None:
                    return self.fail("no such session (it may have expired)", 404)
                move = str(body.get("move", ""))
                if not re.match(r"^[rps][+!]?$|^[rps]\?[rps]$", move):
                    return self.fail("bad move spec")
                with s.lock:
                    events = s.send_move(move)
                if s.over:
                    with SESSIONS_LOCK:
                        SESSIONS.pop(m.group(1), None)
                    s.close()
                return self.send_json({"events": events})

            return self.fail("not found", 404)
        except (KeyError, ValueError, TypeError) as e:
            return self.fail(str(e) or "bad request")
        except subprocess.TimeoutExpired:
            return self.fail("arena run timed out", 504)

    def do_DELETE(self):
        m = re.match(r"^/api/session/([0-9a-f]{12})$", self.path)
        if not m:
            return self.fail("not found", 404)
        with SESSIONS_LOCK:
            s = SESSIONS.pop(m.group(1), None)
        if s:
            s.close()
        return self.send_json({"ok": True})

    @staticmethod
    def common_flags(body):
        flags = []
        if "timeMs" in body:
            flags += ["--time-ms", str(max(50, min(5000, int(body["timeMs"]))))]
        if "turns" in body:
            flags += ["--turns", str(max(1, min(1000, int(body["turns"]))))]
        if "seed" in body:
            flags += ["--seed", str(int(body["seed"]) & 0x7FFFFFFF)]
        return flags


def main():
    ap = argparse.ArgumentParser(description="Crafti RPS Arena web server")
    ap.add_argument("--port", type=int, default=int(os.environ.get("PORT", 8125)))
    ap.add_argument("--host", default="127.0.0.1",
                    help="bind address; use 0.0.0.0 to share the arena on your LAN")
    ap.add_argument("--open", action="store_true", help="open the UI in a browser")
    args = ap.parse_args()

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    url = "http://127.0.0.1:%d" % args.port
    print()
    print("  ◉  RPS ARENA — systems online")
    print("  ─────────────────────────────")
    print("  UI:    %s" % url)
    if args.host == "0.0.0.0":
        print("         (also reachable from other machines on your network)")
    print("  bots:  %s" % (", ".join(list_bots()) or "none — run the start script first"))
    print("  stop:  Ctrl-C")
    print()
    if args.open:
        threading.Timer(0.4, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n  bye 👋")


if __name__ == "__main__":
    main()
