// ============================================================================
//  MY BOT — this is your file. Make it win. 🏆
//
//  Loop:  1. edit choose() below
//         2. make                       (or the ⟳ Rebuild button in the UI)
//         3. ./build/arena play my_bot gambit      (or test in the web UI)
//
//  THE API (full reference in README.md — everything lives in sdk/rps.h):
//
//    s.turn                  current turn, 1-based (game ends after 100)
//    s.me.hand[ROCK]         how many rocks YOU have left (PAPER, SCISSORS too)
//    s.me.coins              your Crafticoins
//    s.opp.hand, s.opp.coins the opponent's — perfect information!
//    s.history               every (myMove, oppMove) pair so far
//    s.myLast(), s.oppLast() last turn's moves (guard with s.hasHistory())
//    s.legalMoves()          every move you can afford right now
//    s.next(mine, theirs)    simulates a full turn -> new State. Search away!
//
//  MOVES:
//    Move::play(PAPER)           plain card                          free
//    Move::plus(ROCK)            r+  beats plain r (ties with r+)    1 coin
//    Move::shift(ROCK, PAPER)    r?p your rock fights as paper       1 coin
//    Move::bomb(SCISSORS)        s!  if it dies, its killer dies too 2 coins
//
//  COINS: play the same BASE shape two turns in a row -> +1 Crafticoin
//         (disguises count as their base; earning happens after spending).
//
//  rng() is a seeded std::mt19937 — use it so your games are reproducible.
// ============================================================================
#include "rps.h"

using namespace rps;

struct MyBot : Bot {
  std::string name() override { return "MyBot"; }  // shown in the arena UI

  Move choose(const State& s) override {
    // ------------------------------------------------------------------
    // Starter strategy (replace me!): play our most plentiful shape, and
    // upgrade it whenever the opponent looks likely to mirror us.
    // ------------------------------------------------------------------
    Shape pick = ROCK;
    for (int i = 0; i < 3; i++)
      if (s.me.hand[i] > s.me.hand[pick]) pick = static_cast<Shape>(i);

    if (s.hasHistory() && s.me.coins >= 1 && s.oppLast().base == pick)
      return Move::plus(pick);

    return Move::play(pick);

    // Ideas worth trying:
    //  * counter their biggest stack:      Move::play(whatBeats(...))
    //  * farm a streak, then cash it in with shift() bluffs
    //  * expected value over s.legalMoves() using s.opp.hand as the forecast
    //  * 1-2 ply search with s.next(mine, theirs) over their likely replies
    //  * bomb when behind on cards — it turns losing trades into even ones
  }
};

RPS_MAIN(MyBot)
