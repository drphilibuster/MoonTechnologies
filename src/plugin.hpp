#pragma once
#include <rack.hpp>


using namespace rack;

// Declared in plugin.cpp.
extern Plugin* pluginInstance;

// One Model per module. Each is defined in src/<Module>/<Module>.cpp.
extern Model* modelAmortization;
extern Model* modelAuditLogic;
extern Model* modelConsolidation;
extern Model* modelDeduction;
extern Model* modelDiversified;
extern Model* modelDividend;
extern Model* modelGarnishment;
extern Model* modelGross;
extern Model* modelInstallment;
extern Model* modelKickback;
extern Model* modelPatchAudit;
extern Model* modelPaymentSchedule;
extern Model* modelRacketeer;
extern Model* modelRepossession;
extern Model* modelScheduleA;
extern Model* modelRetroactive;
extern Model* modelSignHere;
extern Model* modelSixFigures;
extern Model* modelTaxBracket;
extern Model* modelUncertaintyPolicy;
extern Model* modelVolatility;
