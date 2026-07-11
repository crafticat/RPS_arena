// ============================================================================
//  MY BOT — this is your file. Make it win. 🏆
//
//  Loop:  1. edit chooseShop() / chooseAttack() below
//         2. hit ⟳ Rebuild bots in the UI  (or rerun the start script)
//         3. fight it:  ./build/arena play my_bot gambit
//
//  V2 RULES IN 20 SECONDS (full story in README.md):
//    · every turn: SHOP (buy ≤1 thing, both purchases revealed) then ATTACK
//    · win a clash: your card returns, theirs is gone forever
//    · same shape: higher tier wins (upgrades are permanent!)
//    · different shapes: rock > scissors > paper > rock — tiers don't matter
//    · bombs: trade with whatever they meet (both cards die, nobody "wins")
//    · repeat your base shape two turns running: +2 Crafticoins
//    · every 5th turn is a DANGER ROUND: the clash loser burns an extra
//      random card (check s.isSpecialRound())
//
//  PRICES:  upgrade tier0→1: 2🪙 · higher tiers: 1🪙 · bomb: 5🪙 · card: 7🪙
//
//  THE API (everything lives in sdk/rps.h):
//    s.me.count(ROCK)          how many rocks you hold (any tier)
//    s.me.cards[ROCK]          map: tier -> count       s.me.bombs, s.me.coins
//    s.me.maxTier(ROCK)        your best rock (-1 if none)
//    s.opp...                  the opponent's everything — perfect information
//    s.myShopNow, s.oppShopNow this turn's revealed purchases (attack phase)
//    s.history                 every past turn: attacks AND purchases
//    s.legalShops(), s.legalAttacks(), s.isSpecialRound()
//    s.afterShops(a, b), s.next(mine, theirs)   full simulation for search
//    Shop::none()/card(sh)/bomb()/upgrade(sh, tier)
//    Attack::card(sh, tier)/bombCard() · whatBeats(sh) · rng()
// ============================================================================
#include "rps.h"

using namespace rps;

struct MyBot : Bot {
  std::string name() override { return "MyBot"; }  // shown in the arena UI

  // Buy at most one thing per turn. Starter: invest in our favourite shape's
  // best card once the opponent starts mirroring us.
  Shop chooseShop(const State& s) override {
    Shape fav = favourite(s);
    int top = s.me.maxTier(fav);
    bool theyMirror = s.hasHistory() && !s.oppLast().bomb && s.oppLast().shape == fav;
    if (theyMirror && top >= 0 && s.me.coins >= upgradeCost(top))
      return Shop::upgrade(fav, top);
    return Shop::none();  // saving up is often right — cards cost 7
  }

  Attack chooseAttack(const State& s) override {
    Shape fav = favourite(s);
    if (s.me.count(fav) == 0)            // nothing but bombs left in hand
      return s.legalAttacks().front();
    // mirror expected? lead with our best card of that shape — otherwise
    // risk only the cheap one (tiers don't help across shapes)
    bool theyMirror = s.hasHistory() && !s.oppLast().bomb && s.oppLast().shape == fav;
    int tier = theyMirror ? s.me.maxTier(fav) : lowestTier(s, fav);
    return Attack::card(fav, tier);

    // Ideas worth trying:
    //  * read s.oppShopNow — an upgrade telegraphs the shape they'll play
    //  * counter their biggest pile:  whatBeats(...)
    //  * bomb their fat card when they're likely to play it
    //  * play safe (or bomb) on s.isSpecialRound() — losses hurt double
    //  * expected value over s.legalAttacks() with s.opp counts as forecast
  }

  // --- helpers ------------------------------------------------------------
  static Shape favourite(const State& s) {
    if (s.hasHistory() && !s.myLast().bomb && s.me.count(s.myLast().shape) > 0)
      return s.myLast().shape;  // keep the +2 combo flowing
    Shape best = ROCK;
    for (int i = 0; i < 3; i++)
      if (s.me.count(static_cast<Shape>(i)) > s.me.count(best))
        best = static_cast<Shape>(i);
    return best;
  }
  static int lowestTier(const State& s, Shape sh) {
    for (const auto& [tier, cnt] : s.me.cards[sh])
      if (cnt > 0) return tier;
    return 0;
  }
};

RPS_MAIN(MyBot)
