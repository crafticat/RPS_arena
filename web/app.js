/* RPS Arena UI — V2 (shop phase + attack phase). Vanilla JS; all game logic
   lives in the C++ engine — this file renders engine JSON and animates. */
"use strict";

const $ = (id) => document.getElementById(id);
const GLYPH = { r: "🪨", p: "📄", s: "✂️" };
const SHAPES = ["r", "p", "s"];
const SHAPE_NAME = { r: "rock", p: "paper", s: "scissors" };
const REDUCED = matchMedia("(prefers-reduced-motion: reduce)").matches;

const sleep = (ms) => new Promise((r) => setTimeout(r, REDUCED ? 0 : ms));
const rndSeed = () => Math.floor(Math.random() * 1e9);

// boot sequence: let the reactor spin once, then clear it out of the DOM
{
  const boot = $("boot");
  if (boot) setTimeout(() => boot.remove(), REDUCED ? 0 : 2200);
}

async function api(path, body, method) {
  const res = await fetch(path, {
    method: method || (body === undefined ? "GET" : "POST"),
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  let data;
  try { data = await res.json(); } catch { data = null; }
  if (!res.ok) throw new Error((data && data.error) || `${res.status} ${res.statusText}`);
  return data;
}

let toastTimer = null;
function toast(msg) {
  const t = $("toast");
  t.textContent = msg;
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.hidden = true; }, 4000);
}

/* ------------------------------------------------------------- tokens */
/* attack tokens: "r", "r3", "!"  ·  shop tokens: "-", "b", "cr", "ur2" */

function parseAtk(tok) {
  if (tok === "!") return { bomb: true };
  return { bomb: false, shape: tok[0], tier: tok.length > 1 ? parseInt(tok.slice(1), 10) : 0 };
}
function atkLabel(tok) {
  const a = parseAtk(tok);
  if (a.bomb) return "💣";
  return GLYPH[a.shape] + (a.tier > 0 ? "+" + a.tier : "");
}
function shopLabel(tok) {
  if (!tok || tok === "-") return "nothing";
  if (tok === "b") return "a 💣 bomb";
  if (tok[0] === "c") return `a new ${GLYPH[tok[1]]} card`;
  const shape = tok[1], tier = parseInt(tok.slice(2), 10);
  return `⬆ ${GLYPH[shape]}${tier > 0 ? "+" + tier : ""} → +${tier + 1}`;
}
function freshSide() {
  return { coins: 0, bombs: 0, r: [[0, 10]], p: [[0, 10]], s: [[0, 10]], total: 30 };
}
function sideTotal(side) {
  let n = side.bombs;
  for (const sh of SHAPES) for (const [, c] of side[sh]) n += c;
  return n;
}

/* ---------------------------------------------------------------- tabs */

document.querySelectorAll("#tabs button").forEach((btn) => {
  btn.addEventListener("click", () => {
    document.querySelectorAll("#tabs button").forEach((b) => b.classList.toggle("active", b === btn));
    document.querySelectorAll(".tab").forEach((s) => s.classList.remove("active"));
    $("tab-" + btn.dataset.tab).classList.add("active");
  });
});
function switchTab(name) {
  document.querySelector(`#tabs button[data-tab="${name}"]`).click();
}

/* ---------------------------------------------------------------- bots */

async function loadBots() {
  const { bots } = await api("/api/bots");
  const fill = (sel, preferred) => {
    const el = $(sel);
    const prev = el.value;
    el.innerHTML = "";
    bots.forEach((b) => {
      const o = document.createElement("option");
      o.value = o.textContent = b;
      el.appendChild(o);
    });
    if (bots.includes(prev)) el.value = prev;
    else if (bots.includes(preferred)) el.value = preferred;
  };
  fill("play-bot", "gambit");
  fill("watch-a", "my_bot");
  fill("watch-b", "gambit");
  fill("arena-a", "my_bot");
  fill("arena-b", "gambit");
  return bots;
}

/* -------------------------------------------------------------- ledgers */

