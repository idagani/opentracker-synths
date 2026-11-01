// FM4 four-operator FM engine (opentracker-synths modules/fm4, engine 0x80):
// registered with its descriptor, the Init patch plays its note, Volume is
// never applied, Mult sets the pitch, modulator Level and Feedback add
// overtones, the algorithms differ, envelopes end the voice, every parameter
// is audible, random parameter sets stay finite.
#include "test.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "audio/synth_engine.h"
#include "audio/synth_params.h"
#include "fm4.h"
#include "formats/patch_pb.h"
#include "modules/module.h"
#include "modules/synth_descriptor.h"

using namespace ot;
using namespace ot::audio;

namespace {

constexpr int kN = fm4::kParamCount;
constexpr int kBlocksPerSec = int(kSampleRate / float(kBlock)) + 1;   // 345

const PatchParamRange* range(int i) { return synthParamRange(fm4::kEngineId, i); }

// Default (Init) values.
std::vector<float> initValues() {
    std::vector<float> v(kN);
    for (int i = 0; i < kN; ++i) v[i] = range(i)->def;
    return v;
}

// One engine voice pool driven directly (no framework).
struct Rig {
    ISynthEngine* e = synthEngineFor(fm4::kEngineId);
    SynthSlotScratch scratch{};
    SynthParams p;
    SynthVoice v[kSynthMaxVoices];
    bool alive[kSynthMaxVoices] = {};
    std::vector<float> L, R;

