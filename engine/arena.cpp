// ============================================================================
//  arena.cpp — match runner for Advanced Tactical RPS V2
//
//  Bots are separate executables speaking a line protocol on stdin/stdout
//  (see sdk/rps.h). Every turn has two exchanges:
//      -> state ...            <- shop <tok>
//      -> shopped my X opp Y   <- attack <tok>
//  The arena enforces per-phase time limits and turns crashes / hangs /
//  illegal tokens into clean forfeits.
//
//  Modes:
//    arena play <botA> <botB>            one game (pretty text, or --json)
//    arena match <botA> <botB> -n 100    a series + stats
//    arena tournament [bots...]          round robin over build/bots
//    arena interactive <bot> --human a|b JSON event stream for a human game
// ============================================================================
#include "rps.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace rps;

// ---------------------------------------------------------------- utilities

static long long nowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static std::string jesc(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char b[8];
          std::snprintf(b, sizeof b, "\\u%04x", c);
          o += b;
        } else {
          o += c;
        }
    }
  }
  return o;
}

static const char* shapeEmoji(Shape s) {
  switch (s) {
    case ROCK: return "🪨";
    case PAPER: return "📄";
    default: return "✂️";
  }
}

static std::string attackEmoji(const Attack& a) {
  if (a.bomb) return "💣";
  std::string s = shapeEmoji(a.shape);
  if (a.tier > 0) s += "+" + std::to_string(a.tier);
  return s;
}

// ------------------------------------------------------------- bot process
// One struct, two backends: POSIX fork/pipe/poll and Win32 CreateProcess/
// anonymous pipes/PeekNamedPipe. Same contract either way: line-oriented IO
// with per-read deadlines; `dead` distinguishes crash/EOF from a timeout.

#ifdef _WIN32

struct Proc {
  PROCESS_INFORMATION pi{};
  HANDLE toChild = nullptr, fromChild = nullptr;
  std::string rbuf;
  bool dead = false;

  bool spawn(const std::string& exePath, unsigned seed) {
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE inR = nullptr, inW = nullptr, outR = nullptr, outW = nullptr;
    if (!CreatePipe(&inR, &inW, &sa, 0)) return false;
    if (!CreatePipe(&outR, &outW, &sa, 0)) {
      CloseHandle(inR); CloseHandle(inW);
      return false;
    }
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);   // our ends stay ours
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);

    // Games run sequentially, so the parent env is a safe seed channel.
    SetEnvironmentVariableA("RPS_SEED", std::to_string(seed).c_str());

    STARTUPINFOA si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inR;
    si.hStdOutput = outW;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    std::string cmd = "\"" + exePath + "\"";
    BOOL ok = CreateProcessA(nullptr, &cmd[0], nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(inR);
    CloseHandle(outW);
    if (!ok) {
      CloseHandle(inW); CloseHandle(outR);
      return false;
    }
    toChild = inW;
    fromChild = outR;
    return true;
  }

  bool writeLine(const std::string& s) {
    if (dead || !toChild) return false;
    std::string line = s + "\n";
    DWORD off = 0;
    while (off < line.size()) {
      DWORD n = 0;
      if (!WriteFile(toChild, line.data() + off, static_cast<DWORD>(line.size()) - off,
                     &n, nullptr)) {
        dead = true;
        return false;
      }
      off += n;
    }
    return true;
  }

  bool readLine(std::string& out, int timeoutMs) {
    long long deadline = nowMs() + timeoutMs;
    for (;;) {
      size_t nl = rbuf.find('\n');
      if (nl != std::string::npos) {
        out = rbuf.substr(0, nl);
        rbuf.erase(0, nl + 1);
        if (!out.empty() && out.back() == '\r') out.pop_back();
        return true;
      }
      if (dead) return false;
      DWORD avail = 0;
      if (!PeekNamedPipe(fromChild, nullptr, 0, nullptr, &avail, nullptr)) {
        dead = true;  // pipe broken — child is gone
        return false;
      }
      if (avail > 0) {
        char buf[4096];
        DWORD n = 0;
        if (!ReadFile(fromChild, buf, sizeof buf, &n, nullptr) || n == 0) {
          dead = true;
          return false;
        }
        rbuf.append(buf, n);
        continue;
      }
      if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
        dead = true;  // exited and the pipe is drained
        return false;
      }
      if (nowMs() >= deadline) return false;  // timeout
      Sleep(2);
    }
  }

  void terminate() {
    if (toChild) CloseHandle(toChild);
    if (fromChild) CloseHandle(fromChild);
    toChild = fromChild = nullptr;
    if (pi.hProcess) {
      TerminateProcess(pi.hProcess, 1);
      WaitForSingleObject(pi.hProcess, 2000);
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
      pi = PROCESS_INFORMATION{};
    }
  }
};

#else  // ---------------------------------------------------------- POSIX

struct Proc {
  pid_t pid = -1;
  int toChild = -1, fromChild = -1;
  std::string rbuf;
  bool dead = false;

