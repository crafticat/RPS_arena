// Copycat — mirrors your last attack shape, and even copies your shopping
// habits when it can afford them. Converges onto ties against streaks.
#include "rps.h"

struct Copycat : rps::Bot {
  std::string name() override { return "Copycat"; }

  static rps::Attack cheapest(const rps::State& s, rps::Shape sh) {
    for (const auto& [tier, cnt] : s.me.cards[sh])
      if (cnt > 0) return rps::Attack::card(sh, tier);
    return rps::Attack::card(sh, -1);  // marker: none of this shape
  }

  rps::Shop chooseShop(const rps::State& s) override {
    using namespace rps;
    if (!s.hasHistory()) return Shop::none();
    Shop theirs = s.history.back().oppShop;
    if (theirs.kind == Shop::NONE) return Shop::none();
    // copy the KIND of purchase, adapted to my own hand
    if (theirs.kind == Shop::BOMB && s.me.coins >= PRICE_BOMB) return Shop::bomb();
    if (theirs.kind == Shop::CARD && s.me.coins >= PRICE_CARD) return Shop::card(theirs.shape);
    if (theirs.kind == Shop::UPGRADE) {
      for (const auto& [tier, cnt] : s.me.cards[theirs.shape])
        if (cnt > 0 && s.me.coins >= upgradeCost(tier))
          return Shop::upgrade(theirs.shape, tier);
    }
    return Shop::none();
  }

  rps::Attack chooseAttack(const rps::State& s) override {
    using namespace rps;
    if (s.hasHistory() && !s.oppLast().bomb) {
      Attack a = cheapest(s, s.oppLast().shape);
      if (a.tier >= 0) return a;
    }
    Shape best = ROCK;
    for (int i = 0; i < 3; i++)
      if (s.me.count(static_cast<Shape>(i)) > s.me.count(best)) best = static_cast<Shape>(i);
    Attack a = cheapest(s, best);
    if (a.tier >= 0) return a;
    return s.legalAttacks().front();
  }
};

RPS_MAIN(Copycat)
