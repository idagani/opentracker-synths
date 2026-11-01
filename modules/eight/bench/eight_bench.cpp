// 8-Type bench (core tests/bench_api.h): one voice per block incl. prepare.
// eight.unit: the costliest settings: three oscillators (a pulse with PWM, a
// saw synced to it, a triangle ringed by the saw), Notch at Resonance 15 with Dirt, Grit
// 8, Key Track and the filter envelope, the arp at 120 Hz with the stepped
// vibrato, a slide and a glide in progress, at note 96.
// eight.unit.init: the Init patch (one pulse, low-pass), informational.
#include <cstdio>

#include "audio/synth_engine.h"
#include "bench_api.h"
#include "eight.h"
#include "formats/patch_pb.h"

namespace {

double runBlocks(ot::audio::SynthParams& p, int blocks, bool worst) {
    using namespace ot;
    using namespace ot::audio;
    ISynthEngine* e = eight::engine();
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

void eightBench() {
    using namespace ot;
    using namespace ot::audio;
    using eight::osc;
    static SynthParams p;
    int count = 0;
    const PatchParamRange* t = patchEngineParams(eight::kEngineId, &count);
    if (!t) return;
    p.engine = eight::kEngineId;
    p.count = count;
    for (int i = 0; i < count; ++i) p.start[i] = p.end[i] = t[i].def;
    p.serial = 1;
    const int blocks = 800 / otbench::scale();
    const double init = runBlocks(p, blocks, false);
    auto set = [](int i, float v) { p.start[i] = p.end[i] = v; };
    set(osc(0, eight::kWave), float(eight::kPulse));
    set(osc(0, eight::kPw), 0.4f);
    set(osc(0, eight::kPwmDepth), 0.5f);
    set(osc(0, eight::kLevel), 0.5f);
    set(osc(1, eight::kWave), float(eight::kSaw));
    set(osc(1, eight::kLevel), 0.5f);
    set(osc(1, eight::kSemi), 7.0f);
    set(osc(1, eight::kSync), 1.0f);
    set(osc(2, eight::kWave), float(eight::kTri));
    set(osc(2, eight::kLevel), 0.5f);
    set(osc(2, eight::kSemi), -12.0f);
    set(osc(2, eight::kRing), 1.0f);
    set(osc(2, eight::kPwmDepth), 0.5f);
    set(eight::kMode, float(eight::kNotch));
    set(eight::kResonance, 15.0f);
    set(eight::kDirt, 0.7f);
    set(eight::kGrit, 8.0f);
    set(eight::kKeyTrack, 1.0f);
    set(eight::kEnvAmt, 0.6f);
    set(eight::kEnvDecay, 2.0f);
    set(eight::kArpMode, 5.0f);   // Dom7
    set(eight::kArpRate, 3.0f);   // 120 Hz
    set(eight::kVibDepth, 0.5f);
    set(eight::kVibDelay, 0.0f);
    set(eight::kSlideStart, 12.0f);
    set(eight::kSlideTime, 1.0f);
    set(eight::kGlideTime, 3.0f);
    set(eight::kSustain, 15.0f);
    ++p.serial;
    const double worst = runBlocks(p, blocks, true);
    std::printf("    eight.unit %9.1f  (one voice, 3 oscillators: PWM pulse, synced saw, ringed tri; notch, res 15, dirt, grit, arp, glide, limit 27000 M4 insn)\n",
                worst);
    std::printf("    eight.unit.init %9.1f  (one voice, the Init patch)\n", init);
}
