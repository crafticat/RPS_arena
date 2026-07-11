// Farmer — the economy demo. Streaks one shape for +2 combo income and
// pours the coins into upgrading its favourite card: the tier ladder wins
// every mirror against a lazier shopper. Beware: that fat card is exactly
// what paper (or a bomb, or a danger round) loves to eat.
#include "rps.h"

struct Farmer : rps::Bot {
  std::string name() override { return "Farmer"; }

  static rps::Shape favourite(const rps::State& s) {
    using namespace rps;
    if (s.hasHistory() && !s.myLast().bomb && s.me.count(s.myLast().shape) > 0)
      return s.myLast().shape;  // keep the combo alive
    Shape best = ROCK;
    for (int i = 0; i < 3; i++)
      if (s.me.count(static_cast<Shape>(i)) > s.me.count(best)) best = static_cast<Shape>(i);
    return best;
  }

  rps::Shop chooseShop(const rps::State& s) override {
    using namespace rps;
    Shape fav = favourite(s);
    int top = s.me.maxTier(fav);
    if (top >= 0 && s.me.coins >= upgradeCost(top)) return Shop::upgrade(fav, top);
    return Shop::none();
  }

  rps::Attack chooseAttack(const rps::State& s) override {
    using namespace rps;
    Shape fav = favourite(s);
    int top = s.me.maxTier(fav);
    if (top >= 0) return Attack::card(fav, top);  // lead with the invested card
    return s.legalAttacks().front();
  }
};

RPS_MAIN(Farmer)
