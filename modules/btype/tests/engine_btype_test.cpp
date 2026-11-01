// B-Type chip-style engine (opentracker-synths modules/btype, engine 0x88):
// registered with a valid descriptor ("B-Type" on screen, Patches/BTYPE/),
// silent when idle; the tuning (Free exact, Coarse the nearest 8-bit divider
// at the clock, Fine a 16-bit one in tune), the pure 50 % square, every
// distortion's period in divider events (Poly4 15, Poly5 31, Poly9 511,
// Poly5x4 465, a constant at a multiple of 31, the 17-bit ones not repeating),
// Poly Reset, the Hi-Pass notch, channel 2's detune in divider steps, the
// envelope's 1/60 s frames and its 4-bit steps (Bits), the DAC; the CHIP
// tricks and glide; through a SynthBank slot at Poly 1 and 4; no click on
// parameter jumps, no NaN or denormals, Volume never applied, the slot level,
// every parameter audible, golden hashes.
#include "test.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <vector>

#include "audio/chip_dsp.h"
#include "btype.h"
#include "audio/synth_bank.h"
#include "audio/synth_engine.h"
#include "audio/synth_params.h"
#include "formats/patch_pb.h"
#include "modules/module.h"
#include "modules/synth_descriptor.h"
#include "modules/synth_models.h"

using namespace ot;
using namespace ot::audio;
namespace chip = ot::audio::chip;

namespace {

constexpr float kFs = ot::audio::kSampleRate;
constexpr int kN = btype::kParamCount;
constexpr int kBlocksPerSec = int(kFs / float(kBlock)) + 1;   // 345
constexpr double kPi = 3.14159265358979;

const PatchParamRange* range(int i) { return synthParamRange(btype::kEngineId, i); }

std::vector<float> initValues() {
    mod::ModuleHost::instance().init();
    std::vector<float> v(kN);
    for (int i = 0; i < kN; ++i) v[i] = range(i)->def;
    return v;
}

using Buf = std::vector<float>;

// The engine's voices driven directly (no framework).
struct Rig {
    ISynthEngine* e = synthEngineFor(btype::kEngineId);
    SynthSlotScratch scratch{};
    SynthParams p;
    SynthVoice v[kSynthMaxVoices];
    bool alive[kSynthMaxVoices] = {};
    Buf L, R;
    float bpm = 120.0f;

