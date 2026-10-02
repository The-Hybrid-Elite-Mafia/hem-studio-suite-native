#pragma once
#include "Editors.h"
#include "FinishingProcessors.h"
namespace hem {
class VocalEditor final : public ModuleEditor {
public: explicit VocalEditor(VocalProcessor& p) : ModuleEditor(p) {}
};
class TubeEditor final : public ModuleEditor {
public: explicit TubeEditor(TubeProcessor& p) : ModuleEditor(p) {}
};
class TapeEditor final : public ModuleEditor {
public: explicit TapeEditor(TapeProcessor& p) : ModuleEditor(p) {}
};
class MonitorEditor final : public ModuleEditor {
public: explicit MonitorEditor(MonitorProcessor& p) : ModuleEditor(p) {}
};
}
