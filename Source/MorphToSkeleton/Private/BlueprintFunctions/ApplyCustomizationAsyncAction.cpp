// Copyright Calming Current


#include "BlueprintFunctions/ApplyCustomizationAsyncAction.h"

UApplyCustomizationAsyncAction* UApplyCustomizationAsyncAction::ApplyCustomizationAsync(UMorphToSkeletonComponent* InMorphComponent, USkeletalMeshComponent* InTargetMesh)
{
	auto Action = NewObject<UApplyCustomizationAsyncAction>();
	Action->MorphComponent = InMorphComponent;
	Action->TargetMesh = InTargetMesh;

	return Action;
}

void UApplyCustomizationAsyncAction::Activate()
{
	// Validate inputs
	if (!MorphComponent || !TargetMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("Invalid MorphComponent or TargetMesh in ApplyCustomizationAsyncAction"));
		OnCompleted.Broadcast();
		return;
	}

	if (!IsValid(MorphComponent) || !IsValid(TargetMesh))
	{
		UE_LOG(LogTemp, Warning, TEXT("MorphComponent or TargetMesh is not valid in ApplyCustomizationAsyncAction"));
		OnCompleted.Broadcast();
		return;
	}

	// Bind the delegate before starting any async work
	MorphComponent->OnCustomizationApplied.AddDynamic(this, &UApplyCustomizationAsyncAction::OnComponentFinished);

	// Set up a timeout as a safety measure
	if (UWorld* World = TargetMesh->GetWorld())
	{
		World->GetTimerManager().SetTimer(TimeoutHandle, [this]()
			{
				UE_LOG(LogTemp, Warning, TEXT("ApplyCustomizationAsync timed out after 10 seconds"));
				CleanupAndComplete();
			}, 10.0f, false); // 10 second timeout
	}

	// start the async customization work
	MorphComponent->ApplyCustomizationAsync(TargetMesh);
	
}

void UApplyCustomizationAsyncAction::OnComponentFinished()
{
	UE_LOG(LogTemp, Verbose, TEXT("ApplyCustomizationAsyncAction completed"));
	CleanupAndComplete();
}

void UApplyCustomizationAsyncAction::CleanupAndComplete()
{
	// Clear the timeout timer
	if (TimeoutHandle.IsValid())
	{
		if (UWorld* World = TargetMesh ? TargetMesh->GetWorld() : nullptr)
		{
			World->GetTimerManager().ClearTimer(TimeoutHandle);
		}
		TimeoutHandle.Invalidate();
	}

	// Unbind delegate
	if (MorphComponent && IsValid(MorphComponent))
	{
		MorphComponent->OnCustomizationApplied.RemoveDynamic(this, &UApplyCustomizationAsyncAction::OnComponentFinished);
	}

	// Broadcast completion
	if (OnCompleted.IsBound())
	{
		OnCompleted.Broadcast();
	}
}