  bool spawn(const std::string& exePath, unsigned seed) {
    int inPipe[2], outPipe[2];
    if (pipe(inPipe) != 0) return false;
    if (pipe(outPipe) != 0) { close(inPipe[0]); close(inPipe[1]); return false; }
    pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
      dup2(inPipe[0], 0);
      dup2(outPipe[1], 1);
      close(inPipe[0]); close(inPipe[1]);
      close(outPipe[0]); close(outPipe[1]);
      std::string s = std::to_string(seed);
      setenv("RPS_SEED", s.c_str(), 1);
      execl(exePath.c_str(), exePath.c_str(), static_cast<char*>(nullptr));
      _exit(127);
    }
    close(inPipe[0]);
    close(outPipe[1]);
    toChild = inPipe[1];
    fromChild = outPipe[0];
    fcntl(fromChild, F_SETFL, O_NONBLOCK);
    return true;
  }

  bool writeLine(const std::string& s) {
    if (dead || toChild < 0) return false;
    std::string line = s + "\n";
    size_t off = 0;
    while (off < line.size()) {
      ssize_t n = write(toChild, line.data() + off, line.size() - off);
      if (n < 0) {
        if (errno == EINTR) continue;
        dead = true;
        return false;
      }
      off += static_cast<size_t>(n);
    }
    return true;
  }

  bool readLine(std::string& out, int timeoutMs) {
    long long deadline = nowMs() + timeoutMs;
    for (;;) {
      size_t nl = rbuf.find('\n');
      if (nl != std::string::npos) {
        out = rbuf.substr(0, nl);
        rbuf.erase(0, nl + 1);
        if (!out.empty() && out.back() == '\r') out.pop_back();
        return true;
      }
      if (dead) return false;
      long long remain = deadline - nowMs();
      if (remain < 0) remain = 0;
      struct pollfd pfd { fromChild, POLLIN, 0 };
      int pr = poll(&pfd, 1, static_cast<int>(remain));
      if (pr == 0) return false;  // timeout
      if (pr < 0) {
        if (errno == EINTR) continue;
        dead = true;
        return false;
      }
      char buf[4096];
      ssize_t n = read(fromChild, buf, sizeof buf);
      if (n > 0) {
        rbuf.append(buf, static_cast<size_t>(n));
      } else if (n == 0) {
        dead = true;
        return false;
      } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        dead = true;
        return false;
      }
    }
  }

  void terminate() {
    if (toChild >= 0) close(toChild);
    if (fromChild >= 0) close(fromChild);
    toChild = fromChild = -1;
    if (pid > 0) {
      kill(pid, SIGKILL);
      waitpid(pid, nullptr, 0);
      pid = -1;
    }
  }
};

#endif  // _WIN32

// ------------------------------------------------------------------- config

struct Config {
  int timeMs = 300;    // per-PHASE budget advertised to bots
  int graceMs = 50;    // scheduling slack added on top before forfeiting
  int initMs = 2000;   // budget for startup + name reply
  int maxTurns = 100;
  unsigned seed = 1;
  bool json = false;
};

struct BotRef {
  std::string id;    // bare name, e.g. "gambit" — canonical identity everywhere
  std::string path;  // executable path
};

// -------------------------------------------------------------- game record

struct TurnLog {
  int t = 0;
  Shop sa, sb;
  Attack a, b;
  TurnResult r;
  Player aAfter, bAfter;
};

struct GameLog {
  BotRef A, B;
  std::string aDisp, bDisp;
  std::vector<TurnLog> turns;
  char winner = 'd';         // 'a' | 'b' | 'd'
  std::string reason;
  char forfeitSide = 0;      // 0 none, 'a', 'b', 'x' (both)
  std::string forfeitWhy;
  Player finalA, finalB;
  unsigned seed = 0;
};

// ----------------------------------------------------------------- protocol

static std::string sideStateTok(const Player& p) {
  return std::to_string(p.coins) + " " + std::to_string(p.bombs) + " " +
         detail::tierListStr(p.cards[0]) + " " + detail::tierListStr(p.cards[1]) + " " +
         detail::tierListStr(p.cards[2]);
}

static std::string stateLine(int turn, const Player& me, const Player& opp,
                             const Attack* myLast, const Attack* oppLast) {
  std::ostringstream o;
  o << "state turn " << turn
    << " my " << sideStateTok(me)
    << " opp " << sideStateTok(opp)
    << " mylast " << (myLast ? myLast->str() : "-")
    << " opplast " << (oppLast ? oppLast->str() : "-");
  return o.str();
}

struct SideFailure {
  bool failed = false;
  std::string why;
};

// Reads "<keyword> <token>" within the deadline; parse+validate via callback.
template <typename ParseValidate>
static SideFailure readPhase(Proc& p, long long deadline, const char* keyword,
                             ParseValidate pv) {
  SideFailure f;
  std::string line;
  long long remain = deadline - nowMs();
  if (remain < 0) remain = 0;
  if (!p.readLine(line, static_cast<int>(remain))) {
    f.failed = true;
    f.why = p.dead ? "crashed" : std::string("timeout (") + keyword + " phase)";
    return f;
  }
  std::string prefix = std::string(keyword) + " ";
  if (line.rfind(prefix, 0) != 0) {
    f.failed = true;
    f.why = "bad reply (expected '" + std::string(keyword) + " <tok>', got '" +
            line.substr(0, 40) + "')";
    return f;
  }
  std::string err = pv(line.substr(prefix.size()));
  if (!err.empty()) {
    f.failed = true;
    f.why = err;
  }
  return f;
}

