#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "PredictiveVisibilityTypes.h"
#include "PredictiveVisibilityAdaptiveCache.generated.h"

UCLASS(BlueprintType)
class PREDICTIVEVISIBILITYSYSTEMRUNTIME_API UPredictiveVisibilityAdaptiveCache : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	TMap<FAdaptivePredictionKey, FAdaptivePredictionRecord> Records;

	UFUNCTION(BlueprintCallable, Category = "Predictive Visibility")
	void UpdateRecord(const FAdaptivePredictionKey& Key, FName FutureCellName);

	UFUNCTION(BlueprintCallable, Category = "Predictive Visibility")
	void QueryTopFutureCells(const FAdaptivePredictionKey& Key, int32 MaxCount, float MinConfidence, TArray<FName>& OutCellNames) const;

	UFUNCTION(BlueprintCallable, Category = "Predictive Visibility")
	void PruneOldRecords(int32 MaxRecords);
};
