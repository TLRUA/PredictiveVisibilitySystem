#include "PredictiveVisibilityEditorSubsystem.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "CollisionQueryParams.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FeedbackContext.h"
#include "Misc/PackageName.h"
#include "Misc/OutputDevice.h"
#include "Misc/ScopedSlowTask.h"
#include "ObjectTools.h"
#include "PSOPrecache.h"
#include "PredictiveVisibilityBakeData.h"
#include "PredictiveVisibilityBakeVolume.h"
#include "PredictiveVisibilityLog.h"
#include "PredictiveVisibilitySettings.h"
#include "PredictiveVisibilityTypes.h"
#include "UObject/SavePackage.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/LoaderAdapter/LoaderAdapterShape.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorLoaderInterface.h"
#include "WorldPartition/WorldPartitionEditorLoaderAdapter.h"

namespace
{
	struct FCollectedActorVisibilityData
	{
		const AActor* Actor = nullptr;
		FVector Location = FVector::ZeroVector;
		float Radius = 0.0f;
		FSoftObjectPath ActorPath;
		TArray<FSoftObjectPath> StaticMeshes;
		TArray<FSoftObjectPath> Materials;
		TArray<FPredictiveVisibilityPSOPrecacheEntry> PSOPrecacheEntries;
		TArray<FName> RuntimeGridNames;
		TArray<FName> DataLayerNames;
		bool bIsHLOD = false;
	};

	class FPredictiveVisibilitySaveErrorOutputDevice final : public FOutputDevice
	{
	public:
		virtual void Serialize(const TCHAR* Data, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (!Message.IsEmpty())
			{
				Message += LINE_TERMINATOR;
			}
			Message += FString::Printf(TEXT("[%s] %s"), *Category.ToString(), Data);
		}

		const FString& GetMessage() const
		{
			return Message;
		}

	private:
		FString Message;
	};

	bool IsActorBakeEligible(const AActor* Actor, FBox& OutBounds)
	{
		if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() || Actor->IsEditorOnly() || Actor->IsHiddenEd())
		{
			return false;
		}