// --------------------------------------------------------------- run a game

static GameLog runGame(const BotRef& A, const BotRef& B, const Config& cfg) {
  GameLog g;
  g.A = A; g.B = B;
  g.aDisp = A.id; g.bDisp = B.id;
  g.seed = cfg.seed;

  Proc pa, pb;
  bool aUp = pa.spawn(A.path, cfg.seed * 2 + 1);
  bool bUp = pb.spawn(B.path, cfg.seed * 2 + 2);

  Player a, b;

  auto finish = [&](char winner, const std::string& reason) {
    g.winner = winner;
    g.reason = reason;
    g.finalA = a;
    g.finalB = b;
    const char* ra = winner == 'd' ? "draw" : (winner == 'a' ? "win" : "lose");
    const char* rb = winner == 'd' ? "draw" : (winner == 'b' ? "win" : "lose");
    pa.writeLine(std::string("end ") + ra + " reason " + reason);
    pb.writeLine(std::string("end ") + rb + " reason " + reason);
    pa.terminate();
    pb.terminate();
  };
  auto forfeit = [&](char side, const std::string& whyA, const std::string& whyB) {
    g.forfeitSide = side;
    g.forfeitWhy = side == 'a' ? whyA : side == 'b' ? whyB : whyA + " / " + whyB;
    if (side == 'x') finish('d', "double forfeit");
    else finish(side == 'a' ? 'b' : 'a', std::string("forfeit: ") + g.forfeitWhy);
  };

  if (!aUp || !bUp) {
    if (!aUp && !bUp) forfeit('x', "spawn failed", "spawn failed");
    else forfeit(!aUp ? 'a' : 'b', "spawn failed", "spawn failed");
    return g;
  }

  std::ostringstream init;
  init << " time_ms " << cfg.timeMs << " max_turns " << cfg.maxTurns;
  pa.writeLine("init player 0" + init.str());
  pb.writeLine("init player 1" + init.str());

  {
    long long deadline = nowMs() + cfg.initMs;
    std::string la, lb;
    bool oka = pa.readLine(la, cfg.initMs);
    bool okb = pb.readLine(lb, static_cast<int>(std::max(0LL, deadline - nowMs())));
    if (!oka || !okb) {
      if (!oka && !okb) forfeit('x', "no init reply", "no init reply");
      else forfeit(!oka ? 'a' : 'b', "no init reply", "no init reply");
      return g;
    }
    if (la.rfind("name ", 0) == 0) g.aDisp = la.substr(5, 32);
    if (lb.rfind("name ", 0) == 0) g.bDisp = lb.substr(5, 32);
  }

  Attack lastA, lastB;
  bool haveLast = false;

  for (int t = 1; t <= cfg.maxTurns; t++) {
    pa.writeLine(stateLine(t, a, b, haveLast ? &lastA : nullptr, haveLast ? &lastB : nullptr));
    pb.writeLine(stateLine(t, b, a, haveLast ? &lastB : nullptr, haveLast ? &lastA : nullptr));

    // ---- shop phase (simultaneous)
    long long deadline = nowMs() + cfg.timeMs + cfg.graceMs;
    Shop sa, sb;
    SideFailure fa = readPhase(pa, deadline, "shop", [&](const std::string& tok) {
      if (!Shop::parse(tok, sa)) return "unparseable shop '" + tok.substr(0, 20) + "'";
      std::string bad = validateShop(a, sa);
      return bad.empty() ? std::string() : "illegal shop " + sa.str() + " (" + bad + ")";
    });
    SideFailure fb = readPhase(pb, deadline, "shop", [&](const std::string& tok) {
      if (!Shop::parse(tok, sb)) return "unparseable shop '" + tok.substr(0, 20) + "'";
      std::string bad = validateShop(b, sb);
      return bad.empty() ? std::string() : "illegal shop " + sb.str() + " (" + bad + ")";
    });
    if (fa.failed || fb.failed) {
      forfeit(fa.failed && fb.failed ? 'x' : (fa.failed ? 'a' : 'b'), fa.why, fb.why);
      return g;
    }
    applyShop(a, sa);
    applyShop(b, sb);
    pa.writeLine("shopped my " + sa.str() + " opp " + sb.str());
    pb.writeLine("shopped my " + sb.str() + " opp " + sa.str());

    // ---- attack phase (simultaneous)
    deadline = nowMs() + cfg.timeMs + cfg.graceMs;
    Attack aa, ab;
    fa = readPhase(pa, deadline, "attack", [&](const std::string& tok) {
      if (!Attack::parse(tok, aa)) return "unparseable attack '" + tok.substr(0, 20) + "'";
      std::string bad = validateAttack(a, aa);
      return bad.empty() ? std::string() : "illegal attack " + aa.str() + " (" + bad + ")";
    });
    fb = readPhase(pb, deadline, "attack", [&](const std::string& tok) {
      if (!Attack::parse(tok, ab)) return "unparseable attack '" + tok.substr(0, 20) + "'";
      std::string bad = validateAttack(b, ab);
      return bad.empty() ? std::string() : "illegal attack " + ab.str() + " (" + bad + ")";
    });
    if (fa.failed || fb.failed) {
      forfeit(fa.failed && fb.failed ? 'x' : (fa.failed ? 'a' : 'b'), fa.why, fb.why);
      return g;
    }

    TurnLog tl;
    tl.t = t;
    tl.sa = sa; tl.sb = sb;
    tl.a = aa; tl.b = ab;
    tl.r = applyCombat(a, b, aa, ab, t);
    tl.aAfter = a; tl.bAfter = b;
    g.turns.push_back(tl);

    lastA = aa; lastB = ab; haveLast = true;

    bool aOut = a.total() == 0, bOut = b.total() == 0;
    if (aOut || bOut) {
      if (aOut && bOut) finish('d', "mutual elimination");
      else finish(aOut ? 'b' : 'a', "elimination");
      return g;
    }
  }

  finish(adjudicateByCards(a, b), "turn limit");
  return g;
}

