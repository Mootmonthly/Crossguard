#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CrossguardArtLibrary.generated.h"

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

	// The skeleton's first Count bone names (finding the root and hips to pass above).
	UFUNCTION(BlueprintCallable, Category = "Crossguard|Art")
	static TArray<FName> FirstBoneNames(const USkeleton* Skeleton, int32 Count);
};
