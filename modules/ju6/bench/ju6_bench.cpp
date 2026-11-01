// JU6 bench (core tests/bench_api.h): one voice per block incl. prepare, the
// costliest settings: saw + pulse with LFO PWM + sub + noise, bass-boost HPF,
// high resonance, the envelope, LFO (vibrato, VCF) and key follow moving.
// ju6.unit.chorus: the built-in chorus alone, once per slot and block
// (slotPost on a full bus, Chorus II; I costs the same).
#include <cstdio>

#include "audio/synth_engine.h"
#include "bench_api.h"
#include "formats/patch_pb.h"
#include "ju6.h"

void ju6Bench() {
    using namespace ot;
    using namespace ot::audio;
    static SynthParams p;
    int count = 0;
    const PatchParamRange* t = patchEngineParams(ju6::kEngineId, &count);
    if (!t) return;
    p.engine = ju6::kEngineId;
    p.count = count;
    for (int i = 0; i < count; ++i) p.start[i] = p.end[i] = t[i].def;
    auto set = [](int i, float v) { p.start[i] = p.end[i] = v; };
    set(ju6::kPulse, 1.0f);
    set(ju6::kPwMode, float(ju6::kPwLfo));
    set(ju6::kSub, 0.6f);
    set(ju6::kNoise, 0.2f);
    set(ju6::kVibrato, 0.2f);
    set(ju6::kHpf, 0.0f);
    set(ju6::kResonance, 0.9f);
    set(ju6::kVcfLfo, 0.3f);
    set(ju6::kVelVcf, 0.5f);
    set(ju6::kLfoDelay, 0.0f);
    p.serial = 1;
    ISynthEngine* e = ju6::engine();
    static SynthSlotScratch sc;
    static SynthVoice v;
    e->slotReset(sc);
    v = SynthVoice{};
    v.slotScratch = &sc;
    v.gate = true;
    SynthNote n;
    n.pitch = 96.0f;   // polyBLEP corrections grow with the pitch
    e->noteOn(v, p, n);
    static float l[kBlock], r[kBlock];
    const int blocks = 800 / otbench::scale();
    const double t0 = otbench::nowUnits();
    for (int b = 0; b < blocks; ++b) {
        e->prepare(sc, p);
        e->render(v, p, 0.5f, 0.0f, l, r);
    }
    std::printf("    ju6.unit %9.1f  (one voice, saw + PWM pulse + sub + noise, HPF, resonant VCF, LFO, limit 18000 M4 insn)\n",
                (otbench::nowUnits() - t0) / blocks);
    set(ju6::kChorus, float(ju6::kChorusII));
    ++p.serial;
    e->prepare(sc, p);
    double sum = 0.0;
    for (int b = 0; b < blocks; ++b) {
        for (uint32_t i = 0; i < kBlock; ++i) l[i] = r[i] = 0.0f;
        e->render(v, p, 0.5f, 0.0f, l, r);
        const double c0 = otbench::nowUnits();
        e->slotPost(0, sc, p, l, r);
        sum += otbench::nowUnits() - c0;
    }
    std::printf("    ju6.unit.chorus %9.1f  (the chorus per slot and block, Chorus II, limit 12000 M4 insn)\n", sum / blocks);
}
