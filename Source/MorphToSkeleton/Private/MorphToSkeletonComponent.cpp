// 2024 Calming Current Games


#include "MorphToSkeletonComponent.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "RenderUtils.h"
#include "Editor.h"
#include "Async/ParallelFor.h"
#include "EditorFramework/AssetImportData.h"

// Static Variable Initialization
FCriticalSection UMorphToSkeletonComponent::BoneMapMutex;
TMap<USkeletalMesh*, TMap<int32, FBoneWeightMap>> UMorphToSkeletonComponent::SkeletalMeshBoneWeightMapCache;

// Sets default values for this component's properties
UMorphToSkeletonComponent::UMorphToSkeletonComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = false;

	// ...
}


// Called when the game starts
void UMorphToSkeletonComponent::BeginPlay()
{
	Super::BeginPlay();

	// ...
	
}



void UMorphToSkeletonComponent::SaveBoneWeightMap(USkeletalMeshComponent* SkeletalMeshComponent)
{
	USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMeshAsset();
	if (!SkeletalMesh->IsValidLowLevelFast())
	{
		UE_LOG(LogTemp, Error, TEXT("SkeletalMesh is null."));
		return;
	}
	{
		FScopeLock Lock(&BoneMapMutex);
		if (SkeletalMeshBoneWeightMapCache.Contains(SkeletalMesh))
		{
			return; //Bone weight map already exists for this skeletal mesh
		}
	}

	FSkeletalMeshLODRenderData& LODRenderData = SkeletalMeshComponent->GetSkeletalMeshAsset()->GetResourceForRendering()->LODRenderData[0];
	FSkinWeightVertexBuffer* SkinWeightBuffer = &LODRenderData.SkinWeightVertexBuffer;
	TArray<FSkinWeightInfo> SkinWeightInfo;
	SkinWeightBuffer->GetSkinWeights(SkinWeightInfo);

	TMap<int32, FBoneWeightMap> BoneMapVertexWeights;

		
	ParallelFor(LODRenderData.RenderSections.Num(), [&](int32 SectionIndex)
		{
			const FSkelMeshRenderSection& Section = LODRenderData.RenderSections[SectionIndex];
			for (uint32 i = Section.BaseVertexIndex; i < Section.BaseVertexIndex + Section.NumVertices; i++)
			{
				for (uint32 InfluenceIndex = 0; InfluenceIndex < SkinWeightBuffer->GetMaxBoneInfluences(); ++InfluenceIndex)
				{
					int32 BoneIndex = SkinWeightBuffer->GetBoneIndex(i, InfluenceIndex);
					if (BoneIndex == INDEX_NONE)
					{
						continue;
					}

					if (BoneIndex < 0 || BoneIndex >= Section.BoneMap.Num())
					{
						continue;
					}

					int32 ActualBone = Section.BoneMap[BoneIndex];
					uint16 RawWeight = SkinWeightBuffer->GetBoneWeight(i, InfluenceIndex);
					float Weight = RawWeight / 65535.0f;

					if (Weight <= 0.f)
					{
						continue;
					}
					// Locking mechanism to prevent race conditions
					{
						FScopeLock Lock(&BoneMapMutex);
						FBoneWeightMap& WeightMap = BoneMapVertexWeights.FindOrAdd(ActualBone);
						WeightMap.VertexWeight.Add(i, Weight);
					}
				}
			}
		});
	{
		FScopeLock Lock(&BoneMapMutex);
		SkeletalMeshBoneWeightMapCache.Add(SkeletalMesh, BoneMapVertexWeights);
	}
}


