// ============================================================================
//  rps.h — Advanced Tactical RPS: game rules + bot SDK (single header)
//
//  This file is the single source of truth for the game rules. The arena
//  engine, the unit tests, and every bot all include this same header, so
//  what your bot simulates is exactly what the engine executes.
//
//  Contestants: you only need the types marked with [BOT API] below.
//  See README.md for the full guide.
// ============================================================================
#pragma once

#include <array>
#include <cstdlib>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace rps {

// ---------------------------------------------------------------------------
// Shapes & upgrades  [BOT API]
// ---------------------------------------------------------------------------

enum Shape { ROCK = 0, PAPER = 1, SCISSORS = 2 };

enum Upgrade {
  NONE = 0,   // plain card, free
  PLUS = 1,   // x+ : beats the standard version of itself   (1 Crafticoin)
  SHIFT = 2,  // x? : fights disguised as another shape      (1 Crafticoin)
  BOMB = 3,   // x! : if it loses, destroys the winner too   (2 Crafticoins)
};

inline const char* shapeName(Shape s) {
  switch (s) {
    case ROCK: return "rock";
    case PAPER: return "paper";
    default: return "scissors";
  }
}
inline char shapeChar(Shape s) { return "rps"[static_cast<int>(s)]; }
inline bool shapeFromChar(char c, Shape& out) {
  if (c == 'r') out = ROCK;
  else if (c == 'p') out = PAPER;
  else if (c == 's') out = SCISSORS;
  else return false;
  return true;
}

// rock beats scissors, scissors beats paper, paper beats rock
inline Shape whatBeats(Shape s)   { return s == ROCK ? PAPER : s == PAPER ? SCISSORS : ROCK; }
inline Shape whatLosesTo(Shape s) { return s == ROCK ? SCISSORS : s == PAPER ? ROCK : PAPER; }
inline bool beats(Shape a, Shape b) { return whatLosesTo(a) == b; }

inline int upgradeCost(Upgrade u) { return u == NONE ? 0 : u == BOMB ? 2 : 1; }

// ---------------------------------------------------------------------------
// Moves  [BOT API]
// ---------------------------------------------------------------------------
// Text notation used everywhere (CLI, protocol, replays, UI):
//   "r"    plain rock          "p+"  upgraded paper
//   "s!"   scissors bomb       "r?p" rock disguised as paper

struct Move {
  Shape base = ROCK;      // the real card taken from your hand
  Upgrade upgrade = NONE;
  Shape disguise = ROCK;  // meaningful only when upgrade == SHIFT

  // The shape this card fights as this turn.
  Shape effective() const { return upgrade == SHIFT ? disguise : base; }

  std::string str() const {
    std::string s(1, shapeChar(base));
    if (upgrade == PLUS) s += '+';
    else if (upgrade == BOMB) s += '!';
    else if (upgrade == SHIFT) { s += '?'; s += shapeChar(disguise); }
    return s;
  }

  static bool parse(const std::string& t, Move& out) {
    if (t.empty() || t.size() > 3) return false;
    Move m;
    if (!shapeFromChar(t[0], m.base)) return false;
    if (t.size() == 2) {
      if (t[1] == '+') m.upgrade = PLUS;
      else if (t[1] == '!') m.upgrade = BOMB;
      else return false;
    } else if (t.size() == 3) {
      if (t[1] != '?') return false;
      if (!shapeFromChar(t[2], m.disguise)) return false;
      if (m.disguise == m.base) return false;  // must disguise as a DIFFERENT shape
      m.upgrade = SHIFT;
    }
    out = m;
    return true;
  }

  static Move play(Shape s) { Move m; m.base = s; return m; }
  static Move plus(Shape s) { Move m; m.base = s; m.upgrade = PLUS; return m; }
  static Move bomb(Shape s) { Move m; m.base = s; m.upgrade = BOMB; return m; }
  static Move shift(Shape base, Shape as) {
    Move m; m.base = base; m.upgrade = SHIFT; m.disguise = as; return m;
  }
};