function renderLedger(el, { corner, name, side, earned }) {
  const stacks = SHAPES.map((sh) => {
    const groups = side[sh].filter(([, c]) => c > 0);
    const count = groups.reduce((n, [, c]) => n + c, 0);
    const badges = groups.filter(([t]) => t > 0)
      .map(([t, c]) => `+${t}${c > 1 ? "×" + c : ""}`).join(" ");
    return `<div class="stack" title="${count} ${SHAPE_NAME[sh]}">
      <span class="glyph">${GLYPH[sh]}</span><span class="count">×${count}</span>
      ${badges ? `<span class="badges">${badges}</span>` : ""}</div>`;
  }).join("");
  const bombs = side.bombs > 0
    ? `<div class="stack bombs" title="${side.bombs} bomb(s)">
         <span class="glyph">💣</span><span class="count">×${side.bombs}</span></div>`
    : "";
  el.innerHTML = `
    <div class="who"><div class="corner">${corner} corner</div><div class="name">${name}</div></div>
    <div class="coins" title="Crafticoins"><span class="arc${earned ? " pulse" : ""}"></span>${side.coins}${earned ? `<span class="plus-one">+${earned}</span>` : ""}</div>
    <div class="stacks">${stacks}${bombs}</div>`;
}

/* ----------------------------------------------------------- duel cards */

function setCardBack(slot) {
  slot.innerHTML = `<div class="card-face back">?</div>`;
}
function setCardFace(slot, tok, effects = {}) {
  const a = parseAtk(tok);
  const glyph = a.bomb ? "💣" : GLYPH[a.shape];
  const tier = !a.bomb && a.tier > 0 ? `<span class="tier">+${a.tier}</span>` : "";
  const cls = ["card-face"];
  if (effects.flip) cls.push("flip-in");
  slot.innerHTML = `<div class="${cls.join(" ")}">${tier}${glyph}</div>`;
  return slot.firstElementChild;
}

/* Spark burst when a card dies; bombs get more sparks plus a shockwave. */
function spawnSparks(slot, boom) {
  if (REDUCED) return;
  const n = boom ? 16 : 9;
  for (let i = 0; i < n; i++) {
    const s = document.createElement("span");
    s.className = "spark" + (boom ? " hot" : Math.random() < 0.5 ? " cyan" : "");
    const ang = Math.random() * 2 * Math.PI;
    const dist = 40 + Math.random() * (boom ? 110 : 70);
    s.style.setProperty("--dx", `${Math.cos(ang) * dist}px`);
    s.style.setProperty("--dy", `${Math.sin(ang) * dist}px`);
    slot.appendChild(s);
    setTimeout(() => s.remove(), 750);
  }
  if (boom) {
    const w = document.createElement("span");
    w.className = "shockwave";
    slot.appendChild(w);
    setTimeout(() => w.remove(), 700);
  }
}

function setDanger(prefix, on) {
  document.querySelector(`#tab-${prefix} .duel-mid`).classList.toggle("danger", !!on);
}

/* One simultaneous reveal. ev is a V2 turn event. */
async function animateDuel(prefix, ev, speedFactor = 1) {
  const slotA = $(`${prefix}-card-a`), slotB = $(`${prefix}-card-b`);
  const bought = (ev.a.shop !== "-" || ev.b.shop !== "-")
    ? `🛒 ${shopLabel(ev.a.shop)} · ${shopLabel(ev.b.shop)}` : "";
  $(`${prefix}-clash`).textContent = bought;
  const faceA = setCardFace(slotA, ev.a.mv, { flip: true });
  const faceB = setCardFace(slotB, ev.b.mv, { flip: true });
  await sleep(480 * speedFactor);

  const boom = ev.win === "trade";
  if (ev.a.destroyed) { faceA.classList.add(boom ? "boom" : "destroyed"); spawnSparks(slotA, boom); }
  if (ev.b.destroyed) { faceB.classList.add(boom ? "boom" : "destroyed"); spawnSparks(slotB, boom); }
  if (ev.win === "a" && !ev.a.destroyed) faceA.classList.add("winner-glow");
  if (ev.win === "b" && !ev.b.destroyed) faceB.classList.add("winner-glow");

  let note =
    ev.win === "trade" ? "💥 bomb trade — both cards destroyed" :
    ev.win === "tie" ? "tie — both cards return" :
    ev.win === "a" ? "red corner takes the exchange" :
    "blue corner takes the exchange";
  if (ev.a.lost) note += ` · ⚠ red burns ${atkLabel(ev.a.lost)}`;
  if (ev.b.lost) note += ` · ⚠ blue burns ${atkLabel(ev.b.lost)}`;
  $(`${prefix}-clash`).textContent = note;
  await sleep(680 * speedFactor);
}

