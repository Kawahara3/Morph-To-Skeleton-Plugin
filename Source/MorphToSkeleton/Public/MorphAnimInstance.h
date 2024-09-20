// 2024 Calming Current Games

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "MorphAnimInstance.generated.h"

/**
 * 
 */
UCLASS()
class MORPHTOSKELETON_API UMorphAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
	
protected:
	void NativePostEvaluateAnimation() override;

};
