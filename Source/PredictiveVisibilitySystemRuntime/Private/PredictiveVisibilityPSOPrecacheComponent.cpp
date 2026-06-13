#include "PredictiveVisibilityPSOPrecacheComponent.h"

#include "PSOPrecache.h"
#include "PSOPrecacheMaterial.h"

namespace
{
	int32 SubmitCollectedPSOPrecacheParams(
		FMaterialInterfacePSOPrecacheParamsList& ParamsList,
		TArray<FMaterialPSOPrecacheRequestID>& OutRequestIDs,
		FGraphEventArray& OutGraphEvents)
	{
#if UE_WITH_PSO_PRECACHING
		if (ParamsList.IsEmpty())
		{
			return 0;
		}

		for (FMaterialInterfacePSOPrecacheParams& Params : ParamsList)
		{
			Params.Priority = EPSOPrecachePriority::Medium;
		}

		const int32 SubmittedMaterialParamCount = ParamsList.Num();
		PrecacheMaterialPSOs(ParamsList, OutRequestIDs, OutGraphEvents);
		return SubmittedMaterialParamCount;
#else
		return 0;
#endif
	}
}

int32 UPredictiveVisibilityStaticMeshPSOPrecacheComponent::SubmitCollectedPSOPrecache(
	uint64 BasePSOPrecacheParamsData,
	TArray<FMaterialPSOPrecacheRequestID>& OutRequestIDs,
	FGraphEventArray& OutGraphEvents)
{
#if UE_WITH_PSO_PRECACHING
	FPSOPrecacheParams BaseParams;
	BaseParams.Data = BasePSOPrecacheParamsData;

	FMaterialInterfacePSOPrecacheParamsList ParamsList;
	CollectPSOPrecacheData(BaseParams, ParamsList);
	return SubmitCollectedPSOPrecacheParams(ParamsList, OutRequestIDs, OutGraphEvents);
#else
	return 0;
#endif
}

int32 UPredictiveVisibilityInstancedStaticMeshPSOPrecacheComponent::SubmitCollectedPSOPrecache(
	uint64 BasePSOPrecacheParamsData,
	TArray<FMaterialPSOPrecacheRequestID>& OutRequestIDs,
	FGraphEventArray& OutGraphEvents)
{
#if UE_WITH_PSO_PRECACHING
	FPSOPrecacheParams BaseParams;
	BaseParams.Data = BasePSOPrecacheParamsData;

	FMaterialInterfacePSOPrecacheParamsList ParamsList;
	CollectPSOPrecacheData(BaseParams, ParamsList);
	return SubmitCollectedPSOPrecacheParams(ParamsList, OutRequestIDs, OutGraphEvents);
#else
	return 0;
#endif
}

int32 UPredictiveVisibilityHierarchicalInstancedStaticMeshPSOPrecacheComponent::SubmitCollectedPSOPrecache(
	uint64 BasePSOPrecacheParamsData,
	TArray<FMaterialPSOPrecacheRequestID>& OutRequestIDs,
	FGraphEventArray& OutGraphEvents)
{
#if UE_WITH_PSO_PRECACHING
	FPSOPrecacheParams BaseParams;
	BaseParams.Data = BasePSOPrecacheParamsData;

	FMaterialInterfacePSOPrecacheParamsList ParamsList;
	CollectPSOPrecacheData(BaseParams, ParamsList);
	return SubmitCollectedPSOPrecacheParams(ParamsList, OutRequestIDs, OutGraphEvents);
#else
	return 0;
#endif
}
