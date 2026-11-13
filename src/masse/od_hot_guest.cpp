// opdarkness (2026-11-01) - copies localisées en masse (src/copies_masse.txt, tools/copies_masse.py) : la garde qui
// compare une copie à la fonction recompilée, et les bilans. Repris de renut-nx (ex banjo-nx,
// app/src/cpu/banjo_hot_guest.cpp, GPL-3.0 ; mécanisme de sa2-nx et masseffect-nx), sans les natives écrites à la main,
// les cartes d'écriture ni le compte rendu de vérification de banjo. Réglages :
//   od_masse (toutes), od_masse_sub_XXXXXXXX (chacune), od_masse_garde_appels / od_masse_garde_periode (PC : 64 / 4096 ;
//   Switch : 0 / 0 = appel direct, sans garde), od_masse_verif (chaque appel comparé), od_masse_compte (mesure, PC).
// Une différence qui se répète 3 fois éteint la copie pour le reste de la partie (« [masse] DIFFÉRENCE » au journal).
//
// Ne citer ici l'adresse d'aucune fonction du jeu (tools/apres_codegen.py et tools/copies_masse.py tiennent pour
// interceptée toute adresse 82xxxxxx citée dans src/).
//
// Texte d'origine (garde) :
// banjo-nx - native replacements of hot guest functions (CPU enhancements banjo_cpu_native_*).
//
// The mechanism is sa2-nx's (app/src/cpu/sa2_hot_guest.cpp, GPL-3.0; it came there from masseffect-nx's
// me_hot_guest.cpp): each hook replaces one recompiled function by a native version and checks itself. Banjo's
// natives are all "localized copies" written by tools/localize_native.py (app/generated/hot/n_<id>.h, listed in
// app/src/cpu/hot/localized.txt): the recompiled function's own statements with its context registers in C++ locals.
//
// THE GUARD. For the first banjo_hot_guard_calls calls of each thread, one call in banjo_hot_guard_period afterwards
// and EVERY call with banjo_cpu_verify, a call is answered like this:
//   1. the copy's logged form runs on a copy of the registers: every guest store is recorded with the bytes it
//      replaces; the memory is rolled back from that log;
//   2. the ORIGINAL statements' logged form runs the same way and is rolled back;
//      (if either reached a call it cannot repeat - anything outside the copy: an indirect call, a kernel import, a
//      hooked function - the call is "not comparable": the recompiled original answers it in verify mode, the copy
//      otherwise, and it is counted;)
//   3. the bytes either of them wrote are the compare set: they are saved, the real copy runs on a copy of the
//      registers, its bytes are taken and the saved ones put back;
//   4. the recompiled original runs for real;
//   5. every register of the context, the written bytes and the host's flush mode are compared, and so are the
//      registers of the two logged runs.
// A difference that repeats when the whole pass is made again from the same inputs (three passes in a row) logs
// "[hot] DIFFERENCE ...", counts as a difference of the verify session (me::native::VerifyDifference) and switches
// that hook off for the rest of the run: the original had already produced the state. One that does not repeat came
// from another thread changing the inputs between the runs; it is counted apart ("did not repeat").
// A hook also stays off unless the loaded guest code is byte for byte the code its copy was made from.
//
// WHY EXACT: the copies are the same statements (tools/localize_native.py says what is renamed); the guard runs both.
// RISK: a difference on inputs the guard did not see; state another thread changes between the guard's runs is
//   reported as a difference (a false one) and only costs that hook.
// KEYS: banjo_cpu_native_<function> each (CPU group, under banjo_cpu_enhancements); banjo_hot_guard_calls,
//   banjo_hot_guard_period; banjo_hot_count (a measurement, see Counted).
//
// Name no guest function's address in this file: tools/direct_calls.py and tools/localize_native.py treat every
// 82xxxxxx address the app's sources mention as hooked. The list is app/src/cpu/hot/localized.txt.

#include "opdarkness_pch.h"

#include "masse/od_hot_guest.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/logging.h>


#if REX_PLATFORM_SWITCH
#define OD_MASSE_DEFAUT(switch_, ailleurs) switch_
#else
#define OD_MASSE_DEFAUT(switch_, ailleurs) ailleurs
#endif

