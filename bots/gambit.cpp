// Gambit — the benchmark bot to beat. Predicts your next shape from your
// hand, your habits, and what you just BOUGHT (purchases are public!), then
// maximizes expected card swing. Shops with a simple doctrine: bomb when
// behind, contest mirrors with tiers, buy counters when rich.
#include "rps.h"

#include <algorithm>

struct Gambit : rps::Bot {
  std::string name() override { return "Gambit"; }

  static rps::Shape biggest(const rps::Player& p) {
    using namespace rps;
    Shape best = ROCK;
    for (int i = 0; i < 3; i++)
      if (p.count(static_cast<Shape>(i)) > p.count(best)) best = static_cast<Shape>(i);
    return best;
  }

  static int lowTier(const rps::Player& p, rps::Shape sh) {
    for (const auto& [tier, cnt] : p.cards[sh])
      if (cnt > 0) return tier;
    return -1;
  }

  rps::Shop chooseShop(const rps::State& s) override {
    using namespace rps;
    // behind on material? a bomb guarantees a trade next time we're read
    if (s.me.coins >= PRICE_BOMB && s.opp.total() - s.me.total() >= 2 && s.me.bombs == 0)
      return Shop::bomb();
    // contest the mirror we're most likely to fight: our streak shape
    if (s.hasHistory() && !s.myLast().bomb) {
      Shape mine = s.myLast().shape;
      int myTop = s.me.maxTier(mine);
      if (myTop >= 0 && s.opp.count(mine) > 0 && myTop <= s.opp.maxTier(mine) &&
          s.me.coins >= upgradeCost(myTop))
        return Shop::upgrade(mine, myTop);
    }
    // rich: stock the shape that beats their biggest pile
    if (s.me.coins >= PRICE_CARD + 2) return Shop::card(whatBeats(biggest(s.opp)));
    return Shop::none();
  }

  rps::Attack chooseAttack(const rps::State& s) override {
    using namespace rps;

    // 1. Opponent shape forecast: hand mix + streak habit + shopping signal.
    double w[3];
    double tot = std::max(1, s.opp.count(ROCK) + s.opp.count(PAPER) + s.opp.count(SCISSORS));
    for (int i = 0; i < 3; i++) w[i] = s.opp.count(static_cast<Shape>(i)) / tot;
    if (s.hasHistory() && !s.oppLast().bomb && s.opp.count(s.oppLast().shape) > 0)
      w[s.oppLast().shape] += 0.35;  // combo income makes streaks popular
    if (s.shopped) {
      const Shop& os = s.oppShopNow;   // purchases are public — read them!
      if (os.kind == Shop::UPGRADE && s.opp.count(os.shape) > 0) w[os.shape] += 0.30;
      if (os.kind == Shop::CARD) w[os.shape] += 0.15;
    }
    // counter-detection: have their last attacks each beaten my previous one?
    // Then their next play is whatBeats(myLast) — get ahead of it.
    int counterStreak = 0;
    for (size_t i = s.history.size(); i-- > 1;) {
      const Attack& theirs = s.history[i].oppAtk;
      const Attack& myPrev = s.history[i - 1].myAtk;
      if (theirs.bomb || myPrev.bomb || theirs.shape != whatBeats(myPrev.shape)) break;
      if (++counterStreak >= 3) break;
    }
    if (counterStreak >= 2 && s.hasHistory() && !s.myLast().bomb)
      w[whatBeats(s.myLast().shape)] += 1.5;
    double sum = w[0] + w[1] + w[2];
    for (double& x : w) x /= sum;

    // 2. Their most-likely play, and its tier if it's a mirror-invested card.
    Shape likely = ROCK;
    for (int i = 1; i < 3; i++) if (w[i] > w[likely]) likely = static_cast<Shape>(i);

    // A bomb spends best when their expected card carries real investment.
    if (s.me.bombs > 0 && s.opp.maxTier(likely) >= 2 && w[likely] > 0.5)
      return Attack::bombCard();

    // 3. Expected value per shape; risk cheap cards cross-shape, fight
    //    mirrors only with tier superiority.
    Shape bestShape = ROCK;
    int bestTier = 0;
    double bestEv = -1e9;
    for (int i = 0; i < 3; i++) {
      Shape mine = static_cast<Shape>(i);
      if (s.me.count(mine) == 0) continue;
      int myTop = s.me.maxTier(mine);
      int oppTop = s.opp.maxTier(mine);
      bool winMirror = myTop > oppTop;
      double ev = 0;
      for (int o = 0; o < 3; o++) {
        Shape theirs = static_cast<Shape>(o);
        double v;
        if (mine == theirs) v = winMirror ? 0.9 : (myTop < oppTop ? -0.9 : 0.0);
        else v = beats(mine, theirs) ? 1.0 : -1.0;
        ev += w[o] * v;
      }
      if (s.hasHistory() && !s.myLast().bomb && mine == s.myLast().shape)
        ev += 0.25;  // combo income
      ev += 0.02 * s.me.count(mine);
      if (ev > bestEv) {
        bestEv = ev;
        bestShape = mine;
        // only expose the fat card when it actually wins the expected mirror
        bestTier = (winMirror && w[mine] > 0.4) ? myTop : lowTier(s.me, mine);
      }
    }
    if (s.me.count(bestShape) == 0)      // nothing but bombs left in hand
      return s.legalAttacks().front();
    return Attack::card(bestShape, bestTier);
  }
};

RPS_MAIN(Gambit)
