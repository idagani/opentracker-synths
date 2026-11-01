// ACD synth module (ACD1): registers the engine with the module host (modules/module.h).
#include "acd.h"
#include "modules/module.h"
#include "modules/synth_descriptor.h"

namespace ot {
namespace mod {

extern const SynthDescriptor kAcdDescriptor;   // acd_desc.cpp

namespace {

const ModuleInfo kInfo = {"ACD1", kModuleApi, 100, "ACD synth", kCapSynthEngine | kCapSynthDescriptor, 0, 0, 3296, 18000, 8};

class Module final : public IModule {
public:
    const ModuleInfo& info() const override { return kInfo; }
    bool attach(ModuleHost&) override { return true; }
    audio::ISynthEngine* synthEngine(int32_t engineId) override {
        return engineId == kSynthEngineAcd ? audio::synthAcdEngine() : nullptr;
    }
    const SynthDescriptor* synthDescriptor(int i) const override { return i == 0 ? &kAcdDescriptor : nullptr; }
};

Module gModule;   // vptr-only constructor: constant-initialised

}  // namespace

IModule& acdModule() { return gModule; }

}  // namespace mod
}  // namespace ot
