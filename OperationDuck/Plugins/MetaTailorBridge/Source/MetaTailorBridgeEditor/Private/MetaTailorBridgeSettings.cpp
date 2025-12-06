#include "MetaTailorBridgeSettings.h"

UMetaTailorBridgeSettings::UMetaTailorBridgeSettings()
	: MetaTailorPort(53485)
	, UnrealBridgePort(53487) 
	, PathForOutfitImports(TEXT("/Game/MetaTailorImported/Outfits/"))
	, PathForAvatarImports(TEXT("/Game/MetaTailorImported/Avatars/"))
	, ExportTextureResolution(1024)
{
}

#if WITH_EDITOR
FText UMetaTailorBridgeSettings::GetSectionText() const
{
	return NSLOCTEXT("MetaTailorBridge", "SettingsSection", "METATAILOR Bridge");
}

FText UMetaTailorBridgeSettings::GetSectionDescription() const
{
	return NSLOCTEXT("MetaTailorBridge", "SettingsDescription", "Configure connection ports and import paths for the METATAILOR Bridge plugin.");
}
#endif // WITH_EDITOR