// 2024 Calming Current Games

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "HAL/Platform.h"
#include "Misc/ScopeLock.h"
#include "Quickhull/QuickHull.hpp"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "MorphToSkeletonComponent.generated.h"


DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCustomizationApplied);

USTRUCT()
struct FBoneWeightMap
{
	GENERATED_BODY()

	TMap<uint32, float> VertexWeight;
};


struct FAsyncTaskData
{
	FReferenceSkeleton RefSkeleton = FReferenceSkeleton();
	FTransform ComponentToWorld = FTransform();
	TArray<FTransform> BoneTransforms;
	TMap<int32, TArray<FVector>> OriginalVerticesByBone;
};

struct FPreparedHullData
{
	FName BoneName;
	TArray<FVector> HullVertices;
};


struct FKConvexElem;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MORPHTOSKELETON_API UMorphToSkeletonComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UMorphToSkeletonComponent();
private:
	

	bool bInitialized = false;
	static FCriticalSection BoneMapMutex;
	static TMap<USkeleton*, TMap<int32, FBoneWeightMap>> SkeletalMeshBoneWeightMapCache;
	static TMap<USkeleton*, TMap<int32, TSet<int32>>> SkeletalMeshHighestWeightVertexMapCache;

	TAtomic<int32> PendingAsyncCooks{ 0 };
	TWeakObjectPtr<USkeletalMeshComponent> PendingCookOwner;

public:

	UPROPERTY(BlueprintAssignable)
	FOnCustomizationApplied OnCustomizationApplied;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MorphToSkeleton|Settings")
	int32 MaxInfluenceBones = 2;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MorphToSkeleton|Settings")
	float MinWeightThreshold = .1f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MorphToSkeleton|Settings")
	bool ForceRemap = false;

protected:
	//USkeletalMesh* DuplicatedMesh;


	UPROPERTY(Transient)
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, TWeakObjectPtr<UPhysicsAsset>> PhysicsAssetInstances;

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
	virtual void BeginDestroy() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
	// Save the weights of each vertex that a bone is associated with to use later
	void SaveBoneWeightMap(USkeletalMeshComponent* SkeletalMeshComponent);

	// Save the vertices that have the highest weight associated with the bone to use later
	void SaveHighestWeightMap(USkeletalMeshComponent* SkeletalMeshComponent);

	// Save vertices that are weighted to a certain bone
	void SaveBoneInfluenceMap(USkeletalMeshComponent* SkeletalMeshComponent);

	void CleanInvalidPhysicsAssetInstances();

	// Store the amount that each bone should move based on the morph and calculations
	void CacheTranslation(USkeletalMeshComponent* SkeletalMeshComponent, FName MorphTarget, float MorphValue, FSkeletalMeshLODRenderData& LODRenderData, FSkinWeightVertexBuffer* SkinWeightBuffer);

	void CacheTranslations(USkeletalMeshComponent* SkeletalMeshComponent, TMap<FName, float> MorphTargets);

	void StoreBoneMorphTranslations(USkeletalMeshComponent* SkeletalMeshComponent);
	
	// PHYSICS ASSET
	// Create a vertices for a convex hull
	TArray<FVector> ComputeConvexHull(const TArray<FVector>& InputVertices);
	FKConvexElem CreateFKConvexElemFromVertices(const TArray<FVector>& HullVertices);
	
	void UpdateBodyConvexHull(UBodySetup* BodySetup, FName BoneName, const TArray<FVector>& BoneSpaceVertices);

	void UpdatePhysicsForMorphedBones(USkeletalMeshComponent* SkeletalMeshComponent, const TMap<int32, TArray<FVector>>& MorphedVerticesByBone);
	void UpdatePhysicsAsset(USkeletalMeshComponent* SkeletalMeshComponent);

	void GetMorphedVerticesByBone(USkeletalMeshComponent* SkeletalMeshComponent, const TMap<FName, float>& CustomizationData, TMap<int32, TArray<FVector>>& OutMorphedVertices);
	void GetOriginalVerticesByBone(USkeletalMeshComponent* SkeletalMeshComponent, TMap<int32, TArray<FVector>>& OutVertices);

	UPhysicsAsset* DuplicatePhysicsAsset(UPhysicsAsset* SourceAsset);

	void ProcessHullsAsync(TSharedPtr<FAsyncTaskData> TaskData, TArray<FPreparedHullData>& OutPreparedHulls);
	void ApplyPreparedHulls(const TArray<FPreparedHullData>& PreparedHulls);
	void FinishCustomizationApplication();
	void RestoreComponentState(USkeletalMeshComponent* TargetMesh);

public:
	// Call to Store information about the mesh you are morphing
	UFUNCTION(BlueprintCallable, Category = "MorphToSkeleton")
	void PreMorphInitialize(USkeletalMeshComponent* SkeletalMeshComponent);

	UFUNCTION(BlueprintCallable, Category = "MorphToSkeleton")
	void ApplyCharacterCustomization(USkeletalMeshComponent* TargetMesh, const TMap<FName, float>& MorphData);

	UFUNCTION(BlueprintCallable, Category = "MorphToSkeleton")
	void ApplyCustomization(USkeletalMeshComponent* TargetMesh);

	UFUNCTION(BlueprintCallable, Category = "MorphToSkeleton")
	void ApplyCustomizationAsync(USkeletalMeshComponent* TargetMesh);

	// Stores the necessary information for the morphs that you want
	UFUNCTION(BlueprintCallable, Category = "MorphToSkeleton|Morphs")
	void SetMorph(USkeletalMeshComponent* SkeletalMeshComponent, FName MorphTarget, float MorphValue);

	UFUNCTION(BlueprintCallable, Category = "MorphToSkeleton|Morphs")
	void SetMorphs(USkeletalMeshComponent* SkeletalMeshComponent, TMap<FName, float> MorphTargets);
	
	UPhysicsAsset* GetOrCreatePhysicsAssetInstance(USkeletalMeshComponent* TargetMesh);

	void SetPhysicsAssetInstance(USkeletalMeshComponent* TargetMesh, UPhysicsAsset* PhysicsAsset);

	UFUNCTION(BlueprintCallable, Category = "MorphToSkeleton")
	const TMap<int32, FVector3f>& GetRelativeTransforms() { return RelativeTranslations; }

	UFUNCTION(BlueprintCallable, Category = "MorphToSkeleton")
	const TArray<FName>& GetTranslatedBoneNames() { return PAIRTranslatedBoneNames; }

	UFUNCTION(BlueprintCallable, Category = "MorphToSkeleton")
	const TArray<FVector3f>& GetTranslatedBoneTranslations() { return PAIRTranslatedBoneTranslations; }
};