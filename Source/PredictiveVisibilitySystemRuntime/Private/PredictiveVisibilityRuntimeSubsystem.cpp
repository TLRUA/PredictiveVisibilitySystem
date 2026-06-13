#include "PredictiveVisibilityRuntimeSubsystem.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Engine/AssetManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "PSOPrecache.h"
#include "PSOPrecacheMaterial.h"
#include "PredictiveVisibilityAdaptiveCache.h"
#include "PredictiveVisibilityBakeData.h"
#include "PredictiveVisibilityLog.h"
#include "PredictiveVisibilityPSOPrecacheComponent.h"
#include "PredictiveVisibilitySettings.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

namespace
{
	constexpr float BakeCellBaseScore = 0.75f;
	constexpr float BakeAssetBaseScore = 0.45f;
	constexpr float AdaptiveCellBaseScore = 1.0f;
	constexpr float MinimumAdaptiveConfidence = 0.05f;
	constexpr int32 MaxAdaptiveCandidates = 16;

	FString CandidateIdentity(const FPredictivePrefetchCandidate& Candidate)
	{
		switch (Candidate.Type)
		{
		case EPredictivePrefetchCandidateType::Cell:
			return FString::Printf(TEXT("Cell:%s"), *Candidate.CellName.ToString());
		case EPredictivePrefetchCandidateType::PSO:
		{
			const FName StableKey = Candidate.PSOPrecacheEntry.GetStableKey();
			return FString::Printf(TEXT("PSO:%s"), *(StableKey.IsNone() ? Candidate.PSOKey : StableKey).ToString());
		}
		default:
			return FString::Printf(TEXT("Asset:%s:%s"), *UEnum::GetValueAsString(Candidate.Type), *Candidate.AssetPath.ToString());
		}
	}

	TArray<FSoftObjectPath> BuildPSOPrecacheAssetPaths(const FPredictiveVisibilityPSOPrecacheEntry& Entry)
	{
		TArray<FSoftObjectPath> AssetPaths;
		if (Entry.StaticMeshPath.IsValid() && !Entry.StaticMeshPath.IsSubobject())
		{
			AssetPaths.Add(Entry.StaticMeshPath);
		}

		for (const FSoftObjectPath& MaterialPath : Entry.MaterialPaths)
		{
			if (MaterialPath.IsValid() && !MaterialPath.IsSubobject())
			{
				AssetPaths.AddUnique(MaterialPath);
			}
		}

		if (Entry.OverlayMaterialPath.IsValid() && !Entry.OverlayMaterialPath.IsSubobject())
		{
			AssetPaths.AddUnique(Entry.OverlayMaterialPath);
		}

		return AssetPaths;
	}

	bool ArePSOPrecacheAssetsLoaded(const FPredictiveVisibilityPSOPrecacheEntry& Entry)
	{
		for (const FSoftObjectPath& AssetPath : BuildPSOPrecacheAssetPaths(Entry))
		{
			if (!AssetPath.ResolveObject())
			{
				return false;
			}
		}

		return true;
	}

	FString BuildNamePreview(const TArray<FName>& Names, const int32 MaxNamesToShow)
	{
		if (Names.IsEmpty() || MaxNamesToShow <= 0)
		{
			return TEXT("None");
		}

		TArray<FString> PreviewParts;
		const int32 VisibleCount = FMath::Min(MaxNamesToShow, Names.Num());
		PreviewParts.Reserve(VisibleCount + 1);
		for (int32 Index = 0; Index < VisibleCount; ++Index)
		{
			PreviewParts.Add(Names[Index].ToString());
		}

		if (Names.Num() > VisibleCount)
		{
			PreviewParts.Add(FString::Printf(TEXT("+%d more"), Names.Num() - VisibleCount));
		}

		return FString::Join(PreviewParts, TEXT(", "));
	}
}

void UPredictiveVisibilityRuntimeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UWorldPartitionSubsystem>();
	Super::Initialize(Collection);

	const UPredictiveVisibilitySettings* Settings = GetDefault<UPredictiveVisibilitySettings>();
	if (Settings)
	{
		if (!Settings->DefaultBakeData.IsNull())
		{
			BakeData = Settings->DefaultBakeData.LoadSynchronous();
		}

		if (UGameplayStatics::DoesSaveGameExist(Settings->SaveGameSlotName, Settings->SaveGameUserIndex))
		{
			AdaptiveCache = Cast<UPredictiveVisibilityAdaptiveCache>(
				UGameplayStatics::LoadGameFromSlot(Settings->SaveGameSlotName, Settings->SaveGameUserIndex));
		}
	}

	if (!AdaptiveCache)
	{
		AdaptiveCache = Cast<UPredictiveVisibilityAdaptiveCache>(
			UGameplayStatics::CreateSaveGameObject(UPredictiveVisibilityAdaptiveCache::StaticClass()));
	}

	RegisterStreamingSourceProvider();

	if (Settings && Settings->bEnableDebugLogs)
	{
		UE_LOG(LogPredictiveVisibility, Log, TEXT("Runtime subsystem initialized. BakeData=%s AdaptiveRecords=%d AutoQueryLogs=%s RuntimePrefetch=%s"),
			BakeData ? *BakeData->GetPathName() : TEXT("None"),
			AdaptiveCache ? AdaptiveCache->Records.Num() : 0,
			Settings->bAutoQueryAndLogCandidates ? TEXT("true") : TEXT("false"),
			Settings->bEnableRuntimePrefetch ? TEXT("true") : TEXT("false"));
	}
}

void UPredictiveVisibilityRuntimeSubsystem::Deinitialize()
{
	UnregisterStreamingSourceProvider();
	SaveAdaptiveCache();
	PendingObservations.Reset();
	PruneExpiredPrefetchState(TNumericLimits<double>::Max());
	ActivePSOPrecacheComponentRefs.Reset();
	AdaptiveCache = nullptr;
	BakeData = nullptr;

	Super::Deinitialize();
}

