// D-Type bench (core tests/bench_api.h): one voice per block incl. prepare.
// dtype.unit: the costliest settings: Narrow Pulse + Tri-Saw + Wide Pulse,
// pink noise, Feedback 100 %, Emphasis 90 %, VCO 3 on the keyboard at 2' as
// audio-rate FM into VCO 1 / 2 and the filter, both contours moving, a glide
// in progress, note 96. dtype.unit.init: the Init patch (informational).
#include <cstdio>

#include "audio/synth_engine.h"
#include "bench_api.h"
#include "dtype.h"
#include "formats/patch_pb.h"

namespace {

double runBlocks(ot::audio::SynthParams& p, int blocks, bool worst) {
    using namespace ot;
    using namespace ot::audio;
    ISynthEngine* e = dtype::engine();
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

void dtypeBench() {
    using namespace ot;
    using namespace ot::audio;
    static SynthParams p;
    int count = 0;
    const PatchParamRange* t = patchEngineParams(dtype::kEngineId, &count);
    if (!t) return;
    p.engine = dtype::kEngineId;
    p.count = count;
    for (int i = 0; i < count; ++i) p.start[i] = p.end[i] = t[i].def;
    p.serial = 1;
    const int blocks = 800 / otbench::scale();
    const double init = runBlocks(p, blocks, false);
    auto set = [](int i, float v) { p.start[i] = p.end[i] = v; };
    set(dtype::kVco1Wave, float(dtype::kNarrow));
    set(dtype::kVco2Wave, float(dtype::kTriSaw));
    set(dtype::kVco3Wave, float(dtype::kWide));
    set(dtype::kVco3Range, float(dtype::k2));
    set(dtype::kVco3Kb, 1.0f);
    set(dtype::kVco3Level, 0.5f);
    set(dtype::kVco2Tune, 7.0f);
    set(dtype::kNoiseLevel, 0.3f);
    set(dtype::kNoiseColour, float(dtype::kPink));
    set(dtype::kFeedback, 1.0f);
    set(dtype::kEmphasis, 0.9f);
    set(dtype::kContour, 0.6f);
    set(dtype::kKeyTrack, float(dtype::kKeyFull));
    set(dtype::kFAttack, 1.0f);
    set(dtype::kLAttack, 1.0f);
    set(dtype::kModMix, 0.3f);
    set(dtype::kOscMod, 0.6f);
    set(dtype::kFilterMod, 0.5f);
    set(dtype::kWheel, 1.0f);
    set(dtype::kGlideMode, float(dtype::kAlways));
    set(dtype::kGlideTime, 3.0f);
    ++p.serial;
    const double worst = runBlocks(p, blocks, true);
    std::printf("    dtype.unit %9.1f  (one voice, 3 VCOs (pulse, Tri-Saw, audio-rate FM), pink noise, feedback, ladder, glide, limit 26000 M4 insn)\n",
                worst);
    std::printf("    dtype.unit.init %9.1f  (one voice, the Init patch)\n", init);
}
