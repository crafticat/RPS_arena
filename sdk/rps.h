// ============================================================================
//  rps.h — Advanced Tactical RPS V2: game rules + bot SDK (single header)
//
//  V2 in one breath: every turn has a SHOP phase (buy at most one thing,
//  purchases are permanent, both purchases are revealed) and an ATTACK phase
//  (simultaneous card clash). Upgrades stack on specific cards; bombs are
//  kamikaze cards that trade 1-for-1; every 5th turn the clash loser also
//  burns their cheapest card of the shape they just played. The whole game
//  is deterministic — State::next() simulates the engine exactly.
//
//  This file is the single source of truth for the rules. The arena engine,
//  the unit tests, and every bot include this same header, so what your bot
//  simulates is exactly what the engine executes.
//
//  Contestants: you only need the types marked with [BOT API] below.
//  See README.md for the full guide.
// ============================================================================
#pragma once

#include <array>
#include <cstdlib>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace rps {

// ---------------------------------------------------------------------------
// Shapes & prices  [BOT API]
// ---------------------------------------------------------------------------

enum Shape { ROCK = 0, PAPER = 1, SCISSORS = 2 };

constexpr int PRICE_BOMB = 5;      // kamikaze card: trades with whatever it meets
constexpr int PRICE_CARD = 7;      // brand-new card of any shape (tier 0)
constexpr int PRICE_UP_FIRST = 2;  // upgrading a tier-0 card
constexpr int PRICE_UP_NEXT = 1;   // upgrading tier 1+, one tier at a time
constexpr int COMBO_COINS = 2;     // repeat your base shape -> +2 Crafticoins
constexpr int SPECIAL_EVERY = 5;   // every 5th turn: loser burns an extra card
constexpr int START_CARDS = 10;    // per shape

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

inline int upgradeCost(int currentTier) {
  return currentTier == 0 ? PRICE_UP_FIRST : PRICE_UP_NEXT;
}

// ---------------------------------------------------------------------------
// Attack — the card you fight with this turn  [BOT API]
// Notation: "r" (rock tier 0), "r3" (rock tier 3), "!" (bomb)
// ---------------------------------------------------------------------------

struct Attack {
  bool bomb = false;
  Shape shape = ROCK;  // meaningless when bomb
  int tier = 0;

  static Attack card(Shape s, int t = 0) { Attack a; a.shape = s; a.tier = t; return a; }
  static Attack bombCard() { Attack a; a.bomb = true; return a; }

  std::string str() const {
    if (bomb) return "!";
    std::string s(1, shapeChar(shape));
    if (tier > 0) s += std::to_string(tier);
    return s;
  }

  static bool parse(const std::string& t, Attack& out) {
    if (t.empty()) return false;
    if (t == "!") { out = bombCard(); return true; }
    Attack a;
    if (!shapeFromChar(t[0], a.shape)) return false;
    if (t.size() > 1) {
      for (size_t i = 1; i < t.size(); i++)
        if (t[i] < '0' || t[i] > '9') return false;
      if (t.size() > 3) return false;  // tiers stay well below 3 digits
      a.tier = std::atoi(t.c_str() + 1);
    }
    out = a;
    return true;
  }
};

// ---------------------------------------------------------------------------
// Shop — at most ONE purchase per turn  [BOT API]
// Notation: "-" nothing · "b" bomb (5) · "cr"/"cp"/"cs" new card (7)
//           "ur2" upgrade one of your rock tier-2 cards to tier 3 (2 if
//           tier 0, else 1)
// ---------------------------------------------------------------------------

struct Shop {
  enum Kind { NONE = 0, CARD = 1, BOMB = 2, UPGRADE = 3 };
  Kind kind = NONE;
  Shape shape = ROCK;  // CARD: what to buy · UPGRADE: which card
  int tier = 0;        // UPGRADE: the card's CURRENT tier

  static Shop none() { return Shop{}; }
  static Shop card(Shape s) { Shop x; x.kind = CARD; x.shape = s; return x; }
  static Shop bomb() { Shop x; x.kind = BOMB; return x; }
  static Shop upgrade(Shape s, int t) { Shop x; x.kind = UPGRADE; x.shape = s; x.tier = t; return x; }

