// 2024 Calming Current Games


#include "MorphToSkeletonComponent.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "RenderUtils.h"
#include "Editor.h"
#include "Async/ParallelFor.h"
#include "EditorFramework/AssetImportData.h"

#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/ConvexElem.h"

#include "LatentActions.h"
#include "Engine/World.h"
#include "Async/Async.h"



using namespace quickhull;



// Static Variable Initialization
FCriticalSection UMorphToSkeletonComponent::BoneMapMutex;
TMap<USkeleton*, TMap<int32, FBoneWeightMap>> UMorphToSkeletonComponent::SkeletalMeshBoneWeightMapCache;
TMap<USkeleton*, TMap<int32, TSet<int32>>> UMorphToSkeletonComponent::SkeletalMeshHighestWeightVertexMapCache;


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

}

void UMorphToSkeletonComponent::BeginDestroy()
{
	Super::BeginDestroy();
}

void UMorphToSkeletonComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
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
		if (SkeletalMeshBoneWeightMapCache.Contains(SkeletalMesh->GetSkeleton()))
		{
			UE_LOG(LogTemp, Warning, TEXT("SaveBoneWeightMap Cached Already"));
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
		SkeletalMeshBoneWeightMapCache.Add(SkeletalMesh->GetSkeleton(), BoneMapVertexWeights);
	}
}




void UMorphToSkeletonComponent::SaveHighestWeightMap(USkeletalMeshComponent* SkeletalMeshComponent)
{
	if (!SkeletalMeshComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("Null SkeletalMeshComponent"));
		return;
	}

	USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMeshAsset();
	

	//UE_LOG(LogTemp, Error, TEXT("%s"), *SkeletalMeshComponent->GetName());
	if (!SkeletalMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("SkeletalMesh is null."));
		return;
	}
	if (SkeletalMeshHighestWeightVertexMapCache.Contains(SkeletalMesh->GetSkeleton()))
	{
		UE_LOG(LogTemp, Warning, TEXT("SkeletalMesh Already Cached in SkeletalMeshHighestWeightVertexMapCache"));
		return;
	}
	// Debug: Print skeleton bones
	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
	//UE_LOG(LogTemp, Warning, TEXT("Skeleton has %d bones"), RefSkeleton.GetNum());

	const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
	if (!RenderData || RenderData->LODRenderData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid render data"));
		return;
	}

	const FSkeletalMeshLODRenderData& LODRenderData = RenderData->LODRenderData[0];
	const FSkinWeightVertexBuffer* SkinWeightBuffer = &LODRenderData.SkinWeightVertexBuffer;



	TMap<int32, TSet<int32>> HighestWeightMap;

	for (const FSkelMeshRenderSection& Section : LODRenderData.RenderSections)
	{


		for (uint32 VertIdx = Section.BaseVertexIndex; VertIdx < Section.BaseVertexIndex + Section.NumVertices; ++VertIdx)
		{
			uint16 HighestWeight = 0;
			int32 HighestBone = INDEX_NONE;

			for (uint32 InfluenceIdx = 0; InfluenceIdx < SkinWeightBuffer->GetMaxBoneInfluences(); ++InfluenceIdx)
			{
				const int32 BoneIndex = SkinWeightBuffer->GetBoneIndex(VertIdx, InfluenceIdx);
				const uint16 Weight = SkinWeightBuffer->GetBoneWeight(VertIdx, InfluenceIdx);

				// Debug per vertex if needed:
				// UE_LOG(LogTemp, Verbose, TEXT("Vert %d: Influence %d -> Bone %d (Weight %hu)"), 
				//     VertIdx, InfluenceIdx, BoneIndex, Weight);

				if (BoneIndex != INDEX_NONE &&
					BoneIndex < Section.BoneMap.Num() &&
					Weight > HighestWeight)
				{
					HighestWeight = Weight;
					HighestBone = Section.BoneMap[BoneIndex];
				}
			}

			if (HighestBone != INDEX_NONE)
			{
				HighestWeightMap.FindOrAdd(HighestBone).Add(VertIdx);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("Vertex %d has no valid bone influences!"), VertIdx);
			}
		}
	}



	{
		FScopeLock Lock(&BoneMapMutex);
		SkeletalMeshHighestWeightVertexMapCache.Add(SkeletalMesh->GetSkeleton(), MoveTemp(HighestWeightMap));
	}

}

