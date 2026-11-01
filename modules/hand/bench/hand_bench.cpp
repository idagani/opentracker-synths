// Hand-Type bench (core tests/bench_api.h): one voice per block incl. prepare.
// hand.unit: the costliest channel: twin pulse (pulse 2 detuned, both duties
// ramping), the sweep up, the arp at 120 Hz (Dom7) with the stepped vibrato,
// glide, a 4-bit DAC, at note 84. hand.unit.wave / hand.unit.noise: the Wave
// channel, the Noise channel at its fastest clock (8 shifts a frame),
// informational. hand.unit.drums: a kit of five hits in one block (five
// voices, per voice), informational.
#include <cstdio>

#include "audio/synth_engine.h"
#include "bench_api.h"
#include "formats/patch_pb.h"
#include "hand.h"

namespace {

double runBlocks(ot::audio::SynthParams& p, int blocks, const int* notes, int count) {
    using namespace ot;
    using namespace ot::audio;
    ISynthEngine* e = hand::engine();
    static SynthSlotScratch sc;
    static SynthVoice v[5];
    static float l[kBlock], r[kBlock];
    e->slotReset(sc);
    e->setTempo(120.0f);
    double t = 0.0;
    for (int b = 0; b < blocks; ++b) {
        if (b % 60 == 0) {
            for (int k = 0; k < count; ++k) {
                v[k] = SynthVoice{};
                v[k].slotScratch = &sc;
                v[k].gate = true;
                SynthNote n;
                n.pitch = float(notes[k]);
                n.note = uint8_t(notes[k]);
                e->noteOn(v[k], p, n);
            }
        }
        const double t0 = otbench::nowUnits();
        e->prepare(sc, p);
        for (int k = 0; k < count; ++k) e->render(v[k], p, 0.5f, 0.0f, l, r);
        t += otbench::nowUnits() - t0;
    }
    return t / blocks / count;
}

}  // namespace

void handBench() {
    using namespace ot;
    using namespace ot::audio;
    static SynthParams p;
    int count = 0;
    const PatchParamRange* t = patchEngineParams(hand::kEngineId, &count);
    if (!t) return;
    p.engine = hand::kEngineId;
    p.count = count;
    for (int i = 0; i < count; ++i) p.start[i] = p.end[i] = t[i].def;
    p.serial = 1;
    auto set = [](int i, float v) { p.start[i] = p.end[i] = v; };
    const int blocks = 800 / otbench::scale();
    const int lead[1] = {84};
    set(hand::kEnvDir, float(hand::kUp));
    set(hand::kP2Level, 0.7f);
    set(hand::kP2Detune, 12.0f);
    set(hand::kSweepDir, float(hand::kSweepUp));
    set(hand::kSweepShift, 7.0f);
    set(hand::kSweepTime, 7.0f);
    set(hand::kSweepCut, 0.0f);
    set(hand::kArpMode, 5.0f);   // Dom7
    set(hand::kArpRate, 3.0f);   // 120 Hz
    set(hand::kVibDepth, 0.5f);
    set(hand::kVibDelay, 0.0f);
    set(hand::kGlideTime, 0.5f);
    ++p.serial;
    // Duties ramping every block (the worst case of the pulse pass).
    double worst = 0.0;
    {
        using namespace ot;
        ISynthEngine* e = hand::engine();
        static SynthSlotScratch sc;
        static SynthVoice v;
        static float l[kBlock], r[kBlock];
        e->slotReset(sc);
        e->setTempo(120.0f);
        v = SynthVoice{};
        v.slotScratch = &sc;
        v.gate = true;
        SynthNote n;
        n.pitch = 60.0f;
        e->noteOn(v, p, n);
        n.pitch = 84.0f;
        n.reused = n.overlap = true;
        e->noteOn(v, p, n);
        const double t0 = otbench::nowUnits();
        for (int b = 0; b < blocks; ++b) {
            set(hand::kDuty, float(b & 3));
            set(hand::kP2Duty, float((b + 1) & 3));
            ++p.serial;
            p.moving = true;
            e->prepare(sc, p);
            e->render(v, p, 0.5f, 0.0f, l, r);
        }
        worst = (otbench::nowUnits() - t0) / blocks;
        p.moving = false;
    }
    set(hand::kChannel, float(hand::kWaveCh));
    ++p.serial;
    const double wave = runBlocks(p, blocks, lead, 1);
    set(hand::kChannel, float(hand::kNoise));
    set(hand::kNoiseShift, 0.0f);
    ++p.serial;
    const double noise = runBlocks(p, blocks, lead, 1);
    set(hand::kTrack, 1.0f);
    ++p.serial;
    const double track = runBlocks(p, blocks, lead, 1);
    set(hand::kTrack, 0.0f);
    set(hand::kChannel, float(hand::kDrums));
    ++p.serial;
    const int kit[5] = {60, 62, 64, 69, 52};
    const double drums = runBlocks(p, blocks, kit, 5);
    std::printf("    hand.unit %9.1f  (one voice, twin pulse, sweep, arp 120 Hz, vibrato, glide, 4-bit DAC, limit 12000 M4 insn)\n", worst);
    std::printf("    hand.unit.wave %9.1f  (one voice, the Wave channel with the tricks)\n", wave);
    std::printf("    hand.unit.noise %9.1f  (one voice, the Noise channel at 8 shifts a frame)\n", noise);
    std::printf("    hand.unit.drums %9.1f  (per voice, five drum hits)\n", drums);
    std::printf("    hand.unit.track %9.1f  (one voice, the Noise channel following the note, the arp)\n", track);
}