// ------------------------------------------------------------- JSON output

static std::string tierPairsJson(const std::map<int, int>& m) {
  std::string s = "[";
  bool first = true;
  for (const auto& [t, c] : m) {
    if (c <= 0) continue;
    if (!first) s += ",";
    first = false;
    s += "[" + std::to_string(t) + "," + std::to_string(c) + "]";
  }
  return s + "]";
}

static std::string sideJson(const Player& p) {
  std::ostringstream o;
  o << "{\"coins\":" << p.coins << ",\"bombs\":" << p.bombs
    << ",\"r\":" << tierPairsJson(p.cards[0])
    << ",\"p\":" << tierPairsJson(p.cards[1])
    << ",\"s\":" << tierPairsJson(p.cards[2])
    << ",\"total\":" << p.total() << "}";
  return o.str();
}

static std::string turnJson(const TurnLog& t) {
  auto side = [](const Shop& shop, const Attack& atk, int earned, bool destroyed,
                 const std::string& lost) {
    std::ostringstream o;
    o << "{\"shop\":\"" << shop.str() << "\",\"mv\":\"" << atk.str()
      << "\",\"earned\":" << earned << ",\"destroyed\":" << (destroyed ? "true" : "false")
      << ",\"lost\":" << (lost.empty() ? "null" : "\"" + lost + "\"") << "}";
    return o.str();
  };
  std::ostringstream o;
  o << "{\"t\":" << t.t << ",\"special\":" << (t.r.special ? "true" : "false")
    << ",\"a\":" << side(t.sa, t.a, t.r.aEarned, t.r.aDestroyed, t.r.aSpecialLost)
    << ",\"b\":" << side(t.sb, t.b, t.r.bEarned, t.r.bDestroyed, t.r.bSpecialLost)
    << ",\"win\":\""
    << (t.r.bombTrade ? "trade" : t.r.winner > 0 ? "a" : t.r.winner < 0 ? "b" : "tie")
    << "\",\"after\":{\"a\":" << sideJson(t.aAfter) << ",\"b\":" << sideJson(t.bAfter) << "}}";
  return o.str();
}

static std::string gameJson(const GameLog& g) {
  std::ostringstream o;
  o << "{\"a\":\"" << jesc(g.A.id) << "\",\"b\":\"" << jesc(g.B.id) << "\""
    << ",\"aName\":\"" << jesc(g.aDisp) << "\",\"bName\":\"" << jesc(g.bDisp) << "\""
    << ",\"winner\":\"" << (g.winner == 'a' ? "a" : g.winner == 'b' ? "b" : "draw") << "\""
    << ",\"reason\":\"" << jesc(g.reason) << "\""
    << ",\"seed\":" << g.seed
    << ",\"turnsPlayed\":" << g.turns.size();
  if (g.forfeitSide) {
    o << ",\"forfeit\":{\"side\":\""
      << (g.forfeitSide == 'x' ? "both" : g.forfeitSide == 'a' ? "a" : "b")
      << "\",\"why\":\"" << jesc(g.forfeitWhy) << "\"}";
  } else {
    o << ",\"forfeit\":null";
  }
  o << ",\"final\":{\"a\":" << sideJson(g.finalA) << ",\"b\":" << sideJson(g.finalB) << "}";
  o << ",\"turns\":[";
  for (size_t i = 0; i < g.turns.size(); i++) {
    if (i) o << ",";
    o << turnJson(g.turns[i]);
  }
  o << "]}";
  return o.str();
}

// ------------------------------------------------------------ pretty output

