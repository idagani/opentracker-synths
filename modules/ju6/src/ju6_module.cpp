// JU6 synth module (JU6S): a module engine with its own id (0x81), in the
// Synth page model list after the Tracker Mini models (modules/module.h).
// Static SRAM: the chorus lines, 3 slots x 256 int16 frames (1536 B).
#include "ju6.h"
#include "modules/module.h"
#include "modules/synth_descriptor.h"

namespace ot {
namespace mod {

extern const SynthDescriptor kJu6Descriptor;   // ju6_desc.cpp

namespace {

const ModuleInfo kInfo = {"JU6S", kModuleApi, 100, "JU6 synth", kCapSynthEngine | kCapSynthDescriptor, 1536, 0, 0, 18000, 8};

class Module final : public IModule {
public:
    const ModuleInfo& info() const override { return kInfo; }
    bool attach(ModuleHost&) override { return true; }
    audio::ISynthEngine* synthEngine(int32_t engineId) override {
        return engineId == audio::ju6::kEngineId ? audio::ju6::engine() : nullptr;
    }
    const SynthDescriptor* synthDescriptor(int i) const override { return i == 0 ? &kJu6Descriptor : nullptr; }
};

Module gModule;

}  // namespace

IModule& ju6Module() { return gModule; }

}  // namespace mod
}  // namespace ot