void UPredictiveVisibilityRuntimeSubsystem::Tick(float DeltaTime)
{
	const UPredictiveVisibilitySettings* Settings = GetDefault<UPredictiveVisibilitySettings>();
	if (!Settings || !AdaptiveCache)
	{
		return;
	}

	FVector Position = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	if (!GetObservedPlayerState(Position, Velocity, ViewRotation))
	{
		if (Settings->bEnableDebugLogs && !bLoggedMissingPlayer)
		{
			UE_LOG(LogPredictiveVisibility, Warning, TEXT("Runtime subsystem is ticking but no local PlayerController Pawn is available yet."));
			bLoggedMissingPlayer = true;
		}
		return;
	}
	bLoggedMissingPlayer = false;

	UWorld* World = GetWorld();
	const double CurrentTimeSeconds = World ? World->GetTimeSeconds() : 0.0;
	const FName CurrentCellName = PredictiveVisibility::MakeSpatialCellName(Position, Settings->GetRuntimeSpatialCellSizeCm());

	for (int32 Index = PendingObservations.Num() - 1; Index >= 0; --Index)
	{
		if (CurrentTimeSeconds < PendingObservations[Index].ObservationTimeSeconds)
		{
			continue;
		}

		// The v1 cache deliberately learns transitions between coarse plugin-owned spatial keys.
		AdaptiveCache->UpdateRecord(PendingObservations[Index].Key, CurrentCellName);
		PendingObservations.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		bAdaptiveCacheDirty = true;
	}

	AdaptiveSampleAccumulator += DeltaTime;
	if (AdaptiveSampleAccumulator >= Settings->AdaptiveSampleIntervalSeconds)
	{
		AdaptiveSampleAccumulator = 0.0f;

		FPendingAdaptiveObservation Observation;
		Observation.Key = BuildAdaptiveKey(Position, Velocity, ViewRotation);
		Observation.ObservationTimeSeconds = CurrentTimeSeconds + Settings->FutureObservationSeconds;
		PendingObservations.Add(Observation);

		if (Settings->bEnableDebugLogs)
		{
			UE_LOG(LogPredictiveVisibility, Log, TEXT("Adaptive sample Cell=%s Direction=%d Speed=%d ObserveAt=%.2f"),
				*Observation.Key.CurrentCellName.ToString(),
				static_cast<int32>(Observation.Key.DirectionBucket),
				static_cast<int32>(Observation.Key.SpeedBucket),
				Observation.ObservationTimeSeconds);
		}
	}

	RuntimeDebugLogAccumulator += DeltaTime;
	if (Settings->bEnableDebugLogs
		&& Settings->bAutoQueryAndLogCandidates
		&& RuntimeDebugLogAccumulator >= Settings->RuntimeDebugLogIntervalSeconds)
	{
		RuntimeDebugLogAccumulator = 0.0f;
		LogRuntimeDebugState(Position, Velocity, ViewRotation);
	}

	PruneExpiredPrefetchState(CurrentTimeSeconds);

	RuntimePrefetchAccumulator += DeltaTime;
	if (Settings->bEnableRuntimePrefetch
		&& RuntimePrefetchAccumulator >= Settings->RuntimePrefetchIntervalSeconds)
	{
		RuntimePrefetchAccumulator = 0.0f;
		UpdateRuntimePrefetch(Position, Velocity, ViewRotation, CurrentTimeSeconds);
	}

	if (bAdaptiveCacheDirty)
	{
		AdaptiveCache->PruneOldRecords(Settings->MaxAdaptiveRecordCount);
	}
}

TStatId UPredictiveVisibilityRuntimeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPredictiveVisibilityRuntimeSubsystem, STATGROUP_Tickables);
}

bool UPredictiveVisibilityRuntimeSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UPredictiveVisibilityRuntimeSubsystem::GetStreamingSources(TArray<FWorldPartitionStreamingSource>& OutStreamingSources) const
{
	const UPredictiveVisibilitySettings* Settings = GetDefault<UPredictiveVisibilitySettings>();
	if (!Settings || !Settings->bEnableRuntimePrefetch || !Settings->bPrefetchWorldPartitionCells)
	{
		return false;
	}

	for (const FActivePrefetchStreamingSource& ActiveSource : ActivePrefetchStreamingSources)
	{
		FWorldPartitionStreamingSource StreamingSource;
		StreamingSource.Name = FName(*FString::Printf(TEXT("PredictiveVisibility_%s"), *ActiveSource.CellName.ToString()));
		StreamingSource.Location = ActiveSource.Location;
		StreamingSource.Rotation = ActiveSource.Rotation;
		StreamingSource.TargetState = Settings->bActivatePredictedWorldPartitionCells
			? EStreamingSourceTargetState::Activated
			: EStreamingSourceTargetState::Loaded;
		StreamingSource.bBlockOnSlowLoading = false;
		StreamingSource.Priority = EStreamingSourcePriority::Low;
		StreamingSource.DebugColor = FColor::Cyan;
		StreamingSource.TargetBehavior = EStreamingSourceTargetBehavior::Include;
		StreamingSource.TargetGrids = ActiveSource.TargetGrids;

		FStreamingSourceShape Shape;
		Shape.bUseGridLoadingRange = false;
		Shape.Radius = Settings->GetPrefetchStreamingSourceRadiusCm();
		Shape.bIsSector = false;
		StreamingSource.Shapes.Add(Shape);

		OutStreamingSources.Add(MoveTemp(StreamingSource));
	}

	return !ActivePrefetchStreamingSources.IsEmpty();
}

void UPredictiveVisibilityRuntimeSubsystem::SetBakeData(UPredictiveVisibilityBakeData* InBakeData)
{
	BakeData = InBakeData;
}