    explicit Rig(const std::vector<float>& vals) {
        p.engine = btype::kEngineId;
        p.count = kN;
        set(vals);
        e->slotReset(scratch);
        for (auto& x : v) {
            x = SynthVoice{};
            x.slotScratch = &scratch;
        }
    }
    void set(const std::vector<float>& vals) {
        for (int i = 0; i < kN; ++i) p.start[i] = p.end[i] = vals[size_t(i)];
        ++p.serial;
    }
    void on(int k, float pitch, bool reused = false, bool overlap = false) {
        if (!reused) {
            std::memset(v[k].state, 0, sizeof(v[k].state));
            v[k].level = 0.0f;
        }
        v[k].gate = true;
        v[k].note = uint8_t(std::clamp(int(pitch), 0, 127));
        SynthNote n;
        n.pitch = pitch;
        n.note = uint8_t(std::clamp(int(pitch), 0, 127));
        n.reused = reused;
        n.overlap = overlap;
        e->setTempo(bpm);
        e->noteOn(v[k], p, n);
        alive[k] = true;
    }
    void off(int k) {
        v[k].gate = false;
        e->noteOff(v[k], p);
    }
    int run(int blocks) {
        int live = 0;
        for (int b = 0; b < blocks; ++b) {
            float l[kBlock] = {}, r[kBlock] = {};
            e->setTempo(bpm);
            e->prepare(scratch, p);
            live = 0;
            for (int k = 0; k < kSynthMaxVoices; ++k) {
                if (!alive[k]) continue;
                alive[k] = e->render(v[k], p, 1.0f, 0.0f, l, r);
                live += alive[k];
            }
            L.insert(L.end(), l, l + kBlock);
            R.insert(R.end(), r, r + kBlock);
        }
        return live;
    }
};

bool finite(const Buf& x) {
    for (float s : x)
        if (!std::isfinite(s)) return false;
    return true;
}
long subnormals(const Buf& x) {
    long n = 0;
    for (float s : x) n += std::fpclassify(s) == FP_SUBNORMAL;
    return n;
}
float peak(const Buf& x, size_t a = 0, size_t b = SIZE_MAX) {
    float m = 0.0f;
    for (size_t i = a; i < std::min(b, x.size()); ++i) m = std::max(m, std::fabs(x[i]));
    return m;
}
double rms(const Buf& x, size_t a = 0, size_t b = SIZE_MAX) {
    b = std::min(b, x.size());
    double s = 0.0;
    for (size_t i = a; i < b; ++i) s += double(x[i]) * x[i];
    return b > a ? std::sqrt(s / double(b - a)) : 0.0;
}
double rmsDiff(const Buf& a, const Buf& b) {
    const size_t n = std::min(a.size(), b.size());
    double s = 0.0;
    for (size_t i = 0; i < n; ++i) s += double(a[i] - b[i]) * (a[i] - b[i]);
    return std::sqrt(s / double(n ? n : 1));
}
double hz(float pitch) { return 440.0 * std::pow(2.0, (double(pitch) - 69.0) / 12.0); }
double semis(double f, double ref) { return 12.0 * std::log2(f / ref); }
// Upward zero crossings (interpolated, in frames) of x around its mean.
std::vector<double> crossings(const Buf& x, size_t a = 0, size_t b = SIZE_MAX) {
    b = std::min(b, x.size());
    double mean = 0;
    for (size_t i = a; i < b; ++i) mean += x[i];
    mean /= double(b > a ? b - a : 1);
    std::vector<double> t;
    for (size_t i = a + 1; i < b; ++i) {
        const double y0 = x[i - 1] - mean, y1 = x[i] - mean;
        if (y0 < 0.0 && y1 >= 0.0) t.push_back(double(i - 1) + y0 / (y0 - y1));
    }
    return t;
}
// Mean frequency of x over [a, b) from its upward crossings.
double freqOf(const Buf& x, size_t a, size_t b) {
    const std::vector<double> t = crossings(x, a, b);
    if (t.size() < 3) return 0.0;
    return kFs * double(t.size() - 1) / (t.back() - t.front());
}
// Goertzel magnitude (Hann) at hz.
double tone(const Buf& b, double f) {
    const double w = 2.0 * kPi * f / kFs, c = 2.0 * std::cos(w), wn = 2.0 * kPi / double(b.size());
    double s1 = 0, s2 = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        const double s0 = b[i] * (0.5 - 0.5 * std::cos(wn * double(i))) + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / double(b.size());
}

// The Init patch (or an edited one) through a real SynthBank slot at the
// default slot gain (-9 dB).
struct BankSlot {
    SynthPatch patch;
    Governor gov;
    SynthBank bank;
    SynthMixInputs in;
    float L[kBlock], R[kBlock], D[kBlock], V[kBlock];
    float pk = 0.0f;
    long clips = 0;
    Buf out;
    BankSlot(int poly, const std::vector<float>* vals = nullptr, int32_t engine = btype::kEngineId) {
        mod::ModuleHost::instance().init();
        gov.setBudget(8);
        bank.setGovernor(&gov);
        bank.setStartsPerBlock(0);
        patchInit(engine, &patch);
        if (vals)
            for (int i = 0; i < kN; ++i) synthParamSet(patch, i, (*vals)[size_t(i)]);
        bank.bindPatch(0, &patch);
        SynthSlotRecord r = SynthSlotRecord::defaults(0);
        r.model = uint32_t(SynthModel::ACD);
        r.polyphony = uint32_t(poly);
        r.delaySend = 0.0f;
        r.reverbSend = 0.0f;
        bank.setSlotRecord(0, r);
        bank.setSlotEngine(0, engine >= 0x80 ? engine : 0);
        block();
    }
    void block() {
        for (uint32_t i = 0; i < kBlock; ++i) L[i] = R[i] = D[i] = V[i] = 0.0f;
        bank.render(in, L, R, D, V, nullptr);
    }
    void on(int track, int note) {
        SynthNoteOn n;
        n.slot = 0;
        n.note = uint8_t(note);
        n.volume = -1;
        bank.noteOn(track, n);
    }
    void off(int track) { bank.noteOff(track, NoteOffMode::Off); }
    void run(int n) {
        for (int b = 0; b < n; ++b) {
            block();
            out.insert(out.end(), L, L + kBlock);
            for (uint32_t i = 0; i < kBlock; ++i) {
                const float a = std::max(std::fabs(L[i]), std::fabs(R[i]));
                pk = std::max(pk, a);
                if (a >= 1.0f) ++clips;
            }
        }
    }
};
double dbfs(float x) { return 20.0 * std::log10(x > 1e-9f ? double(x) : 1e-9); }


// A held, clean channel: a pure square, Free, the envelope at 15 at once, the DAC clean.
std::vector<float> clean(int dist = btype::kPure) {
    std::vector<float> c = initValues();
    c[btype::kDistortion] = float(dist);
    c[btype::kAttack] = 0.0f;
    c[btype::kSustain] = 15.0f;
    c[btype::kDacBits] = 8.0f;
    c[btype::kPolyReset] = 1.0f;
    return c;
}
Buf play(const std::vector<float>& vals, float pitch = 57.0f, float seconds = 0.5f) {
    Rig r(vals);
    r.on(0, pitch);
    r.run(int(seconds * kFs / float(kBlock)) + 1);
    return r.L;
}
double pitchOf(float hzWant) { return 69.0 + 12.0 * std::log2(hzWant / 440.0); }
// The bits a distortion plays, one per divider event, at the 64 kHz clock with
// a step of 32 clocks (1 kHz: 2,000 events a second, ~22 frames an event).
std::vector<int> eventBits(int dist, int events, double stepClocks = 32.0) {
    const double f = 64000.0 / (2.0 * stepClocks), per = kFs / (2.0 * f);
    const Buf x = play(clean(dist), float(pitchOf(float(f))), float((events + 4) * per / kFs));
    float lo = 1e9f, hi = -1e9f;
    for (float v : x) lo = std::min(lo, v), hi = std::max(hi, v);
    std::vector<int> bits;
    for (int k = 0; k < events; ++k) bits.push_back(x[size_t((k + 1.5) * per)] > 0.5f * (lo + hi));
    return bits;
}
int periodOf(const std::vector<int>& b, int maxP) {
    for (int p = 1; p <= maxP; ++p) {
        bool ok = true;
        for (size_t i = 0; i + size_t(p) < b.size() && ok; ++i) ok = b[i] == b[i + size_t(p)];
        if (ok) return p;
    }
    return 0;
}

}  // namespace

