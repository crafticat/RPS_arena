// Rocky — the training dummy. Throws its cheapest rock, never shops.
// The bot every contestant should beat within the first hour.
#include "rps.h"

struct Rocky : rps::Bot {
  std::string name() override { return "Rocky"; }

  rps::Attack chooseAttack(const rps::State& s) override {
    using namespace rps;
    for (Shape sh : {ROCK, PAPER, SCISSORS})
      for (const auto& [tier, cnt] : s.me.cards[sh])
        if (cnt > 0) return Attack::card(sh, tier);   // lowest tier first
    return Attack::bombCard();  // only bombs left (can't happen for Rocky)
  }
};

RPS_MAIN(Rocky)