void UMorphToSkeletonComponent::CacheTranslation(USkeletalMeshComponent* SkeletalMeshComponent, FName MorphTarget, float MorphValue, FSkeletalMeshLODRenderData& LODRenderData, FSkinWeightVertexBuffer* SkinWeightBuffer)
{

	if (FMath::IsNearlyZero(MorphValue))
	{
		return;  // Skip morph targets with zero weight
	}

	UMorphTarget* Morph = SkeletalMeshComponent->GetSkeletalMeshAsset()->FindMorphTarget(MorphTarget);
	if (!Morph)
	{
		return;  // Skip if the morph target is not found
	}

	TArray<FMorphTargetLODModel> MorphLOD = Morph->GetMorphLODModels();
	if (MorphLOD.Num() == 0)
	{
		return;  // Skip if no LOD models
	}

	TArray<FMorphTargetDelta> MorphTargetDeltas = MorphLOD[0].Vertices;
	const TArray<int32>& SectionIndices = MorphLOD[0].SectionIndices;

	for (int32 SectionIndex = 0; SectionIndex < LODRenderData.RenderSections.Num(); SectionIndex++)
	{
		if (!SectionIndices.Contains(SectionIndex))
		{
			continue;  // Skip sections not affected by the morph target
		}

		const FSkelMeshRenderSection& Section = LODRenderData.RenderSections[SectionIndex];

		for (const FMorphTargetDelta& Delta : MorphTargetDeltas)
		{
			uint32 VertexIndex = Delta.SourceIdx;
			FVector3f PositionDelta = Delta.PositionDelta * MorphValue;

			if (VertexIndex < Section.BaseVertexIndex || VertexIndex >= Section.BaseVertexIndex + Section.NumVertices)
			{
				continue;  // Skip vertices out of bounds
			}

			bool bAlreadyAdded = false;
			CachedAffectedVertices.FindOrAdd(VertexIndex, &bAlreadyAdded);

			for (uint32 InfluenceIndex = 0; InfluenceIndex < SkinWeightBuffer->GetMaxBoneInfluences(); ++InfluenceIndex)
			{
				int32 BoneIndex = SkinWeightBuffer->GetBoneIndex(VertexIndex, InfluenceIndex);
				if (BoneIndex == INDEX_NONE)
				{
					continue;
				}
				if (BoneIndex < 0 || BoneIndex >= Section.BoneMap.Num())
				{
					continue;
				}

				int32 ActualBone = Section.BoneMap[BoneIndex];
				float Weight = SkinWeightBuffer->GetBoneWeight(VertexIndex, InfluenceIndex) / 65535.0f;
				FVector3f TotalTransform = PositionDelta * Weight;

				TTuple<float, FVector3f>& TranslationData = CachedTotalTranslations.FindOrAdd(ActualBone);
				if (!bAlreadyAdded)
				{
					TranslationData.Get<0>() += Weight;
				}
				TranslationData.Get<1>() += TotalTransform;
			}
		}
	}
}

void UMorphToSkeletonComponent::CacheTranslations(USkeletalMeshComponent* SkeletalMeshComponent, TMap<FName, float> MorphTargets)
{
	FSkeletalMeshLODRenderData& LODRenderData = SkeletalMeshComponent->GetSkeletalMeshAsset()->GetResourceForRendering()->LODRenderData[0];
	FSkinWeightVertexBuffer* SkinWeightBuffer = &LODRenderData.SkinWeightVertexBuffer;

	for (const TPair<FName, float>& MorphTargetPair : MorphTargets)
	{

		FName MorphTargetName = MorphTargetPair.Key;
		float MorphWeight = MorphTargetPair.Value;

		if (CachedMorphs.Contains(MorphTargetName))
		{
			UE_LOG(LogTemp, Warning, TEXT("MorphTarget Already Applied To Translations: %s"), *MorphTargetName.ToString());
			continue;  // Skip if morph has already been cached
		}

		if (FMath::IsNearlyZero(MorphWeight))
		{
			continue;  // Skip morph targets with zero weight
		}

		UMorphTarget* MorphTarget = SkeletalMeshComponent->GetSkeletalMeshAsset()->FindMorphTarget(MorphTargetName);
		if (!MorphTarget)
		{
			continue;  // Skip if the morph target is not found
		}

		TArray<FMorphTargetLODModel> MorphLOD = MorphTarget->GetMorphLODModels();
		if (MorphLOD.Num() == 0)
		{
			continue;  // Skip if no LOD models
		}

		TArray<FMorphTargetDelta> MorphTargetDeltas = MorphLOD[0].Vertices;
		const TArray<int32>& SectionIndices = MorphLOD[0].SectionIndices;

		for (int32 SectionIndex = 0; SectionIndex < LODRenderData.RenderSections.Num(); SectionIndex++)
		{
			if (!SectionIndices.Contains(SectionIndex))
			{
				continue;  // Skip sections not affected by the morph target
			}

			const FSkelMeshRenderSection& Section = LODRenderData.RenderSections[SectionIndex];

			for (const FMorphTargetDelta& Delta : MorphTargetDeltas)
			{
				uint32 VertexIndex = Delta.SourceIdx;
				FVector3f PositionDelta = Delta.PositionDelta * MorphWeight;

				if (VertexIndex < Section.BaseVertexIndex || VertexIndex >= Section.BaseVertexIndex + Section.NumVertices)
				{
					continue;  // Skip vertices out of bounds
				}

				bool bAlreadyAdded = false;
				CachedAffectedVertices.FindOrAdd(VertexIndex, &bAlreadyAdded);

				for (uint32 InfluenceIndex = 0; InfluenceIndex < SkinWeightBuffer->GetMaxBoneInfluences(); ++InfluenceIndex)
				{
					int32 BoneIndex = SkinWeightBuffer->GetBoneIndex(VertexIndex, InfluenceIndex);
					if (BoneIndex == INDEX_NONE)
					{
						continue;
					}
					if (BoneIndex < 0 || BoneIndex >= Section.BoneMap.Num())
					{
						continue;
					}

					int32 ActualBone = Section.BoneMap[BoneIndex];
					float Weight = SkinWeightBuffer->GetBoneWeight(VertexIndex, InfluenceIndex) / 65535.0f;
					FVector3f TotalTransform = PositionDelta * Weight;

					TTuple<float, FVector3f>& TranslationData = CachedTotalTranslations.FindOrAdd(ActualBone);
					if (!bAlreadyAdded)
					{
						TranslationData.Get<0>() += Weight;
					}
					TranslationData.Get<1>() += TotalTransform;

				}


			}
		}
		CachedMorphs.Add(MorphTargetName, MorphWeight);
	}
}

