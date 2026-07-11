# 🪨📄✂️ Crafti RPS Arena

A complete competition kit for **Advanced Tactical RPS** — the resource-management,
deck-depletion Rock-Paper-Scissors. You write a bot in C++ (one file, one function),
and the arena gives you everything around it: a match engine with time limits and
crash handling, a CLI, and a web UI where you can fight your bot yourself, watch
bot-vs-bot replays, run 100-game matches, and hold the round-robin tournament.

Built in the spirit of Sebastian Lague's *Tiny Chess Bots* challenge: all your time
goes into strategy, none into plumbing.

## Quickstart (one script)

| Your OS | Run this |
|---|---|
| **macOS / Linux** | `./start.sh` |
| **Windows** | double-click **`start.bat`** |

That compiles the engine and every bot, then opens the UI at
`http://127.0.0.1:8125`. Works fully offline. To rebuild after editing your
bot, use the **⟳ Rebuild bots** button in the UI (or run the script again).

### One-time prerequisites — a C++ compiler and Python 3, nothing else

- **macOS:** `xcode-select --install` (gives you both).
- **Linux:** `sudo apt install build-essential python3` (or your distro's equivalent).
- **Windows:** [Python 3](https://python.org) — tick *"Add python.exe to PATH"* —
  plus a C++ compiler: **MinGW-w64 g++** (easiest: [winlibs.com](https://winlibs.com),
  unzip and add its `bin/` to PATH; or MSYS2) *or* Visual Studio Build Tools
  (then launch `start.bat` from a *Developer Command Prompt* so `cl` is on PATH).

Under the hood the scripts run `python3 build.py` (finds your compiler itself —
no `make` needed) and `python3 server.py --open`. On macOS/Linux the classic
`make`, `make test`, and `make serve` also still work. `python3 build.py --test`
runs the 98-check rules suite on any platform.

## Write your bot

Your whole world is **`bots/my_bot.cpp`**. Edit `choose()`, hit ⟳ Rebuild, fight:

```cpp
#include "rps.h"
using namespace rps;

struct MyBot : Bot {
  std::string name() override { return "MyBot"; }

  Move choose(const State& s) override {
    // your genius here
    return Move::play(ROCK);
  }
};
RPS_MAIN(MyBot)
```

Then `make` (or the **⟳ Rebuild bots** button in the UI) and it's live everywhere.
To enter the tournament under your own name, copy the file: any
`bots/<name>.cpp` becomes bot `<name>` automatically.

### The `State` you get every turn

| Field / helper | Meaning |
|---|---|
| `s.turn` | 1-based turn number; the game ends after 100 |
| `s.me.hand[ROCK]` | your remaining rocks (`PAPER`, `SCISSORS` likewise) |
| `s.me.coins` | your Crafticoins |
| `s.opp.hand`, `s.opp.coins` | the opponent's — **perfect information** |
| `s.history` | every `(myMove, oppMove)` pair so far |
| `s.hasHistory()`, `s.myLast()`, `s.oppLast()` | last turn's moves (guard with `hasHistory()`) |
| `s.legalMoves()` | every move you can afford right now |
| `s.next(mine, theirs)` | **full one-turn simulation** → new `State`. Build search bots with this. |

### The `Move`s you can return

| Constructor | Notation | Cost | Effect |
|---|---|---|---|
| `Move::play(ROCK)` | `r` | free | plain card |
| `Move::plus(ROCK)` | `r+` | 1🪙 | beats plain `r`; ties `r+`; normal rules otherwise |
| `Move::shift(ROCK, PAPER)` | `r?p` | 1🪙 | your rock **fights as paper** this turn |
| `Move::bomb(ROCK)` | `r!` | 2🪙 | if it **loses**, its killer is destroyed too |

Also handy: `whatBeats(shape)`, `whatLosesTo(shape)`, `beats(a, b)`, and
`rng()` — a `std::mt19937` seeded by the arena, so use it and your games stay
reproducible (`--seed`).

### The contract

- Per-move time limit: **300 ms** by default (`--time-ms` to change). Blowing it,
  crashing, or returning an illegal move **forfeits the game**.
- Print debug output to **stderr** (`std::cerr`) — stdout belongs to the protocol.
- One process per bot per game: globals reset every game, and your crashes can't
  hurt the arena.

## The rules

Each player starts with **7🪨 7📄 7✂️ and 0 Crafticoins**. Both pick one card per
turn, simultaneously. Rock beats scissors, scissors beats paper, paper beats rock.

- **Win:** your card returns to your hand. **Lose:** your card is destroyed forever.
  **Tie** (same shape): both return.
- **Coins:** playing the same *base* shape two turns in a row earns **+1 Crafticoin** —
  even if the card died. Disguised cards count as their base shape.
- **Losing conditions:** reach 0 cards and you lose (both at once → draw). At turn
  100, most total cards wins; equal is a draw.

### Upgrade fine print (what the engine enforces)

1. One upgrade per card per turn — no bomb-disguise stacking.
2. A disguise must be a *different* shape (`r?r` is illegal).
3. Costs are paid at submission; **this turn's coin can't fund this turn's upgrade**.
4. `x+` out-ranks only the same *effective* shape: `r+` beats `r`, `r!`, and `p?r`;
   plain `p` still beats `r+`; `r+` vs `r+` is a tie.
5. A bomb explodes **only when it loses** — and the blast kills the winner's card
   whatever it was (upgraded, disguised, even another bomb). On a win or tie the
   bomb returns as a normal card.
6. Shapeshifters fight as the disguise, return (if they survive) as the base.

## CLI

```bash
./build/arena play my_bot gambit              # one game, pretty-printed
./build/arena play my_bot gambit --json       # replay JSON (what the UI consumes)
./build/arena match my_bot gambit -n 100      # series + win rates
./build/arena tournament                      # round robin over build/bots
./build/arena tournament --games 20 --json
./build/arena list                            # available bots
```

Common flags: `--seed N` (reproducible games), `--time-ms N`, `--turns N`.
On Windows the binary is `build\arena.exe`; bot names work the same everywhere.

## Web UI

| Tab | What it does |
|---|---|
| **Play** | You vs any bot, 1v1. Pick a card, pick an upgrade, watch the simultaneous reveal. Illegal picks just warn you — bots would forfeit. |
| **Watch** | Bot vs bot replay with play/pause, 0.5×–4× speed, stepping, and scrubbing. |
| **Arena** | N-game match: win rates, average length, forfeits, and a dot per game — click a dot to replay that exact game. |
| **Tournament** | Round robin over every bot: standings and a head-to-head grid. |
| **Rules & API** | This document's essentials, in-app. |

**⟳ Rebuild bots** recompiles everything and shows compiler errors right in the
browser — the whole edit → rebuild → fight loop without leaving the UI.

## Running the competition (organizers)

```bash
# collect everyone's my_bot.cpp as bots/<teamname>.cpp, then:
python3 build.py
./build/arena tournament --games 20 --seed 2026        # deterministic + fair
```

- Crashes, hangs and illegal moves are forfeits — one broken bot can't stall the event.
- Same `--seed` → identical tournament, for disputes and replays.
- Big screen: `python3 server.py --host 0.0.0.0` shares the UI on your LAN so
  everyone can watch the finals from their seats.
- Suggested format: everyone starts from `my_bot.cpp`, 5 hours on the clock,
  `gambit` is the boss to beat, round robin at the end on one machine.
- CI: pushes to GitHub build + test on Linux, macOS, and Windows automatically
  (`.github/workflows/ci.yml`), so you'll know the kit is green on every OS
  before the event.

## Under the hood

- `sdk/rps.h` — every rule lives here, once. The engine, the tests, and your bot
  all include the same header, so `s.next()` simulates exactly what the engine does.
- `engine/arena.cpp` — process-isolated match runner. Each bot is an executable
  speaking a 3-line text protocol on stdin/stdout:

  ```
  → init player 0 time_ms 300 max_turns 100
  ← name MyBot
  → state turn 4 my 6 7 7 1 opp 7 5 7 0 mylast r opplast p+
  ← move r?s
  → end win reason elimination
  ```

  (That's the whole protocol — a bot in another language is ~40 lines if you ever
  want one.)
- `server.py` — Python-stdlib HTTP server; shells out to `arena`, owns zero game logic.
- `tests/test_rules.cpp` — the combat matrix, coin timing, bombs, disguises,
  validation, simulation coherence. `make test`.

- `engine/arena.cpp` is cross-platform: POSIX (`fork`/`pipe`/`poll`) and Win32
  (`CreateProcess`/`PeekNamedPipe`) backends behind one interface — macOS,
  Linux, and Windows all run bots natively with real time limits.

```
start.sh / start.bat   the one-click way in (build + serve + open browser)
build.py               cross-platform build — finds clang++/g++/cl itself
bots/                  your bot + 6 examples (rocky … gambit)
build/                 compiled arena + bots (generated; git-ignored)
docs/                  design spec + implementation plan
engine/                arena.cpp (the match runner)
sdk/                   rps.h — the single-header rules + bot SDK
tests/                 test_rules.cpp (98 checks; build.py --test)
web/                   the UI (vanilla html/css/js + bundled OFL fonts)
server.py              the local web server (--port, --host, --open)
```

## Troubleshooting

- **“no C++ compiler found”** — install per the prerequisites above; on Windows
  make sure `g++` (or `cl`) is on PATH in the terminal you're using.
- **`start.bat` window flashes and closes** — it should pause; if not, run it
  from a Command Prompt to read the error (usually Python missing from PATH).
- **“bot 'x' not found”** — run the start script (or `python3 build.py`); bots must exist in `build/bots/`.
- **UI says it can't reach the server** — is the start script still running? It binds `127.0.0.1:8125` (`--port` to change, `--host 0.0.0.0` for LAN).
- **My bot forfeits with “timeout”** — you're over 300 ms/move; test with `--time-ms 1000` while debugging, then optimize.
- **My bot forfeits with “bad reply”** — something printed to stdout. Use `std::cerr`.
- **Two identical bots always tie forever** — that's correct (mirror match, all ties, draw at turn 100). Randomize with `rng()`.
