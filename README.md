# 🪨📄✂️ Crafti RPS Arena

A complete competition kit for **Advanced Tactical RPS V2** — deck-depletion
Rock-Paper-Scissors with an economy: every turn you **shop** (purchases are
permanent and public) and then **attack** (simultaneous clash). You write a bot
in C++ (one file, two functions), and the arena gives you everything around it:
a match engine with time limits and crash handling, a CLI, and a web UI where
you can fight your bot yourself, watch bot-vs-bot replays, run 100-game
matches, and hold the round-robin tournament.

Built in the spirit of Sebastian Lague's *Tiny Chess Bots* challenge: all your
time goes into strategy, none into plumbing.

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
runs the rules test suite on any platform.

## The rules (V2)

Each player starts with **10🪨 10📄 10✂️ and 0 Crafticoins**. A game is up to
100 turns; each turn has two phases. Everything is **public**: hands, tiers,
coins, purchases, history.

**1 · Shop phase** — both players simultaneously buy **at most one** thing,
then both purchases are revealed. Purchases are **permanent**:

| Purchase | Price | Effect |
|---|---|---|
| ⬆ upgrade | **2**🪙 tier 0→1, **1**🪙 above | +1 tier on **one specific card**, forever |
| 💣 bomb | **5**🪙 | a card that trades 1-for-1 with whatever it meets |
| 🛒 card | **7**🪙 | a new tier-0 card of any shape |

**2 · Attack phase** — both players simultaneously play one card:

- Different shapes: rock beats scissors, scissors beats paper, paper beats rock —
  **tiers are ignored across shapes** (plain 📄 eats 🪨+9 and all its upgrades).
- Same shape: **higher tier wins**; equal tiers tie (both cards return).
- Bombs: both played cards are destroyed. No winner, no loser, no prediction needed.
- The loser's card is gone forever; the winner's card returns **keeping its tier**.

**Income:** attack with the same shape two turns running → **+2 Crafticoins**
(even if the card died). Bombs have no shape and break the chain. You can't
spend this turn's income this turn.

**⚠ Danger rounds:** every 5th turn (5, 10, 15…), the clash **loser burns one
extra random card** — upgraded cards and bombs included. Ties and trades are safe.

**Winning:** opponent at 0 total cards (bombs count). Both at 0 → draw. At turn
100, most total cards wins; equal is a draw.

## Write your bot

Your whole world is **`bots/my_bot.cpp`** — two functions:

```cpp
#include "rps.h"
using namespace rps;

struct MyBot : Bot {
  std::string name() override { return "MyBot"; }

  Shop chooseShop(const State& s) override {
    return Shop::none();                    // or upgrade/bomb/card — see below
  }
  Attack chooseAttack(const State& s) override {
    return Attack::card(ROCK, 0);           // your genius here
  }
};
RPS_MAIN(MyBot)
```

Then ⟳ Rebuild and fight. To enter the tournament under your own name, copy the
file: any `bots/<name>.cpp` becomes bot `<name>` automatically.

### The `State` you get every phase

| Field / helper | Meaning |
|---|---|
| `s.turn`, `s.isSpecialRound()` | 1-based turn; is this a mod-5 danger round? |
| `s.me.cards[ROCK]` | your rocks as a `map<int,int>`: tier → count |
| `s.me.count(ROCK)`, `s.me.maxTier(ROCK)` | totals per shape; best tier (−1 if none) |
| `s.me.bombs`, `s.me.coins`, `s.me.total()` | the rest of your side |
| `s.opp.…` | all of the opponent's — **perfect information** |
| `s.myShopNow`, `s.oppShopNow` | this turn's revealed purchases (attack phase) |
| `s.history` | every past turn: both attacks **and both purchases** |
| `s.legalShops()`, `s.legalAttacks()` | everything you may do right now |
| `s.afterShops(a, b)`, `s.next(mine, theirs)` | full simulation — build search bots |

### Moves

| Constructor | Token | Notes |
|---|---|---|
| `Shop::none()` | `-` | always legal; saving is a strategy |
| `Shop::upgrade(ROCK, 2)` | `ur2` | upgrade one of *your* rock tier-2 cards to tier 3 |
| `Shop::bomb()` | `b` | |
| `Shop::card(PAPER)` | `cp` | |
| `Attack::card(ROCK, 0)` | `r` | tier 0 tokens drop the digit |
| `Attack::card(ROCK, 3)` | `r3` | you must own that exact (shape, tier) card |
| `Attack::bombCard()` | `!` | |

