// Gambit — the benchmark bot to beat. Predicts the opponent's next shape
// from their hand composition and habits, then maximizes expected card swing
// over every legal move. Deliberately ignores bombs — that's your edge.
#include "rps.h"

#include <algorithm>

struct Gambit : rps::Bot {
  std::string name() override { return "Gambit"; }

  rps::Move choose(const rps::State& s) override {
    using namespace rps;

    // 1. Opponent model: base rate = hand composition, plus a bump on
    //    repeating their last base shape (coin farming is popular).
    double w[3];
    double tot = std::max(1, s.opp.total());
    for (int i = 0; i < 3; i++) w[i] = s.opp.hand[i] / tot;
    if (s.hasHistory()) {
      Shape ob = s.oppLast().base;
      if (s.opp.hand[ob] > 0) w[ob] += 0.35;
    }
    double sum = w[0] + w[1] + w[2];
    for (double& x : w) x /= sum;

    // 2. Expected value of each legal move against that mix.
    Move best = Move::play(ROCK);
    double bestEv = -1e9;
    for (const Move& m : s.legalMoves()) {
      if (m.upgrade == BOMB) continue;
      double ev = 0;
      for (int o = 0; o < 3; o++) {
        if (w[o] <= 0) continue;
        Shape mine = m.effective(), theirs = static_cast<Shape>(o);
        double val;
        if (mine == theirs) val = m.upgrade == PLUS ? +1.0 : 0.0;
        else val = beats(mine, theirs) ? +1.0 : -1.0;
        ev += w[o] * val;
      }
      ev -= 0.15 * upgradeCost(m.upgrade);                           // coins have value
      if (s.hasHistory() && m.base == s.myLast().base) ev += 0.22;   // combo income
      ev += 0.02 * s.me.hand[m.base];                                // spend from depth
      if (ev > bestEv) { bestEv = ev; best = m; }
    }
    return best;
  }
};

RPS_MAIN(Gambit)
