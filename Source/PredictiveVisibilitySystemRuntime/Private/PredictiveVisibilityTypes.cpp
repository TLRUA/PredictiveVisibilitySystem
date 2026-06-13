#include "PredictiveVisibilityTypes.h"

#include "Misc/Crc.h"

bool FPredictiveVisibilityPSOPrecacheEntry::IsValid() const
{
	return StaticMeshPath.IsValid() && !StaticMeshPath.IsSubobject();
}

FName FPredictiveVisibilityPSOPrecacheEntry::GetStableKey() const
{
	return StableKey.IsNone() ? BuildStableKey() : StableKey;
}

FName FPredictiveVisibilityPSOPrecacheEntry::BuildStableKey() const
{
	if (!IsValid())
	{
		return NAME_None;
	}

	FString KeySource = FString::Printf(
		TEXT("%d|%s|%llu|%d|%d|%s"),
		static_cast<int32>(ComponentType),
		*StaticMeshPath.ToString(),
		static_cast<uint64>(PSOPrecacheParamsData),
		ForcedLodModel,
		bReverseCulling ? 1 : 0,
		*OverlayMaterialPath.ToString());

	for (const FSoftObjectPath& MaterialPath : MaterialPaths)
	{
		KeySource += TEXT("|");
		KeySource += MaterialPath.ToString();
	}

	const uint32 Hash = FCrc::StrCrc32(*KeySource);
	return FName(*FString::Printf(TEXT("PVS_PSO_%08X"), Hash));
}

namespace PredictiveVisibility
{
	FName MakeSpatialCellName(const FVector& Position, float CellSize)
	{
		const float SafeCellSize = FMath::Max(CellSize, 1.0f);
		const int32 X = FMath::FloorToInt(Position.X / SafeCellSize);
		const int32 Y = FMath::FloorToInt(Position.Y / SafeCellSize);
		const int32 Z = FMath::FloorToInt(Position.Z / SafeCellSize);
		return FName(*FString::Printf(TEXT("PVS_X%d_Y%d_Z%d"), X, Y, Z));
	}

	bool TryGetSpatialCellCenter(FName CellName, float CellSize, FVector& OutCellCenter)
	{
		FString CellString = CellName.ToString();
		if (!CellString.RemoveFromStart(TEXT("PVS_X")))
		{
			return false;
		}

		FString XString;
		FString Remainder;
		if (!CellString.Split(TEXT("_Y"), &XString, &Remainder))
		{
			return false;
		}

		FString YString;
		FString ZString;
		if (!Remainder.Split(TEXT("_Z"), &YString, &ZString))
		{
			return false;
		}

		int32 X = 0;
		int32 Y = 0;
		int32 Z = 0;
		if (!LexTryParseString(X, *XString)
			|| !LexTryParseString(Y, *YString)
			|| !LexTryParseString(Z, *ZString))
		{
			return false;
		}

		const float SafeCellSize = FMath::Max(CellSize, 1.0f);
		OutCellCenter = FVector(
			(static_cast<float>(X) + 0.5f) * SafeCellSize,
			(static_cast<float>(Y) + 0.5f) * SafeCellSize,
			(static_cast<float>(Z) + 0.5f) * SafeCellSize);
		return true;
	}

	uint8 QuantizeDirectionBucket(const FRotator& ViewRotation, int32 DirectionBucketCount)
	{
		const int32 SafeBucketCount = FMath::Clamp(DirectionBucketCount, 1, 255);
		const float NormalizedYaw = FMath::Fmod(ViewRotation.Yaw + 360.0f, 360.0f);
		const float BucketSize = 360.0f / static_cast<float>(SafeBucketCount);
		return static_cast<uint8>(FMath::Clamp(FMath::FloorToInt(NormalizedYaw / BucketSize), 0, SafeBucketCount - 1));
	}

	uint8 QuantizeSpeedBucket(float Speed, const TArray<float>& SpeedBucketThresholds)
	{
		const float AbsSpeed = FMath::Max(0.0f, Speed);
		for (int32 Index = 0; Index < SpeedBucketThresholds.Num(); ++Index)
		{
			if (AbsSpeed < SpeedBucketThresholds[Index])
			{
				return static_cast<uint8>(FMath::Clamp(Index, 0, 255));
			}
		}

		return static_cast<uint8>(FMath::Clamp(SpeedBucketThresholds.Num(), 0, 255));
	}
}
