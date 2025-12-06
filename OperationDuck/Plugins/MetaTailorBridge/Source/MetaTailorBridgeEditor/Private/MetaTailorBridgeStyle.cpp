#include "MetaTailorBridgeStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleMacros.h"

#define RootToContentDir Style->RootToContentDir

TSharedPtr<FSlateStyleSet> FMetaTailorBridgeStyle::StyleInstance = nullptr;

void FMetaTailorBridgeStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FMetaTailorBridgeStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FMetaTailorBridgeStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("MetaTailorBridgeStyle"));
	return StyleSetName;
}

const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);

TSharedRef< FSlateStyleSet > FMetaTailorBridgeStyle::Create()
{
	TSharedRef< FSlateStyleSet > Style = MakeShareable(new FSlateStyleSet("MetaTailorBridgeStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("MetaTailorBridge")->GetBaseDir() / TEXT("Resources"));

	Style->Set("MetaTailorBridge.OpenPluginWindow", new IMAGE_BRUSH_SVG(TEXT("PlaceholderButtonIcon"), Icon20x20));
	Style->Set("MetaTailorBridge.PluginIcon", new FSlateImageBrush(RootToContentDir(TEXT("Icon128.png")), FVector2D(40, 40)));
	Style->Set("MetaTailorBridge.SendToMtIcon", new FSlateImageBrush(RootToContentDir(TEXT("ExportToMtIcon128.png")), FVector2D(40, 40)));
	Style->Set("MetaTailorBridge.ReceiveFromMtIcon", new FSlateImageBrush(RootToContentDir(TEXT("ImportFromMtIcon128.png")), FVector2D(40, 40)));

	return Style;
}

void FMetaTailorBridgeStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FMetaTailorBridgeStyle::Get()
{
	return *StyleInstance;
}