    explicit Rig(const std::vector<float>& vals) {
        p.engine = fm4::kEngineId;
        p.count = kN;
        set(vals);
        e->slotReset(scratch);
        for (auto& x : v) {
            x = SynthVoice{};
            x.slotScratch = &scratch;
        }
    }
    void set(const std::vector<float>& vals) {
        for (int i = 0; i < kN; ++i) p.start[i] = p.end[i] = vals[i];
        ++p.serial;
    }
    void on(int k, float pitch, bool reused = false, bool overlap = false) {
        if (!reused) {
            std::memset(v[k].state, 0, sizeof(v[k].state));
            v[k].level = 0.0f;
        }
        v[k].gate = true;
        SynthNote n;
        n.pitch = pitch;
        n.note = uint8_t(std::clamp(int(pitch), 0, 127));
        n.reused = reused;
        n.overlap = overlap;
        e->noteOn(v[k], p, n);
        alive[k] = true;
    }
    void off(int k) {
        v[k].gate = false;
        e->noteOff(v[k], p);
    }
    // Render n blocks; appends to L / R. Returns the number of live voices.
    int run(int blocks, float gain = 1.0f) {
        int live = 0;
        for (int b = 0; b < blocks; ++b) {
            float l[kBlock] = {}, r[kBlock] = {};
            e->prepare(scratch, p);
            live = 0;
            for (int k = 0; k < kSynthMaxVoices; ++k) {
                if (!alive[k]) continue;
                alive[k] = e->render(v[k], p, gain, 0.0f, l, r);
                live += alive[k];
            }
            L.insert(L.end(), l, l + kBlock);
            R.insert(R.end(), r, r + kBlock);
        }
        return live;
    }
};

using Buf = std::vector<float>;

bool finite(const Buf& x) {
    for (float s : x)
        if (!std::isfinite(s)) return false;
    return true;
}
float peak(const Buf& x) {
    float m = 0.0f;
    for (float s : x) m = std::max(m, std::fabs(s));
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
    return std::sqrt(s / double(n));
}
Buf slice(const Buf& b, double t0, double t1) {
    const size_t a = size_t(t0 * kSampleRate), e = std::min(b.size(), size_t(t1 * kSampleRate));
    return Buf(b.begin() + long(std::min(a, e)), b.begin() + long(e));
}
// Fundamental (Hz) in [a, b) by autocorrelation over lags of 60..1500 Hz;
// the first lag within 90 % of the best one (no octave-down picks).
double pitchHz(const Buf& x, size_t a, size_t b) {
    const int lo = int(kSampleRate / 1500.0f), hi = int(kSampleRate / 60.0f);
    std::vector<double> ac(size_t(hi) + 2, 0.0);
    double best = 0.0;
    for (int lag = lo; lag <= hi + 1; ++lag) {
        double s = 0.0;
        for (size_t i = a; i + size_t(lag) < b; ++i) s += double(x[i]) * x[i + size_t(lag)];
        ac[size_t(lag)] = s;
        best = std::max(best, s);
    }
    for (int lag = lo + 1; lag <= hi; ++lag)
        if (ac[size_t(lag)] >= 0.9 * best && ac[size_t(lag)] >= ac[size_t(lag) - 1] && ac[size_t(lag)] >= ac[size_t(lag) + 1]) {
            const double y0 = ac[size_t(lag) - 1], y1 = ac[size_t(lag)], y2 = ac[size_t(lag) + 1];
            const double den = y0 - 2.0 * y1 + y2;
            const double off = den != 0.0 ? 0.5 * (y0 - y2) / den : 0.0;
            return kSampleRate / (double(lag) + off);
        }
    return 0.0;
}

// High-frequency proxy: rms of the first difference over rms.
double bright(const Buf& b) {
    double s = 0;
    for (size_t i = 1; i < b.size(); ++i) s += double(b[i] - b[i - 1]) * (b[i] - b[i - 1]);
    const double r = rms(b);
    return r <= 0 ? 0.0 : std::sqrt(s / double(b.size())) / r;
}
// Goertzel magnitude (normalised, Hann window) at hz.
[[maybe_unused]] double tone(const Buf& b, double hz) {
    const double w = 2.0 * 3.14159265358979 * hz / kSampleRate, c = 2.0 * std::cos(w);
    const double wn = 2.0 * 3.14159265358979 / double(b.size());
    double s1 = 0, s2 = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        const double s0 = b[i] * (0.5 - 0.5 * std::cos(wn * double(i))) + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / double(b.size());
}
std::vector<float> held() {
    std::vector<float> c = initValues();
    c[fm4::op(3, fm4::kSustain)] = 1.0f;
    return c;
}
Buf play(const std::vector<float>& vals, float pitch = 48.0f, float seconds = 0.5f) {
    Rig r(vals);
    r.on(0, pitch);
    r.run(int(seconds * kSampleRate / float(kBlock)) + 1);
    return r.L;
}
double hz(float pitch) { return 440.0 * std::pow(2.0, (double(pitch) - 69.0) / 12.0); }
const size_t kA = size_t(0.15 * 44100), kB = size_t(0.45 * 44100);

std::vector<float> richContext() {
    std::vector<float> c = initValues();
    c[fm4::kAlgorithm] = 4.0f;
    c[fm4::kFeedback] = 3.0f;
    c[fm4::kLfoRate] = 6.0f;
    c[fm4::kVibrato] = 0.3f;
    c[fm4::kTremolo] = 0.4f;
    c[fm4::kGlideTime] = 0.1f;
    for (int k = 0; k < 4; ++k) {
        c[fm4::op(k, fm4::kMult)] = float(k + 1);
        c[fm4::op(k, fm4::kDetune)] = 1.0f;
        c[fm4::op(k, fm4::kLevel)] = 0.6f;
        c[fm4::op(k, fm4::kAttack)] = 0.02f;
        c[fm4::op(k, fm4::kDecay)] = 0.03f;   // reaches the sustain stage inside the notes
        c[fm4::op(k, fm4::kSustain)] = 0.5f;
        c[fm4::op(k, fm4::kSusRate)] = 1.0f;
        c[fm4::op(k, fm4::kRelease)] = 0.2f;
        c[fm4::op(k, fm4::kAm)] = 1.0f;
    }
    return c;
}
Buf renderSequence(const std::vector<float>& vals) {
    Rig r(vals);
    r.run(10);
    r.on(0, 57.0f);
    r.run(kBlocksPerSec / 4);
    r.on(0, 64.0f, true, false);
    r.run(kBlocksPerSec / 4);
    r.off(0);
    r.run(kBlocksPerSec / 4);
    return r.L;
}

}  // namespace

TEST(fm4_registered_with_descriptor) {
    mod::ModuleHost::instance().init();
    ISynthEngine* e = synthEngineFor(fm4::kEngineId);
    CHECK(e != nullptr);
    CHECK_STR(e->name(), "FM4");
    const mod::SynthDescriptor* d = mod::ModuleHost::instance().synthDescriptor(fm4::kEngineId);
    CHECK(d && d->paramCount == fm4::kParamCount && std::strcmp(d->name, "FM4") == 0);
    CHECK_STR(patchEngineName(fm4::kEngineId), "FM4");
    SynthPatch p;
    CHECK(patchInit(fm4::kEngineId, &p));
    CHECK_EQ(validatePatch(p, nullptr, 0), 0);
}

TEST(fm4_init_plays_the_note) {
    const Buf x = play(held(), 60.0f, 0.5f);
    CHECK(finite(x));
    CHECK(rms(x) > 0.01);
    CHECK_NEAR(pitchHz(x, kA, kB), hz(60.0f), hz(60.0f) * 0.01);
    CHECK(peak(x) < 1.0f);
}