REXCVAR_DEFINE_BOOL(od_masse, true, "OpDarkness/Copies",
                    "Copies localisées en masse : chaque fonction du jeu que les profils de la console trouvent chaude "
                    "(src/copies_masse.txt) par sa copie (registres en variables C++ locales ; même résultat). false = "
                    "toutes revenues au code généré (chacune a aussi sa clé od_masse_sub_*)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(od_masse_garde_appels, OD_MASSE_DEFAUT(0, 64), "OpDarkness/Copies",
                     "Copies en masse : les N premiers appels gardés passent tous par la garde (copie et original "
                     "exécutés et comparés)")
    .range(0, 4000)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(od_masse_garde_periode, OD_MASSE_DEFAUT(0, 4096), "OpDarkness/Copies",
                     "Copies en masse : après les N premiers, un appel sur P est gardé (arrondi à une puissance de 2). "
                     "0 avec od_masse_garde_appels = 0 : aucune garde, appel direct de la copie (Switch)")
    .range(0, 1 << 20)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(od_masse_verif, false, "OpDarkness/Copies",
                    "Diagnostic : chaque appel d'une copie en masse est comparé à l'original (lent)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(od_masse_compte, false, "OpDarkness/Copies",
                    "Mesure (PC) : les copies dans leur forme qui compte les blocs (logs/hot_counts.txt) ; sans garde")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace od::hand {
void Count(uint32_t, uint32_t) {}  // aucune native écrite à la main
}  // namespace od::hand