static void printGame(const GameLog& g) {
  std::printf("⚔️  %s (%s)  vs  %s (%s)   [seed %u]\n\n",
              g.A.id.c_str(), g.aDisp.c_str(), g.B.id.c_str(), g.bDisp.c_str(), g.seed);
  for (const TurnLog& t : g.turns) {
    const char* res = t.r.bombTrade ? "trade  "
                      : t.r.winner > 0 ? "A wins "
                      : t.r.winner < 0 ? "B wins " : "tie    ";
    std::string shops;
    if (t.sa.kind != Shop::NONE || t.sb.kind != Shop::NONE)
      shops = "  [buy " + t.sa.str() + "|" + t.sb.str() + "]";
    std::string burns;
    if (!t.r.aSpecialLost.empty()) burns += "  A burns " + t.r.aSpecialLost;
    if (!t.r.bSpecialLost.empty()) burns += "  B burns " + t.r.bSpecialLost;
    std::printf("turn %3d%s %-12s vs %-12s → %s | A:%2d🃏 %2d🪙  B:%2d🃏 %2d🪙%s%s\n",
                t.t, t.r.special ? "⚠" : ":", attackEmoji(t.a).c_str(),
                attackEmoji(t.b).c_str(), res,
                t.aAfter.total(), t.aAfter.coins, t.bAfter.total(), t.bAfter.coins,
                shops.c_str(), burns.c_str());
  }
  std::printf("\n");
  if (g.winner == 'd') {
    std::printf("🤝 Draw (%s) after %zu turns", g.reason.c_str(), g.turns.size());
  } else {
    const BotRef& w = g.winner == 'a' ? g.A : g.B;
    std::printf("🏆 %s wins by %s after %zu turns", w.id.c_str(), g.reason.c_str(),
                g.turns.size());
  }
  std::printf("  —  final cards A:%d B:%d, coins A:%d B:%d\n",
              g.finalA.total(), g.finalB.total(), g.finalA.coins, g.finalB.coins);
}

// -------------------------------------------------------------- bot lookup

#ifdef _WIN32
static const char* kExeSuffix = ".exe";
#else
static const char* kExeSuffix = "";
#endif

static bool runnable(const fs::path& p) {
#ifdef _WIN32
  return fs::exists(p) && p.extension() == ".exe";
#else
  return fs::exists(p) && access(p.string().c_str(), X_OK) == 0;
#endif
}

static std::vector<BotRef> discoverBots(const std::string& dir = "build/bots") {
  std::vector<BotRef> out;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(dir, ec)) {
    if (!e.is_regular_file(ec)) continue;
    std::string name = e.path().filename().string();
    if (name.empty() || name[0] == '.') continue;
    if (!runnable(e.path())) continue;
    out.push_back({e.path().stem().string(), e.path().string()});
  }
  std::sort(out.begin(), out.end(), [](const BotRef& x, const BotRef& y) { return x.id < y.id; });
  return out;
}

static BotRef resolveBot(const std::string& arg) {
  BotRef r;
  if (arg.find('/') != std::string::npos || arg.find('\\') != std::string::npos) {
    r.path = arg;
    r.id = fs::path(arg).stem().string();
  } else {
    r.path = "build/bots/" + arg + kExeSuffix;
    r.id = arg;
  }
  if (!runnable(r.path)) {
    std::fprintf(stderr, "error: bot '%s' not found or not executable (%s)\n",
                 arg.c_str(), r.path.c_str());
    std::fprintf(stderr, "available bots:");
    for (const BotRef& b : discoverBots()) std::fprintf(stderr, " %s", b.id.c_str());
    std::fprintf(stderr, "\n(build them with the start script or build.py)\n");
    std::exit(2);
  }
  return r;
}

// -------------------------------------------------------------- match mode

static int runMatch(const BotRef& A, const BotRef& B, int n, Config cfg) {
  int aw = 0, bw = 0, dr = 0, af = 0, bf = 0;
  long long turnSum = 0;
  std::vector<std::string> replays;
  unsigned base = cfg.seed;
  for (int i = 0; i < n; i++) {
    cfg.seed = base + static_cast<unsigned>(i);
    GameLog g = runGame(A, B, cfg);
    if (g.winner == 'a') aw++;
    else if (g.winner == 'b') bw++;
    else dr++;
    if (g.forfeitSide == 'a' || g.forfeitSide == 'x') af++;
    if (g.forfeitSide == 'b' || g.forfeitSide == 'x') bf++;
    turnSum += static_cast<long long>(g.turns.size());
    if (cfg.json) replays.push_back(gameJson(g));
    else if (n <= 20)
      std::printf("game %2d: %s (%s, %zu turns)\n", i + 1,
                  g.winner == 'a' ? A.id.c_str() : g.winner == 'b' ? B.id.c_str() : "draw",
                  g.reason.c_str(), g.turns.size());
  }
  double avg = n ? static_cast<double>(turnSum) / n : 0.0;
  if (cfg.json) {
    std::ostringstream o;
    o << "{\"type\":\"match\",\"a\":\"" << jesc(A.id) << "\",\"b\":\"" << jesc(B.id)
      << "\",\"n\":" << n << ",\"aWins\":" << aw << ",\"bWins\":" << bw
      << ",\"draws\":" << dr << ",\"aForfeits\":" << af << ",\"bForfeits\":" << bf
      << ",\"avgTurns\":" << avg << ",\"games\":[";
    for (size_t i = 0; i < replays.size(); i++) {
      if (i) o << ",";
      o << replays[i];
    }
    o << "]}";
    std::printf("%s\n", o.str().c_str());
  } else {
    int barw = 40;
    int ab = n ? aw * barw / n : 0, db = n ? dr * barw / n : 0;
    std::string bar(static_cast<size_t>(ab), '#');
    bar += std::string(static_cast<size_t>(db), '=');
    bar.resize(barw, '-');
    std::printf("\n%s vs %s over %d games:\n", A.id.c_str(), B.id.c_str(), n);
    std::printf("  %-12s %4d wins (%.1f%%)%s\n", A.id.c_str(), aw, n ? 100.0 * aw / n : 0,
                af ? (" [" + std::to_string(af) + " forfeits]").c_str() : "");
    std::printf("  %-12s %4d wins (%.1f%%)%s\n", B.id.c_str(), bw, n ? 100.0 * bw / n : 0,
                bf ? (" [" + std::to_string(bf) + " forfeits]").c_str() : "");
    std::printf("  %-12s %4d (%.1f%%)\n", "draws", dr, n ? 100.0 * dr / n : 0);
    std::printf("  [%s]  avg %.1f turns\n", bar.c_str(), avg);
  }
  return 0;
}