// ---------------------------------------------------------------------------
// One side of the game  [BOT API]
// ---------------------------------------------------------------------------

struct Player {
  std::array<int, 3> hand {{7, 7, 7}};  // cards remaining, indexed by Shape
  int coins = 0;                        // Crafticoins
  bool hasLast = false;                 // false only before the first turn
  Shape lastBase = ROCK;                // BASE shape played last turn (disguises don't hide this)

  int total() const { return hand[0] + hand[1] + hand[2]; }
};

// Returns "" when the move is legal, otherwise a human-readable reason.
inline std::string validateMove(const Player& p, const Move& m) {
  if (m.upgrade == SHIFT && m.disguise == m.base)
    return "disguise must be a different shape than the card itself";
  if (p.hand[m.base] <= 0)
    return std::string("no ") + shapeName(m.base) + " left in hand";
  int cost = upgradeCost(m.upgrade);
  if (p.coins < cost)
    return "not enough Crafticoins (need " + std::to_string(cost) +
           ", have " + std::to_string(p.coins) + ")";
  return "";
}

// ---------------------------------------------------------------------------
// Turn resolution — THE rules  [BOT API: used via State::next]
// ---------------------------------------------------------------------------

struct TurnResult {
  int winner = 0;                        // +1 a won the clash, -1 b won, 0 tie
  bool aDestroyed = false, bDestroyed = false;
  int aEarned = 0, bEarned = 0;          // Crafticoins earned this turn (0 or 1)
};

// Applies one simultaneous turn to both players. Moves must already be valid.
// Order of operations: pay upgrade costs -> fight -> survivors return as their
// base shape -> coin combos pay out (so this turn's coin can't fund this turn).
inline TurnResult applyTurn(Player& a, Player& b, const Move& ma, const Move& mb) {
  TurnResult r;
  a.coins -= upgradeCost(ma.upgrade);
  b.coins -= upgradeCost(mb.upgrade);
  a.hand[ma.base]--;
  b.hand[mb.base]--;

  Shape ea = ma.effective(), eb = mb.effective();
  if (ea == eb) {
    // Same effective shape: x+ beats plain x; x+ vs x+ is a normal tie.
    bool pa = ma.upgrade == PLUS, pb = mb.upgrade == PLUS;
    r.winner = (pa && !pb) ? +1 : (pb && !pa) ? -1 : 0;
  } else {
    r.winner = beats(ea, eb) ? +1 : -1;
  }

  if (r.winner > 0) { r.bDestroyed = true; if (mb.upgrade == BOMB) r.aDestroyed = true; }
  if (r.winner < 0) { r.aDestroyed = true; if (ma.upgrade == BOMB) r.bDestroyed = true; }

  if (!r.aDestroyed) a.hand[ma.base]++;  // survivors return as plain base cards
  if (!r.bDestroyed) b.hand[mb.base]++;

  // Coin combo: base shape matches the base shape of the previous turn.
  // Pays out even if the card just died.
  if (a.hasLast && a.lastBase == ma.base) { a.coins++; r.aEarned = 1; }
  if (b.hasLast && b.lastBase == mb.base) { b.coins++; r.bEarned = 1; }
  a.hasLast = b.hasLast = true;
  a.lastBase = ma.base;
  b.lastBase = mb.base;
  return r;
}

// Turn-limit adjudication: most total cards wins ('a'/'b'), equal is a draw ('d').
inline char adjudicateByCards(const Player& a, const Player& b) {
  if (a.total() > b.total()) return 'a';
  if (b.total() > a.total()) return 'b';
  return 'd';
}

// ---------------------------------------------------------------------------
// Full game state from YOUR bot's perspective  [BOT API]
// ---------------------------------------------------------------------------

struct State {
  int turn = 1;      // 1-based; the turn you are currently choosing a move for
  Player me, opp;    // perfect information: you see everything
  std::vector<std::pair<Move, Move>> history;  // (my move, opp move) per past turn

  bool hasHistory() const { return !history.empty(); }
  Move myLast() const { return history.back().first; }
  Move oppLast() const { return history.back().second; }

