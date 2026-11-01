// D-Type synth module (DTY1): a module engine with its own id (0x82), in the
// Synth page model list after the Tracker Mini models and JU6
// (modules/module.h). No static buffers.
#include "dtype.h"
#include "modules/module.h"
#include "modules/synth_descriptor.h"

namespace ot {
namespace mod {

extern const SynthDescriptor kDtypeDescriptor;   // dtype_desc.cpp

namespace {

const ModuleInfo kInfo = {"DTY1", kModuleApi, 100, "D-Type synth", kCapSynthEngine | kCapSynthDescriptor, 0, 0, 0, 26000, 8};

class Module final : public IModule {
public:
    const ModuleInfo& info() const override { return kInfo; }
    bool attach(ModuleHost&) override { return true; }
    audio::ISynthEngine* synthEngine(int32_t engineId) override {
        return engineId == audio::dtype::kEngineId ? audio::dtype::engine() : nullptr;
    }
    const SynthDescriptor* synthDescriptor(int i) const override { return i == 0 ? &kDtypeDescriptor : nullptr; }
};

Module gModule;

}  // namespace

IModule& dtypeModule() { return gModule; }

}  // namespace mod
}  // namespace ot
