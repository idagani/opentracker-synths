// Arc-Type chip-style engine (opentracker-synths modules/arc, engine 0x89):
// registered with a valid descriptor ("Arc-Type" on screen,
// Patches/ARCTYPE/), silent when idle; the carrier's pitch, MUL ratios, DT
// cents growing with the pitch, every algorithm's carrier, the sidebands
// growing with a modulator's Level, feedback bounded; the rate tables, Rate
// Scaling, D1L levels, D2R 0 holding, the loop modes at -48 dB; the LFO's
// PMS cents and AMS dB; the 9-bit DAC and the ladder's gap and bound; the Aux
// square (duty, octave), noise (the 15-bit period) and its stepped decay; the
// CHIP arp and slide, glide; through a SynthBank slot at Poly 1 and 4; no
// click on parameter jumps, no NaN or denormals, Volume never applied, the
// slot level, every parameter audible, golden hashes.
#include "test.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <vector>

#include "arc.h"
#include "audio/chip_dsp.h"
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
using arc::op;

namespace {

constexpr float kFs = ot::audio::kSampleRate;
constexpr int kN = arc::kParamCount;
constexpr int kBlocksPerSec = int(kFs / float(kBlock)) + 1;   // 345
constexpr double kPi = 3.14159265358979;

const PatchParamRange* range(int i) { return synthParamRange(arc::kEngineId, i); }

std::vector<float> initValues() {
    mod::ModuleHost::instance().init();
    std::vector<float> v(kN);
    for (int i = 0; i < kN; ++i) v[i] = range(i)->def;
    return v;
}

using Buf = std::vector<float>;

// The engine's voices driven directly (no framework).
struct Rig {
    ISynthEngine* e = synthEngineFor(arc::kEngineId);
    SynthSlotScratch scratch{};
    SynthParams p;
    SynthVoice v[kSynthMaxVoices];
    bool alive[kSynthMaxVoices] = {};
    Buf L, R;
    float bpm = 120.0f;

