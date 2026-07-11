// Random — a uniformly random legal attack every turn. Never shops.
// Useful baseline: a strategy that can't beat Random is worse than nothing.
#include "rps.h"

struct RandomBot : rps::Bot {
  std::string name() override { return "Random"; }

  rps::Attack chooseAttack(const rps::State& s) override {
    auto options = s.legalAttacks();
    std::uniform_int_distribution<size_t> pick(0, options.size() - 1);
    return options[pick(rps::rng())];
  }
};

RPS_MAIN(RandomBot)
