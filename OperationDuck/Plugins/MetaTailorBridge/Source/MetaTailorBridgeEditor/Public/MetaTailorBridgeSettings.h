#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MetaTailorBridgeSettings.generated.h"

UCLASS(config = EditorPerProjectUserSettings, defaultconfig, meta = (DisplayName = "METATAILOR Bridge Settings"))
class  UMetaTailorBridgeSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UMetaTailorBridgeSettings();
    
    UPROPERTY(config, EditAnywhere, Category = "Connection", meta = (ClampMin = "1024", ClampMax = "65535", UIMin = "1024", UIMax = "65535"))
    int32 MetaTailorPort;

    UPROPERTY(config, EditAnywhere, Category = "Connection", meta = (ClampMin = "1024", ClampMax = "65535", UIMin = "1024", UIMax = "65535"))
    int32 UnrealBridgePort;

    UPROPERTY(config, EditAnywhere, Category = "Import Paths", meta = (ContentDir))
    FString PathForOutfitImports;

    UPROPERTY(config, EditAnywhere, Category = "Import Paths", meta = (ContentDir))
    FString PathForAvatarImports;

    UPROPERTY(config, EditAnywhere, Category = "Export", meta = (ClampMin = "64", ClampMax = "4096", UIMin = "64", UIMax = "4096"))
    int32 ExportTextureResolution;
    static constexpr int32 MAX_TEXTURE_RESOLUTION = 4096;
    static constexpr int32 MIN_TEXTURE_RESOLUTION = 64;
#if WITH_EDITOR
    virtual FText GetSectionText() const override;
    virtual FText GetSectionDescription() const override;

    virtual FName GetContainerName() const override { return TEXT("Editor"); }
    virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
    virtual FName GetSectionName() const override { return TEXT("METATAILOR Bridge"); }
#endif
};