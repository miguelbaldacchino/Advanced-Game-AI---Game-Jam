#pragma once

#include "CoreMinimal.h"
#include "MetaTailorAssetHandler.generated.h"

class FSubobjectEditorTreeNode;
struct FDressedAvatarResponse; 
class UObject;
class UTexture2D;
class UMaterialInterface;

USTRUCT(BlueprintType)
struct FMetaTailorLinearColor //abstraction over standard color to have proper upper and lowercase fields for colors
{
    GENERATED_BODY()
    UPROPERTY()
    float r = 1.0f;
    UPROPERTY()
    float g = 1.0f;
    UPROPERTY()
    float b = 1.0f;
    UPROPERTY()
    float a = 1.0f;

    FMetaTailorLinearColor(){}
    FMetaTailorLinearColor(float r, float g, float b, float a)
    {
        this->r = r;
        this->g = g;
        this->b = b;
        this->a = a;
    }
    FMetaTailorLinearColor(FLinearColor Color)
    {
        this->r = Color.R;
        this->g = Color.G;
        this->b = Color.B;
        this->a = Color.A;
    }
    
    FLinearColor ToLinearColor()
    {
        return FLinearColor(r,g,b,a);
    }
};
USTRUCT(BlueprintType)
struct FMetaTailorMaterialInfo
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Info")
    FString Name;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Info")
    FString AlbedoName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Info")
    FString NormalName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Info")
    FString RoughnessName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Info")
    FString MetallicName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Info")
    FString OcclusionName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Info")
    FString EmissiveName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Info")
    FString OpacityName; //only used on export, not import

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Info")
    float SmoothnessValue = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Info")
    float MetallicValue = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Info")
    FMetaTailorLinearColor Tint = FMetaTailorLinearColor(FLinearColor::White);

    FMetaTailorMaterialInfo() {}
};


USTRUCT(BlueprintType)
struct FMetaTailorMeshInfo
{
    GENERATED_BODY()

public:
    UPROPERTY()
    FString MeshName;

    UPROPERTY()
    TArray<FMetaTailorMaterialInfo> Materials;
    
    FMetaTailorMeshInfo() {}
};
USTRUCT(BlueprintType)
struct FUnrealToMetaTailorData
{
    GENERATED_BODY()
public:
    UPROPERTY()
    FString BridgeVersion;
    UPROPERTY()
    TArray<FMetaTailorMeshInfo> MeshInfos;
};
struct FMetaTailorMetadata
{
    FString MetaTailorVersion;
    TArray<FMetaTailorMeshInfo> MeshMaterials;

    bool FromJson(const FString& JsonString);
};


class FMetaTailorAssetHandler
{
public:
    static UObject* ImportModelFromResponse(const FDressedAvatarResponse& Response);
    static TSharedPtr<FSubobjectEditorTreeNode> GetSelectedSubObjectInBlueprint();
    static AActor* GetSelectedTargetActor();
    static USkeleton* GetSkeletonFromSelectedObject();
    static USkeletalMeshComponent* GetSkeletalMeshFromSelectedObject();
    static UBlueprint* GetBlueprint();
    template <typename T>
    static TArray<T*> GetComponentsInChildrenSimple(AActor* ParentActor);
    static bool RemoveClothingFromFocusedBlueprint();
    static bool ExportActorToFBX(AActor* ActorToExport, FString& OutFbxPath, FString& OutMaterialInfoPath);
    static bool ApplyClothingToActor(UObject* SourceClothingAsset, AActor* TargetActor, bool bReplaceExisting);
    static void ClearMetaTailorClothes(AActor* TargetActor);
    static FUnrealToMetaTailorData GetMeshMaterialInfoFromActor(AActor* Actor, const FString& SaveTexturesIn);
    static bool AddSkeletalMeshComponentToFocusedBlueprint(USkeletalMesh* Mesh);
    static void ReimportStagedFBX();
private:
    static bool StageReceivedFiles(const FString& SourceModelPath, const FString& TargetDirectory, FString& OutCopiedModelPath, FString& OutCopiedTexturePath, FString& OutMetadataJsonContent);
    static TArray<UTexture*> ImportStagedTextures(const FString& SourceAbsoluteTextureFolderPath, const FString& DestAbsoluteTextureFolderPath);
    static UObject* ImportStagedFBX(const FString& FBXPath, const FString& TargetImportFolder);
    static void ConfigureTextureSettings(const FString& TexturePathInProject, bool bIsNormalMap, bool bIsLinear);
    static void ApplyMaterialMetadata(UObject* ImportedAsset, const FMetaTailorMetadata& Metadata, const TArray<UTexture*>& TextureCandidates);
    static FString GetProjectImportPath(bool bIsAvatar);
};
