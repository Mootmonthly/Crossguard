#include "Game/CrossguardArtLibrary.h"

#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"

int32 UCrossguardArtLibrary::UseMeshProportions(USkeleton* Skeleton, const TArray<FName>& AnimationBones)
{
	if (Skeleton == nullptr)
	{
		return 0;
	}
	const FReferenceSkeleton& Bones = Skeleton->GetReferenceSkeleton();
	const int32 Count = Bones.GetNum();
	if (Count == 0)
	{
		return 0;
	}
	// The whole tree from the root first, then the bones that move the body back to the animation's.
	Skeleton->SetBoneTranslationRetargetingMode(0, EBoneTranslationRetargetingMode::Skeleton, true);
	for (const FName& BoneName : AnimationBones)
	{
		const int32 Index = Bones.FindBoneIndex(BoneName);
		if (Index != INDEX_NONE)
		{
			Skeleton->SetBoneTranslationRetargetingMode(Index, EBoneTranslationRetargetingMode::Animation, false);
		}
	}
	Skeleton->MarkPackageDirty();
	return Count;
}

bool UCrossguardArtLibrary::MergeMeshBones(USkeleton* Skeleton, USkeletalMesh* Mesh)
{
	if (Skeleton == nullptr || Mesh == nullptr)
	{
		return false;
	}
	const bool bMerged = Skeleton->MergeAllBonesToBoneTree(Mesh, false);
	Skeleton->MarkPackageDirty();
	return bMerged;
}

TArray<FName> UCrossguardArtLibrary::FirstBoneNames(const USkeleton* Skeleton, int32 Count)
{
	TArray<FName> Names;
	if (Skeleton != nullptr)
	{
		const FReferenceSkeleton& Bones = Skeleton->GetReferenceSkeleton();
		for (int32 Index = 0; Index < FMath::Min(Count, Bones.GetNum()); ++Index)
		{
			Names.Add(Bones.GetBoneName(Index));
		}
	}
	return Names;
}
