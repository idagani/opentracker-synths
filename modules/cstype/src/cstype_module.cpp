// CS-Type synth module (CST1): a module engine with its own id (0x83), in the
// Synth page model list after the Tracker Mini models and the module engines
// before it in the profile (modules/module.h). No static buffers.
#include "cstype.h"
#include "modules/module.h"
#include "modules/synth_descriptor.h"

namespace ot {
namespace mod {

extern const SynthDescriptor kCstypeDescriptor;   // cstype_desc.cpp

namespace {

const ModuleInfo kInfo = {"CST1", kModuleApi, 100, "CS-Type synth", kCapSynthEngine | kCapSynthDescriptor, 0, 0, 0, 36000, 8};

class Module final : public IModule {
public:
    const ModuleInfo& info() const override { return kInfo; }
    bool attach(ModuleHost&) override { return true; }
    audio::ISynthEngine* synthEngine(int32_t engineId) override {
        return engineId == audio::cstype::kEngineId ? audio::cstype::engine() : nullptr;
    }
    const SynthDescriptor* synthDescriptor(int i) const override { return i == 0 ? &kCstypeDescriptor : nullptr; }
};

Module gModule;

}  // namespace

IModule& cstypeModule() { return gModule; }

}  // namespace mod
}  // namespace ot