// --------------------------------------------------------- tournament mode

struct Standing {
  std::string name;
  int played = 0, wins = 0, losses = 0, draws = 0;
  double points() const { return wins + 0.5 * draws; }
};

static int runTournament(std::vector<BotRef> bots, int gamesPerPair, Config cfg) {
  size_t n = bots.size();
  if (n < 2) {
    std::fprintf(stderr, "error: need at least 2 bots (found %zu in build/bots)\n", n);
    return 2;
  }
  std::vector<Standing> st(n);
  for (size_t i = 0; i < n; i++) st[i].name = bots[i].id;
  struct Cell { int w = 0, l = 0, d = 0; };
  std::vector<std::vector<Cell>> grid(n, std::vector<Cell>(n));

  unsigned base = cfg.seed;
  unsigned pairIdx = 0;
  for (size_t i = 0; i < n; i++) {
    for (size_t j = i + 1; j < n; j++, pairIdx++) {
      for (int gi = 0; gi < gamesPerPair; gi++) {
        cfg.seed = base + pairIdx * 1000u + static_cast<unsigned>(gi);
        GameLog g = runGame(bots[i], bots[j], cfg);
        if (g.winner == 'a') {
          st[i].wins++; st[j].losses++; grid[i][j].w++; grid[j][i].l++;
        } else if (g.winner == 'b') {
          st[j].wins++; st[i].losses++; grid[i][j].l++; grid[j][i].w++;
        } else {
          st[i].draws++; st[j].draws++; grid[i][j].d++; grid[j][i].d++;
        }
        st[i].played++; st[j].played++;
      }
      if (!cfg.json)
        std::fprintf(stderr, "  %s vs %s: %d-%d-%d\n", bots[i].id.c_str(), bots[j].id.c_str(),
                     grid[i][j].w, grid[i][j].l, grid[i][j].d);
    }
  }

  std::vector<size_t> order(n);
  for (size_t i = 0; i < n; i++) order[i] = i;
  std::sort(order.begin(), order.end(), [&](size_t x, size_t y) {
    if (st[x].points() != st[y].points()) return st[x].points() > st[y].points();
    if (st[x].wins != st[y].wins) return st[x].wins > st[y].wins;
    return st[x].name < st[y].name;
  });

  if (cfg.json) {
    std::ostringstream o;
    o << "{\"type\":\"tournament\",\"gamesPerPair\":" << gamesPerPair << ",\"bots\":[";
    for (size_t i = 0; i < n; i++) o << (i ? "," : "") << "\"" << jesc(bots[i].id) << "\"";
    o << "],\"standings\":[";
    for (size_t k = 0; k < n; k++) {
      const Standing& s = st[order[k]];
      o << (k ? "," : "") << "{\"name\":\"" << jesc(s.name) << "\",\"played\":" << s.played
        << ",\"wins\":" << s.wins << ",\"losses\":" << s.losses << ",\"draws\":" << s.draws
        << ",\"points\":" << s.points() << "}";
    }
    o << "],\"grid\":[";
    for (size_t i = 0; i < n; i++) {
      o << (i ? "," : "") << "[";
      for (size_t j = 0; j < n; j++) {
        o << (j ? "," : "");
        if (i == j) o << "null";
        else
          o << "{\"w\":" << grid[i][j].w << ",\"l\":" << grid[i][j].l
            << ",\"d\":" << grid[i][j].d << "}";
      }
      o << "]";
    }
    o << "]}";
    std::printf("%s\n", o.str().c_str());
  } else {
    std::printf("\n🏆 Tournament — %d game(s) per pairing\n\n", gamesPerPair);
    std::printf("   %-16s %6s %5s %5s %5s %7s\n", "bot", "played", "W", "L", "D", "pts");
    for (size_t k = 0; k < n; k++) {
      const Standing& s = st[order[k]];
      std::printf("%2zu %-16s %6d %5d %5d %5d %7.1f\n", k + 1, s.name.c_str(), s.played,
                  s.wins, s.losses, s.draws, s.points());
    }
  }
  return 0;
}

// --------------------------------------------------------- interactive mode
// JSON events on stdout, human input on stdin. Used by the web UI's Play tab.
//   in : "shop <tok>" | "attack <tok>" | "resign"
//   out: hello / state / shopped / illegal / turn / end events

static void emitLine(const std::string& s) {
  std::printf("%s\n", s.c_str());
  std::fflush(stdout);
}

