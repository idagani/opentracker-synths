// T-Type drum synth (opentracker-synths modules/ttype, engine 0x85): the
// descriptor ("T-Type", TTYPE, 73 parameters in 9 + 7 sections, the kit
// flag, no Volume), the kit in the slot scratch, the note map (every entry,
// the tom groups, the wraps), every drum on its own note and the drums
// overlapping in the one voice, the hat and cymbal chokes, the Accent law
// and the brightness of soft hits, a Volume FX scaling the ringing kit, the
// steal fade freeing the kit, decays, tunes and sweeps, the Characters, the
// per-drum send taps, Width, Drive (bounded, 0 exact), the ringing cap, no
// clicks on parameter jumps, random settings finite without denormals, the
// levels, through a SynthBank slot at Poly 1 and 4, every parameter audible,
// the cost governor, a track fader and a hand-over keeping the kit's level,
// the rim without a half-rate image, golden hashes.
#include "test.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "audio/synth_bank.h"
#include "audio/synth_engine.h"
#include "audio/synth_params.h"
#include "formats/patch_pb.h"
#include "modules/module.h"
#include "modules/synth_descriptor.h"
#include "modules/synth_models.h"
#include "ttype.h"

using namespace ot;
using namespace ot::audio;
using ttype::Drum;

namespace {

constexpr float kFs = ot::audio::kSampleRate;   // (model/project.h has a kSampleRate too)
constexpr int kN = ttype::kParamCount;
constexpr int kBlocksPerSec = int(kFs / float(kBlock)) + 1;   // 345
using Buf = std::vector<float>;

const PatchParamRange* range(int i) { return synthParamRange(ttype::kEngineId, i); }

std::vector<float> initValues() {
    mod::ModuleHost::instance().init();
    std::vector<float> v(kN);
    for (int i = 0; i < kN; ++i) v[size_t(i)] = range(i)->def;
    return v;
}

// One slot and its one voice driven directly (no framework), with a send tap.
struct Rig {
    ISynthEngine* e = synthEngineFor(ttype::kEngineId);
    SynthSlotScratch scratch{};
    SynthParams p;
    SynthVoice v;
    bool alive = false;
    float lastVel = 1.0f;
    bool tap = false;
    Buf L, R, D, V;   // output, delay and reverb taps