		OutBounds = Actor->GetComponentsBoundingBox(true);
		return OutBounds.IsValid && !OutBounds.GetSize().IsNearlyZero();
	}

	EPredictiveVisibilityPSOComponentType GetPSOComponentType(const AActor* Actor, const UStaticMeshComponent* StaticMeshComponent)
	{
		if (Actor && Actor->IsA<AWorldPartitionHLOD>())
		{
			return EPredictiveVisibilityPSOComponentType::HLOD;
		}

		if (StaticMeshComponent->IsA<UHierarchicalInstancedStaticMeshComponent>())
		{
			return EPredictiveVisibilityPSOComponentType::HierarchicalInstancedStaticMesh;
		}

		if (StaticMeshComponent->IsA<UInstancedStaticMeshComponent>())
		{
			return EPredictiveVisibilityPSOComponentType::InstancedStaticMesh;
		}

		return EPredictiveVisibilityPSOComponentType::StaticMesh;
	}

	void CollectComponentPSOPrecacheEntry(
		const AActor* Actor,
		const UStaticMeshComponent* StaticMeshComponent,
		UStaticMesh* StaticMesh,
		FPredictiveVisibilityPSOPrecacheEntry& OutEntry)
	{
		FPSOPrecacheParams BasePSOParams;
		const_cast<UStaticMeshComponent*>(StaticMeshComponent)->SetupPrecachePSOParams(BasePSOParams);

		OutEntry.ComponentType = GetPSOComponentType(Actor, StaticMeshComponent);
		OutEntry.SourceActorPath = FSoftObjectPath(Actor);
		OutEntry.SourceComponentPath = FSoftObjectPath(StaticMeshComponent);
		OutEntry.StaticMeshPath = FSoftObjectPath(StaticMesh);
		OutEntry.PSOPrecacheParamsData = static_cast<int64>(BasePSOParams.Data);
		OutEntry.ComponentTransform = StaticMeshComponent->GetComponentTransform();
		OutEntry.ForcedLodModel = StaticMeshComponent->ForcedLodModel;
		OutEntry.bReverseCulling = StaticMeshComponent->bReverseCulling;
		OutEntry.OverlayMaterialPath = StaticMeshComponent->GetOverlayMaterial()
			? FSoftObjectPath(StaticMeshComponent->GetOverlayMaterial())
			: FSoftObjectPath();

		const int32 MaterialCount = StaticMeshComponent->GetNumMaterials();
		OutEntry.MaterialPaths.Reserve(MaterialCount);
		for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
		{
			UMaterialInterface* Material = StaticMeshComponent->GetMaterial(MaterialIndex);
			OutEntry.MaterialPaths.Add(Material ? FSoftObjectPath(Material) : FSoftObjectPath());
		}

		OutEntry.StableKey = OutEntry.BuildStableKey();
	}

	void CollectActorData(const AActor* Actor, const FBox& Bounds, FCollectedActorVisibilityData& OutData)
	{
		OutData.Actor = Actor;
		OutData.Location = Bounds.GetCenter();
		OutData.Radius = Bounds.GetExtent().Size();
		OutData.ActorPath = FSoftObjectPath(Actor);
		OutData.bIsHLOD = Actor->IsA<AWorldPartitionHLOD>();

		if (const FName RuntimeGrid = Actor->GetRuntimeGrid(); !RuntimeGrid.IsNone())
		{
			OutData.RuntimeGridNames.AddUnique(RuntimeGrid);
		}

		const TArray<FName> DataLayerNames = Actor->GetDataLayerInstanceNames();
		for (const FName DataLayerName : DataLayerNames)
		{
			if (!DataLayerName.IsNone())
			{
				OutData.DataLayerNames.AddUnique(DataLayerName);
			}
		}

		TInlineComponentArray<UStaticMeshComponent*> StaticMeshComponents;
		Actor->GetComponents(StaticMeshComponents);
		for (const UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
		{
			if (!StaticMeshComponent)
			{
				continue;
			}

			if (UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh())
			{
				OutData.StaticMeshes.AddUnique(FSoftObjectPath(StaticMesh));

				FPredictiveVisibilityPSOPrecacheEntry PSOEntry;
				CollectComponentPSOPrecacheEntry(Actor, StaticMeshComponent, StaticMesh, PSOEntry);
				if (PSOEntry.IsValid())
				{
					OutData.PSOPrecacheEntries.AddUnique(PSOEntry);
				}
			}

			const int32 MaterialCount = StaticMeshComponent->GetNumMaterials();
			for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
			{
				if (UMaterialInterface* Material = StaticMeshComponent->GetMaterial(MaterialIndex))
				{
					OutData.Materials.AddUnique(FSoftObjectPath(Material));
				}
			}
		}
	}

	bool HasLineOfSightToActor(const UWorld* World, const FCollectedActorVisibilityData& ActorData, const FVector& SamplePosition)
	{
		if (!World || !ActorData.Actor)
		{
			return true;
		}

		FHitResult Hit;
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PredictiveVisibilityBakeLineOfSight), true, ActorData.Actor);
		QueryParams.bReturnPhysicalMaterial = false;

		// This is an optional best-effort editor bake test. It uses public collision queries, not renderer occlusion.
		const bool bHitOccluder = World->LineTraceSingleByObjectType(
			Hit,
			SamplePosition,
			ActorData.Location,
			FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects),
			QueryParams);

		return !bHitOccluder;
	}

	bool IsActorPotentiallyRelevant(const FCollectedActorVisibilityData& ActorData, const FVector& SamplePosition, const FVector& ForwardVector, float MaxDistance, float HalfFovCosine)
	{
		const FVector ToActor = ActorData.Location - SamplePosition;
		const float EffectiveDistance = FMath::Max(0.0f, ToActor.Size() - ActorData.Radius);
		if (EffectiveDistance > MaxDistance)
		{
			return false;
		}

		if (ToActor.IsNearlyZero())
		{
			return true;
		}

		const FVector DirectionToActor = ToActor.GetSafeNormal();
		return FVector::DotProduct(ForwardVector, DirectionToActor) >= HalfFovCosine;
	}
}

void UPredictiveVisibilityEditorSubsystem::BakeCurrentWorld()
{
	if (!GEditor)
	{
		return;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		UE_LOG(LogPredictiveVisibility, Warning, TEXT("BakeCurrentWorld failed: no editor world."));
		return;
	}

	const UPredictiveVisibilitySettings* Settings = GetDefault<UPredictiveVisibilitySettings>();
	FPredictiveVisibilityBakeOptions Options;
	if (Settings)
	{
		Options.SampleSpacing = Settings->SampleSpacing;
		Options.MaxPredictionDistance = Settings->MaxPredictionDistance;
		Options.FOVDegrees = Settings->FOVDegrees;
		Options.DirectionBucketCount = Settings->DirectionBucketCount;
		Options.MaxSampleCount = Settings->MaxSampleCount;
	}

	BakeWorld(World, Options);
}

