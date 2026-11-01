// Arc-Type synth module (ARC1): a module engine with its own id (0x89), in
// the Synth page model list after the Tracker Mini models and the module
// engines before it in the profile (modules/module.h). No static buffers.
#include "arc.h"
#include "modules/module.h"
#include "modules/synth_descriptor.h"

namespace ot {
namespace mod {

extern const SynthDescriptor kArcDescriptor;   // arc_desc.cpp

namespace {

const ModuleInfo kInfo = {"ARC1", kModuleApi, 100, "Arc-Type synth", kCapSynthEngine | kCapSynthDescriptor, 0, 0, 0, 26000, 8};

class Module final : public IModule {
public:
    const ModuleInfo& info() const override { return kInfo; }
    bool attach(ModuleHost&) override { return true; }
    audio::ISynthEngine* synthEngine(int32_t engineId) override {
        return engineId == audio::arc::kEngineId ? audio::arc::engine() : nullptr;
    }
    const SynthDescriptor* synthDescriptor(int i) const override { return i == 0 ? &kArcDescriptor : nullptr; }
};

Module gModule;

}  // namespace

IModule& arcModule() { return gModule; }

}  // namespace mod
}  // namespace ot
