#pragma once
#include "Editors.h"
#include "SpecialtyProcessors.h"
namespace hem {
class DhtaEditor final : public ModuleEditor {
public: explicit DhtaEditor(DhtaProcessor& p) : ModuleEditor(p) {}
};

class SubBassEditor final : public ModuleEditor {
public: explicit SubBassEditor(SubBassProcessor& p) : ModuleEditor(p) {}
};
class RestorerEditor final : public ModuleEditor {
public: explicit RestorerEditor(RestorerProcessor& p) : ModuleEditor(p) {}
};
}
