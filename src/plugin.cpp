#include "plugin.hpp"
#include "PatchAudit/ps/JobRunner.hpp"


Plugin* pluginInstance;


void init(Plugin* p) {
	pluginInstance = p;

	// The order here is the order Rack lists the brand's modules in, so keep it
	// alphabetical rather than in the order they were written.
	p->addModel(modelAmortization);
	p->addModel(modelAuditLogic);
	p->addModel(modelCollusion);
	p->addModel(modelConsolidation);
	p->addModel(modelBailout);
	p->addModel(modelTransmittal);
	p->addModel(modelDeduction);
	p->addModel(modelDependents);
	p->addModel(modelDiversified);
	p->addModel(modelDividend);
	p->addModel(modelGarnishment);
	p->addModel(modelGross);
	p->addModel(modelInstallment);
	p->addModel(modelKickback);
	p->addModel(modelToll);
	p->addModel(modelPatchAudit);
	p->addModel(modelPaymentSchedule);
	p->addModel(modelRacketeer);
	p->addModel(modelReconciliation);
	p->addModel(modelRepossession);
	p->addModel(modelScheduleA);
	p->addModel(modelRetroactive);
	p->addModel(modelSignHere);
	p->addModel(modelSixFigures);
	p->addModel(modelTaxBracket);
	p->addModel(modelUncertaintyPolicy);
	p->addModel(modelVolatility);
}


/** Rack looks this symbol up and calls it before dlclose() -- see
    Rack/src/plugin.cpp:317-333 (destroyPlugin). Joining PatchAudit's worker
    threads here is what lets it use joinable threads instead of detached ones:
    nothing from this library can still be executing when it is unmapped.

    It lives in the shared entry point rather than in PatchAudit because Rack
    calls destroy() once per *plugin*, and the three modules now ship as one. */
extern "C" void destroy() {
	ps::JobRunner::global().shutdown();
}
