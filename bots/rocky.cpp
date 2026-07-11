// Rocky — the training dummy. Throws rock until it runs out.
// The bot every contestant should beat within the first hour.
#include "rps.h"

struct Rocky : rps::Bot {
  std::string name() override { return "Rocky"; }

  rps::Move choose(const rps::State& s) override {
    using namespace rps;
    if (s.me.hand[ROCK] > 0) return Move::play(ROCK);
    if (s.me.hand[PAPER] > 0) return Move::play(PAPER);
    return Move::play(SCISSORS);
  }
};

RPS_MAIN(Rocky)
