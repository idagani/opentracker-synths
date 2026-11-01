// WTFM synth module (WTF1): registers the engine with the module host (modules/module.h).
#include "wtfm.h"
#include "modules/module.h"
#include "modules/synth_descriptor.h"

namespace ot {
namespace mod {

extern const SynthDescriptor kWtfmDescriptor;   // wtfm_desc.cpp

namespace {

const ModuleInfo kInfo = {"WTF1", kModuleApi, 100, "WTFM synth", kCapSynthEngine | kCapSynthDescriptor, 0, 0, 0, 32000, 8};

class Module final : public IModule {
public:
    const ModuleInfo& info() const override { return kInfo; }
    bool attach(ModuleHost&) override { return true; }
    audio::ISynthEngine* synthEngine(int32_t engineId) override {
        return engineId == kSynthEngineWtfm ? audio::wtfm::engine() : nullptr;
    }
    const SynthDescriptor* synthDescriptor(int i) const override { return i == 0 ? &kWtfmDescriptor : nullptr; }
};

Module gModule;   // vptr-only constructor: constant-initialised

}  // namespace

IModule& wtfmModule() { return gModule; }

}  // namespace mod
}  // namespace ot
