// FM4 bench (core tests/bench_api.h): one voice per block incl. prepare,
// four operators with Feedback, every operator's envelope and the LFO moving.
#include <cstdio>

#include "audio/synth_engine.h"
#include "bench_api.h"
#include "fm4.h"
#include "formats/patch_pb.h"

void fm4Bench() {
    using namespace ot;
    using namespace ot::audio;
    static SynthParams p;
    int count = 0;
    const PatchParamRange* t = patchEngineParams(fm4::kEngineId, &count);
    if (!t) return;
    p.engine = fm4::kEngineId;
    p.count = count;
    for (int i = 0; i < count; ++i) p.start[i] = p.end[i] = t[i].def;
    p.end[fm4::kAlgorithm] = p.start[fm4::kAlgorithm] = 0.0f;
    p.end[fm4::kFeedback] = p.start[fm4::kFeedback] = 5.0f;
    p.end[fm4::kVibrato] = p.start[fm4::kVibrato] = 0.5f;
    p.end[fm4::kTremolo] = p.start[fm4::kTremolo] = 0.5f;
    for (int k = 0; k < 4; ++k) {
        p.end[fm4::op(k, fm4::kLevel)] = p.start[fm4::op(k, fm4::kLevel)] = 0.8f;
        p.end[fm4::op(k, fm4::kSustain)] = p.start[fm4::op(k, fm4::kSustain)] = 0.7f;
        p.end[fm4::op(k, fm4::kAm)] = p.start[fm4::op(k, fm4::kAm)] = 1.0f;
    }
    p.serial = 1;
    ISynthEngine* e = fm4::engine();
    static SynthSlotScratch sc;
    static SynthVoice v;
    e->slotReset(sc);
    v = SynthVoice{};
    v.slotScratch = &sc;
    v.gate = true;
    SynthNote n;
    n.pitch = 60.0f;
    e->noteOn(v, p, n);
    static float l[kBlock], r[kBlock];
    const int blocks = 800 / otbench::scale();
    const double t0 = otbench::nowUnits();
    for (int b = 0; b < blocks; ++b) {
        e->prepare(sc, p);
        e->render(v, p, 0.5f, 0.0f, l, r);
    }
    std::printf("    fm4.unit %9.1f  (one voice, 4 operators, feedback, LFO, limit 22000 M4 insn)\n",
                (otbench::nowUnits() - t0) / blocks);
}