Also handy: `whatBeats(shape)`, `beats(a, b)`, and `rng()` — a `std::mt19937`
seeded by the arena, so use it and your games stay reproducible (`--seed`).

### The contract

- Per-**phase** time limit: **300 ms** by default (`--time-ms`). Blowing it,
  crashing, or returning an illegal token **forfeits the game**.
- Print debug output to **stderr** (`std::cerr`) — stdout belongs to the protocol.
- One process per bot per game: globals reset every game, and your crashes can't
  hurt the arena.

## CLI

```bash
./build/arena play my_bot gambit              # one game, pretty-printed
./build/arena play my_bot gambit --json       # replay JSON (what the UI consumes)
./build/arena match my_bot gambit -n 100      # series + win rates
./build/arena tournament                      # round robin over build/bots
./build/arena tournament --games 20 --json
./build/arena list                            # available bots
```

Common flags: `--seed N` (reproducible games — including the danger-round burns),
`--time-ms N`, `--turns N`. On Windows the binary is `build\arena.exe`.

## Web UI

| Tab | What it does |
|---|---|
| **Play** | You vs any bot: shop step → both purchases revealed → attack step → simultaneous reveal. Danger rounds pulse red. Illegal picks just warn you — bots would forfeit. |
| **Watch** | Bot vs bot replay with speed control, stepping, scrubbing, purchase reveals and burn callouts. |
| **Arena** | N-game match: win rates, average length, forfeits, and a dot per game — click a dot to replay that exact game. |
| **Tournament** | Round robin over every bot: standings and a head-to-head grid. |
| **Rules & API** | This document's essentials, in-app. |

**⟳ Rebuild bots** recompiles everything and shows compiler errors right in the browser.

## Running the competition (organizers)

```bash
# collect everyone's my_bot.cpp as bots/<teamname>.cpp, then:
python3 build.py
./build/arena tournament --games 20 --seed 2026        # deterministic + fair
```

- Crashes, hangs and illegal tokens are forfeits — one broken bot can't stall the event.
- Same `--seed` → identical tournament, danger-round burns included.
- Big screen: `python3 server.py --host 0.0.0.0` shares the UI on your LAN.
- Suggested format: everyone starts from `my_bot.cpp`, 5 hours on the clock,
  `gambit` is the boss to beat, round robin at the end on one machine.
- CI: pushes to GitHub build + test on Linux, macOS, and Windows automatically.

## Under the hood

- All rules live in **`sdk/rps.h`**, once — engine, tests, and bots share it, so
  `s.next()` simulates exactly what the engine executes. (`State::next()` skips
  the danger-round random burn so searches stay deterministic; the engine's own
  RNG is seeded per game.)
- Bots are separate executables speaking a 4-line-per-turn text protocol:

  ```
  → init player 0 time_ms 300 max_turns 100
  ← name MyBot
  → state turn 4 my 6 0 0:9,1:1 0:10 0:10 opp 2 1 0:10 0:8 0:10 mylast r1 opplast s
  ← shop ur1
  → shopped my ur1 opp b
  ← attack r2
  → end win reason elimination
  ```

- `engine/arena.cpp` is cross-platform: POSIX (`fork`/`pipe`/`poll`) and Win32
  (`CreateProcess`/`PeekNamedPipe`) backends — macOS, Linux, and Windows all run
  bots natively with real time limits.

```
start.sh / start.bat   the one-click way in (build + serve + open browser)
build.py               cross-platform build — finds clang++/g++/cl itself
bots/                  your bot + 6 examples (rocky … gambit)
build/                 compiled arena + bots (generated; git-ignored)
docs/                  design spec + implementation plan (V1 history in git)
engine/                arena.cpp (the match runner)
sdk/                   rps.h — the single-header rules + bot SDK
tests/                 test_rules.cpp (rules suite; build.py --test)
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
- **My bot forfeits with “timeout”** — you're over 300 ms per phase; test with `--time-ms 1000` while debugging, then optimize.
- **My bot forfeits with “bad reply”** — something printed to stdout. Use `std::cerr`.
- **Deterministic bots draw a lot against each other** — mirror stalemates are
  real (see gambit vs my_bot); mix your play with `rng()` or read the shop reveals.
