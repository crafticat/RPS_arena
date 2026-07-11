// Copycat — plays whatever base shape the opponent played last turn.
// Surprisingly annoying to fight: it converges onto ties against streaks.
#include "rps.h"

struct Copycat : rps::Bot {
  std::string name() override { return "Copycat"; }

  rps::Move choose(const rps::State& s) override {
    using namespace rps;
    if (s.hasHistory()) {
      Shape want = s.oppLast().base;
      if (s.me.hand[want] > 0) return Move::play(want);
    }
    Shape best = ROCK;  // fall back to our biggest stack
    for (int i = 0; i < 3; i++)
      if (s.me.hand[i] > s.me.hand[best]) best = static_cast<Shape>(i);
    return Move::play(best);
  }
};

RPS_MAIN(Copycat)