TEST(btype_descriptor_is_valid) {
    mod::ModuleHost::instance().init();
    ISynthEngine* e = synthEngineFor(btype::kEngineId);
    CHECK(e != nullptr);
    if (!e) return;
    CHECK_STR(e->name(), "BTYPE");
    const mod::SynthDescriptor* d = mod::ModuleHost::instance().synthDescriptor(btype::kEngineId);
    CHECK(d != nullptr);
    if (!d) return;
    CHECK_STR(d->name, "BTYPE");
    CHECK_STR(d->displayName, "B-Type");
    CHECK_EQ(int(d->paramCount), 27);
    CHECK_EQ(int(d->paramCount), kN);
    CHECK_EQ(int(d->volumeParam), int(btype::kVolume));
    CHECK_EQ(int(d->sectionCount), 12);
    CHECK_STR(synthEngineFolder(btype::kEngineId), "BTYPE");
    const int k = synthModelListIndex(btype::kEngineId);
    CHECK(k >= kSynthModelCount);
    CHECK_STR(synthModelListName(k), "B-Type");
#if defined(OT_MOD_HND1)
    CHECK(k > synthModelListIndex(0x87));
#endif
    std::vector<int> seen(kN, 0);
    int macros = 0;
    for (int s = 0; s < d->sectionCount; ++s) {
        const synthui::Section& sec = d->sections[s];
        if (sec.macro >= 0) {
            CHECK_EQ(int(sec.macro), macros);
            ++macros;
            continue;
        }
        for (int j = 0; j < sec.count; ++j)
            if (sec.params[j] < kN) ++seen[sec.params[j]];
    }
    CHECK_EQ(macros, 7);
    for (int i = 0; i < kN; ++i) {
        const PatchParamRange& r = d->params[i];
        CHECK(r.min < r.max && r.def >= r.min && r.def <= r.max);
        CHECK(d->ui[i].label != nullptr && std::strlen(d->ui[i].label) > 0);
        if (d->ui[i].fmt == synthui::kFmtEnum) CHECK_EQ(int(d->ui[i].nameCount), int(r.max - r.min) + 1);
        if (d->ui[i].fmt == synthui::kFmtOnOff) CHECK(r.min == 0 && r.max == 1);
        CHECK_EQ(seen[size_t(i)], 1);
    }
    SynthPatch p;
    CHECK(patchInit(btype::kEngineId, &p));
    CHECK_EQ(validatePatch(p, nullptr, 0), 0);
    CHECK_EQ(int(p.macros[kPatchMacroVelocity].targets[0].param), int(btype::kVolume));
}

TEST(btype_patch_file_round_trip) {
    mod::ModuleHost::instance().init();
    SynthPatch p;
    CHECK(patchInit(btype::kEngineId, &p));
    const float vals[][2] = {{btype::kClock, 2}, {btype::kDistortion, 6}, {btype::kTuning, 1}, {btype::kHpFreq, 432.5f},
                             {btype::kOsc2Detune, -5}, {btype::kSustain, 3}, {btype::kDacBits, 6}};
    for (const auto& v : vals) {
        CHECK(patchParamInRange(btype::kEngineId, int(v[0]), v[1]));
        CHECK(synthParamSet(p, int(v[0]), v[1]));
    }
    std::vector<uint8_t> a(kPatchMaxFileSize), b(kPatchMaxFileSize);
    size_t na = 0, nb = 0;
    CHECK(encodePatch(p, a.data(), a.size(), &na) == PatchStatus::kOk);
    SynthPatch q;
    CHECK(decodePatch(a.data(), na, &q) == PatchStatus::kOk);
    CHECK_EQ(q.engine, int32_t(btype::kEngineId));
    CHECK_EQ(validatePatch(q, nullptr, 0), 0);
    for (const auto& v : vals) CHECK_EQ(q.params[int(v[0])].bits, patchFloatToBits(v[1]));
    CHECK(encodePatch(q, b.data(), b.size(), &nb) == PatchStatus::kOk);
    CHECK(na == nb && std::memcmp(a.data(), b.data(), na) == 0);
    CHECK(!patchParamInRange(btype::kEngineId, btype::kDistortion, 8.0f));
}