function pushHistoryChip(prefix, ev) {
  const chip = document.createElement("span");
  chip.className = `chip win-${ev.win === "trade" ? "tie" : ev.win}`;
  chip.textContent = `${atkLabel(ev.a.mv)} ${atkLabel(ev.b.mv)}${ev.special ? "⚠" : ""}`;
  chip.title = `turn ${ev.t}: ${ev.a.mv} vs ${ev.b.mv} — ` +
    (ev.win === "trade" ? "bomb trade" : ev.win === "tie" ? "tie" :
     ev.win === "a" ? "red wins" : "blue wins") +
    (ev.a.shop !== "-" ? ` · red bought ${shopLabel(ev.a.shop)}` : "") +
    (ev.b.shop !== "-" ? ` · blue bought ${shopLabel(ev.b.shop)}` : "");
  $(`${prefix}-history`).appendChild(chip);
  chip.scrollIntoView({ block: "nearest" });
}

const REASON_COPY = {
  elimination: "out of cards",
  "mutual elimination": "double KO — both hands hit zero",
  "turn limit": "turn 100 — biggest hand wins",
  resignation: "resignation",
};
const friendlyReason = (r) => REASON_COPY[r] || r;

/* ==================================================================== PLAY */

const play = {
  sid: null, over: true, botName: "",
  me: freshSide(), opp: freshSide(),
  turn: 1, maxTurns: 100, special: false,
  phase: "shop",          // "shop" | "attack"
  selTok: null, busy: false,
};

function playSyncState(ev) {
  play.turn = ev.turn;
  play.special = !!ev.special;
  play.me = ev.a;
  play.opp = ev.b;
}

function renderPlayLedgers(earnedA, earnedB) {
  renderLedger($("play-you"), { corner: "red", name: "you", side: play.me, earned: earnedA });
  renderLedger($("play-opp"), { corner: "blue", name: play.botName, side: play.opp, earned: earnedB });
  $("play-turn").textContent = `turn ${play.turn}/${play.maxTurns}`;
  $("play-prog").style.width = `${(100 * (play.turn - 1)) / play.maxTurns}%`;
  setDanger("play", play.special);
}

function upgradeCost(tier) { return tier === 0 ? 2 : 1; }

function shopOptions(side) {
  const opts = [{ tok: "-", label: "Save coins", hint: "no purchase" }];
  for (const sh of SHAPES)
    for (const [tier, cnt] of side[sh]) {
      if (cnt <= 0) continue;
      const cost = upgradeCost(tier);
      if (side.coins >= cost)
        opts.push({
          tok: `u${sh}${tier}`,
          label: `⬆ ${GLYPH[sh]}${tier > 0 ? "+" + tier : ""}`,
          hint: `→ +${tier + 1} · ${cost}🪙`,
        });
    }
  if (side.coins >= 5) opts.push({ tok: "b", label: "💣 bomb", hint: "5🪙 · trades 1-for-1" });
  if (side.coins >= 7)
    for (const sh of SHAPES)
      opts.push({ tok: "c" + sh, label: `🛒 ${GLYPH[sh]}`, hint: "new card · 7🪙" });
  return opts;
}

function attackOptions(side) {
  const opts = [];
  for (const sh of SHAPES)
    for (const [tier, cnt] of side[sh]) {
      if (cnt <= 0) continue;
      opts.push({
        tok: sh + (tier > 0 ? tier : ""),
        label: `${GLYPH[sh]}${tier > 0 ? "+" + tier : ""}`,
        hint: `×${cnt}`,
        big: true,
      });
    }
  if (side.bombs > 0) opts.push({ tok: "!", label: "💣", hint: `×${side.bombs} trade`, big: true });
  return opts;
}

function renderPickers() {
  const isShop = play.phase === "shop";
  $("play-phase-note").textContent = play.over ? "" :
    isShop ? "shop phase — buy one thing (or save)" :
    "attack phase — pick your card" + (play.special ? " · danger round: don't lose!" : "");
  const listEl = isShop ? $("pick-shop") : $("pick-attack");
  $("pick-shop").hidden = !isShop;
  $("pick-attack").hidden = isShop;
  listEl.innerHTML = "";
  const opts = isShop ? shopOptions(play.me) : attackOptions(play.me);
  opts.forEach((o) => {
    const b = document.createElement("button");
    b.className = "pick-btn" + (play.selTok === o.tok ? " sel" : "");
    b.disabled = play.over || play.busy;
    b.innerHTML = (o.big ? `<span class="big-glyph">${o.label}</span>` : o.label) +
      ` <small>${o.hint}</small>`;
    b.onclick = () => { play.selTok = o.tok; renderPickers(); };
    listEl.appendChild(b);
  });
  $("play-go").textContent = isShop ? "Confirm purchase" : "Attack!";
  $("play-go").disabled = play.over || play.busy || play.selTok === null;
}