void UPredictiveVisibilityRuntimeSubsystem::QueryPrefetchCandidates(
	const FVector& Position,
	const FVector& Velocity,
	const FRotator& ViewRotation,
	TArray<FPredictivePrefetchCandidate>& OutCandidates) const
{
	OutCandidates.Reset();

	const UPredictiveVisibilitySettings* Settings = GetDefault<UPredictiveVisibilitySettings>();
	const int32 DirectionBucketCount = Settings ? FMath::Clamp(Settings->DirectionBucketCount, 1, 255) : 8;
	const uint8 DirectionBucket = PredictiveVisibility::QuantizeDirectionBucket(ViewRotation, DirectionBucketCount);

	if (BakeData)
	{
		if (const FPredictiveVisibilityBakeRecord* Record = BakeData->FindNearestRecord(Position, DirectionBucket))
		{
			for (const FName CellName : Record->PotentiallyRelevantCellNames)
			{
				FPredictivePrefetchCandidate Candidate;
				Candidate.Type = EPredictivePrefetchCandidateType::Cell;
				Candidate.CellName = CellName;
				Candidate.Score = BakeCellBaseScore;
				Candidate.Reason = TEXT("Nearest bake record cell");
				Candidate.bFromBakeData = true;
				AddOrMergeCandidate(OutCandidates, Candidate);
			}

			for (const FSoftObjectPath& ActorPath : Record->PotentiallyVisibleActors)
			{
				FPredictivePrefetchCandidate Candidate;
				Candidate.Type = EPredictivePrefetchCandidateType::Actor;
				Candidate.AssetPath = ActorPath;
				Candidate.Score = BakeAssetBaseScore;
				Candidate.Reason = TEXT("Nearest bake record actor");
				Candidate.bFromBakeData = true;
				AddOrMergeCandidate(OutCandidates, Candidate);
			}

			for (const FSoftObjectPath& HLODPath : Record->PotentiallyRelevantHLODActors)
			{
				FPredictivePrefetchCandidate Candidate;
				Candidate.Type = EPredictivePrefetchCandidateType::HLOD;
				Candidate.AssetPath = HLODPath;
				Candidate.Score = BakeAssetBaseScore + 0.1f;
				Candidate.Reason = TEXT("Nearest bake record HLOD");
				Candidate.bFromBakeData = true;
				AddOrMergeCandidate(OutCandidates, Candidate);
			}

			for (const FSoftObjectPath& MeshPath : Record->PotentiallyVisibleStaticMeshes)
			{
				FPredictivePrefetchCandidate Candidate;
				Candidate.Type = EPredictivePrefetchCandidateType::StaticMesh;
				Candidate.AssetPath = MeshPath;
				Candidate.Score = BakeAssetBaseScore;
				Candidate.Reason = TEXT("Nearest bake record static mesh");
				Candidate.bFromBakeData = true;
				AddOrMergeCandidate(OutCandidates, Candidate);
			}

			for (const FSoftObjectPath& MaterialPath : Record->PotentiallyVisibleMaterials)
			{
				FPredictivePrefetchCandidate Candidate;
				Candidate.Type = EPredictivePrefetchCandidateType::Material;
				Candidate.AssetPath = MaterialPath;
				Candidate.Score = BakeAssetBaseScore * 0.8f;
				Candidate.Reason = TEXT("Nearest bake record material");
				Candidate.bFromBakeData = true;
				AddOrMergeCandidate(OutCandidates, Candidate);
			}

			for (const FPredictiveVisibilityPSOPrecacheEntry& PSOEntry : Record->PotentiallyVisiblePSOPrecacheEntries)
			{
				if (!PSOEntry.IsValid())
				{
					continue;
				}

				FPredictivePrefetchCandidate Candidate;
				Candidate.Type = EPredictivePrefetchCandidateType::PSO;
				Candidate.PSOPrecacheEntry = PSOEntry;
				Candidate.PSOKey = PSOEntry.GetStableKey();
				Candidate.Score = BakeAssetBaseScore + 0.2f;
				Candidate.Reason = TEXT("Nearest bake record PSO");
				Candidate.bFromBakeData = true;
				AddOrMergeCandidate(OutCandidates, Candidate);
			}
		}
	}

	if (AdaptiveCache && Settings)
	{
		const FAdaptivePredictionKey Key = BuildAdaptiveKey(Position, Velocity, ViewRotation);
		TArray<FName> AdaptiveCells;
		AdaptiveCache->QueryTopFutureCells(Key, MaxAdaptiveCandidates, MinimumAdaptiveConfidence, AdaptiveCells);
		for (const FName CellName : AdaptiveCells)
		{
			FPredictivePrefetchCandidate Candidate;
			Candidate.Type = EPredictivePrefetchCandidateType::Cell;
			Candidate.CellName = CellName;
			Candidate.Score = AdaptiveCellBaseScore;
			Candidate.Reason = TEXT("Adaptive cache future cell");
			Candidate.bFromAdaptiveCache = true;
			AddOrMergeCandidate(OutCandidates, Candidate);
		}
	}

	OutCandidates.Sort([](const FPredictivePrefetchCandidate& Left, const FPredictivePrefetchCandidate& Right)
	{
		return Left.Score > Right.Score;
	});
}

void UPredictiveVisibilityRuntimeSubsystem::RequestCellPrefetch(FName CellName, float Score, const FString& Reason)
{
	const UPredictiveVisibilitySettings* Settings = GetDefault<UPredictiveVisibilitySettings>();
	UWorld* World = GetWorld();
	if (Settings && World && Settings->bPrefetchWorldPartitionCells)
	{
		AddOrRefreshPrefetchCell(CellName, FRotator::ZeroRotator, TArray<FName>(), World->GetTimeSeconds(), *Settings);
	}

	if (Settings && Settings->bEnablePrefetchRequestLogs)
	{
		UE_LOG(LogPredictiveVisibility, Log, TEXT("RequestCellPrefetch Cell=%s Score=%.3f Reason=%s"),
			*CellName.ToString(),
			Score,
			*Reason);
	}
}

void UPredictiveVisibilityRuntimeSubsystem::RequestAssetPrefetch(const FSoftObjectPath& AssetPath, float Score, const FString& Reason)
{
	const UPredictiveVisibilitySettings* Settings = GetDefault<UPredictiveVisibilitySettings>();
	UWorld* World = GetWorld();
	if (Settings && World && Settings->bPrefetchAssets && AssetPath.IsValid() && !AssetPath.IsSubobject() && !IsAssetAlreadyPrefetched(AssetPath))
	{
		RequestAssetPrefetchBatch(TArray<FSoftObjectPath>{ AssetPath }, World->GetTimeSeconds(), *Settings);
	}

	if (Settings && Settings->bEnablePrefetchRequestLogs)
	{
		UE_LOG(LogPredictiveVisibility, Log, TEXT("RequestAssetPrefetch Asset=%s Score=%.3f Reason=%s"),
			*AssetPath.ToString(),
			Score,
			*Reason);
	}
}

void UPredictiveVisibilityRuntimeSubsystem::RequestPSOPrecache(
	const FPredictiveVisibilityPSOPrecacheEntry& Entry,
	float Score,
	const FString& Reason)
{
	const UPredictiveVisibilitySettings* Settings = GetDefault<UPredictiveVisibilitySettings>();
	UWorld* World = GetWorld();
	if (Settings && World)
	{
		FPSOPrecacheUpdateStats Stats;
		TArray<FPredictiveVisibilityPSOPrecacheEntry> SingleEntry;
		SingleEntry.Add(Entry);
		RequestPSOPrecacheBatch(SingleEntry, World->GetTimeSeconds(), *Settings, Stats);
	}

	if (Settings && Settings->bEnablePrefetchRequestLogs)
	{
		UE_LOG(LogPredictiveVisibility, Log, TEXT("RequestPSOPrecache PSO=%s Score=%.3f Reason=%s"),
			*Entry.GetStableKey().ToString(),
			Score,
			*Reason);
	}
}

