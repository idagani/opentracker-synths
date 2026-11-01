// B-Type synth module (BTY1): a module engine with its own id (0x88), in
// the Synth page model list after the Tracker Mini models and the module
// engines before it in the profile (modules/module.h). No static buffers.
#include "btype.h"
#include "modules/module.h"
#include "modules/synth_descriptor.h"

namespace ot {
namespace mod {

extern const SynthDescriptor kBtypeDescriptor;   // btype_desc.cpp

namespace {

const ModuleInfo kInfo = {"BTY1", kModuleApi, 100, "B-Type synth", kCapSynthEngine | kCapSynthDescriptor, 0, 0, 0, 10500, 8};

class Module final : public IModule {
public:
    const ModuleInfo& info() const override { return kInfo; }
    bool attach(ModuleHost&) override { return true; }
    audio::ISynthEngine* synthEngine(int32_t engineId) override {
        return engineId == audio::btype::kEngineId ? audio::btype::engine() : nullptr;
    }
    const SynthDescriptor* synthDescriptor(int i) const override { return i == 0 ? &kBtypeDescriptor : nullptr; }
};

Module gModule;

}  // namespace

IModule& btypeModule() { return gModule; }

}  // namespace mod
}  // namespace ot
