// PERC synth module (PRC1): registers the engine with the module host (modules/module.h).
#include "perc.h"
#include "modules/module.h"
#include "modules/synth_descriptor.h"

namespace ot {
namespace mod {

extern const SynthDescriptor kPercDescriptor;   // perc_desc.cpp

namespace {

const ModuleInfo kInfo = {"PRC1", kModuleApi, 100, "PERC synth", kCapSynthEngine | kCapSynthDescriptor, 0, 0, 896, 30000, 3};

class Module final : public IModule {
public:
    const ModuleInfo& info() const override { return kInfo; }
    bool attach(ModuleHost&) override { return true; }
    audio::ISynthEngine* synthEngine(int32_t engineId) override {
        return engineId == kSynthEnginePerc ? audio::percEngine() : nullptr;
    }
    const SynthDescriptor* synthDescriptor(int i) const override { return i == 0 ? &kPercDescriptor : nullptr; }
};

Module gModule;   // vptr-only constructor: constant-initialised

}  // namespace

IModule& percModule() { return gModule; }

}  // namespace mod
}  // namespace ot