void UPredictiveVisibilityRuntimeSubsystem::RequestPSOPrecache(FName PSOKey, float Score, const FString& Reason)
{
	const UPredictiveVisibilitySettings* Settings = GetDefault<UPredictiveVisibilitySettings>();
	if (Settings && Settings->bEnablePrefetchRequestLogs)
	{
		UE_LOG(LogPredictiveVisibility, Warning, TEXT("RequestPSOPrecache ignored legacy key-only request PSO=%s Score=%.3f Reason=%s"),
			*PSOKey.ToString(),
			Score,
			*Reason);
	}
}

void UPredictiveVisibilityRuntimeSubsystem::SaveAdaptiveCache()
{
	if (!bAdaptiveCacheDirty || !AdaptiveCache)
	{
		return;
	}

	const UPredictiveVisibilitySettings* Settings = GetDefault<UPredictiveVisibilitySettings>();
	if (!Settings)
	{
		return;
	}

	UGameplayStatics::SaveGameToSlot(AdaptiveCache, Settings->SaveGameSlotName, Settings->SaveGameUserIndex);
	bAdaptiveCacheDirty = false;
}

bool UPredictiveVisibilityRuntimeSubsystem::GetObservedPlayerState(FVector& OutPosition, FVector& OutVelocity, FRotator& OutViewRotation) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	APlayerController* PlayerController = World->GetFirstPlayerController();
	if (!PlayerController)
	{
		return false;
	}

	APawn* Pawn = PlayerController->GetPawn();
	if (!Pawn)
	{
		return false;
	}

	OutPosition = Pawn->GetActorLocation();
	OutVelocity = Pawn->GetVelocity();
	OutViewRotation = PlayerController->GetControlRotation();
	return true;
}

FAdaptivePredictionKey UPredictiveVisibilityRuntimeSubsystem::BuildAdaptiveKey(const FVector& Position, const FVector& Velocity, const FRotator& ViewRotation) const
{
	const UPredictiveVisibilitySettings* Settings = GetDefault<UPredictiveVisibilitySettings>();

	FAdaptivePredictionKey Key;
	const float RuntimeSpatialCellSizeCm = Settings ? Settings->GetRuntimeSpatialCellSizeCm() : 10000.0f;
	const TArray<float> SpeedBucketThresholdsCmPerSecond = Settings
		? Settings->GetSpeedBucketThresholdsCmPerSecond()
		: TArray<float>();
	Key.CurrentCellName = PredictiveVisibility::MakeSpatialCellName(Position, RuntimeSpatialCellSizeCm);
	Key.DirectionBucket = PredictiveVisibility::QuantizeDirectionBucket(ViewRotation, Settings ? Settings->DirectionBucketCount : 8);
	Key.SpeedBucket = PredictiveVisibility::QuantizeSpeedBucket(Velocity.Size(), SpeedBucketThresholdsCmPerSecond);
	return Key;
}

void UPredictiveVisibilityRuntimeSubsystem::AddOrMergeCandidate(TArray<FPredictivePrefetchCandidate>& Candidates, const FPredictivePrefetchCandidate& NewCandidate) const
{
	const FString NewIdentity = CandidateIdentity(NewCandidate);
	for (FPredictivePrefetchCandidate& ExistingCandidate : Candidates)
	{
		if (CandidateIdentity(ExistingCandidate) != NewIdentity)
		{
			continue;
		}

		ExistingCandidate.Score = FMath::Max(ExistingCandidate.Score, NewCandidate.Score);
		ExistingCandidate.bFromBakeData |= NewCandidate.bFromBakeData;
		ExistingCandidate.bFromAdaptiveCache |= NewCandidate.bFromAdaptiveCache;
		if (!ExistingCandidate.Reason.Contains(NewCandidate.Reason))
		{
			ExistingCandidate.Reason += TEXT("; ");
			ExistingCandidate.Reason += NewCandidate.Reason;
		}
		return;
	}

	Candidates.Add(NewCandidate);
}

void UPredictiveVisibilityRuntimeSubsystem::RegisterStreamingSourceProvider()
{
	if (bRegisteredStreamingSourceProvider)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	if (UWorldPartitionSubsystem* WorldPartitionSubsystem = World->GetSubsystem<UWorldPartitionSubsystem>())
	{
		if (!WorldPartitionSubsystem->IsStreamingSourceProviderRegistered(this))
		{
			WorldPartitionSubsystem->RegisterStreamingSourceProvider(this);
		}
		bRegisteredStreamingSourceProvider = true;
	}
}

void UPredictiveVisibilityRuntimeSubsystem::UnregisterStreamingSourceProvider()
{
	if (!bRegisteredStreamingSourceProvider)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World)
	{
		if (UWorldPartitionSubsystem* WorldPartitionSubsystem = World->GetSubsystem<UWorldPartitionSubsystem>())
		{
			WorldPartitionSubsystem->UnregisterStreamingSourceProvider(this);
		}
	}

	bRegisteredStreamingSourceProvider = false;
}