static int runInteractive(const BotRef& bot, char humanSide, Config cfg) {
  Player a, b;
  Attack lastA, lastB;
  bool haveLast = false;
  Proc p;

  auto endEvent = [&](char winner, const std::string& reason) {
    std::ostringstream o;
    o << "{\"type\":\"end\",\"winner\":\""
      << (winner == 'a' ? "a" : winner == 'b' ? "b" : "draw")
      << "\",\"reason\":\"" << jesc(reason) << "\""
      << ",\"final\":{\"a\":" << sideJson(a) << ",\"b\":" << sideJson(b) << "}}";
    emitLine(o.str());
    p.writeLine(std::string("end ") + (winner == 'd' ? "draw" : "lose") + " reason " + reason);
    p.terminate();
  };

  char botSide = humanSide == 'a' ? 'b' : 'a';
  if (!p.spawn(bot.path, cfg.seed)) {
    endEvent(humanSide, "forfeit: spawn failed");
    return 0;
  }
  std::ostringstream init;
  init << "init player " << (botSide == 'a' ? 0 : 1) << " time_ms " << cfg.timeMs
       << " max_turns " << cfg.maxTurns;
  p.writeLine(init.str());
  std::string botDisp = bot.id;
  {
    std::string l;
    if (!p.readLine(l, cfg.initMs)) {
      endEvent(humanSide, "forfeit: no init reply");
      return 0;
    }
    if (l.rfind("name ", 0) == 0) botDisp = l.substr(5, 32);
  }
  emitLine("{\"type\":\"hello\",\"bot\":\"" + jesc(bot.id) + "\",\"botName\":\"" +
           jesc(botDisp) + "\",\"human\":\"" + std::string(1, humanSide) +
           "\",\"maxTurns\":" + std::to_string(cfg.maxTurns) + "}");

  // reads one human line; returns false on EOF/resign
  auto humanLine = [&](std::string& out) {
    if (!std::getline(std::cin, out)) return false;
    while (!out.empty() && (out.back() == '\r' || out.back() == ' ')) out.pop_back();
    return out != "resign";
  };

  for (int t = 1; t <= cfg.maxTurns; t++) {
    Player& hp = humanSide == 'a' ? a : b;
    Player& bp = humanSide == 'a' ? b : a;

    emitLine("{\"type\":\"state\",\"turn\":" + std::to_string(t) +
             ",\"special\":" + (t % SPECIAL_EVERY == 0 ? "true" : "false") +
             ",\"a\":" + sideJson(a) + ",\"b\":" + sideJson(b) +
             ",\"human\":\"" + std::string(1, humanSide) + "\"}");

    // ---- human shop (retries allowed)
    Shop hShop;
    for (;;) {
      std::string line;
      if (!humanLine(line)) { endEvent(botSide, "resignation"); return 0; }
      if (line.rfind("shop ", 0) != 0) {
        emitLine("{\"type\":\"illegal\",\"phase\":\"shop\",\"reason\":\"expected 'shop <tok>' or 'resign'\"}");
        continue;
      }
      if (!Shop::parse(line.substr(5), hShop)) {
        emitLine("{\"type\":\"illegal\",\"phase\":\"shop\",\"reason\":\"unparseable purchase\"}");
        continue;
      }
      std::string bad = validateShop(hp, hShop);
      if (!bad.empty()) {
        emitLine("{\"type\":\"illegal\",\"phase\":\"shop\",\"reason\":\"" + jesc(bad) + "\"}");
        continue;
      }
      break;
    }

    // ---- bot shop (only now does the bot see this turn's state)
    const Attack* botLast = haveLast ? (botSide == 'a' ? &lastA : &lastB) : nullptr;
    const Attack* humLast = haveLast ? (botSide == 'a' ? &lastB : &lastA) : nullptr;
    p.writeLine(stateLine(t, bp, hp, botLast, humLast));
    Shop bShop;
    SideFailure f = readPhase(p, nowMs() + cfg.timeMs + cfg.graceMs, "shop",
                              [&](const std::string& tok) {
      if (!Shop::parse(tok, bShop)) return std::string("unparseable shop");
      std::string bad = validateShop(bp, bShop);
      return bad.empty() ? std::string() : "illegal shop (" + bad + ")";
    });
    if (f.failed) { endEvent(humanSide, "forfeit: " + f.why); return 0; }

    applyShop(hp, hShop);
    applyShop(bp, bShop);
    const Shop& sa = humanSide == 'a' ? hShop : bShop;
    const Shop& sb = humanSide == 'a' ? bShop : hShop;
    emitLine("{\"type\":\"shopped\",\"a\":\"" + sa.str() + "\",\"b\":\"" + sb.str() + "\"}");
    p.writeLine("shopped my " + bShop.str() + " opp " + hShop.str());

    // ---- human attack (retries allowed)
    Attack hAtk;
    for (;;) {
      std::string line;
      if (!humanLine(line)) { endEvent(botSide, "resignation"); return 0; }
      if (line.rfind("attack ", 0) != 0) {
        emitLine("{\"type\":\"illegal\",\"phase\":\"attack\",\"reason\":\"expected 'attack <tok>' or 'resign'\"}");
        continue;
      }
      if (!Attack::parse(line.substr(7), hAtk)) {
        emitLine("{\"type\":\"illegal\",\"phase\":\"attack\",\"reason\":\"unparseable attack\"}");
        continue;
      }
      std::string bad = validateAttack(hp, hAtk);
      if (!bad.empty()) {
        emitLine("{\"type\":\"illegal\",\"phase\":\"attack\",\"reason\":\"" + jesc(bad) + "\"}");
        continue;
      }
      break;
    }

    // ---- bot attack
    Attack bAtk;
    f = readPhase(p, nowMs() + cfg.timeMs + cfg.graceMs, "attack",
                  [&](const std::string& tok) {
      if (!Attack::parse(tok, bAtk)) return std::string("unparseable attack");
      std::string bad = validateAttack(bp, bAtk);
      return bad.empty() ? std::string() : "illegal attack (" + bad + ")";
    });
    if (f.failed) { endEvent(humanSide, "forfeit: " + f.why); return 0; }

    TurnLog tl;
    tl.t = t;
    tl.sa = sa; tl.sb = sb;
    tl.a = humanSide == 'a' ? hAtk : bAtk;
    tl.b = humanSide == 'a' ? bAtk : hAtk;
    tl.r = applyCombat(a, b, tl.a, tl.b, t);
    tl.aAfter = a; tl.bAfter = b;
    std::string tj = turnJson(tl);
    tj.insert(1, "\"type\":\"turn\",");
    emitLine(tj);

    lastA = tl.a; lastB = tl.b; haveLast = true;

    bool aOut = a.total() == 0, bOut = b.total() == 0;
    if (aOut || bOut) {
      if (aOut && bOut) endEvent('d', "mutual elimination");
      else endEvent(aOut ? 'b' : 'a', "elimination");
      return 0;
    }
  }
  endEvent(adjudicateByCards(a, b), "turn limit");
  return 0;
}