UPredictiveVisibilityBakeData* UPredictiveVisibilityEditorSubsystem::BakeWorld(UWorld* World, const FPredictiveVisibilityBakeOptions& Options)
{
	if (!World)
	{
		UE_LOG(LogPredictiveVisibility, Warning, TEXT("BakeWorld failed: World is null."));
		return nullptr;
	}

	FBox BakeBounds(ForceInit);
	if (!ResolveBakeBounds(World, Options, BakeBounds))
	{
		UE_LOG(LogPredictiveVisibility, Warning, TEXT("BakeWorld failed: no valid bake bounds. Place APredictiveVisibilityBakeVolume or configure fallback bounds."));
		return nullptr;
	}

	const float SampleSpacing = FMath::Max(UPredictiveVisibilitySettings::MetersToCentimeters(Options.SampleSpacing), 100.0f);
	const float MaxPredictionDistance = FMath::Max(UPredictiveVisibilitySettings::MetersToCentimeters(Options.MaxPredictionDistance), 100.0f);
	const float FOVDegrees = FMath::Clamp(Options.FOVDegrees, 5.0f, 179.0f);
	const int32 DirectionBucketCount = FMath::Clamp(Options.DirectionBucketCount, 1, 255);
	const int32 MaxSampleCount = FMath::Max(Options.MaxSampleCount, 1);
	const UPredictiveVisibilitySettings* Settings = GetDefault<UPredictiveVisibilitySettings>();
	const bool bUseLineOfSightTrace = Settings && Settings->bUseLineOfSightTraceDuringBake;
	const float RuntimeSpatialCellSize = Settings ? Settings->GetRuntimeSpatialCellSizeCm() : 10000.0f;

	AutoLoadWorldPartitionRegionForBake(World, BakeBounds);

	TArray<FVector> SamplePositions;
	for (float X = BakeBounds.Min.X; X <= BakeBounds.Max.X && SamplePositions.Num() < MaxSampleCount; X += SampleSpacing)
	{
		for (float Y = BakeBounds.Min.Y; Y <= BakeBounds.Max.Y && SamplePositions.Num() < MaxSampleCount; Y += SampleSpacing)
		{
			SamplePositions.Add(FVector(X, Y, BakeBounds.GetCenter().Z));
		}
	}

	if (SamplePositions.IsEmpty())
	{
		SamplePositions.Add(BakeBounds.GetCenter());
	}

	TArray<FCollectedActorVisibilityData> ActorData;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		FBox ActorBounds(ForceInit);
		if (!IsActorBakeEligible(Actor, ActorBounds))
		{
			continue;
		}

		FCollectedActorVisibilityData Data;
		CollectActorData(Actor, ActorBounds, Data);
		ActorData.Add(MoveTemp(Data));
	}

	UE_LOG(LogPredictiveVisibility, Log, TEXT("Bake setup BoundsMin=%s BoundsMax=%s SamplePositions=%d DirectionBuckets=%d LoadedEligibleActors=%d UseLineOfSightTrace=%s"),
		*BakeBounds.Min.ToString(),
		*BakeBounds.Max.ToString(),
		SamplePositions.Num(),
		DirectionBucketCount,
		ActorData.Num(),
		bUseLineOfSightTrace ? TEXT("true") : TEXT("false"));

	TArray<FPredictiveVisibilityBakeRecord> Records;
	Records.Reserve(SamplePositions.Num() * DirectionBucketCount);

	const int32 TotalWork = SamplePositions.Num() * DirectionBucketCount;
	FScopedSlowTask SlowTask(TotalWork, NSLOCTEXT("PredictiveVisibility", "BakeCurrentWorld", "Baking predictive visibility data"));
	SlowTask.MakeDialog(true);

	const float HalfFovRadians = FMath::DegreesToRadians(FOVDegrees * 0.5f);
	const float HalfFovCosine = FMath::Cos(HalfFovRadians);

	for (const FVector& SamplePosition : SamplePositions)
	{
		for (int32 DirectionBucket = 0; DirectionBucket < DirectionBucketCount; ++DirectionBucket)
		{
			SlowTask.EnterProgressFrame(1.0f);
			if (SlowTask.ShouldCancel())
			{
				UE_LOG(LogPredictiveVisibility, Warning, TEXT("Predictive visibility bake cancelled."));
				return nullptr;
			}

			const float Yaw = (360.0f * static_cast<float>(DirectionBucket)) / static_cast<float>(DirectionBucketCount);
			const FRotator SampleRotation(0.0f, Yaw, 0.0f);
			const FVector ForwardVector = SampleRotation.Vector();

			FPredictiveVisibilityBakeRecord Record;
			Record.SamplePosition = SamplePosition;
			Record.DirectionBucket = static_cast<uint8>(DirectionBucket);
			Record.SampleRotation = SampleRotation;
			Record.SampleRadius = MaxPredictionDistance;

			for (const FCollectedActorVisibilityData& Data : ActorData)
			{
				if (!IsActorPotentiallyRelevant(Data, SamplePosition, ForwardVector, MaxPredictionDistance, HalfFovCosine))
				{
					continue;
				}

				if (bUseLineOfSightTrace && !HasLineOfSightToActor(World, Data, SamplePosition))
				{
					continue;
				}

				Record.PotentiallyVisibleActors.AddUnique(Data.ActorPath);
				if (Data.bIsHLOD)
				{
					Record.PotentiallyRelevantHLODActors.AddUnique(Data.ActorPath);
				}

				for (const FSoftObjectPath& MeshPath : Data.StaticMeshes)
				{
					Record.PotentiallyVisibleStaticMeshes.AddUnique(MeshPath);
				}

				for (const FSoftObjectPath& MaterialPath : Data.Materials)
				{
					Record.PotentiallyVisibleMaterials.AddUnique(MaterialPath);
				}

				for (const FPredictiveVisibilityPSOPrecacheEntry& PSOEntry : Data.PSOPrecacheEntries)
				{
					Record.PotentiallyVisiblePSOPrecacheEntries.AddUnique(PSOEntry);
				}

				for (const FName RuntimeGridName : Data.RuntimeGridNames)
				{
					Record.RuntimeGridNames.AddUnique(RuntimeGridName);
				}

				for (const FName DataLayerName : Data.DataLayerNames)
				{
					Record.DataLayerNames.AddUnique(DataLayerName);
				}

				// v1 intentionally uses plugin-owned spatial keys instead of private World Partition runtime cell APIs.
				Record.PotentiallyRelevantCellNames.AddUnique(PredictiveVisibility::MakeSpatialCellName(Data.Location, RuntimeSpatialCellSize));
			}

			Record.DebugVisibleActorCount = Record.PotentiallyVisibleActors.Num();
			Records.Add(MoveTemp(Record));
		}
	}

	UPredictiveVisibilityBakeData* SavedAsset = SaveBakeDataAsset(World, Options, BakeBounds, ActorData.Num(), Records);
	if (SavedAsset)
	{
		UE_LOG(LogPredictiveVisibility, Log, TEXT("Predictive visibility bake saved. Records=%d CollectedActors=%d TotalRelevantActorRefs=%d TotalPSOPrecacheEntries=%d Asset=%s"),
			Records.Num(),
			SavedAsset->DebugCollectedActorCount,
			SavedAsset->DebugTotalRelevantActorRefs,
			SavedAsset->DebugTotalPSOPrecacheEntries,
			*SavedAsset->GetPathName());
	}
	return SavedAsset;
}