    explicit Rig(const std::vector<float>& vals) {
        p.engine = arc::kEngineId;
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
    BankSlot(int poly, const std::vector<float>* vals = nullptr, int32_t engine = arc::kEngineId) {
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

// One operator as the carrier (algorithm 1, 2, 3, 4 = all carriers: only
// operator k sounds), a held envelope, the DAC off, no LFO.
std::vector<float> lone(int k = 3, float level = 1.0f) {
    std::vector<float> c = initValues();
    c[arc::kAlgorithm] = 7.0f;
    c[arc::kDac] = float(arc::kDacOff);
    for (int j = 0; j < 4; ++j) {
        c[size_t(op(j, arc::kLevel))] = j == k ? level : 0.0f;
        c[size_t(op(j, arc::kD1r))] = 0.0f;
        c[size_t(op(j, arc::kD2r))] = 0.0f;
        c[size_t(op(j, arc::kRs))] = 0.0f;
    }
    return c;
}
Buf play(const std::vector<float>& vals, float pitch = 57.0f, float seconds = 0.5f) {
    Rig r(vals);
    r.on(0, pitch);
    r.run(int(seconds * kFs / float(kBlock)) + 1);
    return r.L;
}
Buf mid(const Buf& x, double t0 = 0.15, double t1 = 0.45) {
    return Buf(x.begin() + long(t0 * kFs), x.begin() + long(std::min(double(x.size()), t1 * kFs)));
}
// The envelope of x: peak per 64 frames.
std::vector<float> envelope(const Buf& x) {
    std::vector<float> e;
    for (size_t a = 0; a + 64 <= x.size(); a += 64) e.push_back(peak(x, a, a + 64));
    return e;
}
double secondsTo(const std::vector<float>& e, float level, bool falling, size_t from = 0) {
    for (size_t i = from; i < e.size(); ++i)
        if (falling ? e[i] <= level : e[i] >= level) return double(i) * 64.0 / kFs;
    return 1e9;
}
double db(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }

}  // namespace

TEST(arc_descriptor_is_valid) {
    mod::ModuleHost::instance().init();
    ISynthEngine* e = synthEngineFor(arc::kEngineId);
    CHECK(e != nullptr);
    if (!e) return;
    CHECK_STR(e->name(), "ARCTYPE");
    const mod::SynthDescriptor* d = mod::ModuleHost::instance().synthDescriptor(arc::kEngineId);
    CHECK(d != nullptr);
    if (!d) return;
    CHECK_STR(d->name, "ARCTYPE");
    CHECK_STR(d->displayName, "Arc-Type");
    CHECK_EQ(int(d->paramCount), 62);
    CHECK_EQ(int(d->paramCount), kN);
    CHECK_EQ(int(d->volumeParam), int(arc::kVolume));
    CHECK_EQ(int(d->sectionCount), 15);
    CHECK_STR(synthEngineFolder(arc::kEngineId), "ARCTYPE");
    const int k = synthModelListIndex(arc::kEngineId);
    CHECK(k >= kSynthModelCount);
    CHECK_STR(synthModelListName(k), "Arc-Type");
#if defined(OT_MOD_BTY1)
    CHECK(k > synthModelListIndex(0x88));
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
        CHECK_EQ(seen[size_t(i)], 1);
    }
    SynthPatch p;
    CHECK(patchInit(arc::kEngineId, &p));
    CHECK_EQ(validatePatch(p, nullptr, 0), 0);
    CHECK_EQ(int(p.macros[kPatchMacroVelocity].targets[0].param), int(arc::kVolume));
}

TEST(arc_patch_file_round_trip) {
    mod::ModuleHost::instance().init();
    SynthPatch p;
    CHECK(patchInit(arc::kEngineId, &p));
    const float vals[][2] = {{arc::kAlgorithm, 4}, {arc::kFeedback, 5}, {float(op(1, arc::kMul)), 7}, {float(op(2, arc::kDt)), -2},
                             {float(op(3, arc::kLoop)), 3}, {arc::kAuxWave, 2}, {arc::kAuxOctave, -1}};
    for (const auto& v : vals) {
        CHECK(patchParamInRange(arc::kEngineId, int(v[0]), v[1]));
        CHECK(synthParamSet(p, int(v[0]), v[1]));
    }
    std::vector<uint8_t> a(kPatchMaxFileSize), b(kPatchMaxFileSize);
    size_t na = 0, nb = 0;
    CHECK(encodePatch(p, a.data(), a.size(), &na) == PatchStatus::kOk);
    SynthPatch q;
    CHECK(decodePatch(a.data(), na, &q) == PatchStatus::kOk);
    CHECK_EQ(validatePatch(q, nullptr, 0), 0);
    for (const auto& v : vals) CHECK_EQ(q.params[int(v[0])].bits, patchFloatToBits(v[1]));
    CHECK(encodePatch(q, b.data(), b.size(), &nb) == PatchStatus::kOk);
    CHECK(na == nb && std::memcmp(a.data(), b.data(), na) == 0);
}

TEST(arc_silent_when_idle) {
    Rig r(initValues());
    r.run(20);
    CHECK_EQ(peak(r.L), 0.0f);
    r.on(0, 60.0f);
    r.run(kBlocksPerSec / 4);
    CHECK(rms(r.L) > 0.01);
    r.off(0);
    int blocks = 0;
    while (r.run(1) && blocks < 10 * kBlocksPerSec) ++blocks;
    CHECK(blocks < 10 * kBlocksPerSec);
    r.L.clear();
    r.run(20);
    CHECK_EQ(peak(r.L), 0.0f);
}

TEST(arc_pitch_mul_dt_and_algorithms) {
    // The carrier at the note; MUL ratios; DT cents grow with the pitch.
    CHECK(std::fabs(freqOf(play(lone(), 69.0f), 5000, 20000) - 440.0) < 0.5);
    for (int m : {0, 2, 3}) {
        std::vector<float> c = lone();
        c[size_t(op(3, arc::kMul))] = float(m);
        CHECK(std::fabs(freqOf(play(c, 57.0f), 5000, 20000) / hz(57.0f) - arc::mulOf(m)) < 0.005 * arc::mulOf(m));
    }
    CHECK(std::fabs(arc::dtCents(3, 64.0f) - 9.0f) < 1e-4f);
    CHECK(std::fabs(arc::dtCents(-2, 64.0f) + 6.0f) < 1e-4f);
    CHECK(arc::dtCents(3, 100.0f) > arc::dtCents(3, 40.0f));
    std::vector<float> d = lone();
    d[size_t(op(3, arc::kDt))] = 3.0f;
    const double c = 1200.0 * std::log2(freqOf(play(d, 64.0f), 5000, 20000) / hz(64.0f));
    CHECK(std::fabs(c - 9.0) < 1.0);
    // Every algorithm: the carrier operator 4 always sounds at the note; a
    // modulator at Level 50 % into it adds sidebands (more with more Level).
    for (int a = 0; a < 8; ++a) {
        std::vector<float> c2 = initValues();
        c2[arc::kAlgorithm] = float(a);
        c2[arc::kDac] = 0.0f;
        for (int j = 0; j < 4; ++j) c2[size_t(op(j, arc::kLevel))] = j == 3 ? 1.0f : 0.0f;
        const Buf x = mid(play(c2, 57.0f));
        CHECK(tone(x, hz(57.0f)) > 0.01);
    }
    std::vector<float> fmv = lone();
    fmv[arc::kAlgorithm] = 4.0f;   // 1 > 2, 3 > 4
    fmv[size_t(op(2, arc::kLevel))] = 0.1f;
    fmv[size_t(op(2, arc::kMul))] = 2.0f;
    auto bands = [](const Buf& x, double f0) {
        int n = 0;
        for (int k = 1; k <= 8; ++k) n += tone(x, f0 * (1 + 2 * k)) > 0.002;   // sidebands at f0 (1 +- 2k)
        return n;
    };
    const int few = bands(mid(play(fmv, 45.0f)), hz(45.0f));
    fmv[size_t(op(2, arc::kLevel))] = 0.6f;
    const int many = bands(mid(play(fmv, 45.0f)), hz(45.0f));
    std::printf("  sidebands: %d at modulator Level 10 %%, %d at 60 %%\n", few, many);
    CHECK(many > few);
    // Feedback bounded.
    std::vector<float> fb = lone();
    fb[arc::kFeedback] = 7.0f;
    fb[size_t(op(0, arc::kLevel))] = 1.0f;
    fb[size_t(op(3, arc::kLevel))] = 0.0f;
    const Buf y = play(fb, 57.0f);
    CHECK(finite(y));
    CHECK(peak(y) < 1.0f);
    CHECK(rms(y) > 0.01);
}

TEST(arc_rate_tables_scaling_levels_and_hold) {
    // Attack 20 against the table (+-20 %); Rate Scaling 3 speeds it up at a high note.
    std::vector<float> c = lone();
    c[size_t(op(3, arc::kAr))] = 20.0f;
    const std::vector<float> e = envelope(play(c, 48.0f, 1.0f));
    const float top = *std::max_element(e.begin(), e.end());
    const double t = secondsTo(e, 0.95f * top, false);
    std::printf("  attack 20: %.3f s (table %.3f)\n", t, arc::attackSeconds(20));
    CHECK(std::fabs(t - arc::attackSeconds(20)) < 0.2 * arc::attackSeconds(20) + 0.006);
    c[size_t(op(3, arc::kRs))] = 3.0f;
    const double tHi = secondsTo(envelope(play(c, 96.0f, 1.0f)), 0.95f * top, false);
    CHECK(tHi < 0.6 * t);
    // First decay 16 to D1L 6 (-18 dB), D2R 0 holds there.
    std::vector<float> d = lone();
    d[size_t(op(3, arc::kD1r))] = 16.0f;
    d[size_t(op(3, arc::kD1l))] = 6.0f;
    const std::vector<float> de = envelope(play(d, 57.0f, 2.0f));
    const float full = de[2];
    const double held = db(de[de.size() - 10] / full);
    std::printf("  decay to D1L 6: %.2f dB held\n", held);
    CHECK(std::fabs(held + 18.0) < 1.5);
    CHECK(std::fabs(de[de.size() - 10] - de[de.size() - 200]) < 0.01f * full);   // D2R 0: holds
    // 48 dB at the decay table time (+-20 %).
    d[size_t(op(3, arc::kD1l))] = 15.0f;
    d[size_t(op(3, arc::kD2r))] = 16.0f;   // the same rate past D1L (-45 dB)
    const std::vector<float> f = envelope(play(d, 57.0f, 2.0f));
    const double t48 = secondsTo(f, f[2] * 0.0039811f, true, 2);
    std::printf("  decay 16: -48 dB after %.3f s (table %.3f)\n", t48, arc::decaySeconds(16));
    CHECK(std::fabs(t48 - arc::decaySeconds(16)) < 0.2 * arc::decaySeconds(16));
    // Release: the voice ends.
    Rig r(lone());
    r.on(0, 57.0f);
    r.run(40);
    r.off(0);
    int n = 0;
    while (r.run(1) && n < 4 * kBlocksPerSec) ++n;
    CHECK(n < 4 * kBlocksPerSec);
}

TEST(arc_loop_modes) {
    // The envelope at -48 dB: Off decays away; Repeat restarts; Mirror comes back up;
    // Hold Inv holds near full; Repeat Inv rises again.
    double lateDb[5];
    for (int m = 0; m < 5; ++m) {
        std::vector<float> c = lone();
        c[size_t(op(3, arc::kD1r))] = 26.0f;   // fast
        c[size_t(op(3, arc::kD1l))] = 15.0f;
        c[size_t(op(3, arc::kD2r))] = 26.0f;
        c[size_t(op(3, arc::kLoop))] = float(m);
        const std::vector<float> e = envelope(play(c, 57.0f, 1.5f));
        float mx = 0;
        for (size_t i = e.size() - 300; i < e.size(); ++i) mx = std::max(mx, e[i]);
        lateDb[m] = db(mx / e[2]);
        std::printf("  loop %d: late peak %.1f dB\n", m, lateDb[m]);
    }
    CHECK(lateDb[arc::kLoopOff] < -60.0);
    CHECK(lateDb[arc::kRepeat] > -3.0);
    CHECK(lateDb[arc::kMirror] > -3.0);
    CHECK(lateDb[arc::kHoldInv] > -1.0);
    CHECK(lateDb[arc::kRepeatInv] > -3.0);
}

TEST(arc_lfo_pms_and_ams) {
    CHECK_EQ(arc::lfoHz(0), 0.0f);
    CHECK(arc::lfoHz(8) > 60.0f && arc::lfoHz(1) >= 4.0f);
    CHECK_EQ(arc::pmsCents(7), 80.0f);
    // PMS 7: the pitch swings +-80 cents at the LFO's rate.
    std::vector<float> c = lone();
    c[arc::kLfoRate] = 1.0f;   // 4 Hz
    c[arc::kPms] = 7.0f;
    const Buf x = play(c, 69.0f, 1.2f);
    double lo = 0, hi = 0;
    for (size_t a = size_t(0.1 * kFs); a + 600 < x.size(); a += 300) {
        const double ct = 1200.0 * std::log2(freqOf(x, a, a + 600) / 440.0);
        lo = std::min(lo, ct), hi = std::max(hi, ct);
    }
    std::printf("  PMS 7: %.0f .. %.0f cents\n", lo, hi);
    CHECK(hi > 60.0 && hi < 90.0);
    CHECK(lo < -60.0 && lo > -90.0);
    // AMS 3: the level swings by ~11.8 dB.
    std::vector<float> a = lone();
    a[arc::kLfoRate] = 1.0f;
    a[size_t(op(3, arc::kAms))] = 3.0f;
    const std::vector<float> e = envelope(play(a, 69.0f, 1.2f));
    float mx = 0, mn = 1e9f;
    for (size_t i = 100; i < e.size(); ++i) mx = std::max(mx, e[i]), mn = std::min(mn, e[i]);
    std::printf("  AMS 3: %.1f dB swing\n", db(mx / mn));
    CHECK(std::fabs(db(mx / mn) - arc::amsDb(3)) < 1.5);
}

TEST(arc_dac_and_ladder) {
    // 9-bit: at most 512 levels; Off: exact (many); Ladder: a gap around zero, bounded.
    std::vector<float> c = lone(3, 0.9f);
    c[arc::kDac] = float(arc::kDac9);
    const Buf x = play(c, 33.0f, 0.4f);
    std::set<float> lv(x.begin(), x.end());
    CHECK(lv.size() <= 512);
    c[arc::kDac] = float(arc::kDacOff);
    const Buf y = play(c, 33.0f, 0.4f);
    std::set<float> lo(y.begin(), y.end());
    CHECK(lo.size() > 2000);
    c[arc::kDac] = float(arc::kDacLadder);
    const Buf z = play(c, 33.0f, 0.4f);
    // The smallest non-zero magnitude jumps past 2 LSB (x the trim); the error stays within 4 LSB.
    float small = 1e9f, err = 0.0f;
    const float trim = peak(y) / (0.9f / 4.0f);   // one of four carriers (the mean)
    for (size_t i = 0; i < z.size(); ++i) {
        if (z[i] != 0.0f) small = std::min(small, std::fabs(z[i]));
        err = std::max(err, std::fabs(z[i] - y[i]));
    }
    std::printf("  ladder: smallest step %.2f LSB, |error| %.2f LSB\n", small / trim * 256.0, err / trim * 256.0);
    CHECK(small / trim * 256.0 > 2.0);
    CHECK(err / trim * 256.0 < 4.6);
}

TEST(arc_aux_square_noise_octave_decay) {
    std::vector<float> c = lone(3, 0.0f);
    c[arc::kAuxWave] = float(arc::kAuxSquare);
    c[arc::kAuxLevel] = 1.0f;
    c[arc::kAuxDecay] = 0.0f;   // holds
    const Buf x = play(c, 57.0f);
    CHECK(std::fabs(freqOf(x, 5000, 20000) - hz(57.0f)) < 0.5);
    float lo = 1e9f, hi = -1e9f;
    for (size_t i = 5000; i < 20000; ++i) lo = std::min(lo, x[i]), hi = std::max(hi, x[i]);
    long up = 0;
    for (size_t i = 5000; i < 20000; ++i) up += x[i] > 0.5f * (lo + hi);
    CHECK(std::fabs(up / 15000.0 - 0.5) < 0.02);
    c[arc::kAuxOctave] = 1.0f;
    CHECK(std::fabs(freqOf(play(c, 57.0f), 5000, 20000) - hz(69.0f)) < 1.0);
    // Noise: 16 shifts a cycle; a 15-bit register at one shift a frame repeats after 32,767 frames.
    c[arc::kAuxOctave] = 0.0f;
    c[arc::kAuxWave] = float(arc::kAuxNoise);
    const float n1 = float(69.0 + 12.0 * std::log2(kFs / 16.0 / 440.0));
    const Buf nz = play(c, n1, 1.7f);
    long same = 0;
    for (size_t i = 1000; i < 21000; ++i) same += nz[i] == nz[i + 32767];
    CHECK(same > 19900);
    // Decay 2 frames a level: 15 levels in 0.5 s, the voice ends with the FM silent.
    c[arc::kAuxWave] = float(arc::kAuxSquare);
    c[arc::kAuxDecay] = 2.0f;
    Rig r(c);
    r.on(0, 57.0f);
    int b = 0;
    while (r.run(1) && b < 2 * kBlocksPerSec) ++b;
    std::printf("  aux decay 2: ends after %.3f s\n", b * kBlock / kFs);
    CHECK(std::fabs(b * kBlock / kFs - 0.5) < 0.02);
}

TEST(arc_tricks_and_glide) {
    std::vector<float> a = lone();
    a[arc::kArpMode] = float(chip::kArpMaj);
    a[arc::kArpRate] = float(chip::k60Hz);
    const Buf x = play(a, 69.0f, 0.2f);
    const double per = kFs / 60.0;
    const int want[3] = {0, 4, 7};
    for (int k = 0; k < 6; ++k) {
        const double st = semis(freqOf(x, size_t(k * per + 0.2 * per), size_t((k + 1) * per - 0.1 * per)), 440.0);
        CHECK(std::fabs(st - want[k % 3]) < 0.2);
    }
    std::vector<float> s = lone();
    s[arc::kSlideStart] = 24.0f;
    s[arc::kSlideTime] = 0.06f;
    const Buf z = play(s, 45.0f, 0.3f);
    CHECK(semis(freqOf(z, 0, 600), hz(45.0f)) > 18.0);
    CHECK(std::fabs(semis(freqOf(z, size_t(0.15 * kFs), size_t(0.25 * kFs)), hz(45.0f))) < 0.1);
    std::vector<float> g = lone();
    g[arc::kGlideTime] = 0.3f;
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

TEST(arc_through_the_bank_poly1_and_4) {
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

TEST(arc_parameter_jumps_do_not_click) {
    struct Jump {
        int param;
        float a, b;
    };
    const Jump jumps[] = {{op(3, arc::kLevel), 0.2f, 1.0f}, {op(2, arc::kLevel), 0.0f, 0.5f}, {arc::kVolume, 0.2f, 2.0f}};
    for (const Jump& j : jumps) {
        std::vector<float> c = initValues();
        c[size_t(op(3, arc::kD1r))] = 0.0f;
        c[arc::kDac] = 0.0f;
        c[size_t(j.param)] = j.a;
        Rig r(c);
        r.on(0, 45.0f);
        r.run(kBlocksPerSec / 4);
        auto maxStep = [&r](size_t a, size_t b) {
            double m = 0;
            for (size_t i = a + 1; i < b; ++i) m = std::max(m, double(std::fabs(r.L[i] - r.L[i - 1])));
            return m;
        };
        for (float v : {j.b, j.a}) {
            const size_t at = r.L.size();
            const double before = maxStep(at - 20 * kBlock, at);
            c[size_t(j.param)] = v;
            r.set(c);
            r.run(2);
            const double jump = maxStep(at - 1, at + 2 * kBlock);
            r.run(kBlocksPerSec / 4);
            const double after = maxStep(r.L.size() - 20 * kBlock, r.L.size());
            const double lim = 2.0 * std::max(before, after) + 1e-4;
            if (!(jump < lim)) std::printf("       param %d -> %.2f: step %.4f (steady %.4f / %.4f)\n", j.param, v, jump, before, after);
            CHECK(jump < lim);
        }
    }
}

TEST(arc_no_nan_or_denormals) {
    uint32_t seed = 89;
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
        Rig r(a);
        r.on(0, 127.0f);
        r.on(1, 0.0f);
        r.run(kBlocksPerSec);
        r.off(0);
        r.off(1);
        r.run(kBlocksPerSec);
        CHECK(finite(r.L));
        CHECK(peak(r.L) < 2.0f);
        CHECK_EQ(subnormals(r.L), 0L);
    }
}

TEST(arc_voice_level_and_volume) {
    Rig r(initValues());
    r.on(0, 60.0f);
    CHECK_EQ(r.v[0].level, 1.0f);
    std::vector<float> a = initValues(), b = initValues();
    a[arc::kVolume] = 0.0f;
    b[arc::kVolume] = 2.0f;
    CHECK(play(a) == play(b));
}

TEST(arc_level_matches_the_synth_conventions) {
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
    std::printf("  Arc-Type Init at -9 dB: peak %.1f dBFS (ACD %.1f), 4-voice chords %.1f dBFS\n", dbfs(s.pk), dbfs(a.pk), dbfs(c.pk));
    CHECK(std::fabs(dbfs(s.pk) - dbfs(a.pk)) < 3.0);
#else
    std::printf("  Arc-Type Init at -9 dB: peak %.1f dBFS, 4-voice chords %.1f dBFS\n", dbfs(s.pk), dbfs(c.pk));
#endif
    CHECK(dbfs(s.pk) < -6.0 && dbfs(s.pk) > -20.0);
    CHECK(dbfs(c.pk) < -1.0);
    CHECK_EQ(s.clips + c.clips, 0L);
}

namespace {
std::vector<float> richContext() {
    std::vector<float> c = initValues();
    c[arc::kAlgorithm] = 3.0f;   // ((1 > 2) + 3) > 4: every operator heard or modulating
    c[arc::kFeedback] = 3.0f;
    c[arc::kLfoRate] = 3.0f;
    c[arc::kPms] = 3.0f;
    for (int k = 0; k < 4; ++k) {
        c[size_t(op(k, arc::kLevel))] = k == 3 ? 1.0f : 0.4f;
        c[size_t(op(k, arc::kMul))] = float(k + 1);
        c[size_t(op(k, arc::kDt))] = 1.0f;
        c[size_t(op(k, arc::kAr))] = 24.0f;
        c[size_t(op(k, arc::kD1r))] = 18.0f;
        c[size_t(op(k, arc::kD1l))] = 5.0f;
        c[size_t(op(k, arc::kD2r))] = 10.0f;
        c[size_t(op(k, arc::kRr))] = 8.0f;
        c[size_t(op(k, arc::kAms))] = 1.0f;
    }
    c[arc::kAuxWave] = float(arc::kAuxSquare);
    c[arc::kAuxLevel] = 0.3f;
    c[arc::kArpMode] = float(chip::kArpCustom);
    c[arc::kSlideStart] = 5.0f;
    c[arc::kGlideTime] = 0.1f;
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

TEST(arc_every_parameter_has_an_effect) {
    const std::vector<float> ctx = richContext();
    for (int i = 0; i < kN; ++i) {
        if (i == arc::kVolume) continue;
        std::vector<float> a = ctx, b = ctx;
        a[size_t(i)] = range(i)->min;
        b[size_t(i)] = range(i)->max;
        if (i == arc::kGlideMode) {
            a[size_t(i)] = float(arc::kAlways);
            b[size_t(i)] = float(arc::kLegato);
        }
        if (i >= arc::kOpBase && i < arc::kAuxWave && (i - arc::kOpBase) % arc::kOpStride == arc::kLoop) {
            const int k = (i - arc::kOpBase) / arc::kOpStride;   // a fast decay reaches -48 dB within the note
            a[size_t(op(k, arc::kD1l))] = b[size_t(op(k, arc::kD1l))] = 15.0f;
            a[size_t(op(k, arc::kD1r))] = b[size_t(op(k, arc::kD1r))] = 28.0f;
        }
        if (i == arc::kAuxDecay) a[arc::kAuxDecay] = 1.0f;
        if (i >= arc::kOpBase && i < arc::kAuxWave && (i - arc::kOpBase) % arc::kOpStride == arc::kRr) {
            // Held at their first decay levels and a long carrier tail: a modulator's release is heard.
            for (int k = 0; k < 4; ++k) a[size_t(op(k, arc::kD2r))] = b[size_t(op(k, arc::kD2r))] = 0.0f;
            if (i != op(3, arc::kRr)) a[size_t(op(3, arc::kRr))] = b[size_t(op(3, arc::kRr))] = 2.0f;
        }
        const double d = rmsDiff(sequence(a), sequence(b));
        CHECK(d > 1e-4);
        if (!(d > 1e-4)) std::printf("       param %d differs by %g only\n", i, d);
    }
}

TEST(arc_loop_modes_release) {
    // After the note-off every loop mode (the inverted ones from the level they
    // play) decays to silence and the voice ends without a step.
    for (int m = 0; m < 5; ++m) {
        std::vector<float> c = lone();
        c[size_t(op(3, arc::kD1r))] = 26.0f;
        c[size_t(op(3, arc::kD1l))] = 15.0f;
        c[size_t(op(3, arc::kD2r))] = 26.0f;
        c[size_t(op(3, arc::kRr))] = 8.0f;
        c[size_t(op(3, arc::kLoop))] = float(m);
        Rig r(c);
        r.on(0, 57.0f);
        r.run(200);
        const float held = peak(r.L, r.L.size() - 4000, r.L.size());
        r.off(0);
        int n = 0;
        while (r.run(1) && n < 4 * kBlocksPerSec) ++n;
        const float last = peak(r.L, r.L.size() - 2 * kBlock, r.L.size());
        const size_t at = 200 * kBlock + kBlock * 8;
        std::printf("  loop %d release: held %.3f, after 8 blocks %.3f, ends after %d blocks, last %.4f\n", m, held,
                    peak(r.L, at, at + kBlock), n, last);
        CHECK(n < 4 * kBlocksPerSec);
        CHECK(last < 0.01f);
        if (m == arc::kHoldInv || m == arc::kRepeatInv) CHECK(peak(r.L, r.L.size() - 30 * kBlock, r.L.size()) < 0.5f * held + 0.01f);
    }
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

TEST(arc_golden_hashes) {
    const uint64_t e = hashOf(sequence(richContext())), b = bankHash();
    std::printf("  Arc-Type golden: engine %016llx, bank %016llx\n", (unsigned long long)e, (unsigned long long)b);
    CHECK_EQ(e, uint64_t(0x730a649d45eed924ull));
    CHECK_EQ(b, uint64_t(0x97f420e5a6e6891full));
}