async function playNewGame() {
  if (play.sid) api(`/api/session/${play.sid}`, undefined, "DELETE").catch(() => {});
  Object.assign(play, {
    sid: null, over: false, me: freshSide(), opp: freshSide(),
    turn: 1, special: false, phase: "shop", selTok: null, busy: false,
  });
  try {
    const d = await api("/api/session", { bot: $("play-bot").value, humanSide: "a" });
    play.sid = d.id;
    for (const ev of d.events) {
      if (ev.type === "hello") { play.botName = ev.botName; play.maxTurns = ev.maxTurns; }
      if (ev.type === "state") playSyncState(ev);
      if (ev.type === "end") { play.over = true; showPlayEnd(ev); }
    }
  } catch (e) { toast(e.message); return; }
  $("play-table").hidden = false;
  $("play-banner").hidden = true;
  $("play-resign").hidden = false;
  $("play-history").innerHTML = "";
  $("shop-reveal").hidden = true;
  setCardBack($("play-card-a"));
  setCardBack($("play-card-b"));
  $("play-clash").textContent = "";
  renderPlayLedgers();
  renderPickers();
}

function showPlayEnd(ev) {
  play.over = true;
  play.sid = null;
  $("play-resign").hidden = true;
  $("play-phase-note").textContent = "";
  const b = $("play-banner");
  const who = ev.winner === "a" ? "win" : ev.winner === "b" ? "lose" : "draw";
  b.className = "banner " + who;
  const headline = who === "win" ? "🏆 You win!" : who === "lose" ? `${play.botName} wins` : "🤝 Draw";
  const fa = ev.final ? sideTotal(ev.final.a) : "?";
  const fb = ev.final ? sideTotal(ev.final.b) : "?";
  b.innerHTML = `<b>${headline}</b><br>${friendlyReason(ev.reason)} · final cards you ${fa} — ${play.botName} ${fb}`;
  b.hidden = false;
  renderPickers();
}

async function playSubmit() {
  if (play.busy || play.over || play.selTok === null) return;
  const tok = play.selTok;
  play.busy = true;
  renderPickers();
  try {
    if (play.phase === "shop") {
      const d = await api(`/api/session/${play.sid}/shop`, { shop: tok });
      for (const ev of d.events) {
        if (ev.type === "illegal") toast("can't buy that: " + ev.reason);
        if (ev.type === "shopped") {
          play.phase = "attack";
          const strip = $("shop-reveal");
          strip.innerHTML =
            `<span>you bought <b>${shopLabel(ev.a)}</b></span>` +
            `<span>${play.botName} bought <b>${shopLabel(ev.b)}</b></span>`;
          strip.hidden = false;
          // purchases change hands/coins before the attack — refetch from
          // the shop tokens locally is fiddly; the next state event will
          // resync, meanwhile apply the two cheap deltas for the pickers:
          applyShopLocal(play.me, ev.a);
          applyShopLocal(play.opp, ev.b);
          renderPlayLedgers();
        }
        if (ev.type === "end") showPlayEnd(ev);
        if (ev.type === "error") toast(ev.reason);
      }
    } else {
      setCardBack($("play-card-a"));
      setCardBack($("play-card-b"));
      const d = await api(`/api/session/${play.sid}/move`, { move: tok });
      for (const ev of d.events) {
        if (ev.type === "illegal") toast("that attack is illegal: " + ev.reason);
        if (ev.type === "turn") {
          $("shop-reveal").hidden = true;
          await animateDuel("play", ev);
          pushHistoryChip("play", ev);
          play.me = ev.after.a;
          play.opp = ev.after.b;
          renderPlayLedgers(ev.a.earned, ev.b.earned);
        }
        if (ev.type === "state") {
          playSyncState(ev);
          play.phase = "shop";
          renderPlayLedgers();
        }
        if (ev.type === "end") showPlayEnd(ev);
        if (ev.type === "error") toast(ev.reason);
      }
    }
  } catch (e) { toast(e.message); }
  play.busy = false;
  play.selTok = null;
  renderPickers();
}