bool UPredictiveVisibilityEditorSubsystem::ResolveBakeBounds(UWorld* World, const FPredictiveVisibilityBakeOptions& Options, FBox& OutBounds) const
{
	OutBounds.Init();

	for (TActorIterator<APredictiveVisibilityBakeVolume> It(World); It; ++It)
	{
		APredictiveVisibilityBakeVolume* Volume = *It;
		if (!IsValid(Volume) || Volume->IsActorBeingDestroyed())
		{
			continue;
		}

		const FBox VolumeBounds = Volume->GetBakeBounds();
		if (VolumeBounds.IsValid && !VolumeBounds.GetSize().IsNearlyZero())
		{
			OutBounds += VolumeBounds;
		}
	}

	if (OutBounds.IsValid)
	{
		return true;
	}

	if (Options.bUseExplicitBakeBounds)
	{
		OutBounds = FBox(
			UPredictiveVisibilitySettings::MetersToCentimeters(Options.BakeBoundsMin),
			UPredictiveVisibilitySettings::MetersToCentimeters(Options.BakeBoundsMax));
		return OutBounds.IsValid && !OutBounds.GetSize().IsNearlyZero();
	}

	const UPredictiveVisibilitySettings* Settings = GetDefault<UPredictiveVisibilitySettings>();
	if (Settings && Settings->bUseFallbackBakeBounds)
	{
		OutBounds = FBox(Settings->GetFallbackBakeBoundsMinCm(), Settings->GetFallbackBakeBoundsMaxCm());
		return OutBounds.IsValid && !OutBounds.GetSize().IsNearlyZero();
	}

	return false;
}

