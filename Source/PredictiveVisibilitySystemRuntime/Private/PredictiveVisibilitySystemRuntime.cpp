#include "PredictiveVisibilitySystemRuntime.h"

#include "Modules/ModuleManager.h"
#include "PredictiveVisibilityLog.h"

DEFINE_LOG_CATEGORY(LogPredictiveVisibility);

void FPredictiveVisibilitySystemRuntimeModule::StartupModule()
{
}

void FPredictiveVisibilitySystemRuntimeModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FPredictiveVisibilitySystemRuntimeModule, PredictiveVisibilitySystemRuntime)
