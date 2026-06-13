#include "PredictiveVisibilityBakeData.h"

const FPredictiveVisibilityBakeRecord* UPredictiveVisibilityBakeData::FindNearestRecord(const FVector& Position, uint8 DirectionBucket) const
{
	const FPredictiveVisibilityBakeRecord* BestRecord = nullptr;
	float BestDistanceSquared = TNumericLimits<float>::Max();

	for (const FPredictiveVisibilityBakeRecord& Record : Records)
	{
		if (Record.DirectionBucket != DirectionBucket)
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared(Position, Record.SamplePosition);
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestRecord = &Record;
		}
	}

	return BestRecord;
}

bool UPredictiveVisibilityBakeData::BlueprintFindNearestRecord(const FVector& Position, uint8 DirectionBucket, FPredictiveVisibilityBakeRecord& OutRecord) const
{
	if (const FPredictiveVisibilityBakeRecord* Record = FindNearestRecord(Position, DirectionBucket))
	{
		OutRecord = *Record;
		return true;
	}

	return false;
}

void UPredictiveVisibilityBakeData::QueryRecordsAroundPosition(const FVector& Position, float Radius, TArray<FPredictiveVisibilityBakeRecord>& OutRecords) const
{
	OutRecords.Reset();
	const float RadiusSquared = FMath::Square(FMath::Max(Radius, 0.0f));

	for (const FPredictiveVisibilityBakeRecord& Record : Records)
	{
		if (FVector::DistSquared(Position, Record.SamplePosition) <= RadiusSquared)
		{
			OutRecords.Add(Record);
		}
	}
}