bool UPredictiveVisibilityEditorSubsystem::AutoLoadWorldPartitionRegionForBake(UWorld* World, const FBox& BakeBounds) const
{
	const UPredictiveVisibilitySettings* Settings = GetDefault<UPredictiveVisibilitySettings>();
	if (!Settings || !Settings->bAutoLoadWorldPartitionRegionForBake)
	{
		return false;
	}

	if (!World || !BakeBounds.IsValid)
	{
		return false;
	}

	UWorldPartition* WorldPartition = World->GetWorldPartition();
	if (!WorldPartition)
	{
		return false;
	}

	const FVector BoundsSize = BakeBounds.GetSize();
	const float AreaSquareMeters = (BoundsSize.X * BoundsSize.Y) / 10000.0f;
	if (AreaSquareMeters > Settings->MaxAutoLoadWorldPartitionRegionAreaSquareMeters)
	{
		UE_LOG(LogPredictiveVisibility, Warning, TEXT("Skipped auto-loading World Partition bake region. Area=%.1f m2 Limit=%.1f m2 BoundsMin=%s BoundsMax=%s"),
			AreaSquareMeters,
			Settings->MaxAutoLoadWorldPartitionRegionAreaSquareMeters,
			*BakeBounds.Min.ToString(),
			*BakeBounds.Max.ToString());
		return false;
	}

	UWorldPartitionEditorLoaderAdapter* EditorLoaderAdapter = WorldPartition->CreateEditorLoaderAdapter<FLoaderAdapterShape>(
		World,
		BakeBounds,
		TEXT("Predictive Visibility Bake Region"));

	if (!EditorLoaderAdapter || !EditorLoaderAdapter->GetLoaderAdapter())
	{
		UE_LOG(LogPredictiveVisibility, Warning, TEXT("Failed to create World Partition editor loader adapter for predictive visibility bake."));
		return false;
	}

	EditorLoaderAdapter->GetLoaderAdapter()->SetUserCreated(true);
	EditorLoaderAdapter->GetLoaderAdapter()->Load();

	UE_LOG(LogPredictiveVisibility, Log, TEXT("Auto-loaded World Partition bake region. Area=%.1f m2 BoundsMin=%s BoundsMax=%s"),
		AreaSquareMeters,
		*BakeBounds.Min.ToString(),
		*BakeBounds.Max.ToString());

	return true;
}