TEST(btype_silent_when_idle) {
    Rig r(initValues());
    r.run(20);
    CHECK_EQ(peak(r.L), 0.0f);
    r.on(0, 60.0f);
    r.run(kBlocksPerSec / 4);
    CHECK(rms(r.L) > 0.01);
    r.off(0);
    int blocks = 0;
    while (r.run(1) && blocks < 4 * kBlocksPerSec) ++blocks;
    CHECK(blocks < 4 * kBlocksPerSec);
    r.L.clear();
    r.run(20);
    CHECK_EQ(peak(r.L), 0.0f);
}

TEST(btype_tuning_free_coarse_fine) {
    // Free: exact.
    CHECK(std::fabs(freqOf(play(clean(), 69.0f), 5000, 20000) - 440.0) < 0.5);
    // Coarse: the nearest 8-bit divider at the clock, clock / (2 (N + 1)).
    for (int clock = 0; clock < 3; ++clock)
        for (float note : {40.0f, 69.0f, 96.0f}) {
            float N = 0.0f;
            const float f = btype::tunedHz(float(hz(note)), clock, btype::kCoarse, &N);
            const float clk = btype::clockHz(clock);
            const float wantN = std::clamp(std::round(clk / (2.0f * float(hz(note))) - 1.0f), 0.0f, 255.0f);
            CHECK_EQ(N, wantN);
            CHECK(std::fabs(f - clk / (2.0f * (wantN + 1.0f))) < 1e-3f * f);
        }
    std::vector<float> c = clean();
    c[btype::kTuning] = float(btype::kCoarse);
    const float want = btype::tunedHz(float(hz(96.0f)), btype::k64k, btype::kCoarse);
    const double got = freqOf(play(c, 96.0f), 5000, 20000);
    std::printf("  coarse at 64 kHz, note 96: %.1f Hz (%.0f cents off the note), measured %.1f\n", want,
                1200.0 * std::log2(want / hz(96.0f)), got);
    CHECK(std::fabs(got - want) < 0.005 * want);
    CHECK(std::fabs(1200.0 * std::log2(want / hz(96.0f))) > 10.0);   // the detuned top
    // Fine: a 16-bit divider at 1.79 MHz is in tune everywhere (< 2 cents).
    for (float note = 36.0f; note <= 96.0f; note += 5.0f) {
        const float f = btype::tunedHz(float(hz(note)), btype::k64k, btype::kFine);
        CHECK(std::fabs(1200.0 * std::log2(f / hz(note))) < 2.0);
    }
}

TEST(btype_pure_square) {
    const Buf x = play(clean(), 33.0f);
    const size_t a = 5000, b = 20000;
    float lo = 1e9f, hi = -1e9f;
    for (size_t i = a; i < b; ++i) lo = std::min(lo, x[i]), hi = std::max(hi, x[i]);
    long up = 0;
    for (size_t i = a; i < b; ++i) up += x[i] > 0.5f * (lo + hi);
    CHECK(std::fabs(double(up) / double(b - a) - 0.5) < 0.02);
}

TEST(btype_distortion_periods) {
    struct Want {
        int dist, period;
    };
    const Want w[] = {{btype::kPoly4, 15}, {btype::kPoly5, 31}, {btype::kPoly9, 511}, {btype::kPoly5x4, 465}};
    for (const Want& k : w) {
        const std::vector<int> b = eventBits(k.dist, 2 * k.period + 40);
        const int p = periodOf(b, k.period + 10);
        std::printf("  distortion %d: period %d events (want %d)\n", k.dist, p, k.period);
        CHECK_EQ(p, k.period);
    }
    // Poly5 at a step that is a multiple of 31 clocks: a constant (no tone).
    const std::vector<int> c = eventBits(btype::kPoly5, 100, 31.0);
    CHECK_EQ(periodOf(c, 3), 1);
    // The 17-bit counters: no repeat within 20,000 events.
    for (int d : {btype::kPoly17, btype::kPoly5x17}) {
        const std::vector<int> b = eventBits(d, 4000);
        CHECK_EQ(periodOf(b, 1990), 0);
    }
    // Poly5x9: no period below 1,000 events.
    CHECK_EQ(periodOf(eventBits(btype::kPoly5x9, 2400), 1000), 0);
}

