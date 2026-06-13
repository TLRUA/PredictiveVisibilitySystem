#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PredictiveVisibilityBakeVolume.generated.h"

class UBoxComponent;

UCLASS(BlueprintType, HideCategories = (Collision, Physics, Rendering, Replication, Input, Actor))
class PREDICTIVEVISIBILITYSYSTEMRUNTIME_API APredictiveVisibilityBakeVolume : public AActor
{
	GENERATED_BODY()

public:
	APredictiveVisibilityBakeVolume(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Predictive Visibility")
	TObjectPtr<UBoxComponent> BoundsComponent;

	UFUNCTION(BlueprintPure, Category = "Predictive Visibility")
	FBox GetBakeBounds() const;

	// The volume is only used to constrain editor bake sampling and should not affect packaged gameplay.
	virtual bool IsEditorOnly() const override { return true; }
	virtual bool IsSelectable() const override { return true; }
};
