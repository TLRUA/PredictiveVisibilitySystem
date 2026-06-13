#pragma once

#include "CoreMinimal.h"
#include "Async/TaskGraphInterfaces.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "PSOPrecacheFwd.h"
#include "PredictiveVisibilityPSOPrecacheComponent.generated.h"

UCLASS(Transient)
class UPredictiveVisibilityStaticMeshPSOPrecacheComponent final : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	virtual void PrecachePSOs() override {}
	int32 SubmitCollectedPSOPrecache(uint64 BasePSOPrecacheParamsData, TArray<FMaterialPSOPrecacheRequestID>& OutRequestIDs, FGraphEventArray& OutGraphEvents);
};

UCLASS(Transient)
class UPredictiveVisibilityInstancedStaticMeshPSOPrecacheComponent final : public UInstancedStaticMeshComponent
{
	GENERATED_BODY()

public:
	virtual void PrecachePSOs() override {}
	int32 SubmitCollectedPSOPrecache(uint64 BasePSOPrecacheParamsData, TArray<FMaterialPSOPrecacheRequestID>& OutRequestIDs, FGraphEventArray& OutGraphEvents);
};

UCLASS(Transient)
class UPredictiveVisibilityHierarchicalInstancedStaticMeshPSOPrecacheComponent final : public UHierarchicalInstancedStaticMeshComponent
{
	GENERATED_BODY()

public:
	virtual void PrecachePSOs() override {}
	int32 SubmitCollectedPSOPrecache(uint64 BasePSOPrecacheParamsData, TArray<FMaterialPSOPrecacheRequestID>& OutRequestIDs, FGraphEventArray& OutGraphEvents);
};
