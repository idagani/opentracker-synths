// Arc-Type bench (core tests/bench_api.h): one voice per block incl. prepare.
// arc.unit: the costliest settings: algorithm 1 > 2 > 3 > 4, feedback 7, every
// operator looping (Repeat) with Rate Scaling, the LFO with PMS 7 and AMS 3 on
// every operator, the Ladder DAC, the Aux noise, the arp at 120 Hz, a glide,
// at note 96. arc.unit.init: the Init patch, informational.
#include <cstdio>

#include "arc.h"
#include "audio/synth_engine.h"
#include "bench_api.h"
#include "formats/patch_pb.h"

namespace {

double runBlocks(ot::audio::SynthParams& p, int blocks, bool worst) {
    using namespace ot;
    using namespace ot::audio;
    ISynthEngine* e = arc::engine();
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
    n.pitch = 96.0f;
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

void arcBench() {
    using namespace ot;
    using namespace ot::audio;
    using arc::op;
    static SynthParams p;
    int count = 0;
    const PatchParamRange* t = patchEngineParams(arc::kEngineId, &count);
    if (!t) return;
    p.engine = arc::kEngineId;
    p.count = count;
    for (int i = 0; i < count; ++i) p.start[i] = p.end[i] = t[i].def;
    p.serial = 1;
    const int blocks = 800 / otbench::scale();
    const double init = runBlocks(p, blocks, false);
    auto set = [](int i, float v) { p.start[i] = p.end[i] = v; };
    set(arc::kFeedback, 7.0f);
    set(arc::kDac, float(arc::kDacLadder));
    set(arc::kLfoRate, 5.0f);
    set(arc::kPms, 7.0f);
    for (int k = 0; k < 4; ++k) {
        set(op(k, arc::kLevel), 0.6f);
        set(op(k, arc::kMul), float(k + 1));
        set(op(k, arc::kDt), 2.0f);
        set(op(k, arc::kLoop), float(arc::kRepeat));
        set(op(k, arc::kAms), 3.0f);
        set(op(k, arc::kRs), 3.0f);
        set(op(k, arc::kD1r), 20.0f);
        set(op(k, arc::kD1l), 15.0f);
        set(op(k, arc::kD2r), 20.0f);
    }
    set(arc::kAuxWave, float(arc::kAuxNoise));
    set(arc::kAuxLevel, 0.4f);
    set(arc::kAuxDecay, 0.0f);
    set(arc::kArpMode, 5.0f);   // Dom7
    set(arc::kArpRate, 3.0f);   // 120 Hz
    set(arc::kGlideTime, 3.0f);
    ++p.serial;
    const double worst = runBlocks(p, blocks, true);
    std::printf("    arc.unit %9.1f  (one voice, 1>2>3>4, feedback 7, every operator looping, LFO PMS / AMS, ladder, aux noise, arp, limit 26000 M4 insn)\n",
                worst);
    std::printf("    arc.unit.init %9.1f  (one voice, the Init patch)\n", init);
}