TEST(btype_poly_reset_and_hi_pass) {
    // Poly Reset On: the same buzz on every note; Off: the counters run on.
    std::vector<float> c = clean(btype::kPoly9);
    Rig r(c);
    r.on(0, 60.0f);
    r.run(40);
    const Buf a(r.L.begin(), r.L.end());
    r.L.clear();
    r.on(0, 60.0f, true, false);
    r.run(40);
    CHECK(rmsDiff(a, r.L) < 1e-6);
    c[btype::kPolyReset] = 0.0f;
    Rig s(c);
    s.on(0, 60.0f);
    s.run(40);
    const Buf b(s.L.begin(), s.L.end());
    s.L.clear();
    s.on(0, 60.0f, true, false);
    s.run(40);
    CHECK(rmsDiff(b, s.L) > 0.01);
    // Hi-Pass: the output less itself 1 / HP Freq earlier: a notch at HP Freq's multiples.
    std::vector<float> h = clean();
    h[btype::kHiPass] = 1.0f;
    const float f = 440.0f;
    h[btype::kHpFreq] = f;
    const double notch = rms(play(h, 69.0f), 4000, 20000);
    h[btype::kHpFreq] = 2.0f * f;
    const double open = rms(play(h, 69.0f), 4000, 20000);
    h[btype::kHiPass] = 0.0f;
    const double dry = rms(play(h, 69.0f), 4000, 20000);
    std::printf("  hi-pass at the tone: %.4f, at twice it %.4f, off %.4f\n", notch, open, dry);
    CHECK(notch < 0.1 * open);
    CHECK(open > 0.5 * dry);
}

TEST(btype_osc2_detune_in_divider_steps) {
    std::vector<float> c = clean();
    c[btype::kTuning] = float(btype::kCoarse);
    c[btype::kOsc2Level] = 1.0f;
    c[btype::kOsc2Detune] = 2.0f;
    const float note = 81.0f;
    float N = 0.0f;
    const float f1 = btype::tunedHz(float(hz(note)), btype::k64k, btype::kCoarse, &N);
    const float f2 = 64000.0f / (2.0f * (N + 3.0f));
    const Buf xa = play(c, note, 0.6f);
    const Buf x(xa.begin() + 5000, xa.begin() + 25000);
    std::printf("  osc 2 +2 steps: %.1f Hz and %.1f Hz: %.4f %.4f\n", f1, f2, tone(x, f1), tone(x, f2));
    CHECK(tone(x, f1) > 0.02);
    CHECK(tone(x, f2) > 0.02);
    c[btype::kOsc2Level] = 0.0f;
    const Buf ya = play(c, note, 0.6f);
    const Buf y(ya.begin() + 5000, ya.begin() + 25000);
    CHECK(tone(y, f2) < 0.1 * tone(x, f2));
}

TEST(btype_envelope_frames_and_bits) {
    // Attack 4 frames a level: 15 levels in 1 s (+-1 block).
    std::vector<float> c = clean();
    c[btype::kAttack] = 4.0f;
    const Buf x = play(c, 57.0f, 1.3f);
    const float top = peak(x, size_t(1.15 * kFs), size_t(1.25 * kFs));
    size_t t = 0;
    for (size_t i = 0; i < x.size(); ++i)
        if (std::fabs(x[i]) >= 0.999f * top) {
            t = i;
            break;
        }
    std::printf("  attack 4: full after %.3f s\n", t / kFs);
    CHECK(std::fabs(t / kFs - 1.0) < 2.0 * kBlock / kFs + 1.0 / 220.0);
    // Bits On: at most 16 amplitudes; Off: smooth.
    std::set<int> on, off;
    for (int bits = 0; bits < 2; ++bits) {
        c[btype::kEnvBits] = float(1 - bits);
        const Buf y = play(c, 57.0f, 1.0f);
        for (size_t a = 0; a + 400 < y.size(); a += 400) (bits ? off : on).insert(int(std::lround(peak(y, a, a + 400) * 10000)));
    }
    std::printf("  envelope: %zu amplitudes stepped, %zu smooth\n", on.size(), off.size());
    CHECK(on.size() <= 16);
    CHECK(off.size() > 30);
    // Decay to Sustain 5, Release 3 frames a level: the voice ends.
    std::vector<float> d = clean();
    d[btype::kDecay] = 2.0f;
    d[btype::kSustain] = 5.0f;
    d[btype::kRelease] = 3.0f;
    Rig r(d);
    r.on(0, 57.0f);
    r.run(kBlocksPerSec / 2);
    const float sus = peak(r.L, r.L.size() - 2000, r.L.size());
    CHECK(std::fabs(sus / peak(r.L, 0, 600) - 5.0f / 15.0f) < 0.03f);
    r.off(0);
    int n = 0;
    while (r.run(1) && n < kBlocksPerSec * 2) ++n;
    const double tr = double(n + 1) * kBlock / kFs;
    CHECK(std::fabs(tr - 5.0 * 3.0 / 60.0) < 2.0 * kBlock / kFs);
}

TEST(btype_dac_bits) {
    for (int bits = 4; bits <= 8; ++bits) {
        std::vector<float> c = clean();
        c[btype::kOsc2Level] = 0.7f;
        c[btype::kOsc2Detune] = 3.0f;
        c[btype::kDecay] = 2.0f;
        c[btype::kSustain] = 0.0f;
        c[btype::kDacBits] = float(bits);
        const Buf x = play(c, 57.0f, 1.0f);
        std::set<float> lv(x.begin(), x.end());
        std::printf("  dac %d bits: %zu levels\n", bits, lv.size());
        if (bits < 8) CHECK(lv.size() <= size_t(2u << bits));
        else CHECK(lv.size() > 100);
    }
}

