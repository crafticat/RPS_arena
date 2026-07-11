// Unit tests for the Advanced Tactical RPS rules in sdk/rps.h.
// Build & run: make test
#include "rps.h"

#include <cstdio>
#include <string>

using namespace rps;

static int fails = 0, checks = 0;
#define CHECK(cond) \
  do { ++checks; if (!(cond)) { ++fails; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

// Player with given hand/coins; last >= 0 sets lastBase for coin-combo tests.
static Player P(int r, int p, int s, int coins, int last = -1) {
  Player pl;
  pl.hand = {r, p, s};
  pl.coins = coins;
  if (last >= 0) { pl.hasLast = true; pl.lastBase = static_cast<Shape>(last); }
  return pl;
}

static void testBeats() {
  CHECK(beats(ROCK, SCISSORS));
  CHECK(beats(SCISSORS, PAPER));
  CHECK(beats(PAPER, ROCK));
  CHECK(!beats(SCISSORS, ROCK));
  CHECK(!beats(PAPER, SCISSORS));
  CHECK(!beats(ROCK, PAPER));
  CHECK(!beats(ROCK, ROCK));
  CHECK(whatBeats(ROCK) == PAPER);
  CHECK(whatBeats(PAPER) == SCISSORS);
  CHECK(whatBeats(SCISSORS) == ROCK);
  CHECK(whatLosesTo(ROCK) == SCISSORS);
  CHECK(whatLosesTo(PAPER) == ROCK);
  CHECK(whatLosesTo(SCISSORS) == PAPER);
  CHECK(upgradeCost(NONE) == 0);
  CHECK(upgradeCost(PLUS) == 1);
  CHECK(upgradeCost(SHIFT) == 1);
  CHECK(upgradeCost(BOMB) == 2);
}

static void testParseAndStr() {
  Move m;
  CHECK(Move::parse("r", m) && m.base == ROCK && m.upgrade == NONE && m.str() == "r");
  CHECK(Move::parse("p+", m) && m.base == PAPER && m.upgrade == PLUS && m.str() == "p+");
  CHECK(Move::parse("s!", m) && m.base == SCISSORS && m.upgrade == BOMB && m.str() == "s!");
  CHECK(Move::parse("r?p", m) && m.base == ROCK && m.upgrade == SHIFT && m.disguise == PAPER && m.str() == "r?p");
  CHECK(!Move::parse("r?r", m));  // disguise must differ from base
  CHECK(!Move::parse("x", m));
  CHECK(!Move::parse("r?", m));
  CHECK(!Move::parse("r#", m));
  CHECK(!Move::parse("", m));
  CHECK(!Move::parse("rp", m));
  CHECK(!Move::parse("r?pp", m));
  CHECK(Move::shift(ROCK, PAPER).effective() == PAPER);
  CHECK(Move::plus(ROCK).effective() == ROCK);
  CHECK(Move::bomb(SCISSORS).effective() == SCISSORS);
}

static void testBasicCombat() {
  {  // win: winner's card returns, loser's is destroyed
    Player a = P(7,7,7,0), b = P(7,7,7,0);
    TurnResult t = applyTurn(a, b, Move::play(ROCK), Move::play(SCISSORS));
    CHECK(t.winner == 1);
    CHECK(!t.aDestroyed && t.bDestroyed);
    CHECK(a.total() == 21 && b.total() == 20);
    CHECK(b.hand[SCISSORS] == 6);
  }
  {  // tie: both return
    Player a = P(7,7,7,0), b = P(7,7,7,0);
    TurnResult t = applyTurn(a, b, Move::play(ROCK), Move::play(ROCK));
    CHECK(t.winner == 0 && !t.aDestroyed && !t.bDestroyed);
    CHECK(a.total() == 21 && b.total() == 21);
  }
}

static void testPlus() {
  {  // r+ beats r
    Player a = P(7,7,7,1), b = P(7,7,7,0);
    TurnResult t = applyTurn(a, b, Move::plus(ROCK), Move::play(ROCK));
    CHECK(t.winner == 1 && t.bDestroyed && !t.aDestroyed);
    CHECK(a.coins == 0);  // paid 1
  }
  {  // ...and symmetrically for b
    Player a = P(7,7,7,0), b = P(7,7,7,1);
    TurnResult t = applyTurn(a, b, Move::play(ROCK), Move::plus(ROCK));
    CHECK(t.winner == -1 && t.aDestroyed);
  }
  {  // r+ vs r+ is a plain tie
    Player a = P(7,7,7,1), b = P(7,7,7,1);
    TurnResult t = applyTurn(a, b, Move::plus(ROCK), Move::plus(ROCK));
    CHECK(t.winner == 0 && !t.aDestroyed && !t.bDestroyed);
  }
  {  // paper still beats r+
    Player a = P(7,7,7,0), b = P(7,7,7,1);
    TurnResult t = applyTurn(a, b, Move::play(PAPER), Move::plus(ROCK));
    CHECK(t.winner == 1 && t.bDestroyed);
  }
  {  // r+ still beats scissors like a normal rock
    Player a = P(7,7,7,1), b = P(7,7,7,0);
    TurnResult t = applyTurn(a, b, Move::plus(ROCK), Move::play(SCISSORS));
    CHECK(t.winner == 1 && t.bDestroyed && !t.aDestroyed);
    CHECK(a.total() == 21);
  }
}

static void testShift() {
  {  // rock disguised as paper beats rock, survivor returns as ROCK
    Player a = P(7,7,7,1), b = P(7,7,7,0);
    TurnResult t = applyTurn(a, b, Move::shift(ROCK, PAPER), Move::play(ROCK));
    CHECK(t.winner == 1 && t.bDestroyed);
    CHECK(a.hand[ROCK] == 7 && a.total() == 21);  // came back as its base shape
    CHECK(b.hand[ROCK] == 6);
  }
  {  // the disguise is what fights: rock-as-paper loses to scissors
    Player a = P(7,7,7,1), b = P(7,7,7,0);
    TurnResult t = applyTurn(a, b, Move::shift(ROCK, PAPER), Move::play(SCISSORS));
    CHECK(t.winner == -1 && t.aDestroyed);
    CHECK(a.hand[ROCK] == 6);  // the real rock died
  }
  {  // r+ beats paper disguised as rock (same effective shape, plus wins)
    Player a = P(7,7,7,1), b = P(7,7,7,1);
    TurnResult t = applyTurn(a, b, Move::plus(ROCK), Move::shift(PAPER, ROCK));
    CHECK(t.winner == 1 && t.bDestroyed);
    CHECK(b.hand[PAPER] == 6);
  }
}

static void testBomb() {
  {  // bomb loses -> mutual destruction
    Player a = P(7,7,7,2), b = P(7,7,7,0);
    TurnResult t = applyTurn(a, b, Move::bomb(ROCK), Move::play(PAPER));
    CHECK(t.winner == -1);
    CHECK(t.aDestroyed && t.bDestroyed);
    CHECK(a.hand[ROCK] == 6 && b.hand[PAPER] == 6);
    CHECK(a.coins == 0);  // paid 2
  }
  {  // bomb ties -> returns as a normal card
    Player a = P(7,7,7,2), b = P(7,7,7,0);
    TurnResult t = applyTurn(a, b, Move::bomb(ROCK), Move::play(ROCK));
    CHECK(t.winner == 0 && !t.aDestroyed && !t.bDestroyed);
    CHECK(a.total() == 21 && b.total() == 21);
  }
  {  // bomb wins -> behaves like a normal win
    Player a = P(7,7,7,2), b = P(7,7,7,0);
    TurnResult t = applyTurn(a, b, Move::bomb(ROCK), Move::play(SCISSORS));
    CHECK(t.winner == 1 && !t.aDestroyed && t.bDestroyed);
    CHECK(a.total() == 21);
  }
  {  // bomb vs bomb, one loses: winner's card is blown up too
    Player a = P(7,7,7,2), b = P(7,7,7,2);
    TurnResult t = applyTurn(a, b, Move::bomb(ROCK), Move::bomb(PAPER));
    CHECK(t.winner == -1);
    CHECK(t.aDestroyed && t.bDestroyed);
    CHECK(a.total() == 20 && b.total() == 20);
  }
  {  // r+ beats r! (same shape, plus wins) but the bomb still explodes
    Player a = P(7,7,7,1), b = P(7,7,7,2);
    TurnResult t = applyTurn(a, b, Move::plus(ROCK), Move::bomb(ROCK));
    CHECK(t.winner == 1);
    CHECK(t.aDestroyed && t.bDestroyed);
    CHECK(a.total() == 20 && b.total() == 20);
  }
}

static void testCoins() {
  {  // no combo possible on turn 1
    Player a = P(7,7,7,0), b = P(7,7,7,0);
    TurnResult t = applyTurn(a, b, Move::play(ROCK), Move::play(PAPER));
    CHECK(t.aEarned == 0 && t.bEarned == 0);
    CHECK(a.coins == 0 && b.coins == 0);
  }
  {  // repeat base shape -> +1; different shape -> nothing
    Player a = P(7,7,7,0, ROCK), b = P(7,7,7,0, ROCK);
    TurnResult t = applyTurn(a, b, Move::play(ROCK), Move::play(PAPER));
    CHECK(t.aEarned == 1 && a.coins == 1);
    CHECK(t.bEarned == 0 && b.coins == 0);
  }
  {  // the combo counts even when the played card is destroyed
    Player a = P(7,7,7,0, ROCK), b = P(7,7,7,0);
    TurnResult t = applyTurn(a, b, Move::play(ROCK), Move::play(PAPER));
    CHECK(t.winner == -1 && t.aDestroyed);
    CHECK(t.aEarned == 1 && a.coins == 1);
  }
  {  // disguises farm with the ORIGINAL base shape
    Player a = P(7,7,7,1, ROCK), b = P(7,7,7,0);
    TurnResult t = applyTurn(a, b, Move::shift(ROCK, PAPER), Move::play(SCISSORS));
    CHECK(t.aEarned == 1);
    CHECK(a.coins == 1);  // paid 1 for the shift, earned 1 back
    CHECK(a.lastBase == ROCK);  // history remembers the base, not the disguise
  }
  {  // spend-then-earn: this turn's coin can't fund this turn's upgrade
    Player a = P(7,7,7,1, ROCK), b = P(7,7,7,0);
    applyTurn(a, b, Move::plus(ROCK), Move::play(ROCK));
    CHECK(a.coins == 1);  // 1 - 1 (plus) + 1 (combo)
  }
}

static void testValidate() {
  CHECK(!validateMove(P(0,7,7,0), Move::play(ROCK)).empty());       // no rocks left
  CHECK(!validateMove(P(7,7,7,0), Move::plus(ROCK)).empty());       // can't afford +
  CHECK(!validateMove(P(7,7,7,1), Move::bomb(ROCK)).empty());       // bomb needs 2
  CHECK(validateMove(P(7,7,7,1), Move::plus(ROCK)).empty());
  CHECK(validateMove(P(7,7,7,1), Move::shift(ROCK, PAPER)).empty());
  CHECK(validateMove(P(7,7,7,2), Move::bomb(ROCK)).empty());
  CHECK(validateMove(P(1,0,0,0), Move::play(ROCK)).empty());
  Move sameDisguise;  // hand-built r?r must be rejected by validation too
  sameDisguise.base = ROCK; sameDisguise.upgrade = SHIFT; sameDisguise.disguise = ROCK;
  CHECK(!validateMove(P(7,7,7,5), sameDisguise).empty());
}

static void testLegalMoves() {
  State s;
  CHECK(s.legalMoves().size() == 3);       // 0 coins: base plays only
  s.me.coins = 1;
  CHECK(s.legalMoves().size() == 12);      // + plus and 2 disguises per shape
  s.me.coins = 2;
  CHECK(s.legalMoves().size() == 15);      // + bombs
  s.me.hand[ROCK] = 0;
  CHECK(s.legalMoves().size() == 10);      // rock options all gone
}

static void testStateNext() {
  State s;
  State n = s.next(Move::play(ROCK), Move::play(SCISSORS));
  CHECK(n.turn == 2);
  CHECK(n.me.total() == 21 && n.opp.total() == 20);
  CHECK(n.history.size() == 1);
  CHECK(n.history[0].first.str() == "r" && n.history[0].second.str() == "s");
  State n2 = n.next(Move::play(ROCK), Move::play(SCISSORS));
  CHECK(n2.me.coins == 1);   // rock -> rock combo
  CHECK(n2.opp.coins == 1);  // opponent repeated scissors (losing doesn't matter)
  CHECK(n2.opp.total() == 19);
  CHECK(n2.turn == 3 && n2.history.size() == 2);
}

static void testAdjudicate() {
  CHECK(adjudicateByCards(P(3,3,3,0), P(3,3,2,0)) == 'a');
  CHECK(adjudicateByCards(P(1,0,0,0), P(0,2,2,0)) == 'b');
  CHECK(adjudicateByCards(P(2,2,2,0), P(3,3,0,0)) == 'd');
}

int main() {
  testBeats();
  testParseAndStr();
  testBasicCombat();
  testPlus();
  testShift();
  testBomb();
  testCoins();
  testValidate();
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
