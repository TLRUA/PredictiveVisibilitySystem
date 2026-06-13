#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "PredictiveVisibilityTypes.generated.h"

UENUM(BlueprintType)
enum class EPredictivePrefetchCandidateType : uint8
{
	Cell,
	Actor,
	HLOD,
	StaticMesh,
	Material,
	PSO
};

UENUM(BlueprintType)
enum class EPredictiveVisibilityPSOComponentType : uint8
{
	StaticMesh,
	InstancedStaticMesh,
	HierarchicalInstancedStaticMesh,
	HLOD
};

USTRUCT(BlueprintType)
struct PREDICTIVEVISIBILITYSYSTEMRUNTIME_API FPredictiveVisibilityPSOPrecacheEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	EPredictiveVisibilityPSOComponentType ComponentType = EPredictiveVisibilityPSOComponentType::StaticMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	FSoftObjectPath SourceActorPath;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	FSoftObjectPath SourceComponentPath;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	FSoftObjectPath StaticMeshPath;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	TArray<FSoftObjectPath> MaterialPaths;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	FSoftObjectPath OverlayMaterialPath;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	int64 PSOPrecacheParamsData = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	FTransform ComponentTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	int32 ForcedLodModel = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	bool bReverseCulling = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	FName StableKey = NAME_None;

	bool IsValid() const;
	FName GetStableKey() const;
	FName BuildStableKey() const;

	bool operator==(const FPredictiveVisibilityPSOPrecacheEntry& Other) const
	{
		return GetStableKey() == Other.GetStableKey();
	}
};

USTRUCT(BlueprintType)
struct PREDICTIVEVISIBILITYSYSTEMRUNTIME_API FPredictiveVisibilityBakeRecord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	FVector SamplePosition = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	uint8 DirectionBucket = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	FRotator SampleRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	float SampleRadius = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	TArray<FName> PotentiallyRelevantCellNames;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	TArray<FSoftObjectPath> PotentiallyVisibleActors;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	TArray<FSoftObjectPath> PotentiallyRelevantHLODActors;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	TArray<FSoftObjectPath> PotentiallyVisibleStaticMeshes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	TArray<FSoftObjectPath> PotentiallyVisibleMaterials;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	TArray<FPredictiveVisibilityPSOPrecacheEntry> PotentiallyVisiblePSOPrecacheEntries;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	TArray<FName> RuntimeGridNames;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	TArray<FName> DataLayerNames;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	int32 DebugVisibleActorCount = 0;
};

USTRUCT(BlueprintType)
struct PREDICTIVEVISIBILITYSYSTEMRUNTIME_API FAdaptivePredictionKey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Predictive Visibility")
	FName CurrentCellName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Predictive Visibility")
	uint8 DirectionBucket = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Predictive Visibility")
	uint8 SpeedBucket = 0;

	bool operator==(const FAdaptivePredictionKey& Other) const
	{
		return CurrentCellName == Other.CurrentCellName
			&& DirectionBucket == Other.DirectionBucket
			&& SpeedBucket == Other.SpeedBucket;
	}
};

FORCEINLINE uint32 GetTypeHash(const FAdaptivePredictionKey& Key)
{
	uint32 Hash = GetTypeHash(Key.CurrentCellName);
	Hash = HashCombine(Hash, ::GetTypeHash(Key.DirectionBucket));
	Hash = HashCombine(Hash, ::GetTypeHash(Key.SpeedBucket));
	return Hash;
}

USTRUCT(BlueprintType)
struct PREDICTIVEVISIBILITYSYSTEMRUNTIME_API FAdaptivePredictionRecord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	int32 TotalSamples = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	TMap<FName, int32> FutureCellHitCount;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	float Confidence = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	FDateTime LastUpdateTime = FDateTime::MinValue();
};

USTRUCT(BlueprintType)
struct PREDICTIVEVISIBILITYSYSTEMRUNTIME_API FPredictivePrefetchCandidate
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	EPredictivePrefetchCandidateType Type = EPredictivePrefetchCandidateType::Cell;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	FName CellName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	FSoftObjectPath AssetPath;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	FName PSOKey = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	FPredictiveVisibilityPSOPrecacheEntry PSOPrecacheEntry;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	FName RuntimeGridName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	FName DataLayerName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	float Score = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	FString Reason;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	bool bFromBakeData = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	bool bFromAdaptiveCache = false;
};

namespace PredictiveVisibility
{
	PREDICTIVEVISIBILITYSYSTEMRUNTIME_API FName MakeSpatialCellName(const FVector& Position, float CellSize);
	PREDICTIVEVISIBILITYSYSTEMRUNTIME_API bool TryGetSpatialCellCenter(FName CellName, float CellSize, FVector& OutCellCenter);
	PREDICTIVEVISIBILITYSYSTEMRUNTIME_API uint8 QuantizeDirectionBucket(const FRotator& ViewRotation, int32 DirectionBucketCount);
	PREDICTIVEVISIBILITYSYSTEMRUNTIME_API uint8 QuantizeSpeedBucket(float Speed, const TArray<float>& SpeedBucketThresholds);
}
