// Random — plays a uniformly random card it still owns. Never spends coins.
// Useful baseline: a strategy that can't beat Random is worse than nothing.
#include "rps.h"

struct RandomBot : rps::Bot {
  std::string name() override { return "Random"; }

  rps::Move choose(const rps::State& s) override {
    std::vector<rps::Shape> options;
    for (int i = 0; i < 3; i++)
      if (s.me.hand[i] > 0) options.push_back(static_cast<rps::Shape>(i));
    std::uniform_int_distribution<size_t> pick(0, options.size() - 1);
    return rps::Move::play(options[pick(rps::rng())]);
  }
};

RPS_MAIN(RandomBot)
