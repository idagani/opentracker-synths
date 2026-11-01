// T-Type drum synth module (TTY1): a module engine with its own id (0x85), in
// the Synth page model list after the Tracker Mini models and the module
// engines before it in the profile (modules/module.h). No static buffers.
#include "ttype.h"
#include "modules/module.h"
#include "modules/synth_descriptor.h"

namespace ot {
namespace mod {

extern const SynthDescriptor kTtypeDescriptor;   // ttype_desc.cpp

namespace {

const ModuleInfo kInfo = {"TTY1", kModuleApi, 100, "T-Type drums", kCapSynthEngine | kCapSynthDescriptor, 0, 0, 0, 30000, 3};

class Module final : public IModule {
public:
    const ModuleInfo& info() const override { return kInfo; }
    bool attach(ModuleHost&) override { return true; }
    audio::ISynthEngine* synthEngine(int32_t engineId) override {
        return engineId == audio::ttype::kEngineId ? audio::ttype::engine() : nullptr;
    }
    const SynthDescriptor* synthDescriptor(int i) const override { return i == 0 ? &kTtypeDescriptor : nullptr; }
};

Module gModule;

}  // namespace

IModule& ttypeModule() { return gModule; }

}  // namespace mod
}  // namespace ot
