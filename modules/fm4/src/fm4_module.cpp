// FM4 synth module (FM4S): a module engine with its own id (0x80), in the
// Synth page model list after the Tracker Mini models (modules/module.h).
#include "fm4.h"
#include "modules/module.h"
#include "modules/synth_descriptor.h"

namespace ot {
namespace mod {

extern const SynthDescriptor kFm4Descriptor;   // fm4_desc.cpp

namespace {

const ModuleInfo kInfo = {"FM4S", kModuleApi, 100, "FM4 synth", kCapSynthEngine | kCapSynthDescriptor, 0, 0, 0, 22000, 8};

class Module final : public IModule {
public:
    const ModuleInfo& info() const override { return kInfo; }
    bool attach(ModuleHost&) override { return true; }
    audio::ISynthEngine* synthEngine(int32_t engineId) override {
        return engineId == audio::fm4::kEngineId ? audio::fm4::engine() : nullptr;
    }
    const SynthDescriptor* synthDescriptor(int i) const override { return i == 0 ? &kFm4Descriptor : nullptr; }
};

Module gModule;

}  // namespace

IModule& fm4Module() { return gModule; }

}  // namespace mod
}  // namespace ot