TEST(btype_tricks_and_glide) {
    std::vector<float> a = clean();
    a[btype::kArpMode] = float(chip::kArpMaj);
    a[btype::kArpRate] = float(chip::k60Hz);
    const Buf x = play(a, 69.0f, 0.2f);
    const double per = kFs / 60.0;
    const int want[3] = {0, 4, 7};
    for (int k = 0; k < 6; ++k) {
        const double st = semis(freqOf(x, size_t(k * per + 0.2 * per), size_t((k + 1) * per - 0.1 * per)), 440.0);
        CHECK(std::fabs(st - want[k % 3]) < 0.2);
    }
    std::vector<float> s = clean();
    s[btype::kSlideStart] = 12.0f;
    s[btype::kSlideTime] = 0.2f;
    const Buf z = play(s, 57.0f, 0.4f);
    CHECK(semis(freqOf(z, 0, 800), hz(57.0f)) > 10.5);
    CHECK(std::fabs(semis(freqOf(z, size_t(0.25 * kFs), size_t(0.35 * kFs)), hz(57.0f))) < 0.1);
    std::vector<float> g = clean();
    g[btype::kGlideTime] = 0.3f;
    Rig r(g);
    r.on(0, 57.0f);
    r.run(kBlocksPerSec / 4);
    const size_t at = r.L.size();
    r.on(0, 69.0f, true, true);
    r.run(kBlocksPerSec / 2);
    const double m = semis(freqOf(r.L, at + 2000, at + 4000), hz(57.0f)), e = semis(freqOf(r.L, r.L.size() - 3000, r.L.size()), hz(57.0f));
    CHECK(m > 1.0 && m < 11.0);
    CHECK(std::fabs(e - 12.0) < 0.15);
}

TEST(btype_through_the_bank_poly1_and_4) {
    BankSlot s(1);
    s.on(0, 57);
    s.run(kBlocksPerSec / 4);
    s.on(1, 64);
    s.run(kBlocksPerSec / 4);
    CHECK_EQ(s.bank.voicesOf(0), 1);
    BankSlot c(4);
    for (int k = 0; k < 4; ++k) c.on(k, 57 + 3 * k);
    c.run(kBlocksPerSec / 2);
    CHECK_EQ(c.bank.voicesOf(0), 4);
    CHECK_EQ(c.clips, 0L);
}

TEST(btype_parameter_jumps_do_not_click) {
    struct Jump {
        int param;
        float a, b;
    };
    const Jump jumps[] = {{btype::kOsc2Level, 0, 1}, {btype::kVolume, 0.2f, 2}};
    for (const Jump& j : jumps) {
        std::vector<float> c = clean();
        c[size_t(j.param)] = j.a;
        Rig r(c);
        r.on(0, 45.0f);
        r.run(kBlocksPerSec / 4);
        auto maxStep = [&r](size_t a, size_t b) {
            double m = 0;
            for (size_t i = a + 1; i < b; ++i) m = std::max(m, double(std::fabs(r.L[i] - r.L[i - 1])));
            return m;
        };
        const double steady = maxStep(r.L.size() - 40 * kBlock, r.L.size());
        c[size_t(j.param)] = j.b;
        const size_t at = r.L.size();
        r.set(c);
        r.run(3);
        const double jump = maxStep(at - 1, at + 3 * kBlock);
        if (!(jump <= 1.05 * steady + 1e-4)) std::printf("       param %d: step %.4f (steady %.4f)\n", j.param, jump, steady);
        CHECK(jump <= 1.05 * steady + 1e-4);
    }
}

TEST(btype_no_nan_or_denormals) {
    uint32_t seed = 88;
    auto rnd = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return float(seed >> 8) * (1.0f / 16777216.0f);
    };
    int bad = 0;
    long sub = 0;
    for (int set = 0; set < 150; ++set) {
        std::vector<float> a(kN);
        for (int i = 0; i < kN; ++i) a[size_t(i)] = range(i)->min + rnd() * (range(i)->max - range(i)->min);
        Rig r(a);
        r.bpm = 20.0f + rnd() * 400.0f;
        r.on(0, float(int(rnd() * 127.0f)));
        r.on(1, float(int(rnd() * 127.0f)));
        r.run(kBlocksPerSec / 2);
        r.on(1, float(int(rnd() * 127.0f)), true, rnd() < 0.5f);
        r.run(kBlocksPerSec / 4);
        r.off(0);
        r.off(1);
        r.run(kBlocksPerSec / 4);
        if (!finite(r.L) || peak(r.L) > 2.0f) {
            ++bad;
            std::printf("       set %d: peak %.3f%s\n", set, peak(r.L), finite(r.L) ? "" : " (not finite)");
        }
        sub += subnormals(r.L);
    }
    CHECK_EQ(bad, 0);
    CHECK_EQ(sub, 0L);
    for (int ext = 0; ext < 2; ++ext) {
        std::vector<float> a(kN);
        for (int i = 0; i < kN; ++i) a[size_t(i)] = ext ? range(i)->max : range(i)->min;
        for (int d = 0; d < btype::kDistCount; ++d) {
            a[btype::kDistortion] = a[btype::kOsc2Distortion] = float(d);
            Rig r(a);
            r.on(0, 127.0f);
            r.on(1, 0.0f);
            r.run(kBlocksPerSec / 2);
            r.off(0);
            r.off(1);
            r.run(kBlocksPerSec / 2);
            CHECK(finite(r.L));
            CHECK(peak(r.L) < 2.0f);
            CHECK_EQ(subnormals(r.L), 0L);
        }
    }
}