// tiny local mirror of applyShop so pickers are correct pre-resync
function applyShopLocal(side, tok) {
  if (!tok || tok === "-") return;
  if (tok === "b") { side.coins -= 5; side.bombs++; return; }
  if (tok[0] === "c") {
    side.coins -= 7;
    const sh = tok[1];
    const g = side[sh].find(([t]) => t === 0);
    if (g) g[1]++; else side[sh].unshift([0, 1]);
    return;
  }
  const sh = tok[1], tier = parseInt(tok.slice(2), 10);
  side.coins -= upgradeCost(tier);
  const from = side[sh].find(([t]) => t === tier);
  if (from) from[1]--;
  const to = side[sh].find(([t]) => t === tier + 1);
  if (to) to[1]++; else { side[sh].push([tier + 1, 1]); side[sh].sort((x, y) => x[0] - y[0]); }
  side[sh] = side[sh].filter(([, c]) => c > 0);
}

$("play-new").onclick = playNewGame;
$("play-go").onclick = playSubmit;
$("play-resign").onclick = async () => {
  if (!play.sid) return;
  await api(`/api/session/${play.sid}`, undefined, "DELETE").catch(() => {});
  showPlayEnd({ winner: "b", reason: "resignation", final: { a: play.me, b: play.opp } });
};

/* =================================================================== WATCH */

const watch = { replay: null, idx: 0, playing: false, timer: null, gen: 0 };

function watchRenderAt(idx, { animate } = {}) {
  const r = watch.replay;
  const stateA = idx === 0 ? freshSide() : r.turns[idx - 1].after.a;
  const stateB = idx === 0 ? freshSide() : r.turns[idx - 1].after.b;
  renderLedger($("watch-youL"), { corner: "red", name: r.aName, side: stateA });
  renderLedger($("watch-oppL"), { corner: "blue", name: r.bName, side: stateB });
  const shownTurn = Math.min(Math.max(1, idx + (animate ? 0 : 1)), r.turns.length);
  $("watch-turn").textContent = `turn ${Math.max(1, idx)}/${r.turns.length}`;
  $("watch-prog").style.width = r.turns.length ? `${(100 * idx) / r.turns.length}%` : "0%";
  $("w-pos").textContent = `${idx}/${r.turns.length}`;
  $("w-scrub").value = idx;
  setDanger("watch", idx < r.turns.length && (idx + 1) % 5 === 0);
  if (!animate) {
    const h = $("watch-history");
    h.innerHTML = "";
    for (let i = 0; i < idx; i++) pushHistoryChip("watch", r.turns[i]);
    if (idx === 0) { setCardBack($("watch-card-a")); setCardBack($("watch-card-b")); $("watch-clash").textContent = ""; }
    else {
      setCardFace($("watch-card-a"), r.turns[idx - 1].a.mv);
      setCardFace($("watch-card-b"), r.turns[idx - 1].b.mv);
    }
  }
  void shownTurn;
}

async function watchStep() {
  const r = watch.replay;
  if (!r || watch.idx >= r.turns.length) { watchStop(); return false; }
  const ev = r.turns[watch.idx];
  const factor = Math.max(0.15, Math.min(1, Number($("w-speed").value) / 450));
  const gen = watch.gen;
  setDanger("watch", ev.special);
  await animateDuel("watch", ev, factor);
  if (gen !== watch.gen) return false;  // scrub/stop/new replay superseded this step
  watch.idx++;
  pushHistoryChip("watch", ev);
  watchRenderAt(watch.idx, { animate: true });
  if (watch.idx >= r.turns.length) { watchShowEnd(); watchStop(); return false; }
  return true;
}

function watchShowEnd() {
  const r = watch.replay;
  const b = $("watch-banner");
  const name = r.winner === "a" ? r.aName : r.bName;
  b.className = "banner " + (r.winner === "a" ? "win" : r.winner === "b" ? "lose" : "draw");
  b.innerHTML = r.winner === "draw"
    ? `<b>🤝 Draw</b><br>${friendlyReason(r.reason)}`
    : `<b>🏆 ${name} wins</b><br>${friendlyReason(r.reason)} after ${r.turnsPlayed} turns`;
  b.hidden = false;
}

