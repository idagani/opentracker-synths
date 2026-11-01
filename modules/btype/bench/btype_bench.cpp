// B-Type bench (core tests/bench_api.h): one voice per block incl. prepare.
// btype.unit: the costliest settings: two channels Poly5x17 (channel 2 three
// divider steps away) at the 1.79 MHz clock, Coarse, Hi-Pass, the arp at
// 120 Hz with the stepped vibrato, glide, a 4-bit DAC, at note 84.
// btype.unit.init: the Init patch (a pure square), informational.
#include <cstdio>

#include "audio/synth_engine.h"
#include "bench_api.h"
#include "btype.h"
#include "formats/patch_pb.h"

namespace {

double runBlocks(ot::audio::SynthParams& p, int blocks, bool worst) {
    using namespace ot;
    using namespace ot::audio;
    ISynthEngine* e = btype::engine();
    static SynthSlotScratch sc;
    static SynthVoice v;
    static float l[kBlock], r[kBlock];
    e->slotReset(sc);
    e->setTempo(120.0f);
    v = SynthVoice{};
    v.slotScratch = &sc;
    v.gate = true;
    SynthNote n;
    n.pitch = worst ? 72.0f : 60.0f;
    e->noteOn(v, p, n);
    n.pitch = 84.0f;
    n.reused = n.overlap = true;
    if (worst) e->noteOn(v, p, n);
    const double t0 = otbench::nowUnits();
    for (int b = 0; b < blocks; ++b) {
        e->prepare(sc, p);
        e->render(v, p, 0.5f, 0.0f, l, r);
    }
    return (otbench::nowUnits() - t0) / blocks;
}

}  // namespace

void btypeBench() {
    using namespace ot;
    using namespace ot::audio;
    static SynthParams p;
    int count = 0;
    const PatchParamRange* t = patchEngineParams(btype::kEngineId, &count);
    if (!t) return;
    p.engine = btype::kEngineId;
    p.count = count;
    for (int i = 0; i < count; ++i) p.start[i] = p.end[i] = t[i].def;
    p.serial = 1;
    const int blocks = 800 / otbench::scale();
    const double init = runBlocks(p, blocks, false);
    auto set = [](int i, float v) { p.start[i] = p.end[i] = v; };
    set(btype::kClock, float(btype::k179M));
    set(btype::kTuning, float(btype::kCoarse));
    set(btype::kDistortion, float(btype::kPoly5x17));
    set(btype::kOsc2Distortion, float(btype::kPoly5x17));
    set(btype::kOsc2Level, 0.8f);
    set(btype::kOsc2Detune, 3.0f);
    set(btype::kHiPass, 1.0f);
    set(btype::kHpFreq, 900.0f);
    set(btype::kSustain, 15.0f);
    set(btype::kArpMode, 5.0f);   // Dom7
    set(btype::kArpRate, 3.0f);   // 120 Hz
    set(btype::kVibDepth, 0.5f);
    set(btype::kVibDelay, 0.0f);
    set(btype::kGlideTime, 3.0f);
    ++p.serial;
    const double worst = runBlocks(p, blocks, true);
    std::printf("    btype.unit %9.1f  (one voice, two Poly5x17 channels, 1.79 MHz Coarse, Hi-Pass, arp 120 Hz, vibrato, glide, limit 10500 M4 insn)\n",
                worst);
    std::printf("    btype.unit.init %9.1f  (one voice, the Init patch)\n", init);
}