void UPredictiveVisibilityRuntimeSubsystem::UpdateRuntimePrefetch(
	const FVector& Position,
	const FVector& Velocity,
	const FRotator& ViewRotation,
	double CurrentTimeSeconds)
{
	const UPredictiveVisibilitySettings* Settings = GetDefault<UPredictiveVisibilitySettings>();
	if (!Settings)
	{
		return;
	}

	const int32 DirectionBucketCount = FMath::Clamp(Settings->DirectionBucketCount, 1, 255);
	const uint8 DirectionBucket = PredictiveVisibility::QuantizeDirectionBucket(ViewRotation, DirectionBucketCount);
	const FPredictiveVisibilityBakeRecord* Record = BakeData ? BakeData->FindNearestRecord(Position, DirectionBucket) : nullptr;

	int32 NewOrRefreshedCellCount = 0;
	if (Settings->bPrefetchWorldPartitionCells)
	{
		TSet<FName> SeenCells;
		auto TryAddCell = [&](FName CellName)
		{
			if (CellName.IsNone()
				|| SeenCells.Contains(CellName)
				|| NewOrRefreshedCellCount >= Settings->MaxPrefetchCellsPerUpdate)
			{
				return;
			}

			SeenCells.Add(CellName);
			AddOrRefreshPrefetchCell(
				CellName,
				ViewRotation,
				Record ? Record->RuntimeGridNames : TArray<FName>(),
				CurrentTimeSeconds,
				*Settings);
			++NewOrRefreshedCellCount;
		};

		if (AdaptiveCache)
		{
			const FAdaptivePredictionKey Key = BuildAdaptiveKey(Position, Velocity, ViewRotation);
			TArray<FName> AdaptiveCells;
			AdaptiveCache->QueryTopFutureCells(Key, MaxAdaptiveCandidates, MinimumAdaptiveConfidence, AdaptiveCells);
			for (const FName CellName : AdaptiveCells)
			{
				TryAddCell(CellName);
			}
		}

		if (Record)
		{
			for (const FName CellName : Record->PotentiallyRelevantCellNames)
			{
				TryAddCell(CellName);
			}
		}
	}

	TArray<FSoftObjectPath> AssetPathsToPrefetch;
	if (Settings->bPrefetchAssets && Record && Settings->MaxPrefetchAssetsPerUpdate > 0)
	{
		TSet<FSoftObjectPath> NewAssetPaths;
		auto TryAddAsset = [&](const FSoftObjectPath& AssetPath)
		{
			if (AssetPathsToPrefetch.Num() >= Settings->MaxPrefetchAssetsPerUpdate)
			{
				return;
			}

			if (!AssetPath.IsValid() || AssetPath.IsSubobject())
			{
				return;
			}

			if (NewAssetPaths.Contains(AssetPath) || IsAssetAlreadyPrefetched(AssetPath))
			{
				return;
			}

			NewAssetPaths.Add(AssetPath);
			AssetPathsToPrefetch.Add(AssetPath);
		};

		for (const FSoftObjectPath& MeshPath : Record->PotentiallyVisibleStaticMeshes)
		{
			TryAddAsset(MeshPath);
		}

		for (const FSoftObjectPath& MaterialPath : Record->PotentiallyVisibleMaterials)
		{
			TryAddAsset(MaterialPath);
		}
	}

	RequestAssetPrefetchBatch(AssetPathsToPrefetch, CurrentTimeSeconds, *Settings);

	FPSOPrecacheUpdateStats PSOStats;
	if (Settings->bPrefetchPSOs && Record && Settings->MaxPSOPrecacheRequestsPerUpdate > 0)
	{
		RequestPSOPrecacheBatch(Record->PotentiallyVisiblePSOPrecacheEntries, CurrentTimeSeconds, *Settings, PSOStats);
	}

	if (Settings->bEnablePrefetchRequestLogs
		&& (NewOrRefreshedCellCount > 0
			|| AssetPathsToPrefetch.Num() > 0
			|| PSOStats.Candidates > 0
			|| PSOStats.SkippedDisabled > 0))
	{
		UE_LOG(LogPredictiveVisibility, Log, TEXT("Runtime prefetch requested Cells=%d Assets=%d PSOCandidates=%d PSOSubmitted=%d PSOPendingAssets=%d PSOSkippedDuplicate=%d PSOSkippedDisabled=%d PSOMaterialParams=%d PSORequestIDs=%d ActiveSources=%d ActiveAssetBatches=%d ActivePSOs=%d"),
			NewOrRefreshedCellCount,
			AssetPathsToPrefetch.Num(),
			PSOStats.Candidates,
			PSOStats.Submitted,
			PSOStats.PendingAssetLoads,
			PSOStats.SkippedDuplicate,
			PSOStats.SkippedDisabled,
			PSOStats.SubmittedMaterialParams,
			PSOStats.SubmittedRequestIDs,
			ActivePrefetchStreamingSources.Num(),
			ActiveAssetPrefetches.Num(),
			ActivePSOPrecaches.Num());
	}
}

void UPredictiveVisibilityRuntimeSubsystem::AddOrRefreshPrefetchCell(
	FName CellName,
	const FRotator& Rotation,
	const TArray<FName>& RuntimeGridNames,
	double CurrentTimeSeconds,
	const UPredictiveVisibilitySettings& Settings)
{
	FVector CellCenter = FVector::ZeroVector;
	if (!PredictiveVisibility::TryGetSpatialCellCenter(CellName, Settings.GetRuntimeSpatialCellSizeCm(), CellCenter))
	{
		return;
	}

	const double ExpireTimeSeconds = CurrentTimeSeconds + Settings.PrefetchRetentionSeconds;
	for (FActivePrefetchStreamingSource& ActiveSource : ActivePrefetchStreamingSources)
	{
		if (ActiveSource.CellName != CellName)
		{
			continue;
		}

		ActiveSource.Location = CellCenter;
		ActiveSource.Rotation = Rotation;
		ActiveSource.ExpireTimeSeconds = ExpireTimeSeconds;
		ActiveSource.TargetGrids.Reset();
		for (const FName RuntimeGridName : RuntimeGridNames)
		{
			if (!RuntimeGridName.IsNone())
			{
				ActiveSource.TargetGrids.Add(RuntimeGridName);
			}
		}
		return;
	}

	FActivePrefetchStreamingSource NewSource;
	NewSource.CellName = CellName;
	NewSource.Location = CellCenter;
	NewSource.Rotation = Rotation;
	NewSource.ExpireTimeSeconds = ExpireTimeSeconds;
	for (const FName RuntimeGridName : RuntimeGridNames)
	{
		if (!RuntimeGridName.IsNone())
		{
			NewSource.TargetGrids.Add(RuntimeGridName);
		}
	}
	ActivePrefetchStreamingSources.Add(MoveTemp(NewSource));

	if (ActivePrefetchStreamingSources.Num() > Settings.MaxActivePrefetchStreamingSources)
	{
		ActivePrefetchStreamingSources.Sort([](const FActivePrefetchStreamingSource& Left, const FActivePrefetchStreamingSource& Right)
		{
			return Left.ExpireTimeSeconds > Right.ExpireTimeSeconds;
		});
		ActivePrefetchStreamingSources.SetNum(Settings.MaxActivePrefetchStreamingSources, EAllowShrinking::No);
	}
}