// --------------------------------------------------------------------- main

static void usage() {
  std::printf(
      "Crafti RPS Arena — Advanced Tactical RPS V2 match runner\n\n"
      "usage:\n"
      "  arena play <botA> <botB> [options]         one game\n"
      "  arena match <botA> <botB> -n <N> [options] N-game series\n"
      "  arena tournament [bots...] [options]       round robin (default: all bots)\n"
      "  arena interactive <bot> --human a|b        stdin/stdout session for the UI\n"
      "  arena list                                 list available bots\n\n"
      "options:\n"
      "  --json           machine-readable output\n"
      "  --time-ms <N>    per-phase time limit (default 300)\n"
      "  --turns <N>      turn limit (default 100)\n"
      "  --games <N>      tournament games per pairing (default 10)\n"
      "  -n <N>           match length (default 100)\n"
      "  --seed <N>       base RNG seed (default: current time)\n"
      "bots are names from build/bots (e.g. 'gambit') or paths to executables\n");
}

int main(int argc, char** argv) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);  // emoji in the pretty printer
#else
  signal(SIGPIPE, SIG_IGN);
#endif
  std::vector<std::string> pos;
  Config cfg;
  cfg.seed = static_cast<unsigned>(time(nullptr) & 0x7fffffff);
  int n = 100, games = 10;
  std::string human;

  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto need = [&](const char* what) -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "error: %s expects a value\n", what);
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--json") cfg.json = true;
    else if (a == "--time-ms") cfg.timeMs = std::stoi(need("--time-ms"));
    else if (a == "--turns") cfg.maxTurns = std::stoi(need("--turns"));
    else if (a == "--seed") cfg.seed = static_cast<unsigned>(std::stoul(need("--seed")));
    else if (a == "-n") n = std::stoi(need("-n"));
    else if (a == "--games") games = std::stoi(need("--games"));
    else if (a == "--human") human = need("--human");
    else if (a == "-h" || a == "--help") { usage(); return 0; }
    else if (!a.empty() && a[0] == '-') {
      std::fprintf(stderr, "error: unknown option %s\n", a.c_str());
      return 2;
    } else pos.push_back(a);
  }

  if (pos.empty()) { usage(); return 2; }
  std::string mode = pos[0];

  if (mode == "list") {
    for (const BotRef& b : discoverBots()) std::printf("%s\n", b.id.c_str());
    return 0;
  }
  if (mode == "play") {
    if (pos.size() != 3) { usage(); return 2; }
    GameLog g = runGame(resolveBot(pos[1]), resolveBot(pos[2]), cfg);
    if (cfg.json) std::printf("%s\n", gameJson(g).c_str());
    else printGame(g);
    return 0;
  }
  if (mode == "match") {
    if (pos.size() != 3) { usage(); return 2; }
    return runMatch(resolveBot(pos[1]), resolveBot(pos[2]), n, cfg);
  }
  if (mode == "tournament") {
    std::vector<BotRef> bots;
    if (pos.size() > 1)
      for (size_t i = 1; i < pos.size(); i++) bots.push_back(resolveBot(pos[i]));
    else bots = discoverBots();
    return runTournament(bots, games, cfg);
  }
  if (mode == "interactive") {
    if (pos.size() != 2 || (human != "a" && human != "b")) {
      std::fprintf(stderr, "usage: arena interactive <bot> --human a|b\n");
      return 2;
    }
    return runInteractive(resolveBot(pos[1]), human[0], cfg);
  }
  usage();
  return 2;
}
