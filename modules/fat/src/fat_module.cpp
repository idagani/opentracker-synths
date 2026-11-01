// FAT synth module (FAT1): registers the engine with the module host (modules/module.h).
#include "fat.h"
#include "modules/module.h"
#include "modules/synth_descriptor.h"

namespace ot {
namespace mod {

extern const SynthDescriptor kFatDescriptor;   // fat_desc.cpp

namespace {

const ModuleInfo kInfo = {"FAT1", kModuleApi, 100, "FAT synth", kCapSynthEngine | kCapSynthDescriptor, 0, 0, 1800, 22000, 8};

class Module final : public IModule {
public:
    const ModuleInfo& info() const override { return kInfo; }
    bool attach(ModuleHost&) override { return true; }
    audio::ISynthEngine* synthEngine(int32_t engineId) override {
        return engineId == kSynthEngineFat ? audio::fat::engine() : nullptr;
    }
    const SynthDescriptor* synthDescriptor(int i) const override { return i == 0 ? &kFatDescriptor : nullptr; }
};

Module gModule;   // vptr-only constructor: constant-initialised

}  // namespace

IModule& fatModule() { return gModule; }

}  // namespace mod
}  // namespace ot
