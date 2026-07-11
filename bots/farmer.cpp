// Farmer — the economy demo. Repeats one shape to farm Crafticoins, then
// spends them on x+ whenever the opponent looks like they'll mirror.
// Note the infinite-money trick: a repeated x+ costs 1 but the combo pays
// 1 right back, so a mirror war never drains the wallet.
#include "rps.h"

struct Farmer : rps::Bot {
  std::string name() override { return "Farmer"; }

  rps::Move choose(const rps::State& s) override {
    using namespace rps;
    Shape streak = ROCK;
    for (int i = 0; i < 3; i++)
      if (s.me.hand[i] > s.me.hand[streak]) streak = static_cast<Shape>(i);
    if (s.hasHistory()) {
      Shape prev = s.myLast().base;
      if (s.me.hand[prev] > 0) streak = prev;  // keep the combo alive
    }
    if (s.me.coins >= 1 && s.hasHistory() && s.oppLast().base == streak)
      return Move::plus(streak);  // win the mirror
    return Move::play(streak);
  }
};

RPS_MAIN(Farmer)
