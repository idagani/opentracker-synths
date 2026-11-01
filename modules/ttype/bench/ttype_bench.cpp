// T-Type bench (core tests/bench_api.h): one slot (the kit) per block incl.
// prepare, re-hit every 40 blocks. ttype.unit: the costliest eight drums hit
// at once with long decays, Drive and Width on, a send tap, sends up -- as
// much as the cost governor lets ring (ttype.h kKitBudget); the mean block.
// ttype.unit.peak: the costliest block of that run (a hit block: the drums
// the governor cuts fade out in it), informational. ttype.unit.five: five
// drums (kick, snare, closed hat, clap, cymbal), informational.
#include <cstdio>

#include "audio/synth_engine.h"
#include "bench_api.h"
#include "formats/patch_pb.h"
#include "ttype.h"

namespace {

double runBlocks(ot::audio::SynthParams& p, const int* notes, int count, int blocks, double* peak = nullptr) {
    using namespace ot;
    using namespace ot::audio;
    ISynthEngine* e = ttype::engine();
    static SynthSlotScratch sc;
    static SynthVoice v;
    static float l[kBlock], r[kBlock], d[kBlock], rv[kBlock];
    e->slotReset(sc);
    v = SynthVoice{};
    v.slotScratch = &sc;
    v.gate = true;
    double sum = 0.0;
    for (int b = 0; b < blocks; ++b) {
        if (b % 40 == 0) {   // re-hit every ~0.1 s: the drums ring at their loudest
            for (int k = 0; k < count; ++k) {
                SynthNote n;
                n.note = uint8_t(notes[k]);
                n.pitch = float(notes[k]);
                n.velocity = 0.9f;
                n.reused = b > 0 || k > 0;
                e->noteOn(v, p, n);
            }
        }
        for (uint32_t i = 0; i < kBlock; ++i) l[i] = r[i] = d[i] = rv[i] = 0.0f;   // the framework's fresh buses
        const double t0 = otbench::nowUnits();
        e->setSendTap(0, d, rv, 0.5f, 0.5f);
        e->prepare(sc, p);
        e->render(v, p, 0.9f, 0.0f, l, r);
        e->setSendTap(0, nullptr, nullptr, 0.0f, 0.0f);
        const double dt = otbench::nowUnits() - t0;
        sum += dt;
        if (peak && dt > *peak) *peak = dt;
    }
    return sum / blocks;
}

}  // namespace

void ttypeBench() {
    using namespace ot;
    using namespace ot::audio;
    static SynthParams p;
    int count = 0;
    const PatchParamRange* t = patchEngineParams(ttype::kEngineId, &count);
    if (!t) return;
    p.engine = ttype::kEngineId;
    p.count = count;
    for (int i = 0; i < count; ++i) p.start[i] = p.end[i] = t[i].def;
    auto set = [](int i, float x) { p.start[i] = p.end[i] = x; };
    // Long decays (every drum rings through the bench), the costliest variants.
    set(ttype::kKickDecay, 2.5f);
    set(ttype::kSnareDecay, 1.0f);
    set(ttype::kClapDecay, 1.5f);
    set(ttype::kTomLoDecay, 2.0f);
    set(ttype::kTomMidDecay, 2.0f);
    set(ttype::kTomHiDecay, 2.0f);
    set(ttype::kTomTone, 1.0f);
    set(ttype::kBellDecay, 1.5f);
    set(ttype::kHatOpenDecay, 3.0f);
    set(ttype::kCymDecay, 6.0f);
    set(ttype::kDrive, 0.7f);
    set(ttype::kWidth, 1.0f);
    for (int s = 0; s < ttype::kSounds; ++s) set(ttype::sendParam(s, s % 2 == 1), 0.4f);
    p.serial = 1;
    const int blocks = 800 / otbench::scale();
    // The costliest eight of the ten channels (the cheap rim and cowbell out).
    const int worst[8] = {60, 62, 63, 48, 52, 56, 67, 69};
    const int five[5] = {60, 62, 64, 63, 69};
    double pk = 0.0;
    const double w = runBlocks(p, worst, 8, blocks, &pk);
    const double f = runBlocks(p, five, 5, blocks);
    std::printf("    ttype.unit %9.1f  (one kit, the costliest 8 drums hit, Drive, Width, send tap, governed, limit 30000 M4 insn)\n", w);
    std::printf("    ttype.unit.peak %9.1f  (the costliest block of that run, a hit block)\n", pk);
    std::printf("    ttype.unit.five %9.1f  (one kit, kick snare closed hat clap cymbal)\n", f);
}
