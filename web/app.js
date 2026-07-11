/* RPS Arena UI. Vanilla JS; all game logic lives in the C++ engine —
   this file only renders engine JSON and animates the duel. */
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

/* ---------------------------------------------------------- move parsing */

function parseMv(mv) {
  return {
    base: mv[0],
    up: mv.length > 1 ? mv[1] : "",          // '+', '!', '?' or ''
    disguise: mv[1] === "?" ? mv[2] : null,
  };
}
function mvLabel(mv) {
  const m = parseMv(mv);
  if (m.up === "?") return `${GLYPH[m.base]}→${GLYPH[m.disguise]}`;
  if (m.up === "+") return `${GLYPH[m.base]}+`;
  if (m.up === "!") return `${GLYPH[m.base]}💣`;
  return GLYPH[m.base];
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

function renderLedger(el, { corner, name, hand, coins, earned }) {
  const stacks = SHAPES.map((s, i) => {
    const n = hand[i];
    const notches = Array.from({ length: 7 }, (_, k) =>
      `<span class="notch${k < n ? " on" : ""}"></span>`).join("");
    return `<div class="stack" title="${n} ${SHAPE_NAME[s]}">
      <span class="glyph">${GLYPH[s]}</span><span class="count">×${n}</span>
      <span class="notches">${notches}</span></div>`;
  }).join("");
  el.innerHTML = `
    <div class="who"><div class="corner">${corner} corner</div><div class="name">${name}</div></div>
    <div class="coins" title="Crafticoins"><span class="arc${earned ? " pulse" : ""}"></span>${coins}${earned ? '<span class="plus-one">+1</span>' : ""}</div>
    <div class="stacks">${stacks}</div>`;
}

/* ----------------------------------------------------------- duel cards */

function setCardBack(slot) {
  slot.innerHTML = `<div class="card-face back">?</div>`;
}
function setCardFace(slot, mv, effects = {}) {
  const m = parseMv(mv);
  let badge = "";
  if (m.up === "+") badge = `<span class="badge">+1</span>`;
  if (m.up === "!") badge = `<span class="badge">💣</span>`;
  const sub = m.up === "?" ? `<span class="sub">fights as ${GLYPH[m.disguise]}</span>` : "";
  const cls = ["card-face"];
  if (effects.flip) cls.push("flip-in");
  slot.innerHTML = `<div class="${cls.join(" ")}">${badge}${GLYPH[m.base]}${sub}</div>`;
  return slot.firstElementChild;
}

/* Spark burst when a card dies; bombs get more sparks plus a shockwave ring. */
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

/* One simultaneous reveal. prefix is "play" or "watch"; ev is a turn event.
   speedFactor < 1 compresses the choreography for fast playback. */
async function animateDuel(prefix, ev, speedFactor = 1) {
  const slotA = $(`${prefix}-card-a`), slotB = $(`${prefix}-card-b`);
  const faceA = setCardFace(slotA, ev.a.mv, { flip: true });
  const faceB = setCardFace(slotB, ev.b.mv, { flip: true });
  $(`${prefix}-clash`).textContent = "";
  await sleep(480 * speedFactor);

  const bombA = parseMv(ev.a.mv).up === "!" && ev.a.destroyed;
  const bombB = parseMv(ev.b.mv).up === "!" && ev.b.destroyed;
  if (ev.a.destroyed) { faceA.classList.add(bombA ? "boom" : "destroyed"); spawnSparks(slotA, bombA); }
  if (ev.b.destroyed) { faceB.classList.add(bombB ? "boom" : "destroyed"); spawnSparks(slotB, bombB); }
  if (ev.win === "a" && !ev.a.destroyed) faceA.classList.add("winner-glow");
  if (ev.win === "b" && !ev.b.destroyed) faceB.classList.add("winner-glow");

  const note =
    ev.win === "tie" ? "tie — both cards return" :
    ev.a.destroyed && ev.b.destroyed ? "💥 bomb takes both cards" :
    ev.win === "a" ? "red corner takes the exchange" :
    "blue corner takes the exchange";
  $(`${prefix}-clash`).textContent = note;
  await sleep(680 * speedFactor);
}

function pushHistoryChip(prefix, ev) {
  const chip = document.createElement("span");
  chip.className = `chip win-${ev.win}`;
  chip.textContent = `${mvLabel(ev.a.mv)} ${mvLabel(ev.b.mv)}`;
  chip.title = `turn ${ev.t}: ${ev.a.mv} vs ${ev.b.mv} — ` +
    (ev.win === "tie" ? "tie" : ev.win === "a" ? "red wins" : "blue wins");
  $(`${prefix}-history`).appendChild(chip);
  chip.scrollIntoView({ block: "nearest" });
}

const REASON_COPY = {
  elimination: "out of cards",
  "mutual elimination": "double KO — both hands hit zero",
  "turn limit": "turn 100 — biggest hand wins",
  resignation: "resignation",
};
function friendlyReason(r) {
  if (REASON_COPY[r]) return REASON_COPY[r];
  return r; // forfeit reasons come through verbatim from the engine
}

/* ==================================================================== PLAY */

const play = {
  sid: null, over: true, botName: "",
  hand: [7, 7, 7], coins: 0, oppHand: [7, 7, 7], oppCoins: 0,
  turn: 1, maxTurns: 100,
  selCard: null, selUp: "", selDisguise: null, busy: false,
};

function playSyncState(ev) {
  play.turn = ev.turn;
  play.hand = ev.a.hand; play.coins = ev.a.coins;
  play.oppHand = ev.b.hand; play.oppCoins = ev.b.coins;
}

function renderPlayLedgers(earnedA, earnedB) {
  renderLedger($("play-you"), {
    corner: "red", name: "you", hand: play.hand, coins: play.coins, earned: earnedA,
  });
  renderLedger($("play-opp"), {
    corner: "blue", name: play.botName, hand: play.oppHand, coins: play.oppCoins, earned: earnedB,
  });
  $("play-turn").textContent = `turn ${play.turn}/${play.maxTurns}`;
  $("play-prog").style.width = `${(100 * (play.turn - 1)) / play.maxTurns}%`;
}

function upgradeCost(up) { return up === "!" ? 2 : up === "" ? 0 : 1; }

function renderPickers() {
  const cardEl = $("pick-card");
  cardEl.innerHTML = "";
  SHAPES.forEach((s, i) => {
    const b = document.createElement("button");
    b.className = "pick-btn" + (play.selCard === s ? " sel" : "");
    b.disabled = play.hand[i] === 0 || play.over || play.busy;
    b.innerHTML = `<span class="big-glyph">${GLYPH[s]}</span> ${SHAPE_NAME[s]} <small>×${play.hand[i]}</small>`;
    b.onclick = () => { play.selCard = s; renderPickers(); };
    cardEl.appendChild(b);
  });

  const upEl = $("pick-upgrade");
  upEl.innerHTML = "";
  [["", "plain", "free"], ["+", "upgrade", "1🪙 beats its plain self"],
   ["?", "disguise", "1🪙 fights as another shape"], ["!", "bomb", "2🪙 dies loudly"]]
    .forEach(([up, label, hint]) => {
      const b = document.createElement("button");
      b.className = "pick-btn" + (play.selUp === up ? " sel" : "");
      b.disabled = play.over || play.busy || play.coins < upgradeCost(up);
      b.innerHTML = `${label} <small>${hint}</small>`;
      b.onclick = () => {
        play.selUp = up;
        if (up !== "?") play.selDisguise = null;
        renderPickers();
      };
      upEl.appendChild(b);
    });

  const dgEl = $("pick-disguise");
  dgEl.hidden = play.selUp !== "?";
  dgEl.innerHTML = "";
  if (play.selUp === "?" && play.selCard) {
    SHAPES.filter((s) => s !== play.selCard).forEach((s) => {
      const b = document.createElement("button");
      b.className = "pick-btn" + (play.selDisguise === s ? " sel" : "");
      b.innerHTML = `fight as <span class="big-glyph">${GLYPH[s]}</span>`;
      b.onclick = () => { play.selDisguise = s; renderPickers(); };
      dgEl.appendChild(b);
    });
  }

  const ready = play.selCard && !play.over && !play.busy &&
    (play.selUp !== "?" || play.selDisguise);
  $("play-go").disabled = !ready;
}

async function playNewGame() {
  if (play.sid) api(`/api/session/${play.sid}`, undefined, "DELETE").catch(() => {});
  Object.assign(play, {
    sid: null, over: false, hand: [7, 7, 7], coins: 0,
    oppHand: [7, 7, 7], oppCoins: 0, turn: 1,
    selCard: null, selUp: "", selDisguise: null, busy: false,
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
  setCardBack($("play-card-a"));
  setCardBack($("play-card-b"));
  $("play-clash").textContent = "pick a card, then an upgrade";
  renderPlayLedgers();
  renderPickers();
}

function showPlayEnd(ev) {
  play.over = true;
  play.sid = null;
  $("play-resign").hidden = true;
  const b = $("play-banner");
  const who = ev.winner === "a" ? "win" : ev.winner === "b" ? "lose" : "draw";
  b.className = "banner " + who;
  const headline = who === "win" ? "🏆 You win!" : who === "lose" ? `${play.botName} wins` : "🤝 Draw";
  b.innerHTML = `<b>${headline}</b><br>${friendlyReason(ev.reason)} · ` +
    `final cards you ${ev.final.ah[0] + ev.final.ah[1] + ev.final.ah[2]} — ` +
    `${play.botName} ${ev.final.bh[0] + ev.final.bh[1] + ev.final.bh[2]}`;
  b.hidden = false;
  renderPickers();
}

async function playCard() {
  if (play.busy || play.over || !play.selCard) return;
  let spec = play.selCard;
  if (play.selUp === "?") spec += "?" + play.selDisguise;
  else spec += play.selUp;

  play.busy = true;
  renderPickers();
  setCardBack($("play-card-a"));
  setCardBack($("play-card-b"));
  try {
    const d = await api(`/api/session/${play.sid}/move`, { move: spec });
    for (const ev of d.events) {
      if (ev.type === "illegal") toast("that move is illegal: " + ev.reason);
      if (ev.type === "turn") {
        await animateDuel("play", ev);
        pushHistoryChip("play", ev);
        play.hand = ev.after.ah; play.coins = ev.after.ac;
        play.oppHand = ev.after.bh; play.oppCoins = ev.after.bc;
        renderPlayLedgers(ev.a.earned, ev.b.earned);
      }
      if (ev.type === "state") { playSyncState(ev); renderPlayLedgers(); }
      if (ev.type === "end") showPlayEnd(ev);
      if (ev.type === "error") toast(ev.reason);
    }
  } catch (e) { toast(e.message); }
  play.busy = false;
  play.selCard = null; play.selUp = ""; play.selDisguise = null;
  renderPickers();
}

$("play-new").onclick = playNewGame;
$("play-go").onclick = playCard;
$("play-resign").onclick = async () => {
  if (!play.sid) return;
  await api(`/api/session/${play.sid}`, undefined, "DELETE").catch(() => {});
  showPlayEnd({ winner: "b", reason: "resignation", final: { ah: play.hand, bh: play.oppHand } });
};

/* =================================================================== WATCH */

const watch = { replay: null, idx: 0, playing: false, timer: null, gen: 0 };

function watchRenderAt(idx, { animate } = {}) {
  const r = watch.replay;
  const state = idx === 0
    ? { ah: [7, 7, 7], bh: [7, 7, 7], ac: 0, bc: 0 }
    : r.turns[idx - 1].after;
  renderLedger($("watch-youL"), { corner: "red", name: r.aName, hand: state.ah, coins: state.ac });
  renderLedger($("watch-oppL"), { corner: "blue", name: r.bName, hand: state.bh, coins: state.bc });
  $("watch-turn").textContent = `turn ${Math.max(1, idx)}/${r.turns.length}`;
  $("watch-prog").style.width = r.turns.length ? `${(100 * idx) / r.turns.length}%` : "0%";
  $("w-pos").textContent = `${idx}/${r.turns.length}`;
  $("w-scrub").value = idx;
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
}

async function watchStep() {
  const r = watch.replay;
  if (!r || watch.idx >= r.turns.length) { watchStop(); return false; }
  const ev = r.turns[watch.idx];
  const factor = Math.max(0.15, Math.min(1, Number($("w-speed").value) / 450));
  const gen = watch.gen;
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
        const tilt = rate - 0.5; // >0: row bot on top → blue; <0 → red
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
  $("build-out").textContent = "running make…";
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