TEST(btype_voice_level_and_volume) {
    std::vector<float> c = clean();
    c[btype::kAttack] = 3.0f;
    Rig r(c);
    r.on(0, 60.0f);
    CHECK_EQ(r.v[0].level, 1.0f);
    r.run(20);
    CHECK(r.v[0].level > 0.0f && r.v[0].level < 0.5f);
    std::vector<float> a = initValues(), b = initValues();
    a[btype::kVolume] = 0.0f;
    b[btype::kVolume] = 2.0f;
    CHECK(play(a) == play(b));
}

TEST(btype_level_matches_the_synth_conventions) {
    BankSlot s(1);
    for (int k = 0; k < 20; ++k) {
        s.on(0, 45 + 3 * (k % 8));
        s.run(100);
    }
    BankSlot c(4);
    for (int rep = 0; rep < 6; ++rep) {
        for (int k = 0; k < 4; ++k) c.on(k, 57 + 4 * k - 12 * (rep & 1));
        c.run(200);
    }
#if defined(OT_MOD_ACD1)
    BankSlot a(1, nullptr, kSynthEngineAcd);
    for (int j = 0; j < 20; ++j) {
        a.on(0, 45 + 3 * (j % 8));
        a.run(100);
    }
    std::printf("  B-Type Init at -9 dB: peak %.1f dBFS (ACD %.1f), 4-voice chords %.1f dBFS\n", dbfs(s.pk), dbfs(a.pk), dbfs(c.pk));
    CHECK(std::fabs(dbfs(s.pk) - dbfs(a.pk)) < 3.0);
#else
    std::printf("  B-Type Init at -9 dB: peak %.1f dBFS, 4-voice chords %.1f dBFS\n", dbfs(s.pk), dbfs(c.pk));
#endif
    CHECK(dbfs(s.pk) < -6.0 && dbfs(s.pk) > -20.0);
    CHECK(dbfs(c.pk) < -1.0);
    CHECK_EQ(s.clips + c.clips, 0L);
}

namespace {
std::vector<float> richContext() {
    std::vector<float> c = initValues();
    c[btype::kDistortion] = float(btype::kPoly9);
    c[btype::kOsc2Level] = 0.6f;
    c[btype::kOsc2Distortion] = float(btype::kPoly5);
    c[btype::kOsc2Detune] = 2.0f;
    c[btype::kTuning] = float(btype::kCoarse);
    c[btype::kAttack] = 2.0f;
    c[btype::kDecay] = 4.0f;
    c[btype::kSustain] = 8.0f;
    c[btype::kRelease] = 3.0f;
    c[btype::kArpMode] = float(chip::kArpCustom);
    c[btype::kVibDepth] = 0.5f;
    c[btype::kVibDelay] = 0.05f;
    c[btype::kSlideStart] = 5.0f;
    c[btype::kGlideTime] = 0.1f;
    c[btype::kPolyReset] = 1.0f;
    return c;
}
Buf sequence(const std::vector<float>& vals) {
    Rig r(vals);
    r.on(0, 57.0f);
    r.run(kBlocksPerSec / 3);
    r.on(0, 64.0f, true, true);
    r.run(kBlocksPerSec / 3);
    r.off(0);
    r.run(kBlocksPerSec / 3);
    return r.L;
}
}  // namespace

TEST(btype_every_parameter_has_an_effect) {
    const std::vector<float> ctx = richContext();
    for (int i = 0; i < kN; ++i) {
        if (i == btype::kVolume) continue;
        std::vector<float> a = ctx, b = ctx;
        a[size_t(i)] = range(i)->min;
        b[size_t(i)] = range(i)->max;
        if (i == btype::kHpFreq) a[btype::kHiPass] = b[btype::kHiPass] = 1.0f;
        if (i == btype::kGlideMode) {
            a[size_t(i)] = float(btype::kAlways);
            b[size_t(i)] = float(btype::kLegato);
        }
        if (i == btype::kPolyReset) {   // the second note's counters: from the start or running on
            a[btype::kDistortion] = b[btype::kDistortion] = float(btype::kPoly17);
        }
        const double d = rmsDiff(sequence(a), sequence(b));
        CHECK(d > 1e-4);
        if (!(d > 1e-4)) std::printf("       param %d differs by %g only\n", i, d);
    }
}

