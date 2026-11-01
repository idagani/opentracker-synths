// VAP synth module (VAP1): registers the engine with the module host (modules/module.h).
#include "vap.h"
#include "modules/module.h"
#include "modules/synth_descriptor.h"

namespace ot {
namespace mod {

extern const SynthDescriptor kVapDescriptor;   // vap_desc.cpp

namespace {

const ModuleInfo kInfo = {"VAP1", kModuleApi, 100, "VAP synth", kCapSynthEngine | kCapSynthDescriptor, 0, 0, 5284, 18000, 8};

class Module final : public IModule {
public:
    const ModuleInfo& info() const override { return kInfo; }
    bool attach(ModuleHost&) override { return true; }
    audio::ISynthEngine* synthEngine(int32_t engineId) override {
        return engineId == kSynthEngineVap ? audio::vap::engine() : nullptr;
    }
    const SynthDescriptor* synthDescriptor(int i) const override { return i == 0 ? &kVapDescriptor : nullptr; }
};

Module gModule;   // vptr-only constructor: constant-initialised

}  // namespace

IModule& vapModule() { return gModule; }

}  // namespace mod
}  // namespace ot