namespace od::hot {

namespace {

Hook* g_hooks = nullptr;  // every hook (static initialisation, one thread)

// The buffers of one guarded call. A guarded call can contain others (the recompiled original calls hooked
// functions, itself among them), so a thread has one of these per nesting depth. Reached only from Check: the
// thread_local costs the guarded calls alone.
struct Scratch {
  WriteLog log;
  std::vector<Region> ranges;
  std::vector<uint8_t> before;
  std::vector<uint8_t> native_out;
  PPCContext in;  // the registers the call came with
  size_t bytes = 0;
};
struct ThreadScratch {
  std::vector<Scratch*> frames;  // live as long as the thread (guest threads are few and long-lived)
  uint32_t depth = 0;
};
ThreadScratch& Thread() {
  static thread_local ThreadScratch* scratch = nullptr;
  if (!scratch) scratch = new ThreadScratch;
  return *scratch;
}
struct ScratchFrame {
  ThreadScratch& thread;
  Scratch& scratch;
  explicit ScratchFrame(ThreadScratch& t) : thread(t), scratch(Take(t)) {}
  ~ScratchFrame() {
    --thread.depth;
    // A guarded call that wrote megabytes (a memset of a loader's buffer among a thread's first calls) leaves its
    // log and its two copies of those bytes behind, about four times what it wrote, for as long as the thread
    // lives. On the console that memory is the same that textures and the driver need: large ones are given back.
    constexpr size_t kKeepBytes = 256u << 10;
    if (size_t(scratch.log.capacity) * sizeof(WriteLog::Entry) > kKeepBytes) {
      std::free(scratch.log.entries);
      scratch.log.entries = nullptr;
      scratch.log.capacity = scratch.log.count = 0;
    }
    if (scratch.ranges.capacity() * sizeof(Region) > kKeepBytes) std::vector<Region>().swap(scratch.ranges);
    if (scratch.before.capacity() > kKeepBytes) std::vector<uint8_t>().swap(scratch.before);
    if (scratch.native_out.capacity() > kKeepBytes) std::vector<uint8_t>().swap(scratch.native_out);
  }
  static Scratch& Take(ThreadScratch& t) {
    if (t.depth == t.frames.size()) t.frames.push_back(new Scratch);
    return *t.frames[t.depth++];
  }
};

bool GuestCodeMatches(const Hook& h, const uint8_t* base) {
  uint64_t fnv = 0xCBF29CE484222325ull;
  for (uint32_t r = 0; r < h.code_ranges; ++r) {
    const uint8_t* p = base + h.code[r].address;
    for (uint32_t i = 0; i < h.code[r].bytes; ++i) {
      fnv ^= p[i];
      fnv *= 0x100000001B3ull;
    }
  }
  if (fnv == h.code_hash) return true;
  REXLOG_WARN("[masse] {} ({}): the game code at {:08X} is not the code the copy was made from (hash {:016X}, expected "
              "{:016X}); it stays off",
              h.name, h.key, h.code[0].address, fnv, h.code_hash);
  return false;
}

const PPCRegister* Gpr(const PPCContext& c, int n) {
  switch (n) {
#define G(i) \
  case i:    \
    return &c.r##i;
    G(0) G(1) G(2) G(3) G(4) G(5) G(6) G(7) G(8) G(9) G(10) G(11) G(12) G(13) G(14) G(15) G(16) G(17) G(18) G(19)
    G(20) G(21) G(22) G(23) G(24) G(25) G(26) G(27) G(28) G(29) G(30) G(31)
#undef G
  }
  return nullptr;
}
const PPCRegister* Fpr(const PPCContext& c, int n) { return &c.f0 + n; }    // f0..f31 are consecutive
const PPCVRegister* Vr(const PPCContext& c, int n) { return &c.v0 + n; }    // v0..v127 are consecutive
const PPCCRRegister* Cr(const PPCContext& c, int n) { return &c.cr0 + n; }  // cr0..cr7 are consecutive

// First difference between two contexts (every register but the hook's skip set), or an empty string. a: the copy;
// b: the original.
std::string CompareRegisters(const PPCContext& a, const PPCContext& b, const RegSkip& skip) {
  for (int i = 0; i < 32; ++i) {
    if ((skip.gpr >> i) & 1) continue;
    if (Gpr(a, i)->u64 != Gpr(b, i)->u64)
      return fmt::format("r{} copy={:#018x} original={:#018x}", i, Gpr(a, i)->u64, Gpr(b, i)->u64);
  }
  for (int i = 0; i < 32; ++i) {
    if ((skip.fpr >> i) & 1) continue;
    if (Fpr(a, i)->u64 != Fpr(b, i)->u64)
      return fmt::format("f{} copy={:#018x} original={:#018x}", i, Fpr(a, i)->u64, Fpr(b, i)->u64);
  }
  for (int i = 0; i < 128; ++i) {
    if ((skip.vr[i >> 6] >> (i & 63)) & 1) continue;
    if (std::memcmp(Vr(a, i)->u8, Vr(b, i)->u8, 16) != 0) {
      const auto* x = Vr(a, i)->u32;
      const auto* y = Vr(b, i)->u32;
      return fmt::format("v{} copy={:08x}{:08x}{:08x}{:08x} original={:08x}{:08x}{:08x}{:08x}", i, x[0], x[1], x[2],
                         x[3], y[0], y[1], y[2], y[3]);
    }
  }
  for (int i = 0; i < 8; ++i) {
    if ((skip.cr >> i) & 1) continue;
    if (Cr(a, i)->raw() != Cr(b, i)->raw())
      return fmt::format("cr{} copy={:x} original={:x}", i, Cr(a, i)->raw(), Cr(b, i)->raw());
  }
  if (!skip.ctr && a.ctr.u64 != b.ctr.u64) return fmt::format("ctr copy={:#x} original={:#x}", a.ctr.u64, b.ctr.u64);
  if (!skip.xer && (a.xer.so != b.xer.so || a.xer.ov != b.xer.ov || a.xer.ca != b.xer.ca)) return "xer";
  if (a.reserved.u64 != b.reserved.u64) return "reserved";
  if (a.lr != b.lr) return fmt::format("lr copy={:#x} original={:#x}", a.lr, b.lr);
  if (a.vscr_sat != b.vscr_sat) return "vscr_sat";
  // Only the guest-visible bits (rounding, flush): the host's sticky exception flags may differ harmlessly.
  constexpr uint32_t kGuest = rex::ppc::FPSCRRegister::GuestMask;
  if ((a.fpscr.csr & kGuest) != (b.fpscr.csr & kGuest))
    return fmt::format("fpscr.csr copy={:#x} original={:#x}", a.fpscr.csr, b.fpscr.csr);
  return {};
}

void AddRanges(const WriteLog& log, std::vector<Region>& ranges) {
  for (uint32_t i = 0; i < log.count; ++i) ranges.push_back({log.entries[i].addr, log.entries[i].len});
}

// Sorted, without overlaps.
void Merge(std::vector<Region>& ranges) {
  std::sort(ranges.begin(), ranges.end(), [](const Region& a, const Region& b) { return a.addr < b.addr; });
  size_t n = 0;
  for (const Region& r : ranges) {
    if (n && uint64_t(ranges[n - 1].addr) + ranges[n - 1].len >= r.addr) {
      const uint64_t end = std::max(uint64_t(ranges[n - 1].addr) + ranges[n - 1].len, uint64_t(r.addr) + r.len);
      ranges[n - 1].len = uint32_t(end - ranges[n - 1].addr);
    } else {
      ranges[n++] = r;
    }
  }
  ranges.resize(n);
}

std::atomic<int64_t> g_last_report_ms{0};
int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void WriteCounts() {
  FILE* f = std::fopen("logs/hot_counts.txt", "w");
  if (!f) return;
  std::fprintf(f, "# od_masse_compte: <function> <block> <entries>   (tools/hot_arm_compare.py)\n");
  for (Hook* h = g_hooks; h; h = h->next) {
    for (uint32_t i = 0; i < h->hit_count; ++i) {
      const uint64_t n = h->hits[i].load(std::memory_order_relaxed);
      if (n) std::fprintf(f, "%s %u %llu\n", h->name, i, static_cast<unsigned long long>(n));
    }
  }
  std::fclose(f);
}

}  // namespace

Hook::Hook(const char* name_, const char* key_, PPCFunc* orig_, void (*native_)(PPCContext&, uint8_t*),
           void (*native_logged_)(PPCContext&, uint8_t*, Logged&),
           void (*reference_logged_)(PPCContext&, uint8_t*, Logged&), void (*native_counted_)(PPCContext&, uint8_t*),
           std::atomic<uint64_t>* hits_, uint32_t hit_count_, const CodeRange* code_, uint32_t code_ranges_,
           uint64_t code_hash_, bool (*own_)(), uint32_t slot_, const RegSkip* skip_, bool bulk_)
    : name(name_),
      key(key_),
      orig(orig_),
      native(native_),
      native_logged(native_logged_),
      reference_logged(reference_logged_),
      native_counted(native_counted_),
      hits(hits_),
      hit_count(hit_count_),
      code(code_),
      code_ranges(code_ranges_),
      code_hash(code_hash_),
      own(own_),
      slot(slot_),
      skip(skip_),
      bulk(bulk_) {
  // In list order: the hooks are defined in the order of od_hot_bulk_<k>.inc, so append.
  Hook** tail = &g_hooks;
  while (*tail) tail = &(*tail)->next;
  *tail = this;
}

bool WriteLog::Grow() {
  if (capacity >= kMaxEntries) return false;
  const uint32_t wanted = capacity ? capacity * 2 : 4096;
  auto* grown = static_cast<Entry*>(std::realloc(entries, size_t(wanted) * sizeof(Entry)));
  if (!grown) return false;
  entries = grown;
  capacity = wanted;
  return true;
}

uint8_t InitHook(Hook& h, uint8_t* base) {
  // May run on several threads at once for the first call: every one of them stores the same values.
  bool on = REXCVAR_GET(od_masse) && h.own();
    if (on && !GuestCodeMatches(h, base)) on = false;
  const bool verify = REXCVAR_GET(od_masse_verif);
  uint32_t period = 1;
  while (period < uint32_t(std::max(1, REXCVAR_GET(od_masse_garde_periode))) && period < (1u << kCountdownBits))
    period <<= 1;
  h.verify = verify;
  h.guard_calls = uint32_t(std::clamp(REXCVAR_GET(od_masse_garde_appels), 0, 4000));
  h.period = period;
  // No guard at all: the hook calls its copy directly (od_hot_guest.h, kDirect).
  const bool direct = !verify && h.guard_calls == 0 && REXCVAR_GET(od_masse_garde_periode) == 0;
  uint8_t state = on ? (direct ? kDirect : kOn) : kOff;
  if (on && REXCVAR_GET(od_masse_compte) && h.native_counted) state = kCount;
  if (on && h.bulk) {
    // The bulk hooks say so in the summary of the reports (hundreds of first calls would each be a line).
  } else if (on && state == kDirect && h.state.load(std::memory_order_relaxed) == kUninit) {
    REXLOG_INFO("[masse] {} ({}): native copy ON, direct (no guard: od_masse_garde_appels = od_masse_garde_periode = 0)",
                h.name, h.key);
  } else if (on && h.state.load(std::memory_order_relaxed) == kUninit) {
    if (state == kCount) {
      REXLOG_INFO("[masse] {} ({}): native copy ON in its counting form (od_masse_compte; no guard)", h.name, h.key);
    } else if (verify) {
      REXLOG_INFO("[masse] {} ({}): native copy ON, verify mode (every call runs both and compares)", h.name, h.key);
    } else {
      REXLOG_INFO("[masse] {} ({}): native copy ON (guard: the first {} calls of a thread, then 1 in {})", h.name, h.key,
                  h.guard_calls, period);
    }
  }
  h.state.store(state, std::memory_order_release);
  return state;
}

void Counted(Hook& h, PPCContext& ctx, uint8_t* base) {
  h.native_counted(ctx, base);
  if ((h.uses.fetch_add(1, std::memory_order_relaxed) & 0xFFF) == 0) Report();
}

namespace {

enum class Outcome { kEqual, kDifferent, kNotComparable, kPathsDiffer };

// One pass of the guard (the steps at the top of this file). kEqual and kDifferent: the recompiled original has run
// for real and `diff` says what differed. kNotComparable / kPathsDiffer: nothing has run for real yet.
Outcome Attempt(Hook& h, PPCContext& ctx, uint8_t* base, Scratch& t, std::string& diff) {
  const uint32_t hw_before = ctx.fpscr.getcsr();
  const uint32_t csr_before = ctx.fpscr.csr;
  t.ranges.clear();
  t.in = ctx;

  // 1 and 2: what would be written? Both logged forms, each rolled back.
  PPCContext logged_native = ctx;
  t.log.Reset(base);
  {
    Logged s{t.log};
    h.native_logged(logged_native, base, s);
  }
  const bool native_stopped = t.log.stopped;
  bool overflow = t.log.overflow;
  AddRanges(t.log, t.ranges);
  t.log.RollBack();
  ctx.fpscr.setcsr(hw_before);

  PPCContext logged_reference = ctx;
  t.log.Reset(base);
  {
    Logged s{t.log};
    h.reference_logged(logged_reference, base, s);
  }
  const bool reference_stopped = t.log.stopped;
  overflow |= t.log.overflow;
  AddRanges(t.log, t.ranges);
  t.log.RollBack();
  ctx.fpscr.setcsr(hw_before);
  ctx.fpscr.csr = csr_before;

  if (native_stopped || reference_stopped) {
    // A call the guard cannot repeat, or a log past its limit. One of the two stopping and the other not means
    // they took different paths.
    if (native_stopped != reference_stopped && !overflow) {
      diff = fmt::format("the copy {} a call outside itself and the original statements {}",
                         native_stopped ? "reached" : "did not reach", reference_stopped ? "did" : "did not");
      return Outcome::kPathsDiffer;
    }
    return Outcome::kNotComparable;
  }

  // 3: the compare set, saved; the real copy on a copy of the registers; its bytes out, the saved ones back.
  Merge(t.ranges);
  size_t total = 0;
  for (const Region& r : t.ranges) total += r.len;
  t.bytes = total;
  t.before.resize(total);
  t.native_out.resize(total);
  size_t off = 0;
  for (const Region& r : t.ranges) {
    std::memcpy(t.before.data() + off, Raw(base, r.addr), r.len);
    off += r.len;
  }
  PPCContext nat = ctx;
  h.native(nat, base);
  const uint32_t hw_native = nat.fpscr.getcsr();
  off = 0;
  for (const Region& r : t.ranges) {
    std::memcpy(t.native_out.data() + off, Raw(base, r.addr), r.len);
    std::memcpy(Raw(base, r.addr), t.before.data() + off, r.len);
    off += r.len;
  }
  ctx.fpscr.setcsr(hw_before);
  ctx.fpscr.csr = csr_before;

  // 4: the call for real. (It may contain guarded calls of its own: they use the next scratch frame.)
  h.orig(ctx, base);
  const uint32_t hw_orig = ctx.fpscr.getcsr();

  // 5: compare.
  diff = CompareRegisters(nat, ctx, *h.skip);
  if (diff.empty() &&
      (hw_native & rex::ppc::FPSCRRegister::GuestMask) != (hw_orig & rex::ppc::FPSCRRegister::GuestMask))
    diff = fmt::format("host flush mode copy={:#x} original={:#x}", hw_native, hw_orig);
  if (diff.empty()) {
    off = 0;
    for (const Region& r : t.ranges) {
      const uint8_t* now = Raw(base, r.addr);
      if (std::memcmp(now, t.native_out.data() + off, r.len) != 0) {
        uint32_t k = 0;
        while (now[k] == t.native_out[off + k]) ++k;
        diff = fmt::format("memory {:#010x} (+{} of the written range {:#010x}+{}) copy={:#04x} original={:#04x}",
                           r.addr + k, k, r.addr, r.len, t.native_out[off + k], now[k]);
        break;
      }
      off += r.len;
    }
  }
  if (diff.empty()) {
    diff = CompareRegisters(logged_native, ctx, *h.skip);
    if (!diff.empty()) diff = "the copy's logged form: " + diff;
  }
  if (diff.empty()) {
    // (The skip set here too: the recompiled original that just ran reaches the hand-written natives through the
    // hooks of their functions, so it has their registers, not the original statements'.)
    diff = CompareRegisters(logged_reference, ctx, *h.skip);
    if (!diff.empty()) diff = "the original statements' logged form: " + diff;
  }
  return diff.empty() ? Outcome::kEqual : Outcome::kDifferent;
}

}  // namespace

void Check(Hook& h, PPCContext& ctx, uint8_t* base, uint32_t& counter) {
  // The next guarded call of this thread: at once while it is still in its first guard_calls (or in verify mode),
  // else after `period` calls. The counter word: od_hot_guest.h.
  const uint32_t phase = counter >> kCountdownBits;
  if (h.verify) {
    counter = 0;
    h.uses.fetch_add(1, std::memory_order_relaxed);
  } else if (phase < h.guard_calls) {
    counter = (phase + 1) << kCountdownBits;
    h.uses.fetch_add(1, std::memory_order_relaxed);
  } else {
    counter = (phase << kCountdownBits) | (h.period - 1);
    h.uses.fetch_add(h.period, std::memory_order_relaxed);
  }
  Report();  // at most every 10 s
  if (h.state.load(std::memory_order_relaxed) != kOn) {  // switched off by another thread's check meanwhile
    h.orig(ctx, base);
    return;
  }

  ScratchFrame frame(Thread());
  Scratch& t = frame.scratch;
  const uint32_t hw_entry = ctx.fpscr.getcsr();
  std::string diff;
  // A difference must repeat to count. The copy and the original are functions of the registers and of guest
  // memory; when another thread changes memory they read between the guard's runs (the audio threads read buffers
  // the decoder is filling), the runs differ without either being wrong. So on a difference the inputs are put back
  // (the registers the call came with, the bytes it wrote) and the whole pass is made again, twice at most: a wrong
  // copy differs every time, a race does not.
  constexpr int kAttempts = 3;
  for (int attempt = 1;; ++attempt) {
    const Outcome outcome = Attempt(h, ctx, base, t, diff);
    if (outcome == Outcome::kNotComparable) {
      h.not_comparable.fetch_add(1, std::memory_order_relaxed);
      if (h.verify) {
        h.orig(ctx, base);
      } else {
        h.native(ctx, base);
      }
      return;
    }
    if (outcome == Outcome::kEqual) {
      h.compared.fetch_add(1, std::memory_order_relaxed);
      if (attempt > 1) h.not_repeated.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    if (attempt < kAttempts) {
      if (outcome == Outcome::kDifferent) {  // the original has run: put its inputs back
        ctx = t.in;
        size_t off = 0;
        for (const Region& r : t.ranges) {
          std::memcpy(Raw(base, r.addr), t.before.data() + off, r.len);
          off += r.len;
        }
        ctx.fpscr.setcsr(hw_entry);
      }
      continue;
    }
    // It repeated.
    h.compared.fetch_add(1, std::memory_order_relaxed);
    h.differences.fetch_add(1, std::memory_order_relaxed);
    h.state.store(kOff, std::memory_order_relaxed);
    const PPCContext& in = t.in;
    REXLOG_ERROR("[masse] DIFFÉRENCE {} ({}): {} ({} passes in a row); inputs r3={:#x} r4={:#x} r5={:#x} r6={:#x} "
                 "r7={:#x} f1={:#x} f2={:#x}; {} bytes in {} written ranges; native copy switched off for the rest of "
                 "the run",
                 h.name, h.key, diff, kAttempts, in.r3.u64, in.r4.u64, in.r5.u64, in.r6.u64, in.r7.u64, in.f1.u64,
                 in.f2.u64, t.bytes, t.ranges.size());
    if (outcome == Outcome::kPathsDiffer) h.orig(ctx, base);  // nothing has run for real yet
    return;
  }
}

void ForEachKey(void (*row)(void* user, const char* key, bool own), void* user) {
  bool bulk = false;
  for (Hook* h = g_hooks; h; h = h->next) {
    if (!h->bulk) {
      row(user, h->key, h->own());
    } else {
      bulk = true;
      if (!h->own()) row(user, h->key, false);  // a bulk copy switched off by its own key
    }
  }
  if (bulk) row(user, "od_masse", REXCVAR_GET(od_masse));
}

void Report() {
  const int64_t now = NowMs();
  int64_t last = g_last_report_ms.load(std::memory_order_relaxed);
  if (now - last < 10000 || !g_last_report_ms.compare_exchange_strong(last, now, std::memory_order_relaxed)) return;
  if (REXCVAR_GET(od_masse_compte)) {
    WriteCounts();
    return;
  }
  // One line per hook that was reached, so each fits the log's line and a tool reads one function per line.
  uint64_t compared = 0, not_comparable = 0, differences = 0, not_repeated = 0;
  uint32_t reached = 0, on = 0, direct = 0, bulk_total = 0, bulk_reached = 0, bulk_on = 0;
  for (Hook* h = g_hooks; h; h = h->next) {
    const uint8_t state = h->state.load(std::memory_order_relaxed);
    bulk_total += h->bulk;
    if (state == kUninit) continue;
    ++reached;
    on += state == kOn || state == kDirect;
    direct += state == kDirect;
    if (h->bulk) {
      ++bulk_reached;
      bulk_on += state == kOn || state == kDirect;
      compared += h->compared.load(std::memory_order_relaxed);
      not_comparable += h->not_comparable.load(std::memory_order_relaxed);
      not_repeated += h->not_repeated.load(std::memory_order_relaxed);
      const uint64_t d = h->differences.load(std::memory_order_relaxed);
      differences += d;
      // A bulk hook has a line of its own only when it found a difference (its copy is off since).
      if (d)
        REXLOG_INFO("[masse] {} (bulk) off after {} difference(s); {} calls compared before", h->name, d,
                    h->compared.load(std::memory_order_relaxed));
      continue;
    }
    const uint64_t c = h->compared.load(std::memory_order_relaxed);
    const uint64_t n = h->not_comparable.load(std::memory_order_relaxed);
    const uint64_t d = h->differences.load(std::memory_order_relaxed);
    compared += c;
    not_comparable += n;
    differences += d;
    const uint64_t r = h->not_repeated.load(std::memory_order_relaxed);
    not_repeated += r;
    if (state == kDirect) continue;  // nothing is counted without the guard: the summary says it is on
    REXLOG_INFO("[masse] {} {}: about {} calls so far, {} compared with the original, {} not comparable (they reach a "
                "call outside the copy), {} differences, {} more that did not repeat (inputs changed by another thread)",
                h->name, state == kOn ? "ON" : "off", h->uses.load(std::memory_order_relaxed), c, n, d, r);
  }
  REXLOG_INFO("[masse] native copies: {} reached, {} on ({} direct, no guard); bulk copies (od_masse {}): {} "
              "of {} reached, {} on; {} calls compared so far, {} not comparable, {} differed once and not when "
              "repeated, differences: {}",
              reached, on, direct, REXCVAR_GET(od_masse) ? "on" : "off", bulk_reached, bulk_total, bulk_on,
              compared, not_comparable, not_repeated, differences);
}

}  // namespace od::hot

// Les crochets : generated/hot/masse/od_hot_bulk_<k>.inc, compilés par od_hot_shard_<k>.cpp.
