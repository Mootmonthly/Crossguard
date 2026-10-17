#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CrossguardArtLibrary.generated.h"

class USkeletalMesh;
class USkeleton;

// Asset set-up calls for Tools/import_art.py (Unreal Python), where the engine exposes none.
UCLASS()
class CROSSGUARD_API UCrossguardArtLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// One skeleton shared by meshes of different proportions (KayKit's Rig_Medium): every bone takes
	// its length from the mesh playing the animation ("Skeleton" translation retargeting), except
	// AnimationBones (the root and the hips: they move the body), which keep the animation's. Marks
	// the skeleton dirty (save it). Returns the number of bones, 0 without a skeleton.
	UFUNCTION(BlueprintCallable, Category = "Crossguard|Art")
	static int32 UseMeshProportions(USkeleton* Skeleton, const TArray<FName>& AnimationBones);

	// Adds the bones Mesh has and its skeleton lacks (a shared skeleton: the Rogue's cape) to the
	// skeleton, as the editor does when it loads the mesh. Marks it dirty. False without either.
	UFUNCTION(BlueprintCallable, Category = "Crossguard|Art")
	static bool MergeMeshBones(USkeleton* Skeleton, USkeletalMesh* Mesh);

	// The skeleton's first Count bone names (finding the root and hips to pass above).
	UFUNCTION(BlueprintCallable, Category = "Crossguard|Art")
	static TArray<FName> FirstBoneNames(const USkeleton* Skeleton, int32 Count);
};
