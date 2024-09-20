// 2024 Calming Current Games

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "HAL/Platform.h"
#include "Misc/ScopeLock.h"
#include "MorphToSkeletonComponent.generated.h"

USTRUCT()
struct FBoneWeightMap
{
	GENERATED_BODY()

	TMap<uint32, float> VertexWeight;
};


UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MORPHTOSKELETON_API UMorphToSkeletonComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UMorphToSkeletonComponent();
private:

	static FCriticalSection BoneMapMutex;
	static TMap<USkeletalMesh*, TMap<int32, FBoneWeightMap>> SkeletalMeshBoneWeightMapCache;

protected:

	USkeletalMesh* DuplicatedMesh;


	// Stores morphs already applied in CachedTotalTranslations
	TMap<FName, float> CachedMorphs;



	// Stores Moved Vertices
	TSet<uint32> CachedAffectedVertices;

	// Total Translations in Mesh space that must be converted to local space
	TMap <int32, TTuple<float, FVector3f>> CachedTotalTranslations;

	TMap<int32, FVector3f> RelativeTranslations;

	TArray<FName> PAIRTranslatedBoneNames;
	TArray<FVector3f> PAIRTranslatedBoneTranslations;



protected:
	// Called when the game starts
	virtual void BeginPlay() override;
	
	// Save the weights of each vertex that a bone is associated with to use later
	void SaveBoneWeightMap(USkeletalMeshComponent* SkeletalMeshComponent);

	// Store the amount that each bone should move based on the morph and calculations
	void CacheTranslation(USkeletalMeshComponent* SkeletalMeshComponent, FName MorphTarget, float MorphValue, FSkeletalMeshLODRenderData& LODRenderData, FSkinWeightVertexBuffer* SkinWeightBuffer);

	void CacheTranslations(USkeletalMeshComponent* SkeletalMeshComponent, TMap<FName, float> MorphTargets);

	// Apply the Cached Translations to the skeleton
	void ApplyTranslationsToSkeleton(USkeletalMeshComponent* SkeletalMeshComponent);

	// Apply the mesh morphs to the duplicate mesh that we created.
	void ApplyMorphTargetsToDuplicateMesh(USkeletalMeshComponent* SkeletalMeshComponent, const TMap<FName, float>& MorphTargets);

public:
	// Call to Store information about the mesh you are morphing
	UFUNCTION(BlueprintCallable, Category = "MorphToSkeleton")
	void PreMorphInitialize(USkeletalMeshComponent* SkeletalMeshComponent);

	// Stores the necessary information for the morphs that you want
	UFUNCTION(BlueprintCallable, Category = "MorphToSkeleton|Morphs")
	void SetMorph(USkeletalMeshComponent* SkeletalMeshComponent, FName MorphTarget, float MorphValue);

	UFUNCTION(BlueprintCallable, Category = "MorphToSkeleton|Morphs")
	void SetMorphs(USkeletalMeshComponent* SkeletalMeshComponent, TMap<FName, float> MorphTargets);

	// Set the morph targets and translate the skeleton based on the morphs
	UFUNCTION(BlueprintCallable, Category = "MorphToSkeleton")
	void MorphToSkeleton(USkeletalMeshComponent* SkeletalMeshComponent, TMap<FName, float> MorphTargets);


	UFUNCTION(BlueprintCallable, Category = "MorphToSkeleton")
	const TMap<int32, FVector3f>& GetRelativeTransforms() { return RelativeTranslations; }

	UFUNCTION(BlueprintCallable, Category = "MorphToSkeleton")
	const TArray<FName>& GetTranslatedBoneNames() { return PAIRTranslatedBoneNames; }

	UFUNCTION(BlueprintCallable, Category = "MorphToSkeleton")
	const TArray<FVector3f>& GetTranslatedBoneTranslations() { return PAIRTranslatedBoneTranslations; }
};