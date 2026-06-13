#include "PredictiveVisibilityAdaptiveCache.h"

void UPredictiveVisibilityAdaptiveCache::UpdateRecord(const FAdaptivePredictionKey& Key, FName FutureCellName)
{
	if (Key.CurrentCellName.IsNone() || FutureCellName.IsNone())
	{
		return;
	}

	FAdaptivePredictionRecord& Record = Records.FindOrAdd(Key);
	Record.TotalSamples = FMath::Max(0, Record.TotalSamples) + 1;
	Record.FutureCellHitCount.FindOrAdd(FutureCellName)++;
	Record.LastUpdateTime = FDateTime::UtcNow();

	int32 BestHitCount = 0;
	for (const TPair<FName, int32>& Pair : Record.FutureCellHitCount)
	{
		BestHitCount = FMath::Max(BestHitCount, Pair.Value);
	}

	Record.Confidence = Record.TotalSamples > 0
		? static_cast<float>(BestHitCount) / static_cast<float>(Record.TotalSamples)
		: 0.0f;
}

void UPredictiveVisibilityAdaptiveCache::QueryTopFutureCells(const FAdaptivePredictionKey& Key, int32 MaxCount, float MinConfidence, TArray<FName>& OutCellNames) const
{
	OutCellNames.Reset();

	const FAdaptivePredictionRecord* Record = Records.Find(Key);
	if (!Record || Record->TotalSamples <= 0)
	{
		return;
	}

	TArray<TPair<FName, int32>> SortedHits;
	for (const TPair<FName, int32>& Pair : Record->FutureCellHitCount)
	{
		const float CellConfidence = static_cast<float>(Pair.Value) / static_cast<float>(Record->TotalSamples);
		if (CellConfidence >= MinConfidence)
		{
			SortedHits.Add(Pair);
		}
	}

	SortedHits.Sort([](const TPair<FName, int32>& Left, const TPair<FName, int32>& Right)
	{
		return Left.Value > Right.Value;
	});

	const int32 Count = FMath::Min(FMath::Max(MaxCount, 0), SortedHits.Num());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		OutCellNames.Add(SortedHits[Index].Key);
	}
}

void UPredictiveVisibilityAdaptiveCache::PruneOldRecords(int32 MaxRecords)
{
	if (MaxRecords <= 0)
	{
		Records.Reset();
		return;
	}

	if (Records.Num() <= MaxRecords)
	{
		return;
	}

	TArray<TPair<FAdaptivePredictionKey, FDateTime>> RecordsByAge;
	RecordsByAge.Reserve(Records.Num());
	for (const TPair<FAdaptivePredictionKey, FAdaptivePredictionRecord>& Pair : Records)
	{
		RecordsByAge.Emplace(Pair.Key, Pair.Value.LastUpdateTime);
	}

	RecordsByAge.Sort([](const TPair<FAdaptivePredictionKey, FDateTime>& Left, const TPair<FAdaptivePredictionKey, FDateTime>& Right)
	{
		return Left.Value < Right.Value;
	});

	const int32 NumToRemove = Records.Num() - MaxRecords;
	for (int32 Index = 0; Index < NumToRemove; ++Index)
	{
		Records.Remove(RecordsByAge[Index].Key);
	}
}
