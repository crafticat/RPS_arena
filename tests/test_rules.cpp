// Unit tests for the Advanced Tactical RPS **V2** rules in sdk/rps.h.
// V2: shop phase (1 purchase) + attack phase, permanent per-card upgrades,
// bomb cards, mod-5 danger rounds, 2-coin combos. Build & run: make test
#include "rps.h"

#include <cstdio>
#include <random>
#include <string>

using namespace rps;

static int fails = 0, checks = 0;
#define CHECK(cond) \
  do { ++checks; if (!(cond)) { ++fails; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

// Empty-handed player; add what each test needs.
static Player P(int coins = 0) {
  Player p;
  for (auto& m : p.cards) m.clear();
  p.coins = coins;
  return p;
}

static void testDefaults() {
  Player p;
  CHECK(p.count(ROCK) == 10 && p.count(PAPER) == 10 && p.count(SCISSORS) == 10);
  CHECK(p.total() == 30);
  CHECK(p.coins == 0 && p.bombs == 0);
  CHECK(PRICE_BOMB == 5 && PRICE_CARD == 7);
  CHECK(upgradeCost(0) == 2 && upgradeCost(1) == 1 && upgradeCost(4) == 1);
  CHECK(COMBO_COINS == 2 && SPECIAL_EVERY == 5);
}

static void testTokens() {
  Attack a;
  CHECK(Attack::parse("r", a) && !a.bomb && a.shape == ROCK && a.tier == 0 && a.str() == "r");
  CHECK(Attack::parse("p3", a) && a.shape == PAPER && a.tier == 3 && a.str() == "p3");
  CHECK(Attack::parse("s12", a) && a.tier == 12);
  CHECK(Attack::parse("!", a) && a.bomb && a.str() == "!");
  CHECK(!Attack::parse("x", a));
  CHECK(!Attack::parse("r-1", a));
  CHECK(!Attack::parse("", a));
  CHECK(!Attack::parse("!2", a));

  Shop s;
  CHECK(Shop::parse("-", s) && s.kind == Shop::NONE && s.str() == "-");
  CHECK(Shop::parse("b", s) && s.kind == Shop::BOMB && s.str() == "b");
  CHECK(Shop::parse("cr", s) && s.kind == Shop::CARD && s.shape == ROCK && s.str() == "cr");
  CHECK(Shop::parse("cp", s) && s.shape == PAPER);
  CHECK(Shop::parse("ur2", s) && s.kind == Shop::UPGRADE && s.shape == ROCK && s.tier == 2 && s.str() == "ur2");
  CHECK(Shop::parse("us0", s) && s.shape == SCISSORS && s.tier == 0);
  CHECK(!Shop::parse("c", s));
  CHECK(!Shop::parse("cx", s));
  CHECK(!Shop::parse("ur", s));
  CHECK(!Shop::parse("q", s));
}

static void testShopValidationAndApply() {
  {  // card purchase
    Player p = P(7);
    p.add(ROCK, 0, 1);
    CHECK(validateShop(p, Shop::card(PAPER)).empty());
    applyShop(p, Shop::card(PAPER));
    CHECK(p.coins == 0 && p.count(PAPER) == 1 && p.total() == 2);
    CHECK(!validateShop(P(6), Shop::card(ROCK)).empty());  // can't afford
  }
  {  // bomb purchase
    Player p = P(5);
    CHECK(validateShop(p, Shop::bomb()).empty());
    applyShop(p, Shop::bomb());
    CHECK(p.coins == 0 && p.bombs == 1 && p.total() == 1);  // bombs are cards
    CHECK(!validateShop(P(4), Shop::bomb()).empty());
  }
  {  // upgrade: tier 0 costs 2, later tiers cost 1, must own the card
    Player p = P(3);
    p.add(ROCK, 0, 2);
    CHECK(validateShop(p, Shop::upgrade(ROCK, 0)).empty());
    applyShop(p, Shop::upgrade(ROCK, 0));
    CHECK(p.coins == 1 && p.has(ROCK, 1) && p.countAt(ROCK, 0) == 1);
    applyShop(p, Shop::upgrade(ROCK, 1));  // 1 coin: tier 1 -> 2
    CHECK(p.coins == 0 && p.has(ROCK, 2) && !p.has(ROCK, 1));
    CHECK(!validateShop(p, Shop::upgrade(ROCK, 5)).empty());   // no such card
    CHECK(!validateShop(P(1), Shop::upgrade(ROCK, 0)).empty()); // tier0 needs 2
  }
  {  // doing nothing is always legal
    CHECK(validateShop(P(0), Shop::none()).empty());
  }
}

static void testAttackValidation() {
  Player p = P();
  p.add(ROCK, 2, 1);
  p.bombs = 1;
  CHECK(validateAttack(p, Attack::card(ROCK, 2)).empty());
  CHECK(validateAttack(p, Attack::bombCard()).empty());
  CHECK(!validateAttack(p, Attack::card(ROCK, 0)).empty());   // owns only tier 2
  CHECK(!validateAttack(p, Attack::card(PAPER, 0)).empty());
  Player q = P();
  q.add(PAPER, 0, 1);
  CHECK(!validateAttack(q, Attack::bombCard()).empty());      // no bomb owned
}

// non-special turn helper
static TurnResult clashAt(Player& a, Player& b, Attack aa, Attack ab, int turn = 1) {
  std::mt19937 rng(7);
  return applyCombat(a, b, aa, ab, turn, &rng);
}

static void testShapeTrumpsTier() {
  {  // paper flat beats rock+5 — tiers never cross shapes
    Player a = P(), b = P();
    a.add(PAPER, 0, 1);
    b.add(ROCK, 5, 1);
    TurnResult r = clashAt(a, b, Attack::card(PAPER, 0), Attack::card(ROCK, 5));
    CHECK(r.winner == 1 && r.bDestroyed && !r.aDestroyed);
    CHECK(a.total() == 1 && b.total() == 0);   // the +5 investment is gone
  }
}

static void testTierLadder() {
  {  // same shape: higher tier wins
    Player a = P(), b = P();
    a.add(ROCK, 2, 1);
    b.add(ROCK, 1, 1);
    TurnResult r = clashAt(a, b, Attack::card(ROCK, 2), Attack::card(ROCK, 1));
    CHECK(r.winner == 1 && r.bDestroyed);
    CHECK(a.has(ROCK, 2));                     // winner returns WITH its tier
    CHECK(b.total() == 0);
  }
  {  // equal tiers tie; both return with tiers intact
    Player a = P(), b = P();
    a.add(SCISSORS, 3, 1);
    b.add(SCISSORS, 3, 1);
    TurnResult r = clashAt(a, b, Attack::card(SCISSORS, 3), Attack::card(SCISSORS, 3));
    CHECK(r.winner == 0 && !r.aDestroyed && !r.bDestroyed);
    CHECK(a.has(SCISSORS, 3) && b.has(SCISSORS, 3));
  }
}

static void testBombs() {
  {  // bomb vs card: pure trade, both played cards die, nobody "wins"
    Player a = P(), b = P();
    a.bombs = 1;
    b.add(ROCK, 4, 1);
    b.add(PAPER, 0, 1);
    TurnResult r = clashAt(a, b, Attack::bombCard(), Attack::card(ROCK, 4));
    CHECK(r.winner == 0 && r.bombTrade);
    CHECK(r.aDestroyed && r.bDestroyed);
    CHECK(a.bombs == 0 && a.total() == 0);
    CHECK(!b.has(ROCK, 4) && b.total() == 1);  // fat rock traded away
  }
  {  // bomb vs bomb: both die
    Player a = P(), b = P();
    a.bombs = 1; b.bombs = 1;
    TurnResult r = clashAt(a, b, Attack::bombCard(), Attack::bombCard());
    CHECK(r.bombTrade && a.bombs == 0 && b.bombs == 0);
  }
  {  // bombs break the combo chain
    Player a = P(), b = P();
    a.add(ROCK, 0, 3); a.bombs = 1;
    b.add(PAPER, 0, 3); b.bombs = 1;
    clashAt(a, b, Attack::card(ROCK, 0), Attack::card(PAPER, 0), 1);
    TurnResult r2 = clashAt(a, b, Attack::bombCard(), Attack::card(PAPER, 0), 2);
    CHECK(r2.aEarned == 0);                    // bomb has no shape: no combo for a
    CHECK(r2.bEarned == COMBO_COINS);          // b repeated paper (combo pays on trades too)
    TurnResult r3 = clashAt(a, b, Attack::card(ROCK, 0), Attack::card(PAPER, 0), 3);
    CHECK(r3.aEarned == 0);                    // chain was broken by the bomb
  }
}

static void testCoins() {
  {  // +2 for repeating base shape; nothing on turn 1; pays even on a loss
    Player a = P(), b = P();
    a.add(ROCK, 0, 5);
    b.add(PAPER, 0, 5);
    TurnResult r1 = clashAt(a, b, Attack::card(ROCK, 0), Attack::card(PAPER, 0), 1);
    CHECK(r1.aEarned == 0 && r1.bEarned == 0);
    TurnResult r2 = clashAt(a, b, Attack::card(ROCK, 0), Attack::card(PAPER, 0), 2);
    CHECK(r2.aEarned == 2 && a.coins == 2);    // a lost the clash but repeated rock
    CHECK(r2.bEarned == 2);
  }
  {  // different tier, same base shape still combos (it's the same shape)
    Player a = P(), b = P();
    a.add(ROCK, 0, 2); a.add(ROCK, 1, 1);
    b.add(SCISSORS, 0, 3);
    clashAt(a, b, Attack::card(ROCK, 0), Attack::card(SCISSORS, 0), 1);
    TurnResult r = clashAt(a, b, Attack::card(ROCK, 1), Attack::card(SCISSORS, 0), 2);
    CHECK(r.aEarned == 2);
  }
}

static void testSpecialRound() {
  {  // on turns %5==0 the clash loser burns an extra random card
    Player a = P(), b = P();
    a.add(ROCK, 0, 1);
    b.add(SCISSORS, 0, 4);
    b.bombs = 1;                                // victims include bombs
    TurnResult r = clashAt(a, b, Attack::card(ROCK, 0), Attack::card(SCISSORS, 0), 5);
    CHECK(r.special);
    CHECK(r.winner == 1);
    CHECK(b.total() == 3);                      // played card + one extra gone
    CHECK(!r.bSpecialLost.empty());
    CHECK(r.aSpecialLost.empty());
  }
  {  // same clash on a normal turn: only the played card dies
    Player a = P(), b = P();
    a.add(ROCK, 0, 1);
    b.add(SCISSORS, 0, 4);
    TurnResult r = clashAt(a, b, Attack::card(ROCK, 0), Attack::card(SCISSORS, 0), 4);
    CHECK(!r.special && b.total() == 3 + 0);    // 4 - played(destroyed) = 3
  }
  {  // ties and bomb trades have no loser: no extra burn
    Player a = P(), b = P();
    a.add(ROCK, 0, 2);
    b.add(ROCK, 0, 2);
    TurnResult r = clashAt(a, b, Attack::card(ROCK, 0), Attack::card(ROCK, 0), 5);
    CHECK(r.special && r.winner == 0 && a.total() == 2 && b.total() == 2);
    Player c = P(), d = P();
    c.bombs = 1; c.add(PAPER, 0, 3);
    d.add(ROCK, 0, 3);
    TurnResult r2 = clashAt(c, d, Attack::bombCard(), Attack::card(ROCK, 0), 5);
    CHECK(r2.bombTrade && c.total() == 3 && d.total() == 2);
  }
  {  // loser eliminated by the clash itself: nothing left to burn, no crash
    Player a = P(), b = P();
    a.add(ROCK, 0, 1);
    b.add(SCISSORS, 0, 1);
    TurnResult r = clashAt(a, b, Attack::card(ROCK, 0), Attack::card(SCISSORS, 0), 5);
    CHECK(r.winner == 1 && b.total() == 0 && r.bSpecialLost.empty());
  }
}

static void testLegalMoves() {
  State s;
  for (auto& m : s.me.cards) m.clear();
  s.me.add(ROCK, 0, 2);
  s.me.add(ROCK, 2, 1);
  s.me.add(PAPER, 0, 1);
  s.me.coins = 0;
  s.me.bombs = 0;
  CHECK(s.legalAttacks().size() == 3);          // r0, r2, p0
  s.me.bombs = 1;
  CHECK(s.legalAttacks().size() == 4);          // + bomb
  CHECK(s.legalShops().size() == 1);            // broke: only "-"
  s.me.coins = 1;
  CHECK(s.legalShops().size() == 2);            // - , ur2 (tier2 -> 1 coin)
  s.me.coins = 2;
  CHECK(s.legalShops().size() == 4);            // - , ur0, up0, ur2
  s.me.coins = 5;
  CHECK(s.legalShops().size() == 5);            // + bomb
  s.me.coins = 7;
  CHECK(s.legalShops().size() == 8);            // + 3 card buys
}

static void testStateNext() {
  State s;                                       // 10/10/10 both sides
  State n = s.next(Attack::card(ROCK, 0), Attack::card(SCISSORS, 0));
  CHECK(n.turn == 2);
  CHECK(n.me.total() == 30 && n.opp.total() == 29);
  CHECK(n.history.size() == 1);
  State n2 = n.next(Attack::card(ROCK, 0), Attack::card(SCISSORS, 0));
  CHECK(n2.me.coins == 2 && n2.opp.coins == 2);  // both combo'd (+2 each)
  // simulation note: next() skips the mod-5 random burn by design
  State s5 = s;
  s5.turn = 5;
  State n5 = s5.next(Attack::card(ROCK, 0), Attack::card(SCISSORS, 0));
  CHECK(n5.opp.total() == 29);                   // only the played card died
}

static void testAdjudicate() {
  Player a = P(), b = P();
  a.add(ROCK, 0, 3);
  b.add(PAPER, 0, 2);
  b.bombs = 1;
  CHECK(adjudicateByCards(a, b) == 'd');         // bombs count: 3 vs 3
  b.bombs = 0;
  CHECK(adjudicateByCards(a, b) == 'a');
}

int main() {
  testDefaults();
  testTokens();
  testShopValidationAndApply();
  testAttackValidation();
  testShapeTrumpsTier();
  testTierLadder();
  testBombs();
  testCoins();
  testSpecialRound();
  testLegalMoves();
  testStateNext();
  testAdjudicate();
  if (fails == 0) {
    std::printf("ALL TESTS PASSED (%d checks)\n", checks);
    return 0;
  }
  std::printf("%d/%d CHECKS FAILED\n", fails, checks);
  return 1;
}
