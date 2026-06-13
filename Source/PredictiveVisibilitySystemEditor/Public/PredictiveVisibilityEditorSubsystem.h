#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "PredictiveVisibilityEditorSubsystem.generated.h"

class UPredictiveVisibilityBakeData;

USTRUCT(BlueprintType)
struct FPredictiveVisibilityBakeOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Predictive Visibility", meta = (ClampMin = "1.0", ForceUnits = "m", DisplayName = "Sample Spacing (m)"))
	float SampleSpacing = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Predictive Visibility", meta = (ClampMin = "1.0", ForceUnits = "m", DisplayName = "Max Prediction Distance (m)"))
	float MaxPredictionDistance = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Predictive Visibility")
	float FOVDegrees = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Predictive Visibility")
	int32 DirectionBucketCount = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Predictive Visibility")
	int32 MaxSampleCount = 512;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Predictive Visibility")
	bool bUseExplicitBakeBounds = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Predictive Visibility", meta = (ForceUnits = "m", DisplayName = "Bake Bounds Min (m)"))
	FVector BakeBoundsMin = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Predictive Visibility", meta = (ForceUnits = "m", DisplayName = "Bake Bounds Max (m)"))
	FVector BakeBoundsMax = FVector::ZeroVector;
};

UCLASS()
class PREDICTIVEVISIBILITYSYSTEMEDITOR_API UPredictiveVisibilityEditorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Predictive Visibility")
	void BakeCurrentWorld();

	UFUNCTION(BlueprintCallable, Category = "Predictive Visibility")
	UPredictiveVisibilityBakeData* BakeWorld(UWorld* World, const FPredictiveVisibilityBakeOptions& Options);

private:
	bool ResolveBakeBounds(UWorld* World, const FPredictiveVisibilityBakeOptions& Options, FBox& OutBounds) const;
	bool AutoLoadWorldPartitionRegionForBake(UWorld* World, const FBox& BakeBounds) const;
	UPredictiveVisibilityBakeData* SaveBakeDataAsset(UWorld* World, const FPredictiveVisibilityBakeOptions& Options, const FBox& BakeBounds, int32 CollectedActorCount, const TArray<struct FPredictiveVisibilityBakeRecord>& Records) const;
};
