// Copyright Calming Current

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MorphToSkeletonInterface.generated.h"

class UMorphToSkeletonComponent;
// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UMorphToSkeletonInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class MORPHTOSKELETON_API IMorphToSkeletonInterface
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
	UMorphToSkeletonComponent* GetMorphToSkeletonComponent() const;
};
