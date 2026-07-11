// Counter — assumes you repeat your last shape (coin combos make that
// tempting) and plays what beats it, with its cheapest card. Punishes
// farmers and one-track bots hard.
#include "rps.h"

struct Counter : rps::Bot {
  std::string name() override { return "Counter"; }

  static rps::Attack cheapest(const rps::State& s, rps::Shape sh) {
    for (const auto& [tier, cnt] : s.me.cards[sh])
      if (cnt > 0) return rps::Attack::card(sh, tier);
    return rps::Attack::card(sh, -1);
  }

  rps::Shop chooseShop(const rps::State& s) override {
    using namespace rps;
    // stock up on the shape that beats their biggest pile
    if (s.me.coins >= PRICE_CARD) {
      Shape big = ROCK;
      for (int i = 0; i < 3; i++)
        if (s.opp.count(static_cast<Shape>(i)) > s.opp.count(big)) big = static_cast<Shape>(i);
      return Shop::card(whatBeats(big));
    }
    return Shop::none();
  }

  rps::Attack chooseAttack(const rps::State& s) override {
    using namespace rps;
    Shape target;
    if (s.hasHistory() && !s.oppLast().bomb) {
      target = s.oppLast().shape;
    } else {
      target = ROCK;
      for (int i = 0; i < 3; i++)
        if (s.opp.count(static_cast<Shape>(i)) > s.opp.count(target)) target = static_cast<Shape>(i);
    }
    Attack a = cheapest(s, whatBeats(target));
    if (a.tier >= 0) return a;
    return s.legalAttacks().front();
  }
};

RPS_MAIN(Counter)
