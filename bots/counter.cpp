// Counter — assumes the opponent repeats their last base shape and plays
// what beats it. Punishes coin farmers and one-track bots hard.
#include "rps.h"

struct Counter : rps::Bot {
  std::string name() override { return "Counter"; }

  rps::Move choose(const rps::State& s) override {
    using namespace rps;
    Shape target;
    if (s.hasHistory()) {
      target = s.oppLast().base;
    } else {
      target = ROCK;  // turn 1: aim at their biggest stack
      for (int i = 0; i < 3; i++)
        if (s.opp.hand[i] > s.opp.hand[target]) target = static_cast<Shape>(i);
    }
    Shape want = whatBeats(target);
    if (s.me.hand[want] > 0) return Move::play(want);
    Shape best = ROCK;
    for (int i = 0; i < 3; i++)
      if (s.me.hand[i] > s.me.hand[best]) best = static_cast<Shape>(i);
    return Move::play(best);
  }
};

RPS_MAIN(Counter)