function watchStop() {
  watch.gen++;  // cancels any step that is mid-animation
  watch.playing = false;
  clearTimeout(watch.timer);
  $("w-playpause").textContent = "▶";
}
function watchPlayLoop() {
  if (!watch.playing) return;
  watch.timer = setTimeout(async () => {
    const more = await watchStep();
    if (more && watch.playing) watchPlayLoop();
  }, Number($("w-speed").value));
}

function loadReplayIntoWatch(replay, note) {
  watchStop();
  watch.replay = replay;
  watch.idx = 0;
  $("watch-table").hidden = false;
  $("watch-banner").hidden = true;
  $("w-scrub").max = replay.turns.length;
  $("watch-note").textContent = note || "";
  watchRenderAt(0);
  switchTab("watch");
}

$("watch-go").onclick = async () => {
  const btn = $("watch-go");
  btn.disabled = true;
  try {
    const seed = rndSeed();
    const r = await api("/api/play", { a: $("watch-a").value, b: $("watch-b").value, seed });
    loadReplayIntoWatch(r, `seed ${seed} — same seed replays the same game`);
    watch.playing = true;
    $("w-playpause").textContent = "⏸";
    watchPlayLoop();
  } catch (e) { toast(e.message); }
  btn.disabled = false;
};
$("w-playpause").onclick = () => {
  if (!watch.replay) return;
  if (watch.playing) { watchStop(); return; }
  if (watch.idx >= watch.replay.turns.length) { watch.idx = 0; watchRenderAt(0); $("watch-banner").hidden = true; }
  watch.playing = true;
  $("w-playpause").textContent = "⏸";
  watchPlayLoop();
};
$("w-step").onclick = () => { if (watch.replay) { watchStop(); watchStep(); } };
$("w-restart").onclick = () => {
  if (!watch.replay) return;
  watchStop();
  watch.idx = 0;
  $("watch-banner").hidden = true;
  watchRenderAt(0);
};
$("w-scrub").oninput = () => {
  if (!watch.replay) return;
  watchStop();
  watch.idx = Number($("w-scrub").value);
  $("watch-banner").hidden = watch.idx < watch.replay.turns.length;
  if (watch.idx >= watch.replay.turns.length) watchShowEnd();
  watchRenderAt(watch.idx);
};

