// Copyright Calming Current

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "MorphToSkeletonComponent.h"
#include "TimerManager.h"
#include "ApplyCustomizationAsyncAction.generated.h"


DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnApplyCustomizationCompleted);
/**
 * 
 */
UCLASS()
class MORPHTOSKELETON_API UApplyCustomizationAsyncAction : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()
	
public:

	/** Output pin for Blueprint, fires when async customization is complete */
	UPROPERTY(BlueprintAssignable)
	FOnApplyCustomizationCompleted OnCompleted;

	/** Call this from Blueprint, returns the node */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "MorphToSkeleton")
	static UApplyCustomizationAsyncAction* ApplyCustomizationAsync(UMorphToSkeletonComponent* InMorphComponent, USkeletalMeshComponent* InTargetMesh);

	// UBlueprintAsyncActionBase interface
	virtual void Activate() override;

private:

	/** Target component to operate on */
	UPROPERTY()
	UMorphToSkeletonComponent* MorphComponent;

	/** Target skeletal mesh to customize */
	UPROPERTY()
	USkeletalMeshComponent* TargetMesh;

	/** Timer handle for timeout */
	FTimerHandle TimeoutHandle;

	UFUNCTION()
	void OnComponentFinished();

	void CleanupAndComplete();

};