  // Every legal move you can make right now (base plays + affordable upgrades).
  std::vector<Move> legalMoves() const {
    std::vector<Move> v;
    for (int i = 0; i < 3; i++) {
      Shape s = static_cast<Shape>(i);
      if (me.hand[i] <= 0) continue;
      v.push_back(Move::play(s));
      if (me.coins >= 1) {
        v.push_back(Move::plus(s));
        for (int d = 0; d < 3; d++)
          if (d != i) v.push_back(Move::shift(s, static_cast<Shape>(d)));
      }
      if (me.coins >= 2) v.push_back(Move::bomb(s));
    }
    return v;
  }

  // Simulate one turn: what the state looks like if I play `mine` and the
  // opponent plays `theirs`. Perfect for search bots (minimax/MCTS).
  State next(const Move& mine, const Move& theirs) const {
    State n = *this;
    applyTurn(n.me, n.opp, mine, theirs);
    n.turn++;
    n.history.emplace_back(mine, theirs);
    return n;
  }
};

// ---------------------------------------------------------------------------
// Your bot  [BOT API]
// ---------------------------------------------------------------------------

class Bot {
 public:
  virtual ~Bot() = default;
  virtual std::string name() { return "unnamed"; }
  // Called once per turn. Return any legal move — see State::legalMoves().
  virtual Move choose(const State& s) = 0;
};

// Shared RNG, seeded by the arena (env RPS_SEED) so games are reproducible.
inline std::mt19937& rng() {
  static std::mt19937 gen = [] {
    const char* e = std::getenv("RPS_SEED");
    if (e) return std::mt19937(static_cast<unsigned>(std::strtoul(e, nullptr, 10)));
    return std::mt19937(std::random_device{}());
  }();
  return gen;
}

// ---------------------------------------------------------------------------
// Protocol runner — you never need to read this. RPS_MAIN(YourBot) wires your
// bot to the arena over stdin/stdout.
// ---------------------------------------------------------------------------

namespace detail {
inline bool parseMoveToken(const std::string& tok, Move& out, bool& present) {
  if (tok == "-") { present = false; return true; }
  present = true;
  return Move::parse(tok, out);
}
}  // namespace detail

inline int runBot(Bot& bot) {
  std::string line;
  std::vector<std::pair<Move, Move>> history;
  while (std::getline(std::cin, line)) {
    std::istringstream in(line);
    std::string cmd;
    in >> cmd;
    if (cmd == "init") {
      std::cout << "name " << bot.name() << "\n" << std::flush;
    } else if (cmd == "state") {
      State s;
      std::string kw, myLastTok, oppLastTok;
      in >> kw >> s.turn;                                            // turn <t>
      in >> kw >> s.me.hand[0] >> s.me.hand[1] >> s.me.hand[2] >> s.me.coins;    // my ...
      in >> kw >> s.opp.hand[0] >> s.opp.hand[1] >> s.opp.hand[2] >> s.opp.coins; // opp ...
      in >> kw >> myLastTok >> kw >> oppLastTok;                     // mylast X opplast Y
      Move myLast, oppLast;
      bool haveMine = false, haveTheirs = false;
      detail::parseMoveToken(myLastTok, myLast, haveMine);
      detail::parseMoveToken(oppLastTok, oppLast, haveTheirs);
      if (haveMine && haveTheirs) history.emplace_back(myLast, oppLast);
      s.history = history;
      s.me.hasLast = haveMine;
      if (haveMine) s.me.lastBase = myLast.base;
      s.opp.hasLast = haveTheirs;
      if (haveTheirs) s.opp.lastBase = oppLast.base;
      Move m = bot.choose(s);
      std::cout << "move " << m.str() << "\n" << std::flush;
    } else if (cmd == "end") {
      break;
    }
  }
  return 0;
}

// Put this at the bottom of your bot file:  RPS_MAIN(MyBot)
#define RPS_MAIN(BotClass) \
  int main() {             \
    BotClass rpsBotInstance; \
    return ::rps::runBot(rpsBotInstance); \
  }

}  // namespace rps
