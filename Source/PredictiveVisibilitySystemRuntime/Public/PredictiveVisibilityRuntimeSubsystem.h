#pragma once

#include "CoreMinimal.h"
#include "PSOPrecacheFwd.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "Subsystems/WorldSubsystem.h"
#include "PredictiveVisibilityTypes.h"
#include "PredictiveVisibilityRuntimeSubsystem.generated.h"

class UPredictiveVisibilityAdaptiveCache;
class UPredictiveVisibilityBakeData;
class UPredictiveVisibilitySettings;
class UPrimitiveComponent;
struct FStreamableHandle;

UCLASS()
class PREDICTIVEVISIBILITYSYSTEMRUNTIME_API UPredictiveVisibilityRuntimeSubsystem
	: public UTickableWorldSubsystem
	, public IWorldPartitionStreamingSourceProvider
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	virtual bool GetStreamingSources(TArray<FWorldPartitionStreamingSource>& OutStreamingSources) const override;
	virtual const UObject* GetStreamingSourceOwner() const override { return this; }

	UFUNCTION(BlueprintCallable, Category = "Predictive Visibility")
	void SetBakeData(UPredictiveVisibilityBakeData* InBakeData);

	UFUNCTION(BlueprintCallable, Category = "Predictive Visibility")
	void QueryPrefetchCandidates(const FVector& Position, const FVector& Velocity, const FRotator& ViewRotation, TArray<FPredictivePrefetchCandidate>& OutCandidates) const;

	UFUNCTION(BlueprintCallable, Category = "Predictive Visibility")
	void RequestCellPrefetch(FName CellName, float Score, const FString& Reason);

	UFUNCTION(BlueprintCallable, Category = "Predictive Visibility")
	void RequestAssetPrefetch(const FSoftObjectPath& AssetPath, float Score, const FString& Reason);

	UFUNCTION(BlueprintCallable, Category = "Predictive Visibility")
	void RequestPSOPrecache(const FPredictiveVisibilityPSOPrecacheEntry& Entry, float Score, const FString& Reason);

	void RequestPSOPrecache(FName PSOKey, float Score, const FString& Reason);

	UFUNCTION(BlueprintCallable, Category = "Predictive Visibility")
	void SaveAdaptiveCache();

private:
	struct FPendingAdaptiveObservation
	{
		FAdaptivePredictionKey Key;
		double ObservationTimeSeconds = 0.0;
	};

	struct FActivePrefetchStreamingSource
	{
		FName CellName = NAME_None;
		FVector Location = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		TSet<FName> TargetGrids;
		double ExpireTimeSeconds = 0.0;
	};

	struct FActiveAssetPrefetch
	{
		TArray<FSoftObjectPath> AssetPaths;
		TSharedPtr<FStreamableHandle> Handle;
		double ExpireTimeSeconds = 0.0;
	};

	struct FPSOPrecacheUpdateStats
	{
		int32 Candidates = 0;
		int32 Submitted = 0;
		int32 PendingAssetLoads = 0;
		int32 SkippedDuplicate = 0;
		int32 SkippedDisabled = 0;
		int32 SubmittedMaterialParams = 0;
		int32 SubmittedRequestIDs = 0;
	};

	struct FActivePSOPrecache
	{
		FName StableKey = NAME_None;
		FPredictiveVisibilityPSOPrecacheEntry Entry;
		TSharedPtr<FStreamableHandle> Handle;
		TArray<FMaterialPSOPrecacheRequestID> RequestIDs;
		UPrimitiveComponent* HelperComponent = nullptr;
		double ExpireTimeSeconds = 0.0;
		bool bSubmitted = false;
		bool bPendingAssetLoad = false;
		int32 SubmittedMaterialParams = 0;
	};

	UPROPERTY(Transient)
	TObjectPtr<UPredictiveVisibilityBakeData> BakeData;

	UPROPERTY(Transient)
	TObjectPtr<UPredictiveVisibilityAdaptiveCache> AdaptiveCache;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPrimitiveComponent>> ActivePSOPrecacheComponentRefs;

	TArray<FPendingAdaptiveObservation> PendingObservations;
	TArray<FActivePrefetchStreamingSource> ActivePrefetchStreamingSources;
	TArray<FActiveAssetPrefetch> ActiveAssetPrefetches;
	TArray<FActivePSOPrecache> ActivePSOPrecaches;
	float AdaptiveSampleAccumulator = 0.0f;
	float RuntimeDebugLogAccumulator = 0.0f;
	float RuntimePrefetchAccumulator = 0.0f;
	bool bAdaptiveCacheDirty = false;
	bool bLoggedMissingPlayer = false;
	bool bRegisteredStreamingSourceProvider = false;
	bool bLoggedPSOPrecacheUnavailable = false;

	bool GetObservedPlayerState(FVector& OutPosition, FVector& OutVelocity, FRotator& OutViewRotation) const;
	FAdaptivePredictionKey BuildAdaptiveKey(const FVector& Position, const FVector& Velocity, const FRotator& ViewRotation) const;
	void AddOrMergeCandidate(TArray<FPredictivePrefetchCandidate>& Candidates, const FPredictivePrefetchCandidate& NewCandidate) const;
	void RegisterStreamingSourceProvider();
	void UnregisterStreamingSourceProvider();
	void UpdateRuntimePrefetch(const FVector& Position, const FVector& Velocity, const FRotator& ViewRotation, double CurrentTimeSeconds);
	void AddOrRefreshPrefetchCell(FName CellName, const FRotator& Rotation, const TArray<FName>& RuntimeGridNames, double CurrentTimeSeconds, const UPredictiveVisibilitySettings& Settings);
	void RequestAssetPrefetchBatch(const TArray<FSoftObjectPath>& AssetPaths, double CurrentTimeSeconds, const UPredictiveVisibilitySettings& Settings);
	int32 PruneExpiredPrefetchState(double CurrentTimeSeconds);
	bool IsAssetAlreadyPrefetched(const FSoftObjectPath& AssetPath) const;
	bool IsPSOPrecacheAlreadyActive(FName StableKey) const;
	void RequestPSOPrecacheBatch(const TArray<FPredictiveVisibilityPSOPrecacheEntry>& Entries, double CurrentTimeSeconds, const UPredictiveVisibilitySettings& Settings, FPSOPrecacheUpdateStats& OutStats);
	void SubmitPendingPSOPrecache(FName StableKey);
	bool SubmitPSOPrecacheEntry(FActivePSOPrecache& ActivePrecache, double CurrentTimeSeconds, const UPredictiveVisibilitySettings& Settings);
	void ReleaseActivePSOPrecacheAt(int32 Index);
	void LogRuntimeDebugState(const FVector& Position, const FVector& Velocity, const FRotator& ViewRotation) const;
};