void UMorphToSkeletonComponent::SaveBoneInfluenceMap(USkeletalMeshComponent* SkeletalMeshComponent)
{
	if (!SkeletalMeshComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("Null SkeletalMeshComponent"));
		return;
	}

	

	USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMeshAsset();
	if (!SkeletalMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("SkeletalMesh is null."));
		return;
	}
	if (SkeletalMeshHighestWeightVertexMapCache.Contains(SkeletalMesh->GetSkeleton()) && !ForceRemap)
	{
		UE_LOG(LogTemp, Error, TEXT("SkeletalMesh has already been mapped."));
		return;
	}
	const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
	if (!RenderData || RenderData->LODRenderData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid render data"));
		return;
	}

	const FSkeletalMeshLODRenderData& LODRenderData = RenderData->LODRenderData[0];
	const FSkinWeightVertexBuffer* SkinWeightBuffer = &LODRenderData.SkinWeightVertexBuffer;
	const int32 MaxBoneInfluences = SkinWeightBuffer->GetMaxBoneInfluences();

	// Debug: Print skeleton bones
	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();

	// New data structure: Map of BoneIndex -> Set of vertices it influences
	TMap<int32, TSet<int32>> BoneInfluenceMap;

	for (const FSkelMeshRenderSection& Section : LODRenderData.RenderSections)
	{

		for (uint32 VertIdx = Section.BaseVertexIndex;
			VertIdx < Section.BaseVertexIndex + Section.NumVertices;
			++VertIdx)
		{
			// Collect all influences for this vertex
			TArray<TPair<int32, uint16>> Influences;

			for (int32 InfluenceIdx = 0; InfluenceIdx < MaxBoneInfluences; ++InfluenceIdx)
			{
				const int32 BoneIndex = SkinWeightBuffer->GetBoneIndex(VertIdx, InfluenceIdx);
				const uint16 Weight = SkinWeightBuffer->GetBoneWeight(VertIdx, InfluenceIdx);

				if (BoneIndex != INDEX_NONE &&
					BoneIndex < Section.BoneMap.Num() &&
					Weight > 0)
				{
					Influences.Add(TPair<int32, uint16>(Section.BoneMap[BoneIndex], Weight));
				}
			}

			// Sort by weight (descending)
			Influences.Sort([](const TPair<int32, uint16>& A, const TPair<int32, uint16>& B) {
				return A.Value > B.Value;
				});

			// Capture top N influences meeting weight threshold
			int32 CapturedInfluences = 0;
			for (const auto& Influence : Influences)
			{
				if (CapturedInfluences >= MaxInfluenceBones) break;

				const float NormalizedWeight = Influence.Value / 255.0f; // Convert 0-255 to 0-1
				if (NormalizedWeight >= MinWeightThreshold)
				{
					BoneInfluenceMap.FindOrAdd(Influence.Key).Add(VertIdx);
					CapturedInfluences++;
				}
			}

			// Fallback: Always include at least the strongest influence
			if (CapturedInfluences == 0 && Influences.Num() > 0)
			{
				BoneInfluenceMap.FindOrAdd(Influences[0].Key).Add(VertIdx);
			}
		}
	}

	// Thread-safe cache update
	{
		FScopeLock Lock(&BoneMapMutex);
		SkeletalMeshHighestWeightVertexMapCache.Add(SkeletalMesh->GetSkeleton(), MoveTemp(BoneInfluenceMap));
	}


}


void UMorphToSkeletonComponent::CleanInvalidPhysicsAssetInstances()
{
	TArray<TWeakObjectPtr<USkeletalMeshComponent>> ToRemove;
	for (auto& Pair : PhysicsAssetInstances)
	{
		if (!Pair.Key.IsValid() || !Pair.Value.IsValid())
			ToRemove.Add(Pair.Key);
	}
	for (auto& Key : ToRemove)
	{
		PhysicsAssetInstances.Remove(Key);
	}
}


