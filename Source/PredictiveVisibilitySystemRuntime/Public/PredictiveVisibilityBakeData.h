#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PredictiveVisibilityTypes.h"
#include "PredictiveVisibilityBakeData.generated.h"

UCLASS(BlueprintType)
class PREDICTIVEVISIBILITYSYSTEMRUNTIME_API UPredictiveVisibilityBakeData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	FString SourceWorldPath;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	FDateTime BakeTime = FDateTime::MinValue();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility", meta = (ForceUnits = "m", DisplayName = "Sample Spacing (m)"))
	float SampleSpacing = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility", meta = (ForceUnits = "m", DisplayName = "Max Prediction Distance (m)"))
	float MaxPredictionDistance = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	float FOVDegrees = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	int32 DirectionBucketCount = 8;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	int32 NumSamples = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	TArray<FPredictiveVisibilityBakeRecord> Records;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility|Debug", meta = (ForceUnits = "m", DisplayName = "Debug Bake Bounds Min (m)"))
	FVector DebugBakeBoundsMin = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility|Debug", meta = (ForceUnits = "m", DisplayName = "Debug Bake Bounds Max (m)"))
	FVector DebugBakeBoundsMax = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility|Debug")
	int32 DebugCollectedActorCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility|Debug")
	int32 DebugTotalRelevantActorRefs = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility|Debug")
	int32 DebugTotalPSOPrecacheEntries = 0;

	const FPredictiveVisibilityBakeRecord* FindNearestRecord(const FVector& Position, uint8 DirectionBucket) const;

	UFUNCTION(BlueprintCallable, Category = "Predictive Visibility")
	bool BlueprintFindNearestRecord(const FVector& Position, uint8 DirectionBucket, FPredictiveVisibilityBakeRecord& OutRecord) const;

	UFUNCTION(BlueprintCallable, Category = "Predictive Visibility")
	void QueryRecordsAroundPosition(const FVector& Position, float Radius, TArray<FPredictiveVisibilityBakeRecord>& OutRecords) const;
};