UPredictiveVisibilityBakeData* UPredictiveVisibilityEditorSubsystem::SaveBakeDataAsset(
	UWorld* World,
	const FPredictiveVisibilityBakeOptions& Options,
	const FBox& BakeBounds,
	int32 CollectedActorCount,
	const TArray<FPredictiveVisibilityBakeRecord>& Records) const
{
	const FString WorldPackageName = World ? World->GetOutermost()->GetName() : TEXT("UnknownWorld");
	const FString MapName = ObjectTools::SanitizeObjectName(FPackageName::GetShortName(WorldPackageName));
	const FString AssetName = FString::Printf(TEXT("BakeData_%s"), *MapName);
	const FString PackageName = FString::Printf(TEXT("/Game/PredictiveVisibility/%s"), *AssetName);

	auto CreateOrUpdateAndSave = [&](const FString& InPackageName, const FString& InAssetName, UPredictiveVisibilityBakeData*& OutBakeData) -> bool
	{
		const FString PackageFileName = FPackageName::LongPackageNameToFilename(InPackageName, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(PackageFileName), true);

		UPackage* Package = CreatePackage(*InPackageName);
		if (!Package)
		{
			UE_LOG(LogPredictiveVisibility, Warning, TEXT("Failed to create package %s"), *InPackageName);
			return false;
		}

		Package->FullyLoad();

		bool bCreatedAsset = false;
		UPredictiveVisibilityBakeData* BakeData = FindObject<UPredictiveVisibilityBakeData>(Package, *InAssetName);
		if (!BakeData)
		{
			BakeData = NewObject<UPredictiveVisibilityBakeData>(
				Package,
				UPredictiveVisibilityBakeData::StaticClass(),
				*InAssetName,
				RF_Public | RF_Standalone | RF_Transactional);
			bCreatedAsset = true;
		}

		if (!BakeData)
		{
			UE_LOG(LogPredictiveVisibility, Warning, TEXT("Failed to create predictive visibility bake asset %s.%s"), *InPackageName, *InAssetName);
			return false;
		}

		BakeData->Modify();
		BakeData->SourceWorldPath = WorldPackageName;
		BakeData->BakeTime = FDateTime::UtcNow();
		BakeData->SampleSpacing = Options.SampleSpacing;
		BakeData->MaxPredictionDistance = Options.MaxPredictionDistance;
		BakeData->FOVDegrees = Options.FOVDegrees;
		BakeData->DirectionBucketCount = FMath::Clamp(Options.DirectionBucketCount, 1, 255);
		BakeData->NumSamples = Records.Num();
		BakeData->Records = Records;
		BakeData->DebugBakeBoundsMin = BakeBounds.Min / UPredictiveVisibilitySettings::CentimetersPerMeter;
		BakeData->DebugBakeBoundsMax = BakeBounds.Max / UPredictiveVisibilitySettings::CentimetersPerMeter;
		BakeData->DebugCollectedActorCount = CollectedActorCount;
		BakeData->DebugTotalRelevantActorRefs = 0;
		BakeData->DebugTotalPSOPrecacheEntries = 0;
		for (const FPredictiveVisibilityBakeRecord& Record : Records)
		{
			BakeData->DebugTotalRelevantActorRefs += Record.DebugVisibleActorCount;
			BakeData->DebugTotalPSOPrecacheEntries += Record.PotentiallyVisiblePSOPrecacheEntries.Num();
		}

		if (bCreatedAsset)
		{
			FAssetRegistryModule::AssetCreated(BakeData);
		}
		Package->MarkPackageDirty();

		FPredictiveVisibilitySaveErrorOutputDevice SaveErrors;
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_None;
		SaveArgs.Error = &SaveErrors;

		const bool bPackageOkToSave = !GEditor || GEditor->IsPackageOKToSave(Package, PackageFileName, &SaveErrors);
		const bool bSaved = bPackageOkToSave
			&& (GEditor
				? GEditor->SavePackage(Package, BakeData, *PackageFileName, SaveArgs)
				: UPackage::SavePackage(Package, BakeData, *PackageFileName, SaveArgs));

		if (!bSaved)
		{
			UE_LOG(LogPredictiveVisibility, Warning, TEXT("Failed to save predictive visibility bake package %s. Details: %s"),
				*PackageFileName,
				!SaveErrors.GetMessage().IsEmpty() ? *SaveErrors.GetMessage() : TEXT("No detailed save error was reported."));
			return false;
		}

		OutBakeData = BakeData;
		return true;
	};

	UPredictiveVisibilityBakeData* SavedBakeData = nullptr;
	if (CreateOrUpdateAndSave(PackageName, AssetName, SavedBakeData))
	{
		return SavedBakeData;
	}

	const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	const FString FallbackAssetName = FString::Printf(TEXT("%s_%s"), *AssetName, *Timestamp);
	const FString FallbackPackageName = FString::Printf(TEXT("/Game/PredictiveVisibility/%s"), *FallbackAssetName);
	if (CreateOrUpdateAndSave(FallbackPackageName, FallbackAssetName, SavedBakeData))
	{
		UE_LOG(LogPredictiveVisibility, Warning, TEXT("Saved predictive visibility bake to fallback timestamped asset %s after the default asset save failed."),
			*SavedBakeData->GetPathName());
		return SavedBakeData;
	}

	return nullptr;
}