void UMorphToSkeletonComponent::CacheTranslation(USkeletalMeshComponent* SkeletalMeshComponent, FName MorphTarget, float MorphValue, FSkeletalMeshLODRenderData& LODRenderData, FSkinWeightVertexBuffer* SkinWeightBuffer)
{

	if (FMath::IsNearlyZero(MorphValue))
	{
		return;  // Skip morph targets with zero weight
	}

	UMorphTarget* Morph = SkeletalMeshComponent->FindMorphTarget(MorphTarget);
	if (!Morph || Morph->GetMorphLODModels().IsEmpty())
	{
		return;
	}

	const FMorphTargetLODModel& MorphLOD = Morph->GetMorphLODModels()[0];
	TArray<FMorphTargetDelta> MorphTargetDeltas = MorphLOD.Vertices;
	const TArray<int32>& SectionIndices = MorphLOD.SectionIndices;


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

		UMorphTarget* Morph = SkeletalMeshComponent->FindMorphTarget(MorphTargetName);
		if (!Morph || Morph->GetMorphLODModels().IsEmpty()) continue;


		const FMorphTargetLODModel& MorphLOD = Morph->GetMorphLODModels()[0];
		TArray<FMorphTargetDelta> MorphTargetDeltas = MorphLOD.Vertices;
		const TArray<int32>& SectionIndices = MorphLOD.SectionIndices;


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
void UMorphToSkeletonComponent::StoreBoneMorphTranslations(USkeletalMeshComponent* SkeletalMeshComponent)
{
	UE_LOG(LogTemp, Warning, TEXT("Storing Relative Translations"));
	USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMeshAsset();
	for (const auto& BoneElem : CachedTotalTranslations)
	{
		int32 BoneIndex = BoneElem.Key;

		// Retrieve vertex weight map for this bone
		if (SkeletalMeshBoneWeightMapCache.Contains(SkeletalMesh->GetSkeleton()))
		{
			TMap<int32, FBoneWeightMap>& BoneWeightMap = SkeletalMeshBoneWeightMapCache[SkeletalMesh->GetSkeleton()];

			if (BoneWeightMap.Contains(BoneIndex))
			{
				const FBoneWeightMap& VertexWeightMap = BoneWeightMap[BoneIndex];

				for (const TPair<uint32, float>& VertexWeightPair : VertexWeightMap.VertexWeight)
				{
					uint32 VertexIndex = VertexWeightPair.Key;

					if (!CachedAffectedVertices.Contains(VertexIndex))
					{

						float ZeroWeight = VertexWeightPair.Value;
						UE_LOG(LogTemp, Warning, TEXT("Empty Vertex Being Added with weight, %f"), ZeroWeight);
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
			PAIRTranslatedBoneNames.Add(SkeletalMeshComponent->GetSkeletalMeshAsset()->GetRefSkeleton().GetBoneName(BoneIndex));
			PAIRTranslatedBoneTranslations.Add(WeightedTransform - ParentWeightedTransform);

			UE_LOG(LogTemp, Warning, TEXT("Bone: %s, RelativeTransform: %s"), *SkeletalMeshComponent->GetSkeletalMeshAsset()->RefSkeleton.GetBoneName(BoneIndex).ToString(), *RelativeTransform.ToString());
		}
		else
		{
			RelativeTranslations.Add(BoneIndex, WeightedTransform);
			PAIRTranslatedBoneNames.Add(SkeletalMeshComponent->GetSkeletalMeshAsset()->GetRefSkeleton().GetBoneName(BoneIndex));
			PAIRTranslatedBoneTranslations.Add(WeightedTransform);
		}
	}
}

TArray<FVector> UMorphToSkeletonComponent::ComputeConvexHull(const TArray<FVector>& InputVertices)
{
	TArray<FVector> HullVertices;

	if (InputVertices.Num() < 4)
	{
		UE_LOG(LogTemp, Warning, TEXT("Not enough vertices (%d) for hull computation"), InputVertices.Num());
		return InputVertices; // Return original if insufficient
	}

	std::vector<Vector3<float>> PointCloud;
	PointCloud.reserve(InputVertices.Num());

	for (const FVector& V : InputVertices)
	{
		PointCloud.emplace_back(V.X, V.Y, V.Z);
	}

	try {
		QuickHull<float> QH;
		auto Hull = QH.getConvexHull(PointCloud, true, false);

		HullVertices.Reserve(Hull.getVertexBuffer().size());
		for (const auto& V : Hull.getVertexBuffer())
		{
			HullVertices.Add(FVector(V.x, V.y, V.z));
		}
	}
	catch (...)
	{
		UE_LOG(LogTemp, Error, TEXT("QuickHull computation failed"));
		return InputVertices; // Fallback
	}

	return HullVertices;
	
}

FKConvexElem UMorphToSkeletonComponent::CreateFKConvexElemFromVertices(const TArray<FVector>& HullVertices)
{
	FKConvexElem ConvexElem;
	if (HullVertices.Num() < 4) // Minimum for 3D hull
	{
		UE_LOG(LogTemp, Warning, TEXT("Insufficient vertices (%d) for convex hull"), HullVertices.Num());
		return ConvexElem;
	}

	ConvexElem.VertexData = HullVertices;
	ConvexElem.UpdateElemBox();
	ConvexElem.SetTransform(FTransform::Identity);
	return ConvexElem;
}

void UMorphToSkeletonComponent::UpdateBodyConvexHull(UBodySetup* BodySetup, FName BoneName, const TArray<FVector>& BoneSpaceVertices)
{
	if (!BodySetup || BoneSpaceVertices.Num() < 4)
	{
		UE_LOG(LogTemp, Verbose, TEXT("Invalid body setup or insufficient vertices (%d) for bone %s"),
			BoneSpaceVertices.Num(), *BoneName.ToString());
		return;
	}

	// Generate convex hull
	TArray<FVector> HullVertices = ComputeConvexHull(BoneSpaceVertices);
	if (HullVertices.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Hull generation failed for bone %s"), *BoneName.ToString());
		return;
	}

	// Create and configure new convex element
	FKConvexElem NewElem;
	NewElem.VertexData = HullVertices;
	NewElem.UpdateElemBox();

	// Critical: Must modify body setup before changes
	BodySetup->Modify();

	// CRITICAL: Set transform to identity (bone space)
	NewElem.SetTransform(FTransform::Identity);

	// Clear existing elements and add new one
	BodySetup->AggGeom.ConvexElems.Empty();
	BodySetup->AggGeom.ConvexElems.Add(NewElem);

	// Set bone mapping - THIS IS WHERE THE BONE ASSOCIATION HAPPENS
	BodySetup->BoneName = BoneName;

	// increment once per BodySetup we're cooking
	if (PendingCookOwner.IsValid())
	{
		++PendingAsyncCooks;
	}

	// Mark physics data as dirty
	BodySetup->InvalidatePhysicsData();
	BodySetup->CreatePhysicsMeshes();
}

void UMorphToSkeletonComponent::UpdatePhysicsForMorphedBones(USkeletalMeshComponent* SkeletalMeshComponent, const TMap<int32, TArray<FVector>>& MorphedVerticesByBone)
{
	if (!IsValid(SkeletalMeshComponent))
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid SkeletalMeshComponent"));
		return;
	}

	UPhysicsAsset* PhysicsAsset = SkeletalMeshComponent->GetPhysicsAsset();
	if (!PhysicsAsset)
	{
		UE_LOG(LogTemp, Error, TEXT("No Physics Asset assigned"));
		return;
	}

	SkeletalMeshComponent->SetEnableAnimation(false);

	const FReferenceSkeleton& RefSkeleton = SkeletalMeshComponent->GetSkeletalMeshAsset()->GetRefSkeleton();
	const FTransform ComponentToWorld = SkeletalMeshComponent->GetComponentTransform();

	for (const auto& BoneVertPair : MorphedVerticesByBone)
	{
		const int32 BoneIndex = BoneVertPair.Key;
		const FName BoneName = RefSkeleton.GetBoneName(BoneIndex);

		// Find existing body setup (skip if none exists)
		UBodySetup* BodySetup = nullptr;
		for (UBodySetup* ExistingSetup : PhysicsAsset->SkeletalBodySetups)
		{
			if (ExistingSetup && ExistingSetup->BoneName == BoneName)
			{
				BodySetup = ExistingSetup;
				break;
			}
		}

		if (!BodySetup)
		{
			UE_LOG(LogTemp, Verbose, TEXT("Skipping bone %s - no existing physics body"), *BoneName.ToString());
			continue;
		}

		// Process vertices for bones with existing physics
		TArray<FVector> BoneSpaceVertices;
		BoneSpaceVertices.Reserve(BoneVertPair.Value.Num());

		for (const FVector& ComponentSpaceVert : BoneVertPair.Value)
		{
			// Convert component space → world space → bone space
			FVector WorldVert = ComponentToWorld.TransformPosition(ComponentSpaceVert);

			FVector BoneSpaceLocation;
			FRotator BoneSpaceRotation;
			SkeletalMeshComponent->TransformToBoneSpace(
				BoneName,
				WorldVert,
				FRotator::ZeroRotator,
				BoneSpaceLocation,
				BoneSpaceRotation
			);

			// Debug validation
			if (BoneSpaceLocation.IsNearlyZero())
			{
				UE_LOG(LogTemp, Warning, TEXT("Zero bone space location for bone %s (world vert: %s)"),
					*BoneName.ToString(),
					*WorldVert.ToString());
			}

			BoneSpaceVertices.Add(BoneSpaceLocation);
		}

		UpdateBodyConvexHull(BodySetup, BoneName, BoneSpaceVertices);
	}

	// Finalize physics update on game thread
	AsyncTask(ENamedThreads::GameThread, [SkeletalMeshComponent]()
		{
			if (IsValid(SkeletalMeshComponent))
			{
				SkeletalMeshComponent->RecreatePhysicsState();
				SkeletalMeshComponent->SetEnableAnimation(true);
				UE_LOG(LogTemp, Log, TEXT("Physics state recreated for morphed bones"));
			}
		});
	
}

void UMorphToSkeletonComponent::UpdatePhysicsAsset(USkeletalMeshComponent* SkeletalMeshComponent)
{
	if (!IsValid(SkeletalMeshComponent))
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid SkeletalMeshComponent"));
		return;
	}

	UPhysicsAsset* PhysicsAsset = SkeletalMeshComponent->GetPhysicsAsset();
	if (!PhysicsAsset)
	{
		UE_LOG(LogTemp, Error, TEXT("No Physics Asset assigned"));
		return;
	}

	SkeletalMeshComponent->SetEnableAnimation(false);

	const FReferenceSkeleton& RefSkeleton = SkeletalMeshComponent->GetSkeletalMeshAsset()->GetRefSkeleton();
	const FTransform ComponentToWorld = SkeletalMeshComponent->GetComponentTransform();

	// Get the original vertex positions for ALL bones
	TMap<int32, TArray<FVector>> OriginalVerticesByBone;
	GetOriginalVerticesByBone(SkeletalMeshComponent, OriginalVerticesByBone);
	PendingCookOwner = SkeletalMeshComponent;
	PendingAsyncCooks = 0;
	// Loop through EVERY bone in the skeleton
	for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNum(); BoneIndex++)
	{
		const FName BoneName = RefSkeleton.GetBoneName(BoneIndex);

		// Find existing body setup (skip if none exists)
		UBodySetup* BodySetup = nullptr;
		for (UBodySetup* ExistingSetup : PhysicsAsset->SkeletalBodySetups)
		{
			if (ExistingSetup && ExistingSetup->BoneName == BoneName)
			{
				BodySetup = ExistingSetup;
				break;
			}
		}

		if (!BodySetup)
		{
			UE_LOG(LogTemp, Verbose, TEXT("Skipping bone %s - no existing physics body"), *BoneName.ToString());
			continue;
		}

		// Get the original vertices for this bone (skip if no vertices)
		if (!OriginalVerticesByBone.Contains(BoneIndex))
		{
			UE_LOG(LogTemp, Verbose, TEXT("Skipping bone %s - no vertex data"), *BoneName.ToString());
			continue;
		}

		const TArray<FVector>& ComponentSpaceVerts = OriginalVerticesByBone[BoneIndex];

		// Process vertices for bones with existing physics
		TArray<FVector> BoneSpaceVertices;
		BoneSpaceVertices.Reserve(ComponentSpaceVerts.Num());

		for (const FVector& ComponentSpaceVert : ComponentSpaceVerts)
		{
			// Convert component space → world space → bone space
			FVector WorldVert = ComponentToWorld.TransformPosition(ComponentSpaceVert);

			FVector BoneSpaceLocation;
			FRotator BoneSpaceRotation;
			SkeletalMeshComponent->TransformToBoneSpace(
				BoneName,
				WorldVert,
				FRotator::ZeroRotator,
				BoneSpaceLocation,
				BoneSpaceRotation
			);

			// Debug validation
			if (BoneSpaceLocation.IsNearlyZero())
			{
				UE_LOG(LogTemp, Warning, TEXT("Zero bone space location for bone %s (world vert: %s)"),
					*BoneName.ToString(),
					*WorldVert.ToString());
			}

			BoneSpaceVertices.Add(BoneSpaceLocation);
		}

		UpdateBodyConvexHull(BodySetup, BoneName, BoneSpaceVertices);
	}


	SkeletalMeshComponent->RecreatePhysicsState();
	SkeletalMeshComponent->SetEnableAnimation(true);
	UE_LOG(LogTemp, Log, TEXT("Physics state recreated for ALL bones"));

}


void UMorphToSkeletonComponent::GetMorphedVerticesByBone(USkeletalMeshComponent* SkeletalMeshComponent, const TMap<FName, float>& CustomizationData, TMap<int32, TArray<FVector>>& OutMorphedVertices)
{
	if (!IsValid(SkeletalMeshComponent)) return;

	USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMeshAsset();
	if (!SkeletalMesh || !SkeletalMeshHighestWeightVertexMapCache.Contains(SkeletalMesh->GetSkeleton()))
	{
		return;
	}

	// Get mesh data - CORRECTED VERSION
	const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
	if (!RenderData || RenderData->LODRenderData.IsEmpty())
	{
		return;
	}

	const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[0];
	const FPositionVertexBuffer& PositionBuffer = LODData.StaticVertexBuffers.PositionVertexBuffer;
	const uint32 VertexCount = PositionBuffer.GetNumVertices();

	// Get bone-vertex mapping
	const TMap<int32, TSet<int32>>& BoneVertMap = SkeletalMeshHighestWeightVertexMapCache.FindChecked(SkeletalMesh->GetSkeleton());

	// Prepare output
	OutMorphedVertices.Empty(BoneVertMap.Num());

	// Initialize morph delta buffer
	TArray<FVector3f> CombinedMorphDeltas;
	CombinedMorphDeltas.SetNumZeroed(VertexCount);

	// Accumulate morph deltas
	for (const TPair<FName, float>& MorphPair : CustomizationData)
	{
		if (FMath::IsNearlyZero(MorphPair.Value)) continue;

		UMorphTarget* Morph = SkeletalMesh->FindMorphTarget(MorphPair.Key);
		if (!Morph || Morph->GetMorphLODModels().IsEmpty()) continue;

		const FMorphTargetLODModel& MorphLOD = Morph->GetMorphLODModels()[0];
		for (const FMorphTargetDelta& Delta : MorphLOD.Vertices)
		{
			if (Delta.SourceIdx < VertexCount)
			{
				CombinedMorphDeltas[Delta.SourceIdx] += Delta.PositionDelta * MorphPair.Value;
			}
		}
	}

	// Compute final positions per bone
	for (const TPair<int32, TSet<int32>>& BonePair : BoneVertMap)
	{
		TArray<FVector>& BoneVertices = OutMorphedVertices.Add(BonePair.Key);
		BoneVertices.Reserve(BonePair.Value.Num());

		for (int32 VertIdx : BonePair.Value)
		{
			if (VertIdx < (int32)VertexCount)
			{
				FVector3f BasePos = PositionBuffer.VertexPosition(VertIdx);
				FVector3f MorphedPos = BasePos + CombinedMorphDeltas[VertIdx];
				BoneVertices.Add(FVector(MorphedPos));
			}
		}
	}
}

void UMorphToSkeletonComponent::GetOriginalVerticesByBone(USkeletalMeshComponent* SkeletalMeshComponent, TMap<int32, TArray<FVector>>& OutVertices)
{
	if (!SkeletalMeshComponent) return;

	USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMeshAsset();
	if (!SkeletalMesh) return;

	USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
	if (!Skeleton) return;

	// Get the bone-vertex mapping (ensure this is cached)
	if (!SkeletalMeshHighestWeightVertexMapCache.Contains(SkeletalMesh->GetSkeleton()))
	{
		UE_LOG(LogTemp, Warning, TEXT("No bone-vertex mapping cache found"));
		return;
	}

	const TMap<int32, TSet<int32>>& BoneVertMap = SkeletalMeshHighestWeightVertexMapCache.FindChecked(SkeletalMesh->GetSkeleton());

	// Get the original vertex positions from the mesh
	const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
	if (!RenderData || RenderData->LODRenderData.IsEmpty()) return;

	const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[0];
	const FPositionVertexBuffer& PositionBuffer = LODData.StaticVertexBuffers.PositionVertexBuffer;
	const uint32 VertexCount = PositionBuffer.GetNumVertices();

	// Extract original vertex positions for each bone
	for (const auto& BonePair : BoneVertMap)
	{
		const int32 BoneIndex = BonePair.Key;
		TArray<FVector>& Vertices = OutVertices.Add(BoneIndex);
		Vertices.Reserve(BonePair.Value.Num());

		for (int32 VertIdx : BonePair.Value)
		{
			if (VertIdx < (int32)VertexCount)
			{
				FVector3f OriginalPos = PositionBuffer.VertexPosition(VertIdx);
				Vertices.Add(FVector(OriginalPos));
			}
		}
	}

}

UPhysicsAsset* UMorphToSkeletonComponent::DuplicatePhysicsAsset(UPhysicsAsset* SourceAsset)
{
	UPhysicsAsset* NewAsset = NewObject<UPhysicsAsset>();
	NewAsset->PreviewSkeletalMesh = SourceAsset->PreviewSkeletalMesh;
	NewAsset->SkeletalBodySetups.Reserve(SourceAsset->SkeletalBodySetups.Num());
	for (USkeletalBodySetup* SourceBody : SourceAsset->SkeletalBodySetups)
	{
		// Create new body setup
		USkeletalBodySetup* NewBody = NewObject<USkeletalBodySetup>(NewAsset);
		// Copy all properties
		NewBody->BoneName = SourceBody->BoneName;
		NewBody->bConsiderForBounds = SourceBody->bConsiderForBounds;
		NewBody->bMeshCollideAll = SourceBody->bMeshCollideAll;
		NewBody->bDoubleSidedGeometry = SourceBody->bDoubleSidedGeometry;
		NewBody->bGenerateNonMirroredCollision = SourceBody->bGenerateNonMirroredCollision;
		NewBody->bGenerateMirroredCollision = SourceBody->bGenerateMirroredCollision;

		// Copy collision geometry
		NewBody->AggGeom = SourceBody->AggGeom;

		// Copy collision settings
		NewBody->CollisionTraceFlag = SourceBody->CollisionTraceFlag;
		NewBody->PhysMaterial = SourceBody->PhysMaterial;
		NewBody->WalkableSlopeOverride = SourceBody->WalkableSlopeOverride;

		// Copy collision response settings
		NewBody->DefaultInstance = SourceBody->DefaultInstance;

		// Build the physics data
		NewBody->InvalidatePhysicsData();
		NewBody->CreatePhysicsMeshes();

		NewAsset->SkeletalBodySetups.Add(NewBody);

	}

	NewAsset->BoundsBodies = SourceAsset->BoundsBodies;

	return NewAsset;
}

void UMorphToSkeletonComponent::ProcessHullsAsync(TSharedPtr<FAsyncTaskData> TaskData, TArray<FPreparedHullData>& OutPreparedHulls)
{
	for (int32 BoneIndex = 0; BoneIndex < TaskData->RefSkeleton.GetNum(); BoneIndex++)
	{
		const FName BoneName = TaskData->RefSkeleton.GetBoneName(BoneIndex);


		if (!TaskData->OriginalVerticesByBone.Contains(BoneIndex)) continue;

		const TArray<FVector>& ComponentSpaceVerts = TaskData->OriginalVerticesByBone[BoneIndex];
		if (ComponentSpaceVerts.Num() < 4)
		{
			continue;
		}

		// Convert to bone space using precomputed transforms
		TArray<FVector> BoneSpaceVertices;
		BoneSpaceVertices.Reserve(ComponentSpaceVerts.Num());

		if (BoneIndex < TaskData->BoneTransforms.Num())
		{
			const FTransform& BoneWorldTransform = TaskData->BoneTransforms[BoneIndex];

			// Check for invalid transforms
			if (BoneWorldTransform.ContainsNaN())
			{
				UE_LOG(LogTemp, Warning, TEXT("Invalid bone transform for %s, skipping"), *BoneName.ToString());
				continue;
			}

			const FTransform WorldToBone = BoneWorldTransform.Inverse();

			for (const FVector& ComponentSpaceVert : ComponentSpaceVerts)
			{
				FVector WorldVert = TaskData->ComponentToWorld.TransformPosition(ComponentSpaceVert);
				FVector BoneSpaceLocation = WorldToBone.TransformPosition(WorldVert);

				// Validate the result
				if (BoneSpaceLocation.ContainsNaN())
				{
					UE_LOG(LogTemp, Warning, TEXT("NaN detected in bone space conversion for bone %s"), *BoneName.ToString());
					continue;
				}

				BoneSpaceVertices.Add(BoneSpaceLocation);
			}
		}

		if (BoneSpaceVertices.Num() < 4)
		{
			continue;
		}

		// Compute convex hull
		TArray<FVector> HullVertices = ComputeConvexHull(BoneSpaceVertices);

		// Validate hull vertices
		bool bValidHull = true;
		for (const FVector& Vertex : HullVertices)
		{
			if (Vertex.ContainsNaN())
			{
				bValidHull = false;
				UE_LOG(LogTemp, Warning, TEXT("NaN detected in hull vertices for bone %s"), *BoneName.ToString());
				break;
			}
		}

		if (!bValidHull || HullVertices.Num() < 4)
		{
			continue;
		}

		// Store the hull data
		FPreparedHullData HullData;
		HullData.BoneName = BoneName;
		HullData.HullVertices = HullVertices;
		OutPreparedHulls.Add(HullData);
	}
}

void UMorphToSkeletonComponent::ApplyPreparedHulls(const TArray<FPreparedHullData>& PreparedHulls)
{
	if (!PendingCookOwner.IsValid()) return;

	USkeletalMeshComponent* TargetMesh = PendingCookOwner.Get();
	UPhysicsAsset* PhysicsAsset = TargetMesh->GetPhysicsAsset();

	if (!PhysicsAsset)
	{
		RestoreComponentState(TargetMesh);
		return;
	}

	// First, check if we need to modify anything
	if (PreparedHulls.Num() == 0)
	{
		RestoreComponentState(TargetMesh);
		return;
	}


	for (const auto& HullData : PreparedHulls)
	{
		const FName& BoneName = HullData.BoneName;
		const TArray<FVector>& HullVertices = HullData.HullVertices;

		UBodySetup* BodySetup = nullptr;
		for (UBodySetup* ExistingSetup : PhysicsAsset->SkeletalBodySetups)
		{
			if (ExistingSetup && ExistingSetup->BoneName == BoneName)
			{
				BodySetup = ExistingSetup;
				break;
			}
		}

		if (!BodySetup) continue;


		// Create the convex element on the game thread
		FKConvexElem ConvexElem;
		ConvexElem.VertexData = HullVertices;
		ConvexElem.UpdateElemBox();
		ConvexElem.SetTransform(FTransform::Identity);

		BodySetup->Modify();
		BodySetup->AggGeom.ConvexElems.Empty();
		BodySetup->AggGeom.ConvexElems.Add(ConvexElem);
		BodySetup->BoneName = BoneName;

		// Add to async cooks
		++PendingAsyncCooks;

		BodySetup->InvalidatePhysicsData();
		BodySetup->CreatePhysicsMeshesAsync(
			FOnAsyncPhysicsCookFinished::CreateLambda([this](bool bSuccess)
				{
					if (PendingCookOwner.IsValid() && (--PendingAsyncCooks) <= 0)
					{
						// Once finished with all async cooks
						FinishCustomizationApplication();
					}
				})
		);
	}

	// If no cooks were started, finish immediately
	if (PendingAsyncCooks == 0)
	{
		FinishCustomizationApplication();
	}
}

void UMorphToSkeletonComponent::FinishCustomizationApplication()
{
	if (USkeletalMeshComponent* TargetMesh = PendingCookOwner.Get())
	{
		// Validate that we're in a good state to finish
		if (!IsValid(TargetMesh) || !IsValid(TargetMesh->GetPhysicsAsset()))
		{
			UE_LOG(LogTemp, Warning, TEXT("Invalid state during FinishCustomizationApplication"));
			PendingCookOwner.Reset();
			return;
		}

		UE_LOG(LogTemp, Log, TEXT("Finishing customization application for %s"), *TargetMesh->GetName());

		// First, ensure physics is completely disabled before recreating
		TargetMesh->SetSimulatePhysics(false);
		TargetMesh->PutAllRigidBodiesToSleep();

		// Wait one frame to ensure all physics operations have settled
		// This helps prevent race conditions with physics cooking
		TargetMesh->GetWorld()->GetTimerManager().SetTimerForNextTick([this, TargetMesh]()
			{
				if (!IsValid(TargetMesh))
				{
					UE_LOG(LogTemp, Warning, TEXT("TargetMesh became invalid during timer callback"));
					return;
				}

				// Validate physics asset state before recreating
				UPhysicsAsset* PhysicsAsset = TargetMesh->GetPhysicsAsset();
				if (PhysicsAsset)
				{
					// Check for any bodies with invalid bounds before recreation
					bool bHasValidBodies = false;
					for (UBodySetup* BodySetup : PhysicsAsset->SkeletalBodySetups)
					{
						if (BodySetup && BodySetup->AggGeom.ConvexElems.Num() > 0)
						{
							for (const FKConvexElem& ConvexElem : BodySetup->AggGeom.ConvexElems)
							{
								// Validate the convex element's bounds
								FBox ElemBox = ConvexElem.ElemBox;
								if (!ElemBox.Min.ContainsNaN() && !ElemBox.Max.ContainsNaN() && ElemBox.IsValid)
								{
									bHasValidBodies = true;
								}
								else
								{
									UE_LOG(LogTemp, Warning, TEXT("Invalid bounds detected for bone %s before recreation"),
										*BodySetup->BoneName.ToString());
								}
							}
						}
					}

					if (!bHasValidBodies)
					{
						UE_LOG(LogTemp, Error, TEXT("No valid physics bodies found, skipping physics recreation"));
						RestoreComponentState(TargetMesh);
						return;
					}
				}

				// Recreate physics state
				TargetMesh->RecreatePhysicsState();

				// Restore component state
				TargetMesh->RefreshBoneTransforms();
				TargetMesh->InvalidateCachedBounds();
				TargetMesh->UpdateBounds();
				TargetMesh->MarkRenderStateDirty();
				TargetMesh->SetEnableAnimation(true);

				UE_LOG(LogTemp, Log, TEXT("Physics state recreated after async cooking"));

				// Broadcast completion
				if (OnCustomizationApplied.IsBound())
				{
					OnCustomizationApplied.Broadcast();
				}
			});
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("PendingCookOwner is invalid in FinishCustomizationApplication"));
	}

	PendingCookOwner.Reset();
}

