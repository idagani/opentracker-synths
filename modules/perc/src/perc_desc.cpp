// PERC synth descriptor (synth_descriptor.h, docs/PLAN_3X_M2.md M-A): the stored
// parameter table (spec 21 §8.6 with the MINI_PLAN corrections) and the Edit Patch
// labels, value formats, enum names and sections (Tracker Mini manual), moved
// from the OpenTracker core.
#include "modules/synth_descriptor.h"

namespace ot {
namespace mod {

namespace {

using namespace ot::synthui;
using namespace ot::synthui::names;

// --- stored ranges -----------------------------------------------------------
#define MIXER(level, pan, delay) {level, 0, 2}, {pan, -1, 1}, {delay, 0, 1}, {0, 0, 1}, {0, 0, 2}
const PatchParamRange kParams[84] = {
    // Kick 0-8
    {0, 0, 175}, {31, 12, 119}, {1.8f, 0, 100}, {0.26f, 0, 10}, {0.32f, -1, 1}, {0.5f, 0, 1},
    {0.13f, 0, 10}, {0.2f, -1, 1}, {2, 0, 2},
    // Tom 9-17
    {0, 0, 175}, {1.4f, 0, 100}, {0.41f, 0, 10}, {0.58f, 0, 1}, {1, 0, 2}, {0.05f, 0, 10},
    {1.65f, -10, 10}, {0.4f, -1, 1}, {1, 0, 2},
    // Snare 18-26
    {0, 0, 175}, {50, 12, 119}, {0.5f, 0, 1}, {1, 0, 4}, {0.17f, 0, 10}, {0.8f, 0, 100},
    {0.26f, 0, 10}, {-0.39f, -1, 1}, {0.13f, 0, 1},
    // Hi-hat 27-35
    {0.12f, 0, 1}, {0.16f, 0, 10}, {0.81f, 0, 10}, {5080, 10, 20000}, {9040, 10, 20000},
    {209, 100, 880}, {-0.09f, -1, 1}, {0, 0, 24}, {0, 0, 1},
    // Cymbal 36-44
    {3000, 500, 10000}, {7000, 500, 10000}, {1.5f, 0, 10}, {0.25f, 0, 10}, {204, 80, 880},
    {-0.39f, -1, 1}, {0.29f, -1, 1}, {0, 0, 1}, {0, 0, 24},
    // Perc 45-48
    {0, 0, 4}, {0.07f, 0, 1}, {0.62f, 0, 1}, {0.14f, 0, 2},
    // Mixer 49-78: Kick, Tom, Snare, Hihat, Cymbal, Perc
    MIXER(1, 0, 0), MIXER(0.8f, 0.2f, 0), MIXER(1, -0.1f, 0.5f), MIXER(1, 0.3f, 0.5f),
    MIXER(1, -0.3f, 0.5f), MIXER(1, 0.1f, 0.5f),
    // FX 79-83
    {0, 0, 1}, {0, 0, 4}, {0, -18, 18}, {0, -18, 18}, {0, -18, 18},
};
#undef MIXER

// --- enum names --------------------------------------------------------------
const char* const kKickModel[3] = {"Classic8", "Synced8", "Classic9"};
const char* const kTomModel[3] = {"Classic", "Classic+", "Resonant"};
const char* const kSnareModel[5] = {"Classic8", "Classic9", "Classic9+", "Modern", "Modern+"};
const char* const kTwoModel[2] = {"Classic", "FM"};
const char* const kPercModel[5] = {"Clap", "Clap+", "Maracas", "Shaker", "Cowbell"};
const char* const kOutput[3] = {"Main", "Post Overdrive", "Post EQ"};
const char* const kShaper[5] = {"Saturate", "Overdrive", "Overdrive+", "Bitcrush", "Bitcrush+"};
const char* const kBodyWave[16] = {"Sat-Sin", "Triangle", "Square", "Square FM", "FM1",    "FM3",
                                   "FM7",     "FM11",     "tzFM1",  "tzFM3",     "tzFM7",  "tzFM11",
                                   "Glitch 1", "Glitch 2", "Glitch 3", "Glitch 4"};

#define E(n) OT_SYNTH_ENUM(n)
#define F(f) f, nullptr, 0

// --- Edit Patch --------------------------------------------------------------
#define MIXROW(d) {d " Level", F(kFmtPct)}, {d " Pan", F(kFmtPan)}, {d " Delay", F(kFmtVal100)}, \
                  {d " Reverb", F(kFmtVal100)}, {d " Output", E(kOutput)}
const ParamUi kUi[84] = {
    // Kick 0..8
    {"Body Wave", kFmtBodyWave, kBodyWave, 16}, {"Pitch", F(kFmtPercNote)},    {"Body Attack", F(kFmtMs10)},
    {"Body Decay", F(kFmtSec)},     {"Trans Timbre", F(kFmtVal100)}, {"Trans Level", F(kFmtPct)},
    {"Mod Decay", F(kFmtSec)},      {"Mod Amount", F(kFmtVal100)}, {"Model", E(kKickModel)},
    // Tom 9..17: 12 holds the transient level (0..1, 0-100 %), 13 the
    // transient timbre (0..2, 0-200): the manual's ranges (p.166) match the
    // stored ranges that way round.
    {"Body Wave", kFmtBodyWave, kBodyWave, 16}, {"Body Attack", F(kFmtMs10)},  {"Body Decay", F(kFmtSec)},
    {"Trans Level", F(kFmtPct)},    {"Trans Timbre", F(kFmtVal100)}, {"Mod Decay", F(kFmtSec)},
    {"Pitch Mod", F(kFmtVal100)},   {"Wave Mod", F(kFmtVal100)},   {"Model", E(kTomModel)},
    // Snare 18..26
    {"Body Wave", kFmtBodyWave, kBodyWave, 16}, {"Pitch", F(kFmtPercNote)},    {"Tone", F(kFmtPct)},
    {"Model", E(kSnareModel)},      {"Body Decay", F(kFmtSec)},    {"Snap Attack", F(kFmtMs10)},
    {"Snap Decay", F(kFmtSec)},     {"Body/Snap Mix", F(kFmtVal100)}, {"Snap Timbre", F(kFmtPct)},
    // Hi-hat 27..35
    {"Decay Shape", F(kFmtPct)},    {"Closed Decay", F(kFmtSec)},  {"Open Decay", F(kFmtSec)},
    {"HPF Cutoff", F(kFmtHz)},      {"LPF Cutoff", F(kFmtHz)},     {"Tone Pitch", F(kFmtHz)},
    {"Tone/Noise Mix", F(kFmtVal100)}, {"Timbre", F(kFmtInt)},     {"Model", E(kTwoModel)},
    // Cymbal 36..44
    {"Body Filter", F(kFmtHz)},     {"Impact Filter", F(kFmtHz)},  {"Body Decay", F(kFmtSec)},
    {"Impact Decay", F(kFmtSec)},   {"Tone Pitch", F(kFmtHz)},     {"Tone/Noise Mix", F(kFmtVal100)},
    {"Impact/Body Mix", F(kFmtVal100)}, {"Model", E(kTwoModel)},   {"Timbre", F(kFmtInt)},
    // Perc 45..48
    {"Model", E(kPercModel)},       {"Variation", F(kFmtVal100)},  {"Timbre", F(kFmtVal100)},
    {"Decay", F(kFmtSec)},
    // Mixer 49..78
    MIXROW("Kick"), MIXROW("Tom"), MIXROW("Snare"), MIXROW("Hi Hat"), MIXROW("Cymbal"), MIXROW("Perc"),
    // FX 79..83
    {"Waveshaper Level", F(kFmtVal100)}, {"Waveshaper Type", E(kShaper)}, {"EQ Low 80Hz", F(kFmtDb)},
    {"EQ Mid 2.5kHz", F(kFmtDb)},   {"EQ High 12kHz", F(kFmtDb)},
};
#undef MIXROW
const uint8_t kPercKick[] = {8, 1, 0, 2, 3, 6, 7, 4, 5};
const uint8_t kPercTom[] = {17, 9, 10, 11, 14, 15, 16, 13, 12};
const uint8_t kPercSnare[] = {21, 19, 20, 18, 22, 23, 24, 26, 25};
const uint8_t kPercHat[] = {35, 34, 28, 29, 27, 30, 31, 32, 33};
const uint8_t kPercCym[] = {43, 44, 36, 37, 38, 39, 40, 41, 42};
const uint8_t kPercPerc[] = {45, 46, 47, 48};
const uint8_t kPercMix[] = {49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
                            64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78};
const uint8_t kPercFx[] = {80, 79, 81, 82, 83};   // Type, Level (manual p.169)
#undef E
#undef F

const Section kSections[] = {OT_SYNTH_SECTION("Kick", "Kick", kPercKick),     OT_SYNTH_SECTION("Tom", "Toms", kPercTom),
                            OT_SYNTH_SECTION("Snare", "Snare", kPercSnare),  OT_SYNTH_SECTION("Hi Hat", "Hi Hat", kPercHat),
                            OT_SYNTH_SECTION("Cymbal", "Cymbal", kPercCym),  OT_SYNTH_SECTION("Perc", "Percussion", kPercPerc),
                            OT_SYNTH_SECTION("Mixer", "Mixer", kPercMix),    OT_SYNTH_SECTION("FX", "FX", kPercFx), OT_SYNTH_MACRO_SECTIONS};

}  // namespace

extern const SynthDescriptor kPercDescriptor;
const SynthDescriptor kPercDescriptor = {kSynthEnginePerc, "PERC", kSynthFlagKitState, -1, 84, kParams, kUi, kSections,
                                     uint8_t(sizeof(kSections) / sizeof(kSections[0])), 0, nullptr};

}  // namespace mod
}  // namespace ot
