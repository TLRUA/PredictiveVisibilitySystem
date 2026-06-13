#include "PredictiveVisibilityBakeVolume.h"

#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"

APredictiveVisibilityBakeVolume::APredictiveVisibilityBakeVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	BoundsComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("Bounds"));
	SetRootComponent(BoundsComponent);

	BoundsComponent->Mobility = EComponentMobility::Movable;
	BoundsComponent->InitBoxExtent(FVector(500.0f, 500.0f, 500.0f));
	BoundsComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	BoundsComponent->SetGenerateOverlapEvents(false);
	BoundsComponent->SetHiddenInGame(true);
	BoundsComponent->SetCanEverAffectNavigation(false);
	BoundsComponent->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_No;
	BoundsComponent->bSelectable = true;
	BoundsComponent->bDrawOnlyIfSelected = false;
	BoundsComponent->bUseEditorCompositing = true;

	// Keep the bake boundary loaded in World Partition editor sessions; it is not a runtime streaming target.
	SetIsSpatiallyLoaded(false);
	bIsEditorOnlyActor = true;
	SetCanBeDamaged(false);
}

FBox APredictiveVisibilityBakeVolume::GetBakeBounds() const
{
	if (BoundsComponent)
	{
		return BoundsComponent->Bounds.GetBox();
	}

	return GetComponentsBoundingBox(true);
}
