// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaTailorBridgeEditor : ModuleRules
{
	public MetaTailorBridgeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core", 
				"ContentBrowser",
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"InputCore",
				"UnrealEd",
				"Projects",
				"HTTP",
				"Json",
				"JsonUtilities",
				"AssetTools", 
				"AssetRegistry",
				"ContentBrowser",
				"EditorStyle",
				"Sockets",
				"Networking",
				"FbxAutomationTestBuilder", 
				"DeveloperSettings",
				"ToolMenus",
				"BlueprintGraph",
				"BlueprintEditorLibrary",
				"KismetCompiler",
				"Kismet",
				"SubobjectEditor",
				"MetaTailorBridgeRuntime", 
				"MaterialBaking"
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