void UPredictiveVisibilityRuntimeSubsystem::RequestAssetPrefetchBatch(
	const TArray<FSoftObjectPath>& AssetPaths,
	double CurrentTimeSeconds,
	const UPredictiveVisibilitySettings& Settings)
{
	if (AssetPaths.IsEmpty())
	{
		return;
	}

	TArray<FSoftObjectPath> PathsCopy = AssetPaths;
	TSharedPtr<FStreamableHandle> Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		MoveTemp(PathsCopy),
		FStreamableDelegate(),
		static_cast<TAsyncLoadPriority>(Settings.PrefetchAsyncLoadPriority),
		true,
		false,
		FString::Printf(TEXT("PredictiveVisibilityPrefetch_%d"), AssetPaths.Num()));

	if (!Handle.IsValid())
	{
		return;
	}

	FActiveAssetPrefetch ActivePrefetch;
	ActivePrefetch.AssetPaths = AssetPaths;
	ActivePrefetch.Handle = Handle;
	ActivePrefetch.ExpireTimeSeconds = CurrentTimeSeconds + Settings.PrefetchRetentionSeconds;
	ActiveAssetPrefetches.Add(MoveTemp(ActivePrefetch));
}

int32 UPredictiveVisibilityRuntimeSubsystem::PruneExpiredPrefetchState(double CurrentTimeSeconds)
{
	for (int32 Index = ActivePrefetchStreamingSources.Num() - 1; Index >= 0; --Index)
	{
		if (ActivePrefetchStreamingSources[Index].ExpireTimeSeconds <= CurrentTimeSeconds)
		{
			ActivePrefetchStreamingSources.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		}
	}

	for (int32 Index = ActiveAssetPrefetches.Num() - 1; Index >= 0; --Index)
	{
		if (ActiveAssetPrefetches[Index].ExpireTimeSeconds > CurrentTimeSeconds)
		{
			continue;
		}

		if (ActiveAssetPrefetches[Index].Handle.IsValid())
		{
			ActiveAssetPrefetches[Index].Handle->ReleaseHandle();
		}
		ActiveAssetPrefetches.RemoveAtSwap(Index, 1, EAllowShrinking::No);
	}

	int32 ReleasedPSOPrecacheCount = 0;
	for (int32 Index = ActivePSOPrecaches.Num() - 1; Index >= 0; --Index)
	{
		if (ActivePSOPrecaches[Index].ExpireTimeSeconds > CurrentTimeSeconds)
		{
			continue;
		}

		ReleaseActivePSOPrecacheAt(Index);
		++ReleasedPSOPrecacheCount;
	}

	const UPredictiveVisibilitySettings* Settings = GetDefault<UPredictiveVisibilitySettings>();
	if (ReleasedPSOPrecacheCount > 0 && Settings && Settings->bEnablePrefetchRequestLogs)
	{
		UE_LOG(LogPredictiveVisibility, Log, TEXT("Runtime PSO precache released Released=%d ActivePSOs=%d"),
			ReleasedPSOPrecacheCount,
			ActivePSOPrecaches.Num());
	}

	return ReleasedPSOPrecacheCount;
}

bool UPredictiveVisibilityRuntimeSubsystem::IsAssetAlreadyPrefetched(const FSoftObjectPath& AssetPath) const
{
	for (const FActiveAssetPrefetch& ActivePrefetch : ActiveAssetPrefetches)
	{
		if (ActivePrefetch.AssetPaths.Contains(AssetPath))
		{
			return true;
		}
	}

	return false;
}

bool UPredictiveVisibilityRuntimeSubsystem::IsPSOPrecacheAlreadyActive(FName StableKey) const
{
	if (StableKey.IsNone())
	{
		return false;
	}

	for (const FActivePSOPrecache& ActivePrecache : ActivePSOPrecaches)
	{
		if (ActivePrecache.StableKey == StableKey)
		{
			return true;
		}
	}

	return false;
}

void UPredictiveVisibilityRuntimeSubsystem::RequestPSOPrecacheBatch(
	const TArray<FPredictiveVisibilityPSOPrecacheEntry>& Entries,
	double CurrentTimeSeconds,
	const UPredictiveVisibilitySettings& Settings,
	FPSOPrecacheUpdateStats& OutStats)
{
	OutStats.Candidates = Entries.Num();
	if (Entries.IsEmpty())
	{
		return;
	}

	if (!Settings.bPrefetchPSOs || Settings.MaxPSOPrecacheRequestsPerUpdate <= 0)
	{
		OutStats.SkippedDisabled = Entries.Num();
		return;
	}

	if (!IsComponentPSOPrecachingEnabled())
	{
		OutStats.SkippedDisabled = Entries.Num();
		if (Settings.bEnablePrefetchRequestLogs && !bLoggedPSOPrecacheUnavailable)
		{
			UE_LOG(LogPredictiveVisibility, Warning, TEXT("Runtime PSO precache disabled. In PIE this is expected because UE 5.5 disables component PSO precaching under WITH_EDITOR. For runtime validation use non-editor Development/packaged and enable r.PSOPrecache.Components."));
			bLoggedPSOPrecacheUnavailable = true;
		}
		return;
	}

	int32 NewRequestCount = 0;
	for (const FPredictiveVisibilityPSOPrecacheEntry& Entry : Entries)
	{
		if (NewRequestCount >= Settings.MaxPSOPrecacheRequestsPerUpdate)
		{
			break;
		}

		if (!Entry.IsValid())
		{
			continue;
		}

		const FName StableKey = Entry.GetStableKey();
		if (StableKey.IsNone())
		{
			continue;
		}

		if (IsPSOPrecacheAlreadyActive(StableKey))
		{
			++OutStats.SkippedDuplicate;
			continue;
		}

		const int32 ActivePrecacheIndex = ActivePSOPrecaches.Num();
		FActivePSOPrecache& ActivePrecache = ActivePSOPrecaches.AddDefaulted_GetRef();
		ActivePrecache.StableKey = StableKey;
		ActivePrecache.Entry = Entry;
		ActivePrecache.ExpireTimeSeconds = CurrentTimeSeconds + Settings.PSOPrecacheRetentionSeconds;

		if (ArePSOPrecacheAssetsLoaded(Entry))
		{
			if (SubmitPSOPrecacheEntry(ActivePrecache, CurrentTimeSeconds, Settings))
			{
				++OutStats.Submitted;
				OutStats.SubmittedMaterialParams += ActivePrecache.SubmittedMaterialParams;
				OutStats.SubmittedRequestIDs += ActivePrecache.RequestIDs.Num();
			}
			else
			{
				ReleaseActivePSOPrecacheAt(ActivePrecacheIndex);
			}
		}
		else
		{
			TArray<FSoftObjectPath> AssetPaths = BuildPSOPrecacheAssetPaths(Entry);
			TWeakObjectPtr<UPredictiveVisibilityRuntimeSubsystem> WeakThis(this);
			ActivePrecache.Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
				AssetPaths,
				FStreamableDelegate::CreateLambda([WeakThis, StableKey]()
				{
					if (UPredictiveVisibilityRuntimeSubsystem* Subsystem = WeakThis.Get())
					{
						Subsystem->SubmitPendingPSOPrecache(StableKey);
					}
				}),
				static_cast<TAsyncLoadPriority>(Settings.PrefetchAsyncLoadPriority),
				true,
				false,
				FString::Printf(TEXT("PredictiveVisibilityPSOPrecache_%s"), *StableKey.ToString()));

			ActivePrecache.bPendingAssetLoad = ActivePrecache.Handle.IsValid();
			if (ActivePrecache.bPendingAssetLoad)
			{
				++OutStats.PendingAssetLoads;
			}
			else if (SubmitPSOPrecacheEntry(ActivePrecache, CurrentTimeSeconds, Settings))
			{
				++OutStats.Submitted;
				OutStats.SubmittedMaterialParams += ActivePrecache.SubmittedMaterialParams;
				OutStats.SubmittedRequestIDs += ActivePrecache.RequestIDs.Num();
			}
			else
			{
				ReleaseActivePSOPrecacheAt(ActivePrecacheIndex);
			}
		}

		++NewRequestCount;
	}

	if (ActivePSOPrecaches.Num() > Settings.MaxActivePSOPrecacheEntries)
	{
		ActivePSOPrecaches.Sort([](const FActivePSOPrecache& Left, const FActivePSOPrecache& Right)
		{
			return Left.ExpireTimeSeconds > Right.ExpireTimeSeconds;
		});

		while (ActivePSOPrecaches.Num() > Settings.MaxActivePSOPrecacheEntries)
		{
			ReleaseActivePSOPrecacheAt(ActivePSOPrecaches.Num() - 1);
		}
	}
}

