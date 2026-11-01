// A-Type bench (core tests/bench_api.h): one voice per block incl. prepare.
// atype.unit: the costliest settings: both VCOs pulse with PWM, VCO 2 synced,
// the ring modulator, the S&H at 50 Hz into VCO 2 and the filter, the HPF,
// the 4-pole at Resonance 90 % with Key Track 100 % and the LFO into it, a
// glide in progress, Duo with two held notes up to note 96.
// atype.unit.init: the Init patch (informational).
#include <cstdio>

#include "audio/synth_engine.h"
#include "bench_api.h"
#include "atype.h"
#include "formats/patch_pb.h"

namespace {

double runBlocks(ot::audio::SynthParams& p, int blocks, bool worst) {
    using namespace ot;
    using namespace ot::audio;
    ISynthEngine* e = atype::engine();
    static SynthSlotScratch sc;
    static SynthVoice v;
    e->slotReset(sc);
    v = SynthVoice{};
    v.slotScratch = &sc;
    v.gate = true;
    SynthNote n;
    n.pitch = worst ? 72.0f : 60.0f;
    e->noteOn(v, p, n);
    n.pitch = 96.0f;   // polyBLEP corrections grow with the pitch; reused: a glide up
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

void atypeBench() {
    using namespace ot;
    using namespace ot::audio;
    static SynthParams p;
    int count = 0;
    const PatchParamRange* t = patchEngineParams(atype::kEngineId, &count);
    if (!t) return;
    p.engine = atype::kEngineId;
    p.count = count;
    for (int i = 0; i < count; ++i) p.start[i] = p.end[i] = t[i].def;
    p.serial = 1;
    const int blocks = 800 / otbench::scale();
    const double init = runBlocks(p, blocks, false);
    auto set = [](int i, float v) { p.start[i] = p.end[i] = v; };
    set(atype::kVco1Wave, float(atype::kPulse));
    set(atype::kVco1Pw, 0.4f);
    set(atype::kVco1Pwm, 0.5f);
    set(atype::kVco1LfoMod, 0.2f);
    set(atype::kVco2Wave, float(atype::kPulse));
    set(atype::kVco2Range, float(atype::k4));
    set(atype::kVco2Tune, 7.0f);
    set(atype::kVco2Pw, 0.3f);
    set(atype::kVco2Pwm, 0.6f);
    set(atype::kVco2Mod, 0.5f);
    set(atype::kVco2ModSrc, float(atype::kModSh));
    set(atype::kSync, 1.0f);
    set(atype::kSource, float(atype::kRing));
    set(atype::kSourceLevel, 1.0f);
    set(atype::kShInput, float(atype::kShMix));
    set(atype::kShRate, 50.0f);
    set(atype::kShLag, 0.05f);
    set(atype::kShAmt, 0.5f);
    set(atype::kResonance, 0.9f);
    set(atype::kKeyTrack, 1.0f);
    set(atype::kLfoAmt, 0.4f);
    set(atype::kLfoShape, float(atype::kTri));
    set(atype::kHpf, 120.0f);
    set(atype::kAttack, 1.0f);
    set(atype::kKeyboard, float(atype::kDuo));
    set(atype::kGlideMode, float(atype::kAlways));
    set(atype::kGlideTime, 3.0f);
    p.held.count = 2;   // Duo: VCO 1 at the low note, VCO 2 at the high one
    p.held.low = 84;
    p.held.high = 96;
    p.held.mono = 1;
    ++p.serial;
    const double worst = runBlocks(p, blocks, true);
    std::printf("    atype.unit %9.1f  (one voice, 2 pulse VCOs with PWM, sync, ring, S&H, HPF, 4-pole, glide, Duo, limit 24000 M4 insn)\n",
                worst);
    std::printf("    atype.unit.init %9.1f  (one voice, the Init patch)\n", init);
}
