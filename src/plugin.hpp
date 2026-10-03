#pragma once
#include <rack.hpp>


using namespace rack;

// Declared in plugin.cpp.
extern Plugin* pluginInstance;

// One Model per module. Each is defined in src/<Module>/<Module>.cpp.
extern Model* modelAccrual;
extern Model* modelAmortization;
extern Model* modelApportionment;
extern Model* modelAuditLogic;
extern Model* modelCalculation;
extern Model* modelCollusion;
extern Model* modelConsolidation;
extern Model* modelBailout;
extern Model* modelObfuscation;
extern Model* modelTransmittal;
extern Model* modelProjection;
extern Model* modelDeduction;
extern Model* modelContagion;
extern Model* modelDependents;
extern Model* modelDepreciation;
extern Model* modelDiversified;
extern Model* modelDividend;
extern Model* modelGarnishment;
extern Model* modelGross;
extern Model* modelInstallment;
extern Model* modelKickback;
extern Model* modelLedger;
extern Model* modelToll;
extern Model* modelNordicBanking;
extern Model* modelPatchAudit;
extern Model* modelPaymentSchedule;
extern Model* modelRacketeer;
extern Model* modelReconciliation;
extern Model* modelRepossession;
extern Model* modelScheduleA;
extern Model* modelRebate;
extern Model* modelRetroactive;
extern Model* modelSignHere;
extern Model* modelSixFigures;
extern Model* modelTaxBracket;
extern Model* modelUncertaintyPolicy;
extern Model* modelVolatility;