void UPredictiveVisibilityRuntimeSubsystem::SubmitPendingPSOPrecache(FName StableKey)
{
	const UPredictiveVisibilitySettings* Settings = GetDefault<UPredictiveVisibilitySettings>();
	UWorld* World = GetWorld();
	if (!Settings || !World)
	{
		return;
	}

	for (int32 Index = 0; Index < ActivePSOPrecaches.Num(); ++Index)
	{
		FActivePSOPrecache& ActivePrecache = ActivePSOPrecaches[Index];
		if (ActivePrecache.StableKey != StableKey)
		{
			continue;
		}

		ActivePrecache.bPendingAssetLoad = false;
		const bool bSubmitted = SubmitPSOPrecacheEntry(ActivePrecache, World->GetTimeSeconds(), *Settings);
		if (Settings->bEnablePrefetchRequestLogs)
		{
			UE_LOG(LogPredictiveVisibility, Log, TEXT("Runtime PSO async assets ready PSO=%s Submitted=%s MaterialParams=%d RequestIDs=%d ActivePSOs=%d"),
				*StableKey.ToString(),
				bSubmitted ? TEXT("true") : TEXT("false"),
				ActivePrecache.SubmittedMaterialParams,
				ActivePrecache.RequestIDs.Num(),
				ActivePSOPrecaches.Num());
		}
		if (!bSubmitted)
		{
			ReleaseActivePSOPrecacheAt(Index);
		}
		return;
	}
}

bool UPredictiveVisibilityRuntimeSubsystem::SubmitPSOPrecacheEntry(
	FActivePSOPrecache& ActivePrecache,
	double CurrentTimeSeconds,
	const UPredictiveVisibilitySettings& Settings)
{
	if (ActivePrecache.bSubmitted || !ActivePrecache.Entry.IsValid())
	{
		return false;
	}

	UStaticMesh* StaticMesh = Cast<UStaticMesh>(ActivePrecache.Entry.StaticMeshPath.ResolveObject());
	if (!StaticMesh)
	{
		return false;
	}

	UStaticMeshComponent* HelperComponent = nullptr;
	UObject* HelperOuter = GetWorld() ? static_cast<UObject*>(GetWorld()) : static_cast<UObject*>(this);
	switch (ActivePrecache.Entry.ComponentType)
	{
	case EPredictiveVisibilityPSOComponentType::HierarchicalInstancedStaticMesh:
		HelperComponent = NewObject<UPredictiveVisibilityHierarchicalInstancedStaticMeshPSOPrecacheComponent>(HelperOuter, NAME_None, RF_Transient);
		break;
	case EPredictiveVisibilityPSOComponentType::InstancedStaticMesh:
		HelperComponent = NewObject<UPredictiveVisibilityInstancedStaticMeshPSOPrecacheComponent>(HelperOuter, NAME_None, RF_Transient);
		break;
	case EPredictiveVisibilityPSOComponentType::HLOD:
	case EPredictiveVisibilityPSOComponentType::StaticMesh:
	default:
		HelperComponent = NewObject<UPredictiveVisibilityStaticMeshPSOPrecacheComponent>(HelperOuter, NAME_None, RF_Transient);
		break;
	}

	if (!HelperComponent)
	{
		return false;
	}

	HelperComponent->SetWorldTransform(ActivePrecache.Entry.ComponentTransform);
	HelperComponent->SetReverseCulling(ActivePrecache.Entry.bReverseCulling);
	HelperComponent->SetForcedLodModel(ActivePrecache.Entry.ForcedLodModel);
	HelperComponent->SetStaticMesh(StaticMesh);

	for (int32 MaterialIndex = 0; MaterialIndex < ActivePrecache.Entry.MaterialPaths.Num(); ++MaterialIndex)
	{
		const FSoftObjectPath& MaterialPath = ActivePrecache.Entry.MaterialPaths[MaterialIndex];
		if (UMaterialInterface* Material = Cast<UMaterialInterface>(MaterialPath.ResolveObject()))
		{
			HelperComponent->SetMaterial(MaterialIndex, Material);
		}
	}

	if (UMaterialInterface* OverlayMaterial = Cast<UMaterialInterface>(ActivePrecache.Entry.OverlayMaterialPath.ResolveObject()))
	{
		HelperComponent->SetOverlayMaterial(OverlayMaterial);
	}

	FGraphEventArray GraphEvents;
	int32 SubmittedMaterialParams = 0;
	if (UPredictiveVisibilityHierarchicalInstancedStaticMeshPSOPrecacheComponent* HISMHelper = Cast<UPredictiveVisibilityHierarchicalInstancedStaticMeshPSOPrecacheComponent>(HelperComponent))
	{
		SubmittedMaterialParams = HISMHelper->SubmitCollectedPSOPrecache(static_cast<uint64>(ActivePrecache.Entry.PSOPrecacheParamsData), ActivePrecache.RequestIDs, GraphEvents);
	}
	else if (UPredictiveVisibilityInstancedStaticMeshPSOPrecacheComponent* ISMHelper = Cast<UPredictiveVisibilityInstancedStaticMeshPSOPrecacheComponent>(HelperComponent))
	{
		SubmittedMaterialParams = ISMHelper->SubmitCollectedPSOPrecache(static_cast<uint64>(ActivePrecache.Entry.PSOPrecacheParamsData), ActivePrecache.RequestIDs, GraphEvents);
	}
	else if (UPredictiveVisibilityStaticMeshPSOPrecacheComponent* StaticMeshHelper = Cast<UPredictiveVisibilityStaticMeshPSOPrecacheComponent>(HelperComponent))
	{
		SubmittedMaterialParams = StaticMeshHelper->SubmitCollectedPSOPrecache(static_cast<uint64>(ActivePrecache.Entry.PSOPrecacheParamsData), ActivePrecache.RequestIDs, GraphEvents);
	}

	if (SubmittedMaterialParams <= 0)
	{
		return false;
	}

	ActivePrecache.HelperComponent = HelperComponent;
	ActivePrecache.SubmittedMaterialParams = SubmittedMaterialParams;
	ActivePrecache.ExpireTimeSeconds = CurrentTimeSeconds + Settings.PSOPrecacheRetentionSeconds;
	ActivePrecache.bSubmitted = true;
	ActivePrecache.bPendingAssetLoad = false;
	ActivePSOPrecacheComponentRefs.Add(HelperComponent);
	return true;
}

