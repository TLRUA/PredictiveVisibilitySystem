#pragma once

#include "Modules/ModuleManager.h"

class FPredictiveVisibilitySystemRuntimeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
