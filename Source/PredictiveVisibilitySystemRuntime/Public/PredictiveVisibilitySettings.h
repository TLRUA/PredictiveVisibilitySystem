#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PredictiveVisibilityBakeData.h"
#include "PredictiveVisibilitySettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Predictive Visibility System"))
class PREDICTIVEVISIBILITYSYSTEMRUNTIME_API UPredictiveVisibilitySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPredictiveVisibilitySettings();

	static constexpr float CentimetersPerMeter = 100.0f;

	static float MetersToCentimeters(float ValueMeters);
	static FVector MetersToCentimeters(const FVector& ValueMeters);

	float GetSampleSpacingCm() const;
	float GetMaxPredictionDistanceCm() const;
	FVector GetFallbackBakeBoundsMinCm() const;
	FVector GetFallbackBakeBoundsMaxCm() const;
	float GetRuntimeSpatialCellSizeCm() const;
	float GetPrefetchStreamingSourceRadiusCm() const;
	TArray<float> GetSpeedBucketThresholdsCmPerSecond() const;

	UPROPERTY(Config, EditAnywhere, Category = "Bake", meta = (ClampMin = "1.0", ForceUnits = "m", DisplayName = "Sample Spacing (m)"))
	float SampleSpacing;

	UPROPERTY(Config, EditAnywhere, Category = "Bake", meta = (ClampMin = "1.0", ForceUnits = "m", DisplayName = "Max Prediction Distance (m)"))
	float MaxPredictionDistance;

	UPROPERTY(Config, EditAnywhere, Category = "Bake", meta = (ClampMin = "5.0", ClampMax = "179.0"))
	float FOVDegrees;

	UPROPERTY(Config, EditAnywhere, Category = "Bake", meta = (ClampMin = "1", ClampMax = "255"))
	int32 DirectionBucketCount;

	UPROPERTY(Config, EditAnywhere, Category = "Bake", meta = (ClampMin = "1"))
	int32 MaxSampleCount;

	UPROPERTY(Config, EditAnywhere, Category = "Bake")
	bool bUseFallbackBakeBounds;

	UPROPERTY(Config, EditAnywhere, Category = "Bake", meta = (ForceUnits = "m", DisplayName = "Fallback Bake Bounds Min (m)"))
	FVector FallbackBakeBoundsMin;

	UPROPERTY(Config, EditAnywhere, Category = "Bake", meta = (ForceUnits = "m", DisplayName = "Fallback Bake Bounds Max (m)"))
	FVector FallbackBakeBoundsMax;

	UPROPERTY(Config, EditAnywhere, Category = "Bake")
	bool bUseLineOfSightTraceDuringBake;

	UPROPERTY(Config, EditAnywhere, Category = "Bake")
	bool bAutoLoadWorldPartitionRegionForBake;

	UPROPERTY(Config, EditAnywhere, Category = "Bake", meta = (ClampMin = "1.0", ForceUnits = "m^2", DisplayName = "Max Auto Load World Partition Region Area (m^2)"))
	float MaxAutoLoadWorldPartitionRegionAreaSquareMeters;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime", meta = (ClampMin = "1.0", ForceUnits = "m", DisplayName = "Runtime Spatial Cell Size (m)"))
	float RuntimeSpatialCellSize;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime", meta = (ClampMin = "0.1"))
	float AdaptiveSampleIntervalSeconds;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime", meta = (ClampMin = "0.1"))
	float FutureObservationSeconds;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime", meta = (ClampMin = "1"))
	int32 MaxAdaptiveRecordCount;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime", meta = (ForceUnits = "m/s", DisplayName = "Speed Bucket Thresholds (m/s)"))
	TArray<float> SpeedBucketThresholds;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	FString SaveGameSlotName;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime", meta = (ClampMin = "0"))
	int32 SaveGameUserIndex;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	TSoftObjectPtr<UPredictiveVisibilityBakeData> DefaultBakeData;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime|Prefetch")
	bool bEnableRuntimePrefetch;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime|Prefetch")
	bool bPrefetchWorldPartitionCells;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime|Prefetch")
	bool bActivatePredictedWorldPartitionCells;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime|Prefetch")
	bool bPrefetchAssets;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime|Prefetch")
	bool bPrefetchPSOs;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime|Prefetch", meta = (ClampMin = "0.05"))
	float RuntimePrefetchIntervalSeconds;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime|Prefetch", meta = (ClampMin = "1"))
	int32 MaxPrefetchCellsPerUpdate;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime|Prefetch", meta = (ClampMin = "0"))
	int32 MaxPrefetchAssetsPerUpdate;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime|Prefetch", meta = (ClampMin = "0"))
	int32 MaxPSOPrecacheRequestsPerUpdate;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime|Prefetch", meta = (ClampMin = "0.0"))
	float PSOPrecacheRetentionSeconds;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime|Prefetch", meta = (ClampMin = "1"))
	int32 MaxActivePSOPrecacheEntries;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime|Prefetch", meta = (ClampMin = "1"))
	int32 MaxActivePrefetchStreamingSources;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime|Prefetch", meta = (ClampMin = "1.0", ForceUnits = "m", DisplayName = "Prefetch Streaming Source Radius (m)"))
	float PrefetchStreamingSourceRadius;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime|Prefetch", meta = (ClampMin = "0.1"))
	float PrefetchRetentionSeconds;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime|Prefetch")
	int32 PrefetchAsyncLoadPriority;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime|Prefetch")
	bool bEnablePrefetchRequestLogs;

	UPROPERTY(Config, EditAnywhere, Category = "Debug")
	bool bAutoQueryAndLogCandidates;

	UPROPERTY(Config, EditAnywhere, Category = "Debug", meta = (ClampMin = "0.1"))
	float RuntimeDebugLogIntervalSeconds;

	UPROPERTY(Config, EditAnywhere, Category = "Debug", meta = (ClampMin = "0"))
	int32 MaxLoggedCandidatesPerUpdate;

	UPROPERTY(Config, EditAnywhere, Category = "Debug")
	bool bEnableDebugLogs;
};