void UPredictiveVisibilityRuntimeSubsystem::ReleaseActivePSOPrecacheAt(int32 Index)
{
	if (!ActivePSOPrecaches.IsValidIndex(Index))
	{
		return;
	}

	FActivePSOPrecache& ActivePrecache = ActivePSOPrecaches[Index];
	if (ActivePrecache.Handle.IsValid())
	{
		ActivePrecache.Handle->ReleaseHandle();
	}

#if UE_WITH_PSO_PRECACHING
	if (!ActivePrecache.RequestIDs.IsEmpty())
	{
		ReleasePSOPrecacheData(ActivePrecache.RequestIDs);
	}
#endif

	if (ActivePrecache.HelperComponent)
	{
		ActivePSOPrecacheComponentRefs.Remove(ActivePrecache.HelperComponent);
	}

	ActivePSOPrecaches.RemoveAtSwap(Index, 1, EAllowShrinking::No);
}

void UPredictiveVisibilityRuntimeSubsystem::LogRuntimeDebugState(const FVector& Position, const FVector& Velocity, const FRotator& ViewRotation) const
{
	const UPredictiveVisibilitySettings* Settings = GetDefault<UPredictiveVisibilitySettings>();
	if (!Settings)
	{
		return;
	}

	const FAdaptivePredictionKey Key = BuildAdaptiveKey(Position, Velocity, ViewRotation);
	const int32 DirectionBucketCount = FMath::Clamp(Settings->DirectionBucketCount, 1, 255);
	const uint8 DirectionBucket = PredictiveVisibility::QuantizeDirectionBucket(ViewRotation, DirectionBucketCount);
	const FPredictiveVisibilityBakeRecord* Record = BakeData ? BakeData->FindNearestRecord(Position, DirectionBucket) : nullptr;

	TArray<FName> AdaptiveCells;
	if (AdaptiveCache)
	{
		AdaptiveCache->QueryTopFutureCells(
			Key,
			MaxAdaptiveCandidates,
			MinimumAdaptiveConfidence,
			AdaptiveCells);
	}

	const int32 PreviewCount = FMath::Clamp(Settings->MaxLoggedCandidatesPerUpdate, 0, 8);
	const int32 BakeCellCount = Record ? Record->PotentiallyRelevantCellNames.Num() : 0;
	const int32 BakeActorCount = Record ? Record->PotentiallyVisibleActors.Num() : 0;
	const int32 BakeHLODCount = Record ? Record->PotentiallyRelevantHLODActors.Num() : 0;
	const int32 BakeMeshCount = Record ? Record->PotentiallyVisibleStaticMeshes.Num() : 0;
	const int32 BakeMaterialCount = Record ? Record->PotentiallyVisibleMaterials.Num() : 0;
	const int32 BakePSOCount = Record ? Record->PotentiallyVisiblePSOPrecacheEntries.Num() : 0;

	UE_LOG(LogPredictiveVisibility, Log, TEXT("Runtime debug Cell=%s Direction=%d Speed=%d SpeedCmS=%.1f BakeData=%s BakeRecord=%s BakeCells=%d BakeActors=%d BakeHLODs=%d BakeMeshes=%d BakeMaterials=%d BakePSOs=%d AdaptiveCells=%d PendingObservations=%d ActivePSOs=%d"),
		*Key.CurrentCellName.ToString(),
		static_cast<int32>(Key.DirectionBucket),
		static_cast<int32>(Key.SpeedBucket),
		Velocity.Size(),
		BakeData ? *BakeData->GetPathName() : TEXT("None"),
		Record ? TEXT("true") : TEXT("false"),
		BakeCellCount,
		BakeActorCount,
		BakeHLODCount,
		BakeMeshCount,
		BakeMaterialCount,
		BakePSOCount,
		AdaptiveCells.Num(),
		PendingObservations.Num(),
		ActivePSOPrecaches.Num());

	if (PreviewCount > 0)
	{
		if (Record && BakeCellCount > 0)
		{
			UE_LOG(LogPredictiveVisibility, Log, TEXT("  BakeCellPreview=%s"),
				*BuildNamePreview(Record->PotentiallyRelevantCellNames, PreviewCount));
		}

		if (!AdaptiveCells.IsEmpty())
		{
			UE_LOG(LogPredictiveVisibility, Log, TEXT("  AdaptiveCellPreview=%s"),
				*BuildNamePreview(AdaptiveCells, PreviewCount));
		}
	}
}