TEST(fm4_volume_is_never_applied) {
    std::vector<float> a = held(), b = held();
    a[fm4::kVolume] = 0.0f;
    b[fm4::kVolume] = 2.0f;
    CHECK(play(a) == play(b));
}

TEST(fm4_mult_sets_the_carrier_pitch) {
    std::vector<float> c = held();
    c[fm4::op(2, fm4::kLevel)] = 0.0f;   // a plain carrier
    c[fm4::op(3, fm4::kMult)] = 2.0f;
    CHECK_NEAR(pitchHz(play(c, 48.0f), kA, kB), 2.0 * hz(48.0f), 2.0 * hz(48.0f) * 0.01);
    c[fm4::op(3, fm4::kMult)] = 0.0f;    // 0.5
    CHECK_NEAR(pitchHz(play(c, 60.0f), kA, kB), 0.5 * hz(60.0f), 0.5 * hz(60.0f) * 0.01);
}

TEST(fm4_modulation_and_feedback_add_overtones) {
    std::vector<float> c = held();
    c[fm4::op(2, fm4::kLevel)] = 0.0f;
    c[fm4::op(2, fm4::kSustain)] = 1.0f;
    const double plain = bright(slice(play(c), 0.15, 0.45));
    c[fm4::op(2, fm4::kLevel)] = 0.6f;
    const double fm = bright(slice(play(c), 0.15, 0.45));
    CHECK(fm > 1.3 * plain);
    // Feedback on operator 1 as the only carrier (algorithm 8: all carriers).
    std::vector<float> f = initValues();
    f[fm4::kAlgorithm] = 7.0f;
    for (int k = 0; k < 4; ++k) f[fm4::op(k, fm4::kLevel)] = k == 0 ? 1.0f : 0.0f;
    f[fm4::op(0, fm4::kSustain)] = 1.0f;
    const double b0 = bright(slice(play(f), 0.15, 0.45));
    f[fm4::kFeedback] = 7.0f;
    const double b7 = bright(slice(play(f), 0.15, 0.45));
    CHECK(b7 > 1.3 * b0);
}

TEST(fm4_algorithms_differ) {
    std::vector<float> c = richContext();
    std::vector<Buf> outs;
    for (int a = 0; a < 8; ++a) {
        c[fm4::kAlgorithm] = float(a);
        outs.push_back(play(c));
        CHECK(finite(outs.back()));
        CHECK(peak(outs.back()) < 1.5f);
    }
    for (int a = 1; a < 8; ++a) CHECK(rmsDiff(outs[size_t(a - 1)], outs[size_t(a)]) > 1e-3);
}

TEST(fm4_voice_ends_after_release) {
    Rig r(held());
    r.on(0, 48.0f);
    r.run(40);
    r.off(0);
    int blocks = 0;
    while (r.run(1) && blocks < 4 * kBlocksPerSec) ++blocks;
    CHECK(blocks < 4 * kBlocksPerSec);
    // Sustain rate: a held note decays to silence on its own.
    std::vector<float> c = held();
    c[fm4::op(3, fm4::kSusRate)] = 0.3f;
    Rig s(c);
    s.on(0, 48.0f);
    int b2 = 0;
    while (s.run(1) && b2 < 4 * kBlocksPerSec) ++b2;
    CHECK(b2 < 4 * kBlocksPerSec);
}

TEST(fm4_every_parameter_has_an_effect) {
    const std::vector<float> ctx = richContext();
    for (int i = 0; i < kN; ++i) {
        if (i == fm4::kVolume) continue;
        const PatchParamRange* g = range(i);
        const std::vector<float> vals = {g->min, g->max};
        std::vector<float> a = ctx, b = ctx;
        a[i] = vals[0];
        b[i] = vals[1];
        const double d = rmsDiff(renderSequence(a), renderSequence(b));
        CHECK(d > 1e-4);
        if (!(d > 1e-4)) std::printf("       param %d differs by %g only\n", i, d);
    }
}

TEST(fm4_random_parameter_sets_stay_finite) {
    uint32_t seed = 99;
    auto rnd = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return float(seed >> 8) * (1.0f / 16777216.0f);
    };
    int bad = 0;
    for (int set = 0; set < 500; ++set) {
        std::vector<float> a(kN);
        for (int i = 0; i < kN; ++i) a[i] = range(i)->min + rnd() * (range(i)->max - range(i)->min);
        Rig r(a);
        r.on(0, float(int(rnd() * 127.0f)));
        r.on(1, float(int(rnd() * 127.0f)));
        r.run(10);
        r.off(0);
        r.run(5);
        if (!finite(r.L) || peak(r.L) > 4.0f) ++bad;
    }
    CHECK_EQ(bad, 0);
}