void UMorphToSkeletonComponent::RestoreComponentState(USkeletalMeshComponent* TargetMesh)
{
	if (IsValid(TargetMesh))
	{
		TargetMesh->SetEnableAnimation(true);
		// Don't automatically re-enable physics as it might cause issues
	}
	PendingCookOwner.Reset();
}


void UMorphToSkeletonComponent::PreMorphInitialize(USkeletalMeshComponent* SkeletalMeshComponent)
{
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, SkeletalMeshComponent]()
		{
			SaveBoneWeightMap(SkeletalMeshComponent);
			SaveBoneInfluenceMap(SkeletalMeshComponent);
			SaveHighestWeightMap(SkeletalMeshComponent);

		});

	
}

void UMorphToSkeletonComponent::ApplyCharacterCustomization(USkeletalMeshComponent* TargetMesh, const TMap<FName, float>& MorphData)
{
	
	if (!TargetMesh || MorphData.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("Invalid customization request"));
		return;
	}

	// Ensure cache exists (no morph application needed)
	if (!bInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("NotInitialized"));
		
		SaveHighestWeightMap(TargetMesh);
		
		
		
		bInitialized = true;
	}

	// Skip visual morph application - Mutable already handled it
	UE_LOG(LogTemp, Verbose, TEXT("Processing physics for Mutable-generated morphs"));

	// Get morphed vertices
	TMap<int32, TArray<FVector>> MorphedVertices;

	GetMorphedVerticesByBone(TargetMesh, MorphData, MorphedVertices);

	// Filter bones with significant changes
	TMap<int32, TArray<FVector>> FilteredVertices;
	const float MinMovementThreshold = 0.1f; // 1mm threshold

	for (auto& Pair : MorphedVertices)
	{
		TArray<FVector> SignificantVerts;
		SignificantVerts.Reserve(Pair.Value.Num());

		for (const FVector& Vert : Pair.Value)
		{
			if (Vert.SizeSquared() > FMath::Square(MinMovementThreshold))
			{
				SignificantVerts.Add(Vert);
			}
		}

		if (SignificantVerts.Num() > 0)
		{
			FilteredVertices.Add(Pair.Key, MoveTemp(SignificantVerts));
		}
	}

	// Update physics if needed
	if (FilteredVertices.Num() > 0)
	{
		UpdatePhysicsForMorphedBones(TargetMesh, FilteredVertices);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("No significant morph changes - physics unchanged"));
	}
	TargetMesh->SetEnableAnimation(true);
}