namespace {
// Power off the harmonics of f0 below maxHz over the harmonic power (DFT, Hann; the mean removed).
double aliasRatio(const Buf& in, double f0, double maxHz) {
    const size_t n = in.size();
    double mean = 0;
    for (float v : in) mean += v;
    mean /= double(n);
    double harm = 0, alias = 0;
    const double binHz = kFs / double(n), kPi2 = 6.283185307179586;
    for (size_t k = 1; k < n / 2 && double(k) * binHz < maxHz; ++k) {
        double re = 0, im = 0;
        for (size_t i = 0; i < n; ++i) {
            const double w = (in[i] - mean) * (0.5 - 0.5 * std::cos(kPi2 * double(i) / double(n)));
            re += w * std::cos(kPi2 * double(k) * double(i) / double(n));
            im -= w * std::sin(kPi2 * double(k) * double(i) / double(n));
        }
        const double pw = re * re + im * im, f = double(k) * binHz, h = std::round(f / f0);
        if (h >= 1 && std::fabs(f - h * f0) <= 4 * binHz) harm += pw;
        else alias += pw;
    }
    return alias / harm;
}
}  // namespace

TEST(btype_hi_pass_on_the_distortions_and_band_limited) {
    // Every poly note sounds with Hi-Pass on, whatever HP Freq (the delay in
    // events and their fraction; before: a delay rounded to 0 events cancelled).
    for (float hpf : {500.0f, 2000.0f, 8000.0f})
        for (float note : {36.0f, 48.0f, 60.0f, 83.0f}) {   // (84: a step of 31 clocks, Poly5's constant)
            std::vector<float> c = clean(btype::kPoly5);
            c[btype::kHiPass] = 1.0f;
            c[btype::kHpFreq] = hpf;
            const float pk = peak(play(c, note, 0.3f), 2000, 13000);
            if (!(pk > 0.05f)) std::printf("       HP %g Hz note %g: peak %.4f\n", hpf, note, pk);
            CHECK(pk > 0.05f);
        }
    // HP Freq moves the comb on the 17-bit counters too.
    std::vector<float> a = clean(btype::kPoly17), b = clean(btype::kPoly17);
    a[btype::kHiPass] = b[btype::kHiPass] = 1.0f;
    a[btype::kHpFreq] = 50.0f;
    b[btype::kHpFreq] = 8000.0f;
    CHECK(rmsDiff(play(a, 60.0f), play(b, 60.0f)) > 0.01);
    // The pure square's Hi-Pass is band-limited: far less aliasing than a naive comb.
    std::vector<float> h = clean();
    h[btype::kHiPass] = 1.0f;
    h[btype::kHpFreq] = 3000.0f;
    const float note = 84.0f;
    const Buf x = play(h, note, 0.4f);
    const Buf seg(x.begin() + 6000, x.begin() + 6000 + 4096);
    Buf naive(seg.size());
    const double f = hz(note), T = 1.0 / 3000.0;
    for (size_t i = 0; i < naive.size(); ++i) {
        const double t = double(i) / kFs, p = std::fmod(t * f, 1.0), q = std::fmod(t * f - T * f + 8.0, 1.0);
        naive[i] = float((p < 0.5 ? 1.0 : 0.0) - (q < 0.5 ? 1.0 : 0.0));
    }
    const double rj = aliasRatio(seg, f, 8000.0), rn = aliasRatio(naive, f, 8000.0);
    std::printf("  pure hi-pass alias below 8 kHz: naive %.1f dB, B-Type %.1f dB\n", 10 * std::log10(rn), 10 * std::log10(rj));
    CHECK(10 * std::log10(rj) < 10 * std::log10(rn) - 10.0);
}

// Golden hashes (host build, -ffp-contract=off): a rich phrase on the engine
// alone and an Init line through a SynthBank slot. Any change of the sound
// shows here; update the values only for an intended change.
namespace {
uint64_t hashOf(const Buf& x) {
    uint64_t h = 1469598103934665603ull;
    for (float s : x) {
        uint32_t b;
        std::memcpy(&b, &s, 4);
        for (int k = 0; k < 4; ++k) {
            h ^= (b >> (8 * k)) & 0xFFu;
            h *= 1099511628211ull;
        }
    }
    return h;
}
uint64_t bankHash() {
    BankSlot s(1);
    const int line[6] = {36, 48, 43, 36, 55, 31};
    for (int k = 0; k < 6; ++k) {
        s.on(0, line[k] + 12);
        s.run(40);
    }
    s.off(0);
    s.run(200);
    return hashOf(s.out);
}
}  // namespace

TEST(btype_golden_hashes) {
    const uint64_t e = hashOf(sequence(richContext())), b = bankHash();
    std::printf("  B-Type golden: engine %016llx, bank %016llx\n", (unsigned long long)e, (unsigned long long)b);
    CHECK_EQ(e, uint64_t(0xa8c658ff180389b5ull));
    CHECK_EQ(b, uint64_t(0xae96f89a97b0da1cull));
}