    explicit Rig(const std::vector<float>& vals = initValues(), bool withTap = false) : tap(withTap) {
        p.engine = ttype::kEngineId;
        p.count = kN;
        set(vals);
        e->slotReset(scratch);
        v = SynthVoice{};
        v.slotScratch = &scratch;
    }
    ~Rig() { ttype::setSendTap(0, nullptr, nullptr); }
    void set(const std::vector<float>& vals) {
        for (int i = 0; i < kN; ++i) p.start[i] = p.end[i] = vals[size_t(i)];
        ++p.serial;
    }
    // One parameter moved this block (start -> end ramp).
    void move(int i, float to) {
        for (int k = 0; k < kN; ++k) p.start[k] = p.end[k];
        p.end[i] = to;
        p.moving = true;
        ++p.serial;
    }
    void on(int note, float vel = 1.0f) {
        SynthNote n;
        n.note = uint8_t(note);
        n.pitch = float(note);
        n.velocity = vel;
        n.reused = alive;
        n.overlap = alive;
        if (!alive) {
            std::memset(v.state, 0, sizeof(v.state));
            v.level = 0.0f;
        }
        v.gate = true;
        lastVel = vel;
        e->noteOn(v, p, n);
        alive = true;
    }
    // One block at the framework's gain (default: the last hit's velocity).
    bool block(float g0 = -1.0f, float gInc = 0.0f, bool fading = false) {
        float l[kBlock] = {}, r[kBlock] = {}, d[kBlock] = {}, rv[kBlock] = {};
        if (alive) {
            if (g0 < 0.0f) g0 = lastVel;
            v.fading = fading;
            if (tap) e->setSendTap(0, d, rv, 1.0f, 1.0f);
            e->prepare(scratch, p);
            alive = e->render(v, p, g0, gInc, l, r);
            if (tap) e->setSendTap(0, nullptr, nullptr, 0.0f, 0.0f);
        }
        for (int k = 0; k < kN; ++k) p.start[k] = p.end[k];
        p.moving = false;
        L.insert(L.end(), l, l + kBlock);
        R.insert(R.end(), r, r + kBlock);
        D.insert(D.end(), d, d + kBlock);
        V.insert(V.end(), rv, rv + kBlock);
        return alive;
    }
    void run(int blocks) {
        for (int b = 0; b < blocks; ++b) block();
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
double energy(const Buf& x, size_t a = 0, size_t b = SIZE_MAX) {
    double s = 0.0;
    for (size_t i = a; i < std::min(b, x.size()); ++i) s += double(x[i]) * x[i];
    return s;
}
// Share of the energy in the sample-to-sample differences (a brightness proxy).
double bright(const Buf& x, size_t a = 0, size_t b = SIZE_MAX) {
    double d = 0.0;
    b = std::min(b, x.size());
    for (size_t i = a + 1; i < b; ++i) d += double(x[i] - x[i - 1]) * (x[i] - x[i - 1]);
    const double e = energy(x, a, b);
    return e > 0.0 ? d / e : 0.0;
}
double rmsDiff(const Buf& a, const Buf& b) {
    double s = 0.0;
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) s += double(a[i] - b[i]) * (a[i] - b[i]);
    return std::sqrt(s / double(n ? n : 1));
}
// Frequency from the rising zero crossings in [a, b) seconds.
double zcHz(const Buf& x, double a, double b) {
    const size_t i0 = size_t(a * kFs), i1 = std::min(x.size(), size_t(b * kFs));
    long first = -1, last = -1, n = 0;
    for (size_t i = i0 + 1; i < i1; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            if (first < 0) first = long(i);
            last = long(i);
            ++n;
        }
    return n > 1 ? double(n - 1) * kFs / double(last - first) : 0.0;
}
// Seconds from the loudest block until the block peak falls 40 dB below it.
double t40(const Buf& x) {
    const size_t blocks = x.size() / kBlock;
    float top = 0.0f;
    size_t at = 0;
    for (size_t b = 0; b < blocks; ++b) {
        const float pk = peak(x, b * kBlock, (b + 1) * kBlock);
        if (pk > top) {
            top = pk;
            at = b;
        }
    }
    for (size_t b = at; b < blocks; ++b)
        if (peak(x, b * kBlock, (b + 1) * kBlock) < 0.01f * top) return double(b - at) * kBlock / kFs;
    return double(blocks - at) * kBlock / kFs;
}

// A hit of one note on a fresh kit, n blocks.
Buf hit(const std::vector<float>& vals, int note, int blocks = 120, float vel = 1.0f) {
    Rig r(vals);
    r.on(note, vel);
    r.run(blocks);
    return r.L;
}
Buf hitMono(const std::vector<float>& vals, int note, int blocks = 120, float vel = 1.0f) {
    std::vector<float> c = vals;
    c[ttype::kWidth] = 0.0f;
    return hit(c, note, blocks, vel);
}

// The note of each channel (crash for the cymbal, the open hat).
const int kNoteOf[ttype::kDrums] = {60, 62, 63, 48, 52, 56, 61, 68, 67, 69};
const int kLevelOf[ttype::kDrums] = {ttype::kKickLevel, ttype::kSnareLevel, ttype::kClapLevel, ttype::kTomLoLevel,
                                     ttype::kTomMidLevel, ttype::kTomHiLevel, ttype::kRimLevel, ttype::kBellLevel,
                                     ttype::kHatOpen, ttype::kCymLevel};
const int kCharOf[ttype::kDrums] = {ttype::kKickChar, ttype::kSnareChar, ttype::kClapChar, ttype::kTomChar,
                                    ttype::kTomChar, ttype::kTomChar, ttype::kRimChar, ttype::kBellChar,
                                    ttype::kHatChar, ttype::kCymChar};

// Slot 0 of a bare SynthBank on the T-Type engine.
struct BankSlot {
    SynthPatch patch;
    Governor gov;
    SynthBank bank;
    SynthMixInputs in;
    float L[kBlock], R[kBlock], D[kBlock], V[kBlock];
    float pk = 0.0f;
    long clips = 0;
    Buf out;
    BankSlot(int poly, const std::vector<float>* vals = nullptr) {
        mod::ModuleHost::instance().init();
        gov.setBudget(8);
        bank.setGovernor(&gov);
        bank.setStartsPerBlock(0);
        patchInit(ttype::kEngineId, &patch);
        if (vals)
            for (int i = 0; i < kN; ++i) synthParamSet(patch, i, (*vals)[size_t(i)]);
        bank.bindPatch(0, &patch);
        SynthSlotRecord r = SynthSlotRecord::defaults(0);
        r.model = uint32_t(SynthModel::PERC);
        r.polyphony = uint32_t(poly);
        r.delaySend = 0.0f;
        r.reverbSend = 0.0f;
        bank.setSlotRecord(0, r);
        bank.setSlotEngine(0, ttype::kEngineId);
        block();
    }
    void block() {
        for (uint32_t i = 0; i < kBlock; ++i) L[i] = R[i] = D[i] = V[i] = 0.0f;
        bank.render(in, L, R, D, V, nullptr);
    }
    void on(int track, int note, int volume = -1) {
        SynthNoteOn n;
        n.slot = 0;
        n.note = uint8_t(note);
        n.volume = int8_t(volume);
        bank.noteOn(track, n);
    }
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

}  // namespace

TEST(ttype_descriptor_and_state) {
    mod::ModuleHost::instance().init();
    ISynthEngine* e = synthEngineFor(ttype::kEngineId);
    CHECK(e != nullptr);
    if (!e) return;
    CHECK_EQ(e->id(), ttype::kEngineId);
    CHECK(std::strcmp(e->name(), "TTYPE") == 0);
    const mod::SynthDescriptor* d = mod::ModuleHost::instance().synthDescriptor(ttype::kEngineId);
    CHECK(d != nullptr);
    if (!d) return;
    CHECK(std::strcmp(d->name, "TTYPE") == 0);
    CHECK(d->displayName && std::strcmp(d->displayName, "T-Type") == 0);
    CHECK_EQ(int(d->paramCount), 73);
    CHECK_EQ(int(d->sectionCount), 16);
    CHECK(d->flags & mod::kSynthFlagKitState);
    CHECK_EQ(int(d->volumeParam), -1);
    CHECK_EQ(int(d->extCount), 0);
    CHECK_EQ(patchVolumeParam(ttype::kEngineId), -1);
    CHECK_STR(patchEngineName(ttype::kEngineId), "TTYPE");
    CHECK_STR(synthEngineFolder(ttype::kEngineId), "TTYPE");
    CHECK_STR(synthEngineDisplayName(ttype::kEngineId), "T-Type");
    // In the model list after the Mini models and the module engines before it.
    const int k = synthModelListIndex(ttype::kEngineId);
    CHECK(k >= kSynthModelCount);
    CHECK_STR(synthModelListName(k), "T-Type");
#if defined(OT_MOD_JU6S)
    CHECK(k > synthModelListIndex(0x81));
#endif
#if defined(OT_MOD_DTY1)
    CHECK(k > synthModelListIndex(0x82));
#endif
    SynthPatch init;
    CHECK(patchInit(ttype::kEngineId, &init));
    CHECK_EQ(int(init.paramCount), kN);
    int listed[kN] = {};
    for (int s = 0; s < d->sectionCount; ++s)
        for (int k = 0; k < d->sections[s].count; ++k) ++listed[d->sections[s].params[k]];
    for (int i = 0; i < kN; ++i) {
        CHECK_EQ(listed[i], 1);   // every parameter in exactly one section
        CHECK(d->ui[i].label != nullptr);
        CHECK(d->params[i].min <= d->params[i].def && d->params[i].def <= d->params[i].max);
    }
    // Character names: descriptive words, three per drum.
    for (int c : {ttype::kKickChar, ttype::kSnareChar, ttype::kClapChar, ttype::kTomChar, ttype::kRimChar,
                  ttype::kBellChar, ttype::kHatChar, ttype::kCymChar}) {
        CHECK_EQ(int(d->ui[c].nameCount), 3);
        CHECK_EQ(int(d->params[c].max), 2);
    }
    CHECK(std::strcmp(d->ui[ttype::kKickChar].names[1], "Boom") == 0);
    CHECK(ttype::slotStateBytes() <= kSynthSlotScratchBytes);
    CHECK_EQ(ttype::kRingingMax, 8);
}

TEST(ttype_note_map) {
    int var = -1;
    float semis = -99.0f;
    CHECK_EQ(ttype::drumForNote(60), ttype::kKick);
    CHECK_EQ(ttype::drumForNote(61), ttype::kRim);
    CHECK_EQ(ttype::drumForNote(62), ttype::kSnare);
    CHECK_EQ(ttype::drumForNote(63), ttype::kClap);
    CHECK_EQ(ttype::drumForNote(71), ttype::kClap);
    for (int n = 64; n <= 67; ++n) {
        CHECK_EQ(ttype::drumForNote(uint8_t(n), &var), ttype::kHat);
        CHECK_EQ(var, n - 64);
    }
    CHECK_EQ(ttype::drumForNote(68), ttype::kCowbell);
    CHECK_EQ(ttype::drumForNote(69, &var), ttype::kCymbal);
    CHECK_EQ(var, 0);
    CHECK_EQ(ttype::drumForNote(70, &var), ttype::kCymbal);
    CHECK_EQ(var, 1);
    const Drum groups[3] = {ttype::kTomLo, ttype::kTomMid, ttype::kTomHi};
    for (int g = 0; g < 3; ++g)
        for (int k = 0; k < 4; ++k) {
            CHECK_EQ(ttype::drumForNote(uint8_t(48 + 4 * g + k), &var, &semis), groups[g]);
            CHECK_EQ(semis, float(k));
        }
    // From 72 up: the octave above wraps into 60..71.
    for (int n = 72; n < 128; ++n) {
        int v1 = -1, v2 = -2;
        CHECK_EQ(ttype::drumForNote(uint8_t(n), &v1), ttype::drumForNote(uint8_t(60 + (n - 72) % 12), &v2));
        CHECK_EQ(v1, v2);
    }
    // Below 48: the low tom, transposed.
    for (int n : {0, 24, 36, 47}) {
        CHECK_EQ(ttype::drumForNote(uint8_t(n), &var, &semis), ttype::kTomLo);
        CHECK_EQ(semis, float(n - 48));
    }
    CHECK_EQ(ttype::drumForNote(60, &var, &semis), ttype::kKick);
    CHECK_EQ(semis, 0.0f);
    // Places: kick, snare and mid tom centred; the rest spread both ways.
    CHECK_EQ(ttype::placeOf(ttype::kKick), 0.0f);
    CHECK_EQ(ttype::placeOf(ttype::kSnare), 0.0f);
    CHECK_EQ(ttype::placeOf(ttype::kTomMid), 0.0f);
    CHECK(ttype::placeOf(ttype::kTomLo) < 0.0f && ttype::placeOf(ttype::kTomHi) > 0.0f);
    CHECK(ttype::placeOf(ttype::kHat) > 0.0f && ttype::placeOf(ttype::kRim) < 0.0f);
    CHECK(ttype::placeOf(ttype::kCymbal, 0) < 0.0f && ttype::placeOf(ttype::kCymbal, 1) > 0.0f);
}

TEST(ttype_each_drum_triggers_on_its_note) {
    const std::vector<float> init = initValues();
    for (int d = 0; d < ttype::kDrums; ++d) {
        Rig r(init);
        r.on(kNoteOf[d]);
        r.run(60);
        CHECK(finite(r.L) && finite(r.R));
        CHECK(energy(r.L) + energy(r.R) > 1e-2);
        CHECK(peak(r.L) < 1.0f && peak(r.R) < 1.0f);
        std::printf("  T-Type drum %d (note %d) peak %.2f\n", d, kNoteOf[d], double(std::max(peak(r.L), peak(r.R))));
        CHECK_EQ(ttype::ringing(r.scratch) <= 1, true);
        // That drum alone: its level at 0 silences the note.
        std::vector<float> c = init;
        c[size_t(kLevelOf[d])] = 0.0f;
        if (d == ttype::kHat) c[ttype::kHatClosed] = 0.0f;
        const Buf s = hit(c, kNoteOf[d], 30);
        CHECK_EQ(energy(s), 0.0);
    }
    for (int n = 64; n <= 67; ++n) CHECK(energy(hit(init, n, 30)) > 1e-3);
    for (int n : {49, 50, 51, 53, 57, 59, 70, 71, 30, 100}) CHECK(energy(hit(init, n, 30)) > 1e-3);
}

TEST(ttype_kit_ends_when_silent) {
    Rig r;
    r.on(64);   // closed hat 60 ms
    int b = 0;
    while (r.block() && b < 4000) ++b;
    CHECK(b > 3);
    CHECK(b < 120);
    CHECK(!r.alive);
    CHECK_EQ(ttype::ringing(r.scratch), 0);
}

TEST(ttype_drums_overlap_in_one_voice) {
    const std::vector<float> init = initValues();
    std::vector<float> q = init;
    q[ttype::kKickAttack] = 0.0f;   // no click noise: the snare's noise starts where a fresh kit's does
    Rig r(q);
    r.on(60);
    r.run(2);
    r.on(62);
    r.run(2);
    r.on(67);
    r.run(1);
    CHECK_EQ(ttype::ringing(r.scratch), 3);
    // The kick goes on ringing under the snare: the output is their sum.
    Rig a(q), b(q), c(q);
    a.on(60);
    a.run(4);
    b.run(2);
    b.on(62);
    b.run(2);
    c.run(4);
    c.on(67);
    c.run(1);
    const size_t at = 4 * kBlock;
    double diff = 0.0;
    for (size_t i = 0; i < at; ++i) diff += std::fabs(r.L[i] - (a.L[i] + b.L[i]));
    CHECK(diff < 1e-3);
    // A retrigger restarts its own drum only.
    Rig x(init);
    x.on(60);
    x.on(68);
    x.run(3);
    x.on(68);
    CHECK_EQ(ttype::ringing(x.scratch), 2);
}

TEST(ttype_chokes) {
    const std::vector<float> init = initValues();
    // A closed hat cuts the open one: much less energy after it.
    const Buf open = hit(init, 67, 200);
    Rig r(init);
    r.on(67);
    r.run(20);
    r.on(64);
    r.run(180);
    const size_t from = 40 * kBlock;
    CHECK(energy(r.L, from) < 0.05 * energy(open, from));
    CHECK_EQ(ttype::ringing(r.scratch), 0);
    // Crash and ride share the cymbal channel.
    Rig c(init);
    c.on(69);
    c.run(3);
    c.on(70);
    CHECK_EQ(ttype::ringing(c.scratch), 1);
    // Different channels do not choke: open hat + crash both ring.
    Rig d(init);
    d.on(67);
    d.on(69);
    d.run(2);
    CHECK_EQ(ttype::ringing(d.scratch), 2);
}

TEST(ttype_accent_law_and_brightness) {
    std::vector<float> c = initValues();
    c[ttype::kKickAttack] = 0.0f;   // no click: the body alone is linear in the gain
    c[ttype::kKickDrive] = 0.0f;
    c[ttype::kWidth] = 0.0f;
    // Accent 1: velocity 0.5 halves the hit.
    const double full = energy(hit(c, 60, 60, 1.0f)), half = energy(hit(c, 60, 60, 0.5f));
    CHECK_NEAR(half / full, 0.25, 0.01);
    // Accent 0.5: (1 - a) + a v = 0.75.
    c[ttype::kAccent] = 0.5f;
    CHECK_NEAR(energy(hit(c, 60, 60, 0.5f)) / full, 0.5625, 0.01);
    // Accent 0: velocity ignored.
    c[ttype::kAccent] = 0.0f;
    CHECK_NEAR(energy(hit(c, 60, 60, 0.2f)) / full, 1.0, 0.01);
    // Soft snare hits are darker (the noise is turned down more than the body).
    std::vector<float> s = initValues();
    const Buf loud = hit(s, 62, 40, 1.0f), soft = hit(s, 62, 40, 0.3f);
    CHECK(bright(soft) < 0.97 * bright(loud));
}

TEST(ttype_volume_fx_scales_the_ringing_kit) {
    const std::vector<float> init = initValues();
    Rig a(init), b(init);
    a.on(69);
    b.on(69);
    a.run(10);
    b.run(10);
    // b's framework gain falls to half (a Volume FX on an empty step).
    for (int k = 0; k < 30; ++k) {
        a.block();
        b.block(k == 0 ? 1.0f : 0.5f, k == 0 ? -0.5f / float(kBlock) : 0.0f);
    }
    const size_t from = 12 * kBlock;
    CHECK_NEAR(energy(b.L, from) / energy(a.L, from), 0.25, 0.02);
    // The next hit (a silent kick: Level 0) carries its own velocity; the halved
    // cymbal stays halved.
    std::vector<float> q = init;
    q[ttype::kKickLevel] = 0.0f;
    Rig c(q), h(q);
    c.on(69);
    h.on(69);
    c.run(10);
    h.run(10);
    for (int k = 0; k < 10; ++k) h.block(k == 0 ? 1.0f : 0.5f, k == 0 ? -0.5f / float(kBlock) : 0.0f);
    c.run(10);
    h.on(60, 1.0f);
    c.on(60, 1.0f);
    for (int k = 0; k < 20; ++k) {
        h.block(1.0f, 0.0f);
        c.block(1.0f, 0.0f);
    }
    const size_t after = 20 * kBlock;
    CHECK_NEAR(energy(h.L, after) / energy(c.L, after), 0.25, 0.02);
}

// A track fader (the framework's gain = velocity x fader) scales the kit once,
// not once more at every hit.
TEST(ttype_fader_ratio_does_not_compound) {
    std::vector<float> q = initValues();
    q[ttype::kKickLevel] = 0.0f;   // the hits only re-fold the kit
    Rig a(q), b(q);
    a.on(69);
    b.on(69);
    for (int k = 0; k < 60; ++k) {
        if (k % 10 == 5) {
            a.on(60);
            b.on(60);
        }
        a.block();
        b.block(b.lastVel * 0.7f, 0.0f);
    }
    const size_t from = 10 * kBlock;
    CHECK_NEAR(energy(b.L, from) / energy(a.L, from), 0.49, 0.02);
}

// Poly > 1: a new voice that takes the kit over keeps what a Volume FX did to it.
TEST(ttype_hand_over_keeps_the_kit_master) {
    std::vector<float> q = initValues();
    q[ttype::kKickLevel] = 0.0f;
    Rig a(q), ref(q);
    a.on(69);
    ref.on(69);
    a.run(5);
    ref.run(5);
    for (int k = 0; k < 5; ++k) {
        a.block(k == 0 ? 1.0f : 0.5f, k == 0 ? -0.5f / float(kBlock) : 0.0f);
        ref.block(k == 0 ? 1.0f : 0.5f, k == 0 ? -0.5f / float(kBlock) : 0.0f);
    }
    SynthVoice other{};
    other.slotScratch = &a.scratch;
    SynthNote n;
    n.note = 60;
    n.pitch = 60.0f;
    a.e->noteOn(other, a.p, n);   // voice B takes the kit (the silent kick)
    ref.on(60);
    float l[kBlock] = {}, r[kBlock] = {}, l2[kBlock] = {}, r2[kBlock] = {};
    CHECK(!a.e->render(a.v, a.p, 0.5f, 0.0f, l, r));   // A adds nothing any more
    for (uint32_t i = 0; i < kBlock; ++i) CHECK_EQ(l[i], 0.0f);
    CHECK(a.e->render(other, a.p, 1.0f, 0.0f, l, r));
    ref.e->render(ref.v, ref.p, 1.0f, 0.0f, l2, r2);
    double e1 = 0.0, e2 = 0.0;
    for (uint32_t i = 0; i < kBlock; ++i) {
        e1 += double(l[i]) * l[i];
        e2 += double(l2[i]) * l2[i];
    }
    CHECK_NEAR(e1 / e2, 1.0, 0.05);
}

TEST(ttype_steal_fade_frees_the_kit) {
    Rig r;
    r.on(69);
    r.on(60);
    r.run(3);
    CHECK_EQ(ttype::ringing(r.scratch), 2);
    CHECK(!r.block(1.0f, -1.0f / float(kBlock), true));
    CHECK_EQ(ttype::ringing(r.scratch), 0);
    // The fade ends near zero, without a step.
    const size_t last = r.L.size() - 1;
    CHECK(std::fabs(r.L[last]) < 0.02f);
    // A voice that owned the kit before is not its owner any more.
    Rig q;
    q.on(67);
    q.run(1);
    SynthVoice other{};
    other.slotScratch = &q.scratch;
    SynthNote n;
    n.note = 60;
    n.pitch = 60.0f;
    q.e->noteOn(other, q.p, n);   // a new voice of the slot takes the kit over
    float l[kBlock] = {}, rr[kBlock] = {};
    CHECK(!q.e->render(q.v, q.p, 1.0f, 0.0f, l, rr));
    CHECK(q.e->render(other, q.p, 1.0f, 0.0f, l, rr));
    CHECK_EQ(ttype::ringing(q.scratch), 2);   // the hat keeps ringing under the kick
}

TEST(ttype_decays) {
    const std::vector<float> init = initValues();
    struct Case {
        int note, decay, ch;
        float charVal, a, b;
    };
    const Case cases[] = {
        {60, ttype::kKickDecay, ttype::kKickChar, 1.0f, 0.4f, 0.8f},
        {62, ttype::kSnareDecay, ttype::kSnareChar, 0.0f, 0.15f, 0.3f},
        {63, ttype::kClapDecay, ttype::kClapChar, 0.0f, 0.3f, 0.6f},
        {48, ttype::kTomLoDecay, ttype::kTomChar, 0.0f, 0.3f, 0.6f},
        {52, ttype::kTomMidDecay, ttype::kTomChar, 0.0f, 0.3f, 0.6f},
        {56, ttype::kTomHiDecay, ttype::kTomChar, 0.0f, 0.3f, 0.6f},
        {68, ttype::kBellDecay, ttype::kBellChar, 1.0f, 0.25f, 0.5f},
        {67, ttype::kHatOpenDecay, ttype::kHatChar, 1.0f, 0.3f, 0.6f},
        {64, ttype::kHatClosedDecay, ttype::kHatChar, 1.0f, 0.1f, 0.2f},
        {69, ttype::kCymDecay, ttype::kCymChar, 0.0f, 0.8f, 1.6f},
    };
    for (const Case& k : cases) {
        std::vector<float> c = init;
        c[size_t(k.ch)] = k.charVal;
        c[size_t(k.decay)] = k.a;
        const double ta = t40(hitMono(c, k.note, 2 * kBlocksPerSec));
        c[size_t(k.decay)] = k.b;
        const double tb = t40(hitMono(c, k.note, 2 * kBlocksPerSec));
        CHECK(ta > 0.0);
        const double ratio = tb / ta;
        CHECK(ratio > 1.6 && ratio < 2.4);
        if (!(ratio > 1.6 && ratio < 2.4)) std::printf("       note %d decay ratio %.2f (%.3f s -> %.3f s)\n", k.note, ratio, ta, tb);
    }
    // The kick (Boom, Decay 1 s, -60 dB at Decay): 40 dB down after ~2/3 s.
    std::vector<float> c = init;
    c[ttype::kKickChar] = 1.0f;
    c[ttype::kKickDecay] = 1.0f;
    const double t = t40(hitMono(c, 60, 2 * kBlocksPerSec));
    CHECK(t > 0.8 * 2.0 / 3.0 && t < 1.2 * 2.0 / 3.0);
}

TEST(ttype_tunes_and_sweeps) {
    std::vector<float> c = initValues();
    c[ttype::kWidth] = 0.0f;
    c[ttype::kKickAttack] = 0.0f;
    c[ttype::kKickChar] = 1.0f;   // Boom: pure
    c[ttype::kKickDecay] = 1.5f;
    for (float hz : {40.0f, 55.0f, 90.0f}) {
        c[ttype::kKickTune] = hz;
        const double f = zcHz(hit(c, 60, 140), 0.15, 0.4);
        CHECK(std::fabs(f / hz - 1.0) < 0.02);
    }
    // The sweep: the start is higher than the settled pitch; Sweep 0 none.
    c[ttype::kKickTune] = 50.0f;
    c[ttype::kKickChar] = 0.0f;   // Punch: two octaves
    const Buf sw = hit(c, 60, 140);
    CHECK(zcHz(sw, 0.0, 0.03) > 1.5 * zcHz(sw, 0.15, 0.4));
    c[ttype::kKickSweep] = 0.0f;
    c[ttype::kKickDrive] = 0.0f;
    const Buf flat = hit(c, 60, 140);
    CHECK(std::fabs(zcHz(flat, 0.0, 0.1) / zcHz(flat, 0.15, 0.4) - 1.0) < 0.05);
    // Toms: Round settles on Tune; a note up the group is a semitone up.
    std::vector<float> t = initValues();
    t[ttype::kWidth] = 0.0f;
    t[ttype::kTomTone] = 0.0f;
    t[ttype::kTomChar] = 1.0f;
    t[ttype::kTomLoDecay] = 1.5f;
    t[ttype::kTomLoTune] = 100.0f;
    const double f48 = zcHz(hit(t, 48, 100), 0.12, 0.3), f49 = zcHz(hit(t, 49, 100), 0.12, 0.3);
    CHECK(std::fabs(f48 / 100.0 - 1.0) < 0.03);
    CHECK(std::fabs(f49 / f48 - 1.0594631) < 0.01);
    const double f36 = zcHz(hit(t, 36, 100), 0.12, 0.3);
    CHECK(std::fabs(f36 / 50.0 - 1.0) < 0.03);
    // Snare body: twice the Tune, twice the pitch.
    std::vector<float> s = initValues();
    s[ttype::kWidth] = 0.0f;
    s[ttype::kSnareSnappy] = 0.0f;
    s[ttype::kSnareChar] = 1.0f;
    s[ttype::kSnareDecay] = 0.8f;
    s[ttype::kSnareTune] = 150.0f;
    const double s1 = zcHz(hit(s, 62, 40), 0.02, 0.08);
    s[ttype::kSnareTune] = 300.0f;
    const double s2 = zcHz(hit(s, 62, 40), 0.02, 0.08);
    CHECK(std::fabs(s2 / s1 - 2.0) < 0.1);
    // Rim, bell, hat and cymbal: a higher Tune is brighter.
    struct Tn {
        int note, param;
        float lo, hi;
    };
    const Tn tn[] = {{61, ttype::kRimTune, 800.0f, 3000.0f},
                     {68, ttype::kBellTune, 350.0f, 1000.0f},
                     {67, ttype::kHatTune, 120.0f, 500.0f},
                     {69, ttype::kCymTune, 0.6f, 1.8f}};
    for (const Tn& k : tn) {
        std::vector<float> v = initValues();
        v[ttype::kWidth] = 0.0f;
        v[size_t(k.param)] = k.lo;
        const double bl = bright(hit(v, k.note, 40));
        v[size_t(k.param)] = k.hi;
        const double bh = bright(hit(v, k.note, 40));
        CHECK(bh > 1.1 * bl);
        if (!(bh > 1.1 * bl)) std::printf("       note %d tune brightness %.4f -> %.4f\n", k.note, bl, bh);
    }
}

TEST(ttype_characters_differ) {
    const std::vector<float> init = initValues();
    for (int d = 0; d < ttype::kDrums; ++d) {
        Buf out[3];
        for (int ch = 0; ch < 3; ++ch) {
            std::vector<float> c = init;
            c[size_t(kCharOf[d])] = float(ch);
            out[ch] = hitMono(c, kNoteOf[d], 100);
            CHECK(energy(out[ch]) > 1e-3);
        }
        for (int a = 0; a < 3; ++a)
            for (int b = a + 1; b < 3; ++b) {
                const double diff = rmsDiff(out[a], out[b]);
                const double ref = std::sqrt(std::max(energy(out[a]), energy(out[b])) / double(out[a].size()));
                CHECK(diff > 0.2 * ref);
                if (!(diff > 0.2 * ref)) std::printf("       drum %d characters %d / %d differ by %.4f of %.4f\n", d, a, b, diff, ref);
            }
    }
}

TEST(ttype_send_taps) {
    std::vector<float> c = initValues();
    for (int i = ttype::kSends; i < ttype::kAccent; ++i) c[size_t(i)] = 0.0f;
    // No sends: the taps stay silent.
    {
        Rig r(c, true);
        r.on(62);
        r.on(67);
        r.run(20);
        CHECK_EQ(energy(r.D) + energy(r.V), 0.0);
    }
    // The snare's reverb send: the reverb tap only, scaled by the amount.
    c[size_t(ttype::sendParam(ttype::kSSnare, true))] = 0.5f;
    Rig a(c, true);
    a.on(62);
    a.run(30);
    CHECK_EQ(energy(a.D), 0.0);
    CHECK(energy(a.V) > 1e-4);
    c[size_t(ttype::sendParam(ttype::kSSnare, true))] = 1.0f;
    Rig b(c, true);
    b.on(62);
    b.run(30);
    CHECK_NEAR(energy(b.V) / energy(a.V), 4.0, 0.2);
    // Another drum without a send adds nothing to the tap while alone.
    Rig k(c, true);
    k.on(60);
    k.run(20);
    CHECK_EQ(energy(k.V), 0.0);
    // Crash and ride have their own sends.
    std::vector<float> e = initValues();
    for (int i = ttype::kSends; i < ttype::kAccent; ++i) e[size_t(i)] = 0.0f;
    e[size_t(ttype::sendParam(ttype::kSRide, false))] = 1.0f;
    Rig cr(e, true), rd(e, true);
    cr.on(69);
    rd.on(70);
    cr.run(10);
    rd.run(10);
    CHECK_EQ(energy(cr.D), 0.0);
    CHECK(energy(rd.D) > 1e-4);
}

TEST(ttype_width_places_the_drums) {
    std::vector<float> c = initValues();
    c[ttype::kWidth] = 0.0f;
    for (int d = 0; d < ttype::kDrums; ++d) {
        Rig r(c);
        r.on(kNoteOf[d]);
        r.run(20);
        CHECK(r.L == r.R);
    }
    c[ttype::kWidth] = 1.0f;
    auto side = [&c](int note) {
        Rig r(c);
        r.on(note);
        r.run(30);
        return std::make_pair(energy(r.L), energy(r.R));
    };
    const auto hat = side(67), rim = side(61), kick = side(60), ride = side(70), crash = side(69);
    CHECK(hat.second > 2.0 * hat.first);
    CHECK(rim.first > 1.5 * rim.second);
    CHECK_NEAR(kick.first / kick.second, 1.0, 1e-9);
    CHECK(ride.second > 2.0 * ride.first);
    CHECK(crash.first > 1.5 * crash.second);
}

TEST(ttype_drive_bounded_and_off_exact) {
    // Drive 0: the kit is linear (half the levels = exactly half the output).
    std::vector<float> a = initValues(), b = initValues();
    for (int d = 0; d < ttype::kDrums; ++d) b[size_t(kLevelOf[d])] *= 0.5f;
    b[ttype::kHatClosed] *= 0.5f;
    a[ttype::kKickDrive] = b[ttype::kKickDrive] = 0.0f;
    auto kit = [](const std::vector<float>& v) {
        Rig r(v);
        for (int n : {60, 62, 63, 48, 61, 68, 67, 69}) r.on(n);
        r.run(40);
        return r;
    };
    const Rig ra = kit(a), rb = kit(b);
    bool exact = true;
    for (size_t i = 0; i < ra.L.size(); ++i) exact = exact && rb.L[i] == 0.5f * ra.L[i] && rb.R[i] == 0.5f * ra.R[i];
    CHECK(exact);
    // Drive on: not linear any more, and bounded with every drum at once.
    a[ttype::kDrive] = b[ttype::kDrive] = 1.0f;
    const Rig da = kit(a), db = kit(b);
    double dev = 0.0;
    for (size_t i = 0; i < da.L.size(); ++i) dev += std::fabs(db.L[i] - 0.5f * da.L[i]);
    CHECK(dev > 1.0);
    CHECK(peak(da.L) < 1.0f && peak(da.R) < 1.0f);
    // The kick's own drive.
    std::vector<float> k = initValues();
    k[ttype::kKickDrive] = 0.0f;
    const Buf k0 = hitMono(k, 60, 60);
    k[ttype::kKickDrive] = 1.0f;
    const Buf k1 = hitMono(k, 60, 60);
    CHECK(rmsDiff(k0, k1) > 1e-3);
    CHECK(peak(k1) < 1.0f);
    // The kick's Drive does not turn the kick down (RMS within -1 dB).
    CHECK(energy(k1) > 0.79 * energy(k0));
}

// The rim's resonators at the top of Tune: the half-rate image (fs / 2 - f)
// must not be audible (the rim runs at full rate).
namespace {
double goertzel(const Buf& x, double hz, size_t n, size_t from = 0) {
    const double w = 2.0 * 3.14159265358979 * hz / kFs, c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = from; i < from + n && i < x.size(); ++i) {
        const double s0 = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}
}  // namespace
TEST(ttype_rim_has_no_half_rate_image) {
    std::vector<float> c = initValues();
    c[ttype::kWidth] = 0.0f;
    c[ttype::kRimTune] = 4000.0f;
    c[ttype::kRimChar] = 1.0f;   // Pop: a resonator at 1.6 x Tune = 6.4 kHz
    const Buf x = hit(c, 61, 6);
    // After the strike's broadband onset: the ring itself.
    const double main = goertzel(x, 6400.0, 64, 2), image = goertzel(x, kFs * 0.5 - 6400.0, 64, 2);
    std::printf("  T-Type rim at 4 kHz Pop: %.1f dB at the half-rate image frequency\n", 10.0 * std::log10(image / main));
    CHECK(image < 0.025 * main);   // -16 dB: the band-pass skirt (full rate -21 dB; at half rate the image made it -9 dB)
}

TEST(ttype_ringing_cap) {
    Rig r;
    for (int n : {60, 61, 62, 63, 48, 52, 56, 68, 67, 69}) r.on(n);
    CHECK(ttype::ringing(r.scratch) <= ttype::kRingingMax);
    r.run(2);
    CHECK(ttype::ringing(r.scratch) <= ttype::kRingingMax);
    // The new hits survive (the quietest others are cut).
    std::vector<float> c = initValues();
    c[ttype::kKickDecay] = 2.5f;
    c[ttype::kTomLoDecay] = c[ttype::kTomMidDecay] = c[ttype::kTomHiDecay] = 2.0f;
    c[ttype::kClapDecay] = c[ttype::kBellDecay] = 1.5f;
    c[ttype::kSnareDecay] = 1.0f;
    Rig q(c);
    for (int n : {60, 61, 62, 63, 48, 52, 56, 68}) q.on(n);
    q.run(4);
    q.on(67);
    q.on(69);
    q.run(1);
    CHECK(ttype::ringing(q.scratch) <= ttype::kRingingMax);
    CHECK(ttype::ringingDrum(q.scratch, ttype::kHat));
    CHECK(ttype::ringingDrum(q.scratch, ttype::kCymbal));
    CHECK(ttype::ringingDrum(q.scratch, ttype::kKick));   // the loudest stays
}

// The cost governor: every drum hit at once with Drive, Width and sends up
// stays within the budget (the quietest drums go); a light kit keeps all eight.
TEST(ttype_cost_governor) {
    std::vector<float> c = initValues();
    c[ttype::kKickDecay] = 2.5f;
    c[ttype::kSnareDecay] = 1.0f;
    c[ttype::kClapDecay] = c[ttype::kBellDecay] = 1.5f;
    c[ttype::kTomLoDecay] = c[ttype::kTomMidDecay] = c[ttype::kTomHiDecay] = 2.0f;
    c[ttype::kHatOpenDecay] = 3.0f;
    c[ttype::kCymDecay] = 6.0f;
    c[ttype::kDrive] = 0.7f;
    for (int s = 0; s < ttype::kSounds; ++s) c[size_t(ttype::sendParam(s, true))] = 0.4f;
    Rig r(c, true);
    for (int n : {60, 62, 63, 48, 52, 56, 61, 68, 67, 69}) r.on(n);
    r.run(2);
    CHECK(ttype::kitCost(r.scratch, r.p) <= ttype::kKitBudget - 1000);
    CHECK(ttype::ringing(r.scratch) >= 4);
    std::printf("  T-Type governor: %d of 10 long drums ring with Drive and sends (est. %d insn), kit state %d B\n",
                ttype::ringing(r.scratch), int(ttype::kitCost(r.scratch, r.p)), ttype::slotStateBytes());
    CHECK(ttype::ringingDrum(r.scratch, ttype::kCymbal));   // the last hit stays
    // A drum cut in the block of its own hit (silent before it) never sounds:
    // it is dropped at once, not faded.
    Rig h(c, true);
    for (int n : {60, 62, 63, 48, 52, 56, 61, 68, 67, 69}) h.on(n);
    CHECK(ttype::kitCost(h.scratch, h.p) <= ttype::kKitBudget - 1000);
    CHECK(ttype::ringing(h.scratch) == ttype::ringing(r.scratch));
    // Turning Drive up on a ringing kit (a macro) re-applies the budget.
    std::vector<float> e = c;
    e[ttype::kDrive] = 0.0f;
    for (int i = ttype::kSends; i < ttype::kAccent; ++i) e[size_t(i)] = 0.0f;
    Rig q(e);
    for (int n : {60, 62, 63, 68, 64, 52, 48}) q.on(n);
    q.run(1);
    CHECK_EQ(ttype::ringing(q.scratch), 7);
    {
        Rig all(e);
        for (int n : {60, 62, 63, 48, 52, 56, 61, 68, 67, 69}) all.on(n);
        std::printf("  T-Type governor: %d of 10 ring without Drive and sends\n", ttype::ringing(all.scratch));
    }
    q.move(ttype::kDrive, 1.0f);
    q.run(2);
    CHECK(ttype::kitCost(q.scratch, q.p) <= ttype::kKitBudget - 1000);
    // The quietest drum goes, its Level counted: a mid tom at Level 0.05, hit
    // last (its envelope the highest), is the one cut.
    {
        std::vector<float> v = e;
        v[ttype::kTomMidLevel] = 0.05f;
        Rig w(v);
        for (int n : {60, 62, 61, 68, 63, 48, 56, 52}) w.on(n);
        w.run(1);
        w.on(67);
        w.on(69);
        CHECK(!ttype::ringingDrum(w.scratch, ttype::kTomMid));
        CHECK(ttype::ringingDrum(w.scratch, ttype::kKick));
    }
    // A kit of the half-rate drums (no hats or cymbal, no Drive, no sends) keeps eight.
    Rig l(e);
    for (int n : {60, 62, 61, 68, 63, 48, 52, 56}) l.on(n);
    l.run(1);
    CHECK_EQ(ttype::ringing(l.scratch), ttype::kRingingMax);
}

TEST(ttype_parameter_jumps_do_not_click) {
    std::vector<float> c = initValues();
    c[ttype::kKickDecay] = 2.0f;
    c[ttype::kKickChar] = 1.0f;
    c[ttype::kKickAttack] = 0.0f;
    Rig r(c);
    r.on(60);
    r.run(30);
    float steady = 0.0f;
    for (size_t i = 10 * kBlock; i < r.L.size(); ++i) steady = std::max(steady, std::fabs(r.L[i] - r.L[i - 1]));
    const size_t from = r.L.size();
    const int params[] = {ttype::kWidth, ttype::kDrive, ttype::kKickDrive, ttype::kKickDecay, ttype::kKickTune,
                          ttype::kKickLevel};
    const float to[] = {1.0f, 1.0f, 1.0f, 0.2f, 120.0f, 0.1f};
    for (int k = 0; k < 6; ++k) {
        r.move(params[k], to[k]);
        r.run(3);
    }
    float worst = 0.0f;
    for (size_t i = from; i < r.L.size(); ++i) worst = std::max(worst, std::fabs(r.L[i] - r.L[i - 1]));
    CHECK(worst < 3.0f * steady + 0.01f);
}

TEST(ttype_random_settings_finite) {
    uint32_t s = 12345;
    auto rnd = [&s]() {
        s = s * 1664525u + 1013904223u;
        return float(s >> 8) / 16777216.0f;
    };
    for (int trial = 0; trial < 12; ++trial) {
        std::vector<float> c(kN);
        for (int i = 0; i < kN; ++i) {
            const PatchParamRange* g = range(i);
            c[size_t(i)] = g->min + rnd() * (g->max - g->min);
            if (i == ttype::kKickChar || i == ttype::kSnareChar || i == ttype::kClapChar || i == ttype::kTomChar ||
                i == ttype::kRimChar || i == ttype::kBellChar || i == ttype::kHatChar || i == ttype::kCymChar)
                c[size_t(i)] = std::floor(c[size_t(i)] + 0.5f);
        }
        Rig r(c, true);
        for (int b = 0; b < 200; ++b) {
            if (b % 7 == 0) r.on(int(rnd() * 128.0f) & 127, 0.1f + 0.9f * rnd());
            if (b % 31 == 0) {
                const int i = int(rnd() * float(kN)) % kN;
                const PatchParamRange* g = range(i);
                r.move(i, g->min + rnd() * (g->max - g->min));
            }
            r.block(r.lastVel * (0.5f + 0.5f * rnd()));
            if (!r.alive) r.on(60);
        }
        CHECK(finite(r.L) && finite(r.R) && finite(r.D) && finite(r.V));
        CHECK_EQ(subnormals(r.L) + subnormals(r.R), 0L);
        CHECK(peak(r.L) < 1.0f && peak(r.R) < 1.0f);
    }
}

TEST(ttype_levels_through_the_bank) {
    // Every drum on its own at the default slot (-9 dB): -30 .. -1 dBFS.
    for (int d = 0; d < ttype::kDrums; ++d) {
        BankSlot s(1);
        s.on(0, kNoteOf[d]);
        s.run(kBlocksPerSec);
        CHECK(dbfs(s.pk) > -30.0 && dbfs(s.pk) < -1.0);
        if (!(dbfs(s.pk) > -30.0 && dbfs(s.pk) < -1.0)) std::printf("       drum %d peak %.1f dBFS\n", d, dbfs(s.pk));
    }
    // The whole kit at once.
    BankSlot k(1);
    for (int n : {60, 62, 63, 48, 61, 68, 67, 69}) k.on(0, n);
    k.run(kBlocksPerSec);
    std::printf("  T-Type Init at -9 dB: whole kit %.1f dBFS\n", dbfs(k.pk));
    CHECK(dbfs(k.pk) < -1.0);
    CHECK_EQ(k.clips, 0L);
}

TEST(ttype_through_the_bank_poly_1_and_4) {
    for (int poly : {1, 4}) {
        BankSlot s(poly);
        // A beat on four tracks: every hit sounds, the kit is shared.
        for (int bar = 0; bar < 2; ++bar)
            for (int step = 0; step < 16; ++step) {
                if (step % 4 == 0) s.on(0, 60);
                if (step % 8 == 4) s.on(1, 62);
                s.on(2, step % 4 == 2 ? 66 : 64);
                if (step == 14) s.on(3, 63);
                s.run(20);
            }
        CHECK(dbfs(s.pk) > -30.0);
        CHECK_EQ(s.clips, 0L);
        CHECK(finite(s.out));
        // The open hat rings under a following kick (no choke across channels).
        BankSlot h(poly);
        h.on(0, 67);
        h.run(5);
        const double alone = energy(h.out);
        const size_t at = h.out.size();
        h.on(1, 60);
        h.run(5);
        CHECK(alone > 0.0);
        // At Poly 4 the kick is a new voice that takes the kit over, at Poly 1
        // the same voice: either way the hat goes on (more than the kick alone).
        BankSlot k(poly);
        k.run(5);
        k.on(1, 60);
        k.run(5);
        CHECK(energy(h.out, at) > 1.05 * energy(k.out, at));
    }
}

namespace {
// A rich context for the parameter sweep: every drum, both hats, crash and
// ride, two velocities, a tap.
Buf richRender(const std::vector<float>& c) {
    Rig r(c, true);
    const int seq[] = {60, 62, 63, 48, 52, 56, 61, 68, 64, 67, 69, 70};
    for (int k = 0; k < 12; ++k) {
        r.on(seq[k], k % 2 ? 0.5f : 1.0f);
        r.run(k == 9 ? 30 : 6);
    }
    r.run(60);
    Buf out = r.L;
    out.insert(out.end(), r.R.begin(), r.R.end());
    out.insert(out.end(), r.D.begin(), r.D.end());
    out.insert(out.end(), r.V.begin(), r.V.end());
    return out;
}
}  // namespace

TEST(ttype_every_parameter_has_an_effect) {
    const std::vector<float> ctx = initValues();
    for (int i = 0; i < kN; ++i) {
        const PatchParamRange* g = range(i);
        std::vector<float> a = ctx, b = ctx;
        a[size_t(i)] = g->min;
        b[size_t(i)] = g->max;
        if (i == ttype::kAccent) a[size_t(i)] = 0.0f;
        const double d = rmsDiff(richRender(a), richRender(b));
        CHECK(d > 1e-5);
        if (!(d > 1e-5)) std::printf("       param %d differs by %g only\n", i, d);
    }
}

// Golden hashes (host build, -ffp-contract=off): a rich hit sequence on the
// engine alone (every drum, both velocities, Drive, sends) and a beat through
// a SynthBank slot. Any change of the sound shows here; update the values only
// for an intended change.
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
    for (int step = 0; step < 32; ++step) {
        if (step % 4 == 0) s.on(0, 60);
        if (step % 8 == 4) s.on(1, 62);
        s.on(2, step % 4 == 2 ? 67 : 64);
        if (step % 16 == 14) s.on(3, 63);
        if (step == 30) s.on(3, 69);
        s.run(20);
    }
    s.run(200);
    return hashOf(s.out);
}
}  // namespace

TEST(ttype_golden_hashes) {
    std::vector<float> c = initValues();
    c[ttype::kDrive] = 0.3f;
    const uint64_t e = hashOf(richRender(c)), b = bankHash();
    std::printf("  T-Type golden: engine %016llx, bank %016llx\n", (unsigned long long)e, (unsigned long long)b);
    CHECK_EQ(e, uint64_t(0x9a40e5934d080567ull));
    CHECK_EQ(b, uint64_t(0x52d2d1408358bdc9ull));
}
