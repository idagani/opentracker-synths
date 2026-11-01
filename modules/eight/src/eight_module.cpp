// 8-Type synth module (8TY1): a module engine with its own id (0x86), in
// the Synth page model list after the Tracker Mini models and the module
// engines before it in the profile (modules/module.h). No static buffers.
#include "eight.h"
#include "modules/module.h"
#include "modules/synth_descriptor.h"

namespace ot {
namespace mod {

extern const SynthDescriptor kEightDescriptor;   // eight_desc.cpp

namespace {

const ModuleInfo kInfo = {"8TY1", kModuleApi, 100, "8-Type synth", kCapSynthEngine | kCapSynthDescriptor, 0, 0, 0, 27000, 8};

class Module final : public IModule {
public:
    const ModuleInfo& info() const override { return kInfo; }
    bool attach(ModuleHost&) override { return true; }
    audio::ISynthEngine* synthEngine(int32_t engineId) override {
        return engineId == audio::eight::kEngineId ? audio::eight::engine() : nullptr;
    }
    const SynthDescriptor* synthDescriptor(int i) const override { return i == 0 ? &kEightDescriptor : nullptr; }
};

Module gModule;

}  // namespace

IModule& eightModule() { return gModule; }

}  // namespace mod
}  // namespace ot
