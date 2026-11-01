// A-Type synth module (ATY1): a module engine with its own id (0x84), in the
// Synth page model list after the Tracker Mini models and the module engines
// before it in the profile (modules/module.h). No static buffers.
#include "atype.h"
#include "modules/module.h"
#include "modules/synth_descriptor.h"

namespace ot {
namespace mod {

extern const SynthDescriptor kAtypeDescriptor;   // atype_desc.cpp

namespace {

const ModuleInfo kInfo = {"ATY1", kModuleApi, 100, "A-Type synth", kCapSynthEngine | kCapSynthDescriptor, 0, 0, 0, 24000, 8};

class Module final : public IModule {
public:
    const ModuleInfo& info() const override { return kInfo; }
    bool attach(ModuleHost&) override { return true; }
    audio::ISynthEngine* synthEngine(int32_t engineId) override {
        return engineId == audio::atype::kEngineId ? audio::atype::engine() : nullptr;
    }
    const SynthDescriptor* synthDescriptor(int i) const override { return i == 0 ? &kAtypeDescriptor : nullptr; }
};

Module gModule;

}  // namespace

IModule& atypeModule() { return gModule; }

}  // namespace mod
}  // namespace ot