void UMorphToSkeletonComponent::ApplyTranslationsToSkeleton(USkeletalMeshComponent* SkeletalMeshComponent)
{
	
	for (const auto& BoneElem : CachedTotalTranslations)
	{
		int32 BoneIndex = BoneElem.Key;

		// Retrieve vertex weight map for this bone
		if (SkeletalMeshBoneWeightMapCache.Contains(SkeletalMeshComponent->GetSkeletalMeshAsset()))
		{
			TMap<int32, FBoneWeightMap>& BoneWeightMap = SkeletalMeshBoneWeightMapCache[SkeletalMeshComponent->GetSkeletalMeshAsset()];

			if (BoneWeightMap.Contains(BoneIndex))
			{
				const FBoneWeightMap& VertexWeightMap = BoneWeightMap[BoneIndex];

				for (const TPair<uint32, float>& VertexWeightPair : VertexWeightMap.VertexWeight)
				{
					uint32 VertexIndex = VertexWeightPair.Key;

					if (!CachedAffectedVertices.Contains(VertexIndex))
					{

						float ZeroWeight = VertexWeightPair.Value;
						//UE_LOG(LogTemp, Warning, TEXT("Empty Vertex Being Added with weight, %f"), ZeroWeight);
						FVector3f TotalTransform = FVector3f::ZeroVector;

						TTuple<float, FVector3f>& TranslationData = CachedTotalTranslations.FindOrAdd(BoneIndex);
						TranslationData.Get<0>() += ZeroWeight;
						TranslationData.Get<1>() += TotalTransform;
					}
				}
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Map does not contain index"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Cache does not contain SkeletalMesh"));
		}
	}




	// Compute relative translations
	// this is relatively cheap
	for (const auto& Elem : CachedTotalTranslations)
	{
		int32 BoneIndex = Elem.Key;
		float TotalWeight = Elem.Value.Get<0>();
		FVector3f TotalTransform = Elem.Value.Get<1>();

		int32 ParentBoneIndex = SkeletalMeshComponent->GetSkeletalMeshAsset()->GetRefSkeleton().GetParentIndex(BoneIndex);

		FVector3f WeightedTransform = (TotalWeight > 0) ? (TotalTransform / TotalWeight) : FVector3f::ZeroVector;

		if (ParentBoneIndex != INDEX_NONE && CachedTotalTranslations.Contains(ParentBoneIndex))
		{
			float ParentTotalWeight = CachedTotalTranslations[ParentBoneIndex].Get<0>();
			FVector3f ParentTotalTransform = CachedTotalTranslations[ParentBoneIndex].Get<1>();

			FVector3f ParentWeightedTransform = (ParentTotalWeight > 0) ? (ParentTotalTransform / ParentTotalWeight) : FVector3f::ZeroVector;

			FVector3f RelativeTransform = WeightedTransform - ParentWeightedTransform;

			RelativeTranslations.Add(BoneIndex, WeightedTransform - ParentWeightedTransform);

			UE_LOG(LogTemp, Warning, TEXT("Bone: %s, RelativeTransform: %s"), *SkeletalMeshComponent->GetSkeletalMeshAsset()->RefSkeleton.GetBoneName(BoneIndex).ToString(), *RelativeTransform.ToString());
			PAIRTranslatedBoneNames.Add(SkeletalMeshComponent->GetSkeletalMeshAsset()->RefSkeleton.GetBoneName(BoneIndex));
			PAIRTranslatedBoneTranslations.Add(WeightedTransform - ParentWeightedTransform);

			//UE_LOG(LogTemp, Warning, TEXT("Bone: %s, RelativeTransform: %s"), *SkeletalMeshComponent->GetSkeletalMeshAsset()->RefSkeleton.GetBoneName(BoneIndex).ToString(), *RelativeTransform.ToString());
		}
		else
		{
			RelativeTranslations.Add(BoneIndex, WeightedTransform);
			PAIRTranslatedBoneNames.Add(SkeletalMeshComponent->GetSkeletalMeshAsset()->RefSkeleton.GetBoneName(BoneIndex));
			PAIRTranslatedBoneTranslations.Add(WeightedTransform);
		}
	}

	
	


	// Duplicate the skeletal mesh to avoid altering the original
	if (!DuplicatedMesh || !DuplicatedMesh->IsValidLowLevelFast())
	{
		DuplicatedMesh = DuplicateObject(SkeletalMeshComponent->GetSkeletalMeshAsset(), nullptr);
	}

	// Get the number of bones in the duplicated mesh's reference skeleton
	const int32 NumBones = DuplicatedMesh->RefSkeleton.GetRawBoneNum();

	// Create an array to hold the new bone transforms, initialized to the original reference pose
	TArray<FTransform> FixedTransforms;
	FixedTransforms.AddDefaulted(NumBones);

	const TArray<FTransform>& Pose = DuplicatedMesh->RefSkeleton.GetRefBonePose();

	// Create a skeleton modifier to update the reference pose transforms
	FReferenceSkeletonModifier SkeletonModifier(DuplicatedMesh->RefSkeleton, DuplicatedMesh->Skeleton);

	// Cache the bone world transforms
	TArray<FTransform> BoneWorldTransforms = SkeletalMeshComponent->GetBoneSpaceTransforms();

	//UE_LOG(LogTemp, Warning, TEXT("Component Space Transforms: %s, Transform: %s"), *DuplicatedMesh->RefSkeleton.GetBoneName(0).ToString(), *SkeletalMeshComponent->GetBoneTransform(0).ToString());
	for (const auto& Elem : RelativeTranslations)
	{
		int32 BoneIndex = Elem.Key;
		FVector3f RelativeTranslation = Elem.Value;
		//UE_LOG(LogTemp, Warning, TEXT("Component Space Transforms: %s, Transform: %s"), *DuplicatedMesh->RefSkeleton.GetBoneName(BoneIndex).ToString(), *SkeletalMeshComponent->GetBoneTransform(BoneIndex).ToString());
		//UE_LOG(LogTemp, Warning, TEXT("We want to translate: %s, %s in world space"), *DuplicatedMesh->RefSkeleton.GetBoneName(BoneIndex).ToString(), *RelativeTranslation.ToString());

		FVector BoneSpaceLocation;
		FRotator BoneSpaceRotation;


		if (BoneIndex != 0)
		{
			int32 ParentBoneIndex = SkeletalMeshComponent->GetSkeletalMeshAsset()->GetRefSkeleton().GetParentIndex(BoneIndex);
			FVector WorldSpaceLocation;
			FRotator WorldSpaceRotation;
			SkeletalMeshComponent->TransformFromBoneSpace(SkeletalMeshComponent->GetBoneName(ParentBoneIndex), Pose[BoneIndex].GetLocation(), Pose[BoneIndex].GetRotation().Rotator(), WorldSpaceLocation, WorldSpaceRotation);
			//UE_LOG(LogTemp, Warning, TEXT("Bone World Transforms: %s, Transform: %s, Rotation: %s"), *DuplicatedMesh->RefSkeleton.GetBoneName(BoneIndex).ToString(), *WorldSpaceLocation.ToString(), *WorldSpaceRotation.ToString());

			WorldSpaceLocation += FVector(RelativeTranslation.Y, RelativeTranslation.X * -1.f, RelativeTranslation.Z);

			//UE_LOG(LogTemp, Warning, TEXT("Post Component Space Transforms: %s, Transform: %s in world space"), *DuplicatedMesh->RefSkeleton.GetBoneName(BoneIndex).ToString(), *WorldSpaceLocation.ToString());

			SkeletalMeshComponent->TransformToBoneSpace(SkeletalMeshComponent->GetBoneName(ParentBoneIndex), WorldSpaceLocation, WorldSpaceRotation, BoneSpaceLocation, BoneSpaceRotation);
		}
		else
		{
			BoneSpaceLocation = FVector(RelativeTranslation.Y, RelativeTranslation.X, RelativeTranslation.Z);
			BoneSpaceRotation = Pose[BoneIndex].GetRotation().Rotator();
		}

		FTransform FinalTransform(BoneSpaceRotation, BoneSpaceLocation);

		//UE_LOG(LogTemp, Warning, TEXT("Bone Local Transforms Modified: %s, Transform: %s"), *DuplicatedMesh->RefSkeleton.GetBoneName(BoneIndex).ToString(), *FinalTransform.ToString());

		SkeletonModifier.UpdateRefPoseTransform(BoneIndex, FinalTransform);
	}

	DuplicatedMesh->RefSkeleton.RebuildRefSkeleton(DuplicatedMesh->Skeleton, false);

	// Set the modified skeletal mesh to the skeletal mesh component
	SkeletalMeshComponent->SetSkeletalMesh(DuplicatedMesh, false);
	SkeletalMeshComponent->SetCPUSkinningEnabled(true, true);
	
}