/* HUD-style count-up for stat tile values like "87%", "37.0", "12". */
function countUp(container) {
  if (REDUCED) return;
  container.querySelectorAll(".v").forEach((v) => {
    const m = v.textContent.match(/^(\d+(?:\.\d+)?)(.*)$/);
    if (!m) return;
    const target = parseFloat(m[1]);
    const decimals = m[1].includes(".") ? 1 : 0;
    const suffix = m[2];
    const t0 = performance.now();
    const tick = (t) => {
      const p = Math.min(1, (t - t0) / 600);
      v.textContent = (target * (1 - Math.pow(1 - p, 3))).toFixed(decimals) + suffix;
      if (p < 1) requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
  });
}

/* =================================================================== ARENA */

$("arena-go").onclick = async () => {
  const btn = $("arena-go");
  btn.disabled = true;
  btn.textContent = "Running…";
  try {
    const a = $("arena-a").value, b = $("arena-b").value;
    const d = await api("/api/match", { a, b, n: Number($("arena-n").value), seed: rndSeed() });
    $("arena-out").hidden = false;

    const forf = d.aForfeits + d.bForfeits;
    $("arena-tiles").innerHTML = [
      [`${(100 * d.aWins / d.n).toFixed(0)}%`, `${a} win rate`],
      [`${(100 * d.bWins / d.n).toFixed(0)}%`, `${b} win rate`],
      [d.avgTurns.toFixed(1), "avg turns"],
      [forf, "forfeits"],
    ].map(([v, k]) => `<div class="stat-tile"><div class="v">${v}</div><div class="k">${k}</div></div>`).join("");
    countUp($("arena-tiles"));

    const bar = $("arena-bar");
    bar.innerHTML = "";
    [["a", d.aWins], ["d", d.draws], ["b", d.bWins]].forEach(([cls, nGames]) => {
      if (!nGames) return;
      const seg = document.createElement("div");
      seg.className = "seg " + cls;
      seg.style.flex = String(nGames);
      seg.textContent = nGames / d.n > 0.07 ? nGames : "";
      seg.title = `${nGames} game${nGames === 1 ? "" : "s"}`;
      bar.appendChild(seg);
    });
    $("arena-legend").innerHTML =
      `<span><span class="sw" style="background:var(--red)"></span>${a} ${d.aWins}</span>` +
      `<span><span class="sw" style="background:var(--violet)"></span>draws ${d.draws}</span>` +
      `<span><span class="sw" style="background:var(--blue)"></span>${b} ${d.bWins}</span>`;

    const dots = $("arena-dots");
    dots.innerHTML = "";
    d.games.forEach((g, i) => {
      const dot = document.createElement("button");
      dot.className = "dot " + (g.winner === "a" ? "a" : g.winner === "b" ? "b" : "d");
      dot.title = `game ${i + 1}: ` + (g.winner === "draw" ? "draw" :
        `${g.winner === "a" ? a : b} wins`) + ` (${g.reason}, ${g.turnsPlayed} turns)`;
      dot.onclick = () => loadReplayIntoWatch(g, `game ${i + 1} of the ${a} vs ${b} match (seed ${g.seed})`);
      dots.appendChild(dot);
    });
  } catch (e) { toast(e.message); }
  btn.disabled = false;
  btn.textContent = "Run match";
};

/* ============================================================== TOURNAMENT */

$("tour-go").onclick = async () => {
  const btn = $("tour-go");
  btn.disabled = true;
  btn.textContent = "Running…";
  try {
    const d = await api("/api/tournament", { games: Number($("tour-n").value) });
    $("tour-out").hidden = false;

    const maxPts = Math.max(...d.standings.map((s) => s.points), 1);
    $("tour-table").innerHTML =
      `<thead><tr><th>#</th><th>bot</th><th class="num">played</th><th class="num">W</th>` +
      `<th class="num">L</th><th class="num">D</th><th>points</th></tr></thead><tbody>` +
      d.standings.map((s, i) =>
        `<tr><td class="num">${i + 1}</td>` +
        `<td>${i === 0 ? '<span class="champ">🏆 </span>' : ""}<b>${s.name}</b></td>` +
        `<td class="num">${s.played}</td><td class="num">${s.wins}</td>` +
        `<td class="num">${s.losses}</td><td class="num">${s.draws}</td>` +
        `<td><span class="ptsbar" style="width:${(120 * s.points / maxPts).toFixed(0)}px"></span>` +
        `<span class="mono">${s.points}</span></td></tr>`).join("") + "</tbody>";

    const names = d.bots;
    const grid = $("tour-grid");
    let html = `<thead><tr><th></th>${names.map((n) => `<th>${n}</th>`).join("")}</tr></thead><tbody>`;
    d.grid.forEach((row, i) => {
      html += `<tr><th>${names[i]}</th>`;
      row.forEach((cell, j) => {
        if (i === j || !cell) { html += `<td class="self">—</td>`; return; }
        const total = cell.w + cell.l + cell.d;
        const rate = total ? (cell.w + cell.d / 2) / total : 0.5;
        const tilt = rate - 0.5; // >0: row bot on top → cyan; <0 → red
        const color = tilt >= 0 ? "47,157,196" : "226,105,95";
        const alpha = Math.min(0.85, Math.abs(tilt) * 1.7).toFixed(2);
        html += `<td style="background:rgba(${color},${alpha})" ` +
          `title="${names[i]} vs ${names[j]}: ${cell.w} wins, ${cell.l} losses, ${cell.d} draws">` +
          `${cell.w}-${cell.l}-${cell.d}</td>`;
      });
      html += "</tr>";
    });
    grid.innerHTML = html + "</tbody>";
  } catch (e) { toast(e.message); }
  btn.disabled = false;
  btn.textContent = "Run tournament";
};

/* ================================================================= REBUILD */

$("rebuild").onclick = async () => {
  const dlg = $("build-dialog");
  $("build-title").textContent = "Rebuilding…";
  $("build-title").className = "";
  $("build-out").textContent = "running the build…";
  dlg.showModal();
  try {
    const d = await api("/api/build", {});
    $("build-title").textContent = d.ok ? "✓ Build succeeded" : "✗ Build failed";
    $("build-title").className = d.ok ? "ok" : "bad";
    $("build-out").textContent = d.output.trim() || "(no compiler output — everything was up to date)";
    if (d.ok) loadBots();
  } catch (e) {
    $("build-title").textContent = "✗ Build failed";
    $("build-title").className = "bad";
    $("build-out").textContent = e.message;
  }
};

/* ==================================================================== init */

loadBots().catch((e) => toast("can't reach the arena server: " + e.message));
