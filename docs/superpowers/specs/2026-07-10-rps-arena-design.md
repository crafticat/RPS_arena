# Crafti RPS Arena — Design Spec

**Date:** 2026-07-10
**Goal:** A Sebastian-Lague-"Tiny Chess Bots"-style competition kit for *Advanced Tactical RPS*: contestants implement a C++ bot in a single file in ~5 hours, then test ideas instantly via CLI and a web UI (human-vs-bot 1v1, bot-vs-bot watching, N-game matches, round-robin tournaments).

## Users & success criteria

- **Contestant:** edits `bots/my_bot.cpp`, runs `make`, immediately plays/watches their bot. Needs a friendly API (state, legal moves, simulation) so the 5 hours go into strategy, not plumbing.
- **Organizer:** runs a fair tournament with crash/timeout/illegal-move handling that can't take down the arena.
- Success: `make && make serve` gives a working UI with example bots out of the box; a new bot is one file + one rebuild away.

## Approaches considered

1. **In-process bots** (compile all bots into the arena, like Lague's C# kit): fastest matches, but one segfault or infinite loop kills the whole arena — unacceptable for a live competition in C++.
2. **Subprocess bots with a line protocol** *(chosen)*: each bot is its own executable; the engine talks over stdin/stdout with per-move timeouts. Crashes/hangs become clean forfeits. A provided single-header SDK hides the protocol entirely.
3. **Rules in two places (C++ engine + JS for UI interactivity):** rejected — divergence risk. Rules live only in C++; the UI is driven entirely by engine-produced JSON (replays and interactive sessions).

Server choice: Python 3 stdlib (`http.server`) wrapping the `arena` binary — zero dependencies, no C++ HTTP library to vendor. All game logic stays in C++.

## Architecture

```
bots/*.cpp ──(make)──► build/bots/* (one executable per bot)
                            ▲ stdin/stdout line protocol, per-move timeout
sdk/rps.h  ── rules + bot SDK (single header, single source of truth)
engine/arena.cpp ── match runner CLI:
    arena play A B [--json]        one game (pretty or JSON replay)
    arena match A B -n N --json    N games + stats + replays
    arena tournament [bots...]     round robin, standings
    arena interactive BOT --human a|b   JSON-event session (human vs bot)
server.py ── stdlib HTTP: static web/ + /api/* (shells out to arena)
web/ ── vanilla JS SPA: Play (human 1v1) | Watch | Arena | Tournament | Docs
```

### Bot API (what contestants see)

```cpp
#include "rps.h"
struct MyBot : rps::Bot {
  rps::Move choose(const rps::State& s) override {
    auto moves = s.legalMoves();          // all legal moves incl. upgrades
    rps::State after = s.next(m, guess);  // full simulation for search bots
    return rps::Move::play(rps::ROCK);    // or plus(x) / bomb(x) / shift(x, as)
  }
};
RPS_MAIN(MyBot)
```

`State`: turn, `me`/`opp` (`hand[3]`, `coins`), full move history, helpers (`total()`, `beats()`, `rng()` seeded per game by the engine via `RPS_SEED`).

### Engine ↔ bot protocol (line-based; SDK users never see it)

- `init player <0|1> time_ms <T> max_turns <N>` → bot: `name <display name>`
- `state turn <t> my <r> <p> <s> <coins> opp <r> <p> <s> <coins> mylast <mv|-> opplast <mv|->` → bot: `move <spec>`
- `end <win|lose|draw> reason <text>`
- Move spec: `r`, `p`, `s`, plus `x+` (upgrade), `x!` (bomb), `x?y` (x disguised as y).

### Rule clarifications (edge cases the spec leaves open — locked here, enforced by engine, documented in README)

1. At most **one** upgrade per card per turn (no stacking, e.g. no bomb+disguise).
2. Disguise must be a **different** shape than the base (`r?r` is illegal).
3. Upgrade cost is paid on submission; the coin earned *this* turn cannot be spent this turn.
4. Coin farming: +1 if base shape (not disguise) equals previous turn's base shape, **regardless of combat outcome**; never on turn 1; max +1/turn; no coin cap.
5. `x+` only beats the same *effective* shape: `r+` beats `r`, `r!`, and `p?r`; `p` still beats `r+`; `x+` vs `x+` ties.
6. Bomb explodes **only on loss** (destroys the winner's card too — even if that card was a bomb or upgraded); on win/tie it returns as a normal card.
7. Both players eliminated same turn (bomb trade at 1 card each) → **draw**. Turn limit 100 → most total cards wins, tie → draw.
8. Bot illegal move / per-move timeout (default 300 ms, configurable) / crash → immediate **forfeit** with recorded reason. Humans in the UI get a retry instead.

### Testing

- `tests/test_rules.cpp` (plain asserts, `make test`): combat matrix incl. `+`/`?`/`!` interactions, coin timing, validation, `legalMoves` counts, `State::next` simulation coherence, card-count adjudication.
- Integration: scripted CLI runs (deterministic bots give known outcomes, e.g. counter must beat all-rock 100%).
- End-to-end: server API smoke tests via curl; UI checked in browser.

### Deliverables

`Makefile`, `sdk/rps.h`, `engine/arena.cpp`, `tests/test_rules.cpp`, example bots (`random_bot`, `rocky`, `copycat`, `counter`, `farmer`, `gambit`) + `my_bot.cpp` starter template, `server.py`, `web/{index.html,style.css,app.js}`, `README.md` (quickstart, full API reference, rules + clarifications, CLI/protocol reference, organizer guide).

### Out of scope (YAGNI)

Auth, persistence/leaderboards across restarts, non-C++ language kits (protocol is documented so it's possible later), Elo ratings, replays saved to disk beyond the session.

## Self-review

- No placeholders/TBDs; rule clarifications cover every interaction pair (shape×upgrade), coin timing, and termination.
- Consistent: one rules implementation (rps.h) consumed by engine, tests, and bots; UI consumes engine JSON only.
- Scope: single implementation plan is feasible; UI is the largest chunk but strictly consumes defined JSON.
- Note: user approval gathered up front from the original request (autonomous run — user not available mid-task); spec not git-committed because the directory is not a git repository.
