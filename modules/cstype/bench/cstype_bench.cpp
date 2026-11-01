// CS-Type bench (core tests/bench_api.h): one voice per block incl. prepare.
// cstype.unit: the costliest settings: both layers with saw + pulse (PWM),
// sine and noise, both resonant filters running (HPF up, LPF near
// self-oscillation, Brilliance and the IL / AL contours moving), the ring
// modulator with its sweep, the sub-osc into pitch / filters / VCAs, Touch on
// every target, a glide in progress, note 96. cstype.unit.init: the Init
// patch (informational).
#include <cstdio>
#include <initializer_list>

#include "audio/synth_engine.h"
#include "bench_api.h"
#include "cstype.h"
#include "formats/patch_pb.h"

namespace {

double runBlocks(ot::audio::SynthParams& p, int blocks, bool worst) {
    using namespace ot;
    using namespace ot::audio;
    ISynthEngine* e = cstype::engine();
    static SynthSlotScratch sc;
    static SynthVoice v;
    e->slotReset(sc);
    v = SynthVoice{};
    v.slotScratch = &sc;
    v.gate = true;
    SynthNote n;
    n.pitch = worst ? 72.0f : 60.0f;
    e->noteOn(v, p, n);
    n.pitch = 96.0f;
    n.reused = true;
    n.overlap = true;
    if (worst) e->noteOn(v, p, n);
    static float l[kBlock], r[kBlock];
    const double t0 = otbench::nowUnits();
    for (int b = 0; b < blocks; ++b) {
        e->prepare(sc, p);
        e->render(v, p, 0.5f, 0.0f, l, r);
    }
    return (otbench::nowUnits() - t0) / blocks;
}

}  // namespace

void cstypeBench() {
    using namespace ot;
    using namespace ot::audio;
    static SynthParams p;
    int count = 0;
    const PatchParamRange* t = patchEngineParams(cstype::kEngineId, &count);
    if (!t) return;
    p.engine = cstype::kEngineId;
    p.count = count;
    for (int i = 0; i < count; ++i) p.start[i] = p.end[i] = t[i].def;
    p.serial = 1;
    const int blocks = 800 / otbench::scale();
    const double init = runBlocks(p, blocks, false);
    auto set = [](int i, float v) { p.start[i] = p.end[i] = v; };
    for (int o : {0, int(cstype::kLayer2)}) {
        set(o + cstype::kPulse, 1.0f);
        set(o + cstype::kPw, 0.3f);
        set(o + cstype::kPwm, 0.5f);
        set(o + cstype::kSine, 0.4f);
        set(o + cstype::kNoise, 0.2f);
        set(o + cstype::kHpfCutoff, 150.0f);
        set(o + cstype::kHpfRes, 0.5f);
        set(o + cstype::kLpfRes, 0.9f);
        set(o + cstype::kInitLevel, 0.2f);
        set(o + cstype::kAttackLevel, 0.8f);
        set(o + cstype::kFAttack, 2.0f);
        set(o + cstype::kAttack, 1.0f);
        set(o + cstype::kLevel, 0.7f);
    }
    set(cstype::kBrilliance, 0.3f);
    set(cstype::kRmDepth, 0.5f);
    set(cstype::kRmSweep, 0.8f);
    set(cstype::kRmDecay, 3.0f);
    set(cstype::kToVco, 0.3f);
    set(cstype::kToVcf, 0.3f);
    set(cstype::kToVca, 0.3f);
    set(cstype::kTouch, 0.6f);
    set(cstype::kAfterVcf, 0.5f);
    set(cstype::kAfterVca, 0.5f);
    set(cstype::kRibbon, 0.2f);
    set(cstype::kGlideTime, 3.0f);
    ++p.serial;
    const double worst = runBlocks(p, blocks, true);
    std::printf("    cstype.unit %9.1f  (one voice, 2 layers: saw+pulse+sine+noise, HPF+LPF, ring, sub-osc, Touch, glide, limit 36000 M4 insn)\n",
                worst);
    std::printf("    cstype.unit.init %9.1f  (one voice, the Init patch)\n", init);
}
