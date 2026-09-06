#pragma once
#include <rack.hpp>


using namespace rack;

// Declared in plugin.cpp.
extern Plugin* pluginInstance;

// One Model per module. Each is defined in src/<Module>/<Module>.cpp.
extern Model* modelAmortization;
extern Model* modelDeduction;
extern Model* modelDividend;
extern Model* modelGross;
extern Model* modelKickback;
extern Model* modelPatchAudit;
extern Model* modelRacketeer;
extern Model* modelRepossession;
extern Model* modelRetroactive;
extern Model* modelSixFigures;
extern Model* modelTaxBracket;
extern Model* modelUncertaintyPolicy;