  int cost() const {
    switch (kind) {
      case CARD: return PRICE_CARD;
      case BOMB: return PRICE_BOMB;
      case UPGRADE: return upgradeCost(tier);
      default: return 0;
    }
  }

  std::string str() const {
    switch (kind) {
      case NONE: return "-";
      case BOMB: return "b";
      case CARD: return std::string("c") + shapeChar(shape);
      default: return std::string("u") + shapeChar(shape) + std::to_string(tier);
    }
  }

  static bool parse(const std::string& t, Shop& out) {
    if (t == "-") { out = none(); return true; }
    if (t == "b") { out = bomb(); return true; }
    if (t.size() >= 2 && t[0] == 'c') {
      Shape s;
      if (t.size() != 2 || !shapeFromChar(t[1], s)) return false;
      out = card(s);
      return true;
    }
    if (t.size() >= 3 && t[0] == 'u') {
      Shape s;
      if (!shapeFromChar(t[1], s)) return false;
      for (size_t i = 2; i < t.size(); i++)
        if (t[i] < '0' || t[i] > '9') return false;
      if (t.size() > 4) return false;
      out = upgrade(s, std::atoi(t.c_str() + 2));
      return true;
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// One side of the game  [BOT API]
// Your hand is a multiset of (shape, tier) cards plus a count of bombs.
// ---------------------------------------------------------------------------

struct Player {
  // cards[shape] maps tier -> how many such cards you hold
  std::array<std::map<int, int>, 3> cards;
  int bombs = 0;
  int coins = 0;
  bool hasLast = false;   // false before your first non-bomb attack
  Shape lastBase = ROCK;  // base shape of your previous attack (bombs reset this)

  Player() { for (auto& m : cards) m[0] = START_CARDS; }

  int count(Shape s) const {
    int n = 0;
    for (const auto& [t, c] : cards[s]) n += c;
    return n;
  }
  int countAt(Shape s, int tier) const {
    auto it = cards[s].find(tier);
    return it == cards[s].end() ? 0 : it->second;
  }
  bool has(Shape s, int tier) const { return countAt(s, tier) > 0; }
  int maxTier(Shape s) const {
    int best = -1;
    for (const auto& [t, c] : cards[s]) if (c > 0 && t > best) best = t;
    return best;  // -1 when you have no cards of this shape
  }
  int total() const { return count(ROCK) + count(PAPER) + count(SCISSORS) + bombs; }

  void add(Shape s, int tier, int n = 1) { cards[s][tier] += n; }
  void remove(Shape s, int tier) {
    auto it = cards[s].find(tier);
    if (it != cards[s].end() && --it->second <= 0) cards[s].erase(it);
  }
};

// ---------------------------------------------------------------------------
// Validation  [BOT API]  — "" when legal, otherwise the reason
// ---------------------------------------------------------------------------

inline std::string validateShop(const Player& p, const Shop& s) {
  if (s.kind == Shop::NONE) return "";
  if (s.kind == Shop::UPGRADE && !p.has(s.shape, s.tier))
    return std::string("no ") + shapeName(s.shape) + " at tier " +
           std::to_string(s.tier) + " to upgrade";
  if (p.coins < s.cost())
    return "not enough Crafticoins (need " + std::to_string(s.cost()) +
           ", have " + std::to_string(p.coins) + ")";
  return "";
}

inline std::string validateAttack(const Player& p, const Attack& a) {
  if (a.bomb) {
    if (p.bombs <= 0) return "no bombs in hand";
    return "";
  }
  if (!p.has(a.shape, a.tier))
    return std::string("no ") + shapeName(a.shape) + " at tier " +
           std::to_string(a.tier) + " in hand";
  return "";
}

// Purchases resolve before the attack phase — you may play what you just
// bought or upgraded. Callers must validate first.
inline void applyShop(Player& p, const Shop& s) {
  p.coins -= s.cost();
  switch (s.kind) {
    case Shop::CARD: p.add(s.shape, 0); break;
    case Shop::BOMB: p.bombs++; break;
    case Shop::UPGRADE: p.remove(s.shape, s.tier); p.add(s.shape, s.tier + 1); break;
    default: break;
  }
}

// ---------------------------------------------------------------------------
// Combat resolution — THE rules  [BOT API: used via State::next]
// ---------------------------------------------------------------------------

struct TurnResult {
  int winner = 0;              // +1 a won the clash, -1 b won, 0 tie or bomb trade
  bool bombTrade = false;      // someone played a bomb: both played cards died
  bool aDestroyed = false, bDestroyed = false;
  int aEarned = 0, bEarned = 0;              // combo Crafticoins this turn
  bool special = false;                      // was this a mod-5 danger round?
  std::string aSpecialLost, bSpecialLost;    // extra card burned ("r2", "!", "")
};

namespace detail {
// Deterministic danger-round burn: the loser's lowest-tier card of the shape
// they just played; if none remain, the lowest-tier card overall (shapes in
// rock→paper→scissors order); if only bombs remain, a bomb. Returns the token.
inline std::string burnCard(Player& p, const Attack& played) {
  if (!played.bomb) {
    const auto& m = p.cards[played.shape];
    for (const auto& [tier, cnt] : m) {
      if (cnt > 0) {
        std::string tok = Attack::card(played.shape, tier).str();
        p.remove(played.shape, tier);
        return tok;
      }
    }
  }
  int bestShape = -1, bestTier = 0;
  for (int s = 0; s < 3; s++)
    for (const auto& [tier, cnt] : p.cards[s]) {
      if (cnt <= 0) continue;
      if (bestShape < 0 || tier < bestTier) { bestShape = s; bestTier = tier; }
      break;  // maps are tier-sorted: the first live entry is this shape's lowest
    }
  if (bestShape >= 0) {
    std::string tok = Attack::card(static_cast<Shape>(bestShape), bestTier).str();
    p.remove(static_cast<Shape>(bestShape), bestTier);
    return tok;
  }
  if (p.bombs > 0) { p.bombs--; return "!"; }
  return "";
}
}  // namespace detail

// Applies one attack phase. Both attacks must already be validated against
// post-shop hands. Fully deterministic — State::next() simulates the engine
// EXACTLY, danger-round burns included.
inline TurnResult applyCombat(Player& a, Player& b, const Attack& aa, const Attack& ab,
                              int turn) {
  TurnResult r;
  r.special = (turn % SPECIAL_EVERY == 0);

  // played cards leave the hands
  if (aa.bomb) a.bombs--; else a.remove(aa.shape, aa.tier);
  if (ab.bomb) b.bombs--; else b.remove(ab.shape, ab.tier);

  if (aa.bomb || ab.bomb) {
    // Bombs trade unconditionally: both played cards die, nobody wins/loses.
    r.bombTrade = true;
    r.aDestroyed = r.bDestroyed = true;
  } else if (aa.shape == ab.shape) {
    // Mirror: the tier ladder decides. Equal tiers tie.
    r.winner = aa.tier > ab.tier ? +1 : ab.tier > aa.tier ? -1 : 0;
  } else {
    // Different shapes: classic RPS — tiers never cross shapes.
    r.winner = beats(aa.shape, ab.shape) ? +1 : -1;
  }

  if (!r.bombTrade) {
    if (r.winner > 0) r.bDestroyed = true;
    if (r.winner < 0) r.aDestroyed = true;
  }

  // survivors return, keeping their tier (upgrades are permanent)
  if (!r.aDestroyed) a.add(aa.shape, aa.tier);
  if (!r.bDestroyed) b.add(ab.shape, ab.tier);

  // danger round: the clash LOSER burns one extra card — their cheapest card
  // of the shape they just played (ties and bomb trades have no loser)
  if (r.special && r.winner != 0) {
    Player& loser = r.winner > 0 ? b : a;
    const Attack& played = r.winner > 0 ? ab : aa;
    std::string& slot = r.winner > 0 ? r.bSpecialLost : r.aSpecialLost;
    slot = detail::burnCard(loser, played);
  }

  // combo income: repeat your base shape two turns running -> +2.
  // Pays even if your card just died. Bombs have no shape and break the chain.
  if (!aa.bomb && a.hasLast && a.lastBase == aa.shape) { a.coins += COMBO_COINS; r.aEarned = COMBO_COINS; }
  if (!ab.bomb && b.hasLast && b.lastBase == ab.shape) { b.coins += COMBO_COINS; r.bEarned = COMBO_COINS; }
  if (aa.bomb) { a.hasLast = false; } else { a.hasLast = true; a.lastBase = aa.shape; }
  if (ab.bomb) { b.hasLast = false; } else { b.hasLast = true; b.lastBase = ab.shape; }
  return r;
}

// Turn-limit adjudication: most total cards (bombs count) wins; equal = draw.
inline char adjudicateByCards(const Player& a, const Player& b) {
  if (a.total() > b.total()) return 'a';
  if (b.total() > a.total()) return 'b';
  return 'd';
}

// ---------------------------------------------------------------------------
// Full game state from YOUR bot's perspective  [BOT API]
// ---------------------------------------------------------------------------

struct TurnRecord {
  Attack myAtk, oppAtk;
  Shop myShop, oppShop;
};

struct State {
  int turn = 1;      // 1-based; the turn you are currently playing
  Player me, opp;    // perfect information: you see everything

  // During chooseAttack(), this turn's revealed purchases:
  bool shopped = false;      // true once the shop phase resolved
  Shop myShopNow, oppShopNow;

  std::vector<TurnRecord> history;  // completed turns, oldest first

  bool hasHistory() const { return !history.empty(); }
  Attack myLast() const { return history.back().myAtk; }
  Attack oppLast() const { return history.back().oppAtk; }
  bool isSpecialRound() const { return turn % SPECIAL_EVERY == 0; }

  // Every legal purchase right now (always includes Shop::none()).
  std::vector<Shop> legalShops() const {
    std::vector<Shop> v;
    v.push_back(Shop::none());
    for (int s = 0; s < 3; s++)
      for (const auto& [tier, cnt] : me.cards[s])
        if (cnt > 0 && me.coins >= upgradeCost(tier))
          v.push_back(Shop::upgrade(static_cast<Shape>(s), tier));
    if (me.coins >= PRICE_BOMB) v.push_back(Shop::bomb());
    if (me.coins >= PRICE_CARD)
      for (int s = 0; s < 3; s++) v.push_back(Shop::card(static_cast<Shape>(s)));
    return v;
  }

  // Every card you can attack with right now.
  std::vector<Attack> legalAttacks() const {
    std::vector<Attack> v;
    for (int s = 0; s < 3; s++)
      for (const auto& [tier, cnt] : me.cards[s])
        if (cnt > 0) v.push_back(Attack::card(static_cast<Shape>(s), tier));
    if (me.bombs > 0) v.push_back(Attack::bombCard());
    return v;
  }

  // Simulate the shop phase (both purchases must be legal).
  State afterShops(const Shop& mine, const Shop& theirs) const {
    State n = *this;
    applyShop(n.me, mine);
    applyShop(n.opp, theirs);
    n.shopped = true;
    n.myShopNow = mine;
    n.oppShopNow = theirs;
    return n;
  }

  // Simulate the attack phase -> the state you'd face next turn.
  // This is EXACT: the whole game is deterministic, danger burns included,
  // so search bots can trust it completely.
  State next(const Attack& mine, const Attack& theirs) const {
    State n = *this;
    applyCombat(n.me, n.opp, mine, theirs, n.turn);
    n.turn++;
    n.shopped = false;
    TurnRecord rec;
    rec.myAtk = mine;
    rec.oppAtk = theirs;
    rec.myShop = shopped ? myShopNow : Shop::none();
    rec.oppShop = shopped ? oppShopNow : Shop::none();
    n.history.push_back(rec);
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

  // Shop phase: buy at most one thing (default: save your coins).
  virtual Shop chooseShop(const State& s) { (void)s; return Shop::none(); }

  // Attack phase: s.myShopNow / s.oppShopNow hold this turn's purchases.
  virtual Attack chooseAttack(const State& s) = 0;
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

// "0:9,2:1" -> {0:9, 2:1};  "-" -> empty
inline void parseTierList(const std::string& tok, std::map<int, int>& out) {
  out.clear();
  if (tok == "-") return;
  std::istringstream in(tok);
  std::string part;
  while (std::getline(in, part, ',')) {
    size_t colon = part.find(':');
    if (colon == std::string::npos) continue;
    out[std::atoi(part.substr(0, colon).c_str())] = std::atoi(part.substr(colon + 1).c_str());
  }
}

inline std::string tierListStr(const std::map<int, int>& m) {
  std::string s;
  for (const auto& [t, c] : m) {
    if (c <= 0) continue;
    if (!s.empty()) s += ",";
    s += std::to_string(t) + ":" + std::to_string(c);
  }
  return s.empty() ? "-" : s;
}

inline void parseSide(std::istringstream& in, Player& p) {
  std::string r, pp, ss;
  in >> p.coins >> p.bombs >> r >> pp >> ss;
  parseTierList(r, p.cards[0]);
  parseTierList(pp, p.cards[1]);
  parseTierList(ss, p.cards[2]);
}

}  // namespace detail

inline int runBot(Bot& bot) {
  std::string line;
  std::vector<TurnRecord> history;
  bool havePending = false;
  TurnRecord pending;  // filled across the two phases of the current turn

  while (std::getline(std::cin, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    std::istringstream in(line);
    std::string cmd;
    in >> cmd;

    if (cmd == "init") {
      std::cout << "name " << bot.name() << "\n" << std::flush;

    } else if (cmd == "state") {
      State s;
      std::string kw, myLastTok, oppLastTok;
      in >> kw >> s.turn;             // turn <t>
      in >> kw;                       // my
      detail::parseSide(in, s.me);
      in >> kw;                       // opp
      detail::parseSide(in, s.opp);
      in >> kw >> myLastTok >> kw >> oppLastTok;

      // completed-turn bookkeeping: opp's attack arrives with the next state
      if (havePending && oppLastTok != "-") {
        Attack theirs;
        if (Attack::parse(oppLastTok, theirs)) {
          pending.oppAtk = theirs;
          history.push_back(pending);
        }
        havePending = false;
      }
      s.history = history;
      Attack myLast;
      if (myLastTok != "-" && Attack::parse(myLastTok, myLast) && !myLast.bomb) {
        s.me.hasLast = true;
        s.me.lastBase = myLast.shape;
      }
      Attack oppLast;
      if (oppLastTok != "-" && Attack::parse(oppLastTok, oppLast) && !oppLast.bomb) {
        s.opp.hasLast = true;
        s.opp.lastBase = oppLast.shape;
      }

      // ---- shop phase
      Shop myShop = bot.chooseShop(s);
      std::cout << "shop " << myShop.str() << "\n" << std::flush;

      // ---- reveal
      std::string reveal;
      if (!std::getline(std::cin, reveal)) break;
      while (!reveal.empty() && (reveal.back() == '\r' || reveal.back() == ' ')) reveal.pop_back();
      std::istringstream rin(reveal);
      std::string rcmd, rkw, mineTok, theirsTok;
      rin >> rcmd;
      if (rcmd == "end") break;
      rin >> rkw >> mineTok >> rkw >> theirsTok;  // shopped my <tok> opp <tok>
      Shop mine, theirs;
      Shop::parse(mineTok, mine);
      Shop::parse(theirsTok, theirs);
      State s2 = s.afterShops(mine, theirs);

      // ---- attack phase
      Attack atk = bot.chooseAttack(s2);
      std::cout << "attack " << atk.str() << "\n" << std::flush;
      pending.myAtk = atk;
      pending.myShop = mine;
      pending.oppShop = theirs;
      havePending = true;

    } else if (cmd == "end") {
      break;
    }
  }
  return 0;
}

// Put this at the bottom of your bot file:  RPS_MAIN(BotClass)
#define RPS_MAIN(BotClass) \
  int main() {             \
    BotClass rpsBotInstance; \
    return ::rps::runBot(rpsBotInstance); \
  }

}  // namespace rps
