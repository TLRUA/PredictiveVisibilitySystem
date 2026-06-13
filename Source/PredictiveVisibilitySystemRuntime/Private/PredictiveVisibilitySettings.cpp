#include "PredictiveVisibilitySettings.h"

UPredictiveVisibilitySettings::UPredictiveVisibilitySettings()
{
	SampleSpacing = 20.0f;
	MaxPredictionDistance = 120.0f;
	FOVDegrees = 90.0f;
	DirectionBucketCount = 8;
	MaxSampleCount = 512;
	bUseFallbackBakeBounds = true;
	FallbackBakeBoundsMin = FVector(-500.0f, -500.0f, 0.0f);
	FallbackBakeBoundsMax = FVector(500.0f, 500.0f, 0.0f);
	bUseLineOfSightTraceDuringBake = false;
	bAutoLoadWorldPartitionRegionForBake = true;
	MaxAutoLoadWorldPartitionRegionAreaSquareMeters = 250000.0f;
	RuntimeSpatialCellSize = 100.0f;
	AdaptiveSampleIntervalSeconds = 0.5f;
	FutureObservationSeconds = 2.0f;
	MaxAdaptiveRecordCount = 10000;
	SpeedBucketThresholds = {3.0f, 12.0f, 30.0f};
	SaveGameSlotName = TEXT("PredictiveVisibilityAdaptiveCache");
	SaveGameUserIndex = 0;
	bEnableRuntimePrefetch = true;
	bPrefetchWorldPartitionCells = true;
	bActivatePredictedWorldPartitionCells = false;
	bPrefetchAssets = true;
	bPrefetchPSOs = true;
	RuntimePrefetchIntervalSeconds = 0.5f;
	MaxPrefetchCellsPerUpdate = 8;
	MaxPrefetchAssetsPerUpdate = 32;
	MaxPSOPrecacheRequestsPerUpdate = 8;
	PSOPrecacheRetentionSeconds = 30.0f;
	MaxActivePSOPrecacheEntries = 512;
	MaxActivePrefetchStreamingSources = 16;
	PrefetchStreamingSourceRadius = 120.0f;
	PrefetchRetentionSeconds = 6.0f;
	PrefetchAsyncLoadPriority = 0;
	bEnablePrefetchRequestLogs = true;
	bAutoQueryAndLogCandidates = false;
	RuntimeDebugLogIntervalSeconds = 1.0f;
	MaxLoggedCandidatesPerUpdate = 8;
	bEnableDebugLogs = true;
}

float UPredictiveVisibilitySettings::MetersToCentimeters(float ValueMeters)
{
	return ValueMeters * CentimetersPerMeter;
}

FVector UPredictiveVisibilitySettings::MetersToCentimeters(const FVector& ValueMeters)
{
	return ValueMeters * CentimetersPerMeter;
}

float UPredictiveVisibilitySettings::GetSampleSpacingCm() const
{
	return MetersToCentimeters(SampleSpacing);
}

float UPredictiveVisibilitySettings::GetMaxPredictionDistanceCm() const
{
	return MetersToCentimeters(MaxPredictionDistance);
}

FVector UPredictiveVisibilitySettings::GetFallbackBakeBoundsMinCm() const
{
	return MetersToCentimeters(FallbackBakeBoundsMin);
}

FVector UPredictiveVisibilitySettings::GetFallbackBakeBoundsMaxCm() const
{
	return MetersToCentimeters(FallbackBakeBoundsMax);
}

float UPredictiveVisibilitySettings::GetRuntimeSpatialCellSizeCm() const
{
	return MetersToCentimeters(RuntimeSpatialCellSize);
}

float UPredictiveVisibilitySettings::GetPrefetchStreamingSourceRadiusCm() const
{
	return MetersToCentimeters(PrefetchStreamingSourceRadius);
}

TArray<float> UPredictiveVisibilitySettings::GetSpeedBucketThresholdsCmPerSecond() const
{
	TArray<float> ThresholdsCmPerSecond;
	ThresholdsCmPerSecond.Reserve(SpeedBucketThresholds.Num());
	for (const float ThresholdMetersPerSecond : SpeedBucketThresholds)
	{
		ThresholdsCmPerSecond.Add(MetersToCentimeters(ThresholdMetersPerSecond));
	}
	return ThresholdsCmPerSecond;
}