void UMorphToSkeletonComponent::ApplyCustomization(USkeletalMeshComponent* TargetMesh)
{

		// Ensure cache exists
		if (!bInitialized)
		{
			SaveHighestWeightMap(TargetMesh);
			bInitialized = true;
		}
		UE_LOG(LogTemp, Verbose, TEXT("Processing physics for Mutable-generated morphs"));
		UpdatePhysicsAsset(TargetMesh);

		TargetMesh->SetEnableAnimation(true);

}
void UMorphToSkeletonComponent::ApplyCustomizationAsync(USkeletalMeshComponent* TargetMesh)
{
	if (!IsValid(TargetMesh)) return;

	
	// Store a strong reference to prevent garbage collection
	PendingCookOwner = TargetMesh;

	// Capture all necessary data on the game thread
	const USkeletalMesh* SkeletalMesh = TargetMesh->GetSkeletalMeshAsset();
	if (!SkeletalMesh)
	{
		RestoreComponentState(TargetMesh);
		return;
	}

	const FReferenceSkeleton RefSkeleton = SkeletalMesh->GetRefSkeleton();
	const FTransform ComponentToWorld = TargetMesh->GetComponentTransform();

	TArray<FTransform> BoneTransforms;
	const int32 NumBones = RefSkeleton.GetNum();
	BoneTransforms.SetNum(NumBones);

	for (int32 BoneIndex = 0; BoneIndex < NumBones; BoneIndex++)
	{
		BoneTransforms[BoneIndex] = TargetMesh->GetBoneTransform(BoneIndex, ComponentToWorld);
	}

	TMap<int32, TArray<FVector>> OriginalVerticesByBone;
	GetOriginalVerticesByBone(TargetMesh, OriginalVerticesByBone);

	// Disable animation and physics
	TargetMesh->SetEnableAnimation(false);
	TargetMesh->SetSimulatePhysics(false);
	TargetMesh->PutAllRigidBodiesToSleep();

	PendingAsyncCooks = 0;

	// Ensure cache is ready
	if (!bInitialized)
	{
		SaveHighestWeightMap(TargetMesh);
		bInitialized = true;
	}

	// Create a structure to hold all the data we need for the async task
	TSharedPtr<FAsyncTaskData> TaskData = MakeShared<FAsyncTaskData>();
	TaskData->RefSkeleton = RefSkeleton;
	TaskData->ComponentToWorld = ComponentToWorld;
	TaskData->BoneTransforms = BoneTransforms;
	TaskData->OriginalVerticesByBone = OriginalVerticesByBone;

	// Async processing
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, TaskData]()
		{
			// Process the data
			TArray<FPreparedHullData> PreparedHulls;
			ProcessHullsAsync(TaskData, PreparedHulls);

			// Return to game thread to apply changes
			AsyncTask(ENamedThreads::GameThread, [this, PreparedHulls]()
				{
					ApplyPreparedHulls(PreparedHulls);
				});
		});
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
	UE_LOG(LogTemp, Log, TEXT("SETTING MORPHS"));
	if (!SkeletalMeshComponent)
	{
		return;
	}
	TArray<UMorphTarget*> MorphTargetsList = SkeletalMeshComponent->GetSkeletalMeshAsset()->GetMorphTargets();
	for (UMorphTarget* Target : MorphTargetsList)
	{
		UE_LOG(LogTemp, Warning, TEXT("Morph Target: %s"), *Target->GetName());
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
	StoreBoneMorphTranslations(SkeletalMeshComponent);
}

