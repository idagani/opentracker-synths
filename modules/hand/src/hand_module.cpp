// Hand-Type synth module (HND1): a module engine with its own id (0x87), in
// the Synth page model list after the Tracker Mini models and the module
// engines before it in the profile (modules/module.h). No static buffers.
#include "hand.h"
#include "modules/module.h"
#include "modules/synth_descriptor.h"

namespace ot {
namespace mod {

extern const SynthDescriptor kHandDescriptor;   // hand_desc.cpp

namespace {

const ModuleInfo kInfo = {"HND1", kModuleApi, 100, "Hand-Type synth", kCapSynthEngine | kCapSynthDescriptor, 0, 0, 0, 12000, 8};

class Module final : public IModule {
public:
    const ModuleInfo& info() const override { return kInfo; }
    bool attach(ModuleHost&) override { return true; }
    audio::ISynthEngine* synthEngine(int32_t engineId) override {
        return engineId == audio::hand::kEngineId ? audio::hand::engine() : nullptr;
    }
    const SynthDescriptor* synthDescriptor(int i) const override { return i == 0 ? &kHandDescriptor : nullptr; }
};

Module gModule;

}  // namespace

IModule& handModule() { return gModule; }

}  // namespace mod
}  // namespace ot