void UMorphToSkeletonComponent::ApplyMorphTargetsToDuplicateMesh(USkeletalMeshComponent* SkeletalMeshComponent, const TMap<FName, float>& MorphTargets)
{
	for (const TPair<FName, float>& MorphTarget : MorphTargets)
	{
		SkeletalMeshComponent->SetMorphTarget(MorphTarget.Key, MorphTarget.Value);
	}
}

void UMorphToSkeletonComponent::PreMorphInitialize(USkeletalMeshComponent* SkeletalMeshComponent)
{
	SaveBoneWeightMap(SkeletalMeshComponent);
}

void UMorphToSkeletonComponent::SetMorph(USkeletalMeshComponent* SkeletalMeshComponent, FName MorphTarget, float MorphValue)
{
	if (!SkeletalMeshComponent)
	{
		return;
	}

	FSkeletalMeshLODRenderData& LODRenderData = SkeletalMeshComponent->GetSkeletalMeshAsset()->GetResourceForRendering()->LODRenderData[0];
	FSkinWeightVertexBuffer* SkinWeightBuffer = &LODRenderData.SkinWeightVertexBuffer;

	// Checks if the MorphTarget already exists. If it does, subtract that from the new value, if not, just add a new entry.
	float& OriginalValueRef = CachedMorphs.FindOrAdd(MorphTarget, 0.f);
	float TranslationWeight = MorphValue - OriginalValueRef;
	OriginalValueRef = MorphValue;

	
	// Cache that new translation from adding the morph
	CacheTranslation(SkeletalMeshComponent, MorphTarget, TranslationWeight, LODRenderData, SkinWeightBuffer);
}