UPhysicsAsset* UMorphToSkeletonComponent::GetOrCreatePhysicsAssetInstance(USkeletalMeshComponent* TargetMesh)
{
	if (!TargetMesh) return nullptr;

	CleanInvalidPhysicsAssetInstances(); // Cleanup old entries

	// Return existing instance if valid
	if (TWeakObjectPtr<UPhysicsAsset>* Found = PhysicsAssetInstances.Find(TargetMesh))
	{
		UE_LOG(LogTemp, Error, TEXT("Physics Asset Already Cached!!!"));
		return Found->Get();

	}


	// Duplicate the original physics asset
	UPhysicsAsset* OriginalAsset = TargetMesh->GetPhysicsAsset();
	if (!OriginalAsset)
	{
		UE_LOG(LogTemp, Error, TEXT("No Physics Asset found!"));
		return nullptr;
	}

	UPhysicsAsset* NewAsset = DuplicateObject<UPhysicsAsset>(OriginalAsset, GetTransientPackage());
	TargetMesh->SetPhysicsAsset(NewAsset);
	PhysicsAssetInstances.Add(TargetMesh, NewAsset);
	return NewAsset;
}

void UMorphToSkeletonComponent::SetPhysicsAssetInstance(USkeletalMeshComponent* TargetMesh, UPhysicsAsset* PhysicsAsset)
{
	if (!TargetMesh) return;

	CleanInvalidPhysicsAssetInstances(); // Cleanup old entries

	// Return existing instance if valid
	if (TWeakObjectPtr<UPhysicsAsset>* Found = PhysicsAssetInstances.Find(TargetMesh))
	{
		UE_LOG(LogTemp, Error, TEXT("Physics Asset Already Cached!!!"));
		return;

	}


	
	TargetMesh->SetPhysicsAsset(PhysicsAsset);
	PhysicsAssetInstances.Add(TargetMesh, PhysicsAsset);
}





