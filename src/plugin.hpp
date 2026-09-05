#pragma once
#include <rack.hpp>


using namespace rack;

// Declared in plugin.cpp.
extern Plugin* pluginInstance;

// One Model per module. Each is defined in src/<Module>/<Module>.cpp.
extern Model* modelPatchAudit;
extern Model* modelRetroactive;
extern Model* modelUncertaintyPolicy;