void UMorphToSkeletonComponent::SetMorphs(USkeletalMeshComponent* SkeletalMeshComponent, TMap<FName, float> MorphTargets)
{
	if (!SkeletalMeshComponent)
	{
		return;
	}

	FSkeletalMeshLODRenderData& LODRenderData = SkeletalMeshComponent->GetSkeletalMeshAsset()->GetResourceForRendering()->LODRenderData[0];
	FSkinWeightVertexBuffer* SkinWeightBuffer = &LODRenderData.SkinWeightVertexBuffer;

	for (const TPair<FName, float>& MorphTargetPair : MorphTargets)
	{
		// Checks if the MorphTarget already exists. If it does, subtract that from the new value, if not, just add a new entry.
		float& OriginalValueRef = CachedMorphs.FindOrAdd(MorphTargetPair.Key, 0.f);
		float TranslationWeight = MorphTargetPair.Value - OriginalValueRef;
		OriginalValueRef = MorphTargetPair.Value;

		// Cache that new translation from adding the morph
		CacheTranslation(SkeletalMeshComponent, MorphTargetPair.Key, TranslationWeight, LODRenderData, SkinWeightBuffer);
	}
}


void UMorphToSkeletonComponent::MorphToSkeleton(USkeletalMeshComponent* SkeletalMeshComponent, TMap<FName, float> MorphTargets)
{
	SetMorphs(SkeletalMeshComponent, MorphTargets);

	ApplyTranslationsToSkeleton(SkeletalMeshComponent);

	ApplyMorphTargetsToDuplicateMesh(SkeletalMeshComponent, MorphTargets);
}




