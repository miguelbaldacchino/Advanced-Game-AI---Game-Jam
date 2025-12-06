#include "MetaTailorAssetHandler.h"

#include <string>

#include "AssetExportTask.h"
#include "MetaTailorBridgeSettings.h"
#include "MetaTailorIPC.h"
#include "AssetToolsModule.h"
#include "BlueprintEditorModule.h"
#include "EditorMetadataOverrides.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "IAssetTools.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "IMaterialBakingModule.h"
#include "JsonObjectConverter.h"
#include "MaterialBakingStructures.h"
#include "Factories/FbxImportUI.h"
#include "ObjectTools.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/Texture2D.h"
#include "EditorFramework/AssetImportData.h"
#include "Exporters/Exporter.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxSkeletalMeshImportData.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Selection.h"
#include "UMetaTailorSkeletalMeshComponent.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "SSubobjectEditor.h"
#include "Exporters/FbxExportOption.h"
#include "Factories/FbxTextureImportData.h"
#include "Factories/TextureFactory.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"


//builtin json handling does lowerCamelCase, we want PascalCase
TSharedPtr<FJsonObject> UStructToJsonObjectPascalCaseRecursive(const UStruct* StructDefinition, const void* Struct);

TSharedPtr<FJsonValue> UPropertyToJsonValuePascalCaseRecursive(FProperty* Property, const void* Container)
{
    if (!Property || !Container)
    {
        return MakeShared<FJsonValueNull>();
    }

    // --- Handle TArray of UStructs ---
    if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
    {
        TArray<TSharedPtr<FJsonValue>> JsonArray;
        FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(Container));

        if (FStructProperty* ArrayInnerStructProperty = CastField<FStructProperty>(ArrayProperty->Inner))
        {
            for (int32 i = 0; i < ArrayHelper.Num(); ++i)
            {
                // Recursive call for each struct in the array
                TSharedPtr<FJsonObject> ElementJsonObject = UStructToJsonObjectPascalCaseRecursive(
                    ArrayInnerStructProperty->Struct,
                    ArrayHelper.GetRawPtr(i)
                );
                JsonArray.Add(MakeShared<FJsonValueObject>(ElementJsonObject));
            }
            return MakeShared<FJsonValueArray>(JsonArray);
        }
    }

    // --- Handle a single nested UStruct ---
    if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
    {
        // Recursive call for the nested struct
        TSharedPtr<FJsonObject> NestedJsonObject = UStructToJsonObjectPascalCaseRecursive(
            StructProperty->Struct,
            Property->ContainerPtrToValuePtr<void>(Container)
        );
        return MakeShared<FJsonValueObject>(NestedJsonObject);
    }
    
    // --- Fallback for all other simple types (int, string, bool, etc.) ---
    // For these, the default converter is fine as it has no sub-fields to capitalize.
    return FJsonObjectConverter::UPropertyToJsonValue(Property, Property->ContainerPtrToValuePtr<void>(Container));
}


TSharedPtr<FJsonObject> UStructToJsonObjectPascalCaseRecursive(const UStruct* StructDefinition, const void* Struct)
{
    TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
    if (!StructDefinition || !Struct)
    {
        return JsonObject;
    }

    for (TFieldIterator<FProperty> It(StructDefinition); It; ++It)
    {
        FProperty* Property = *It;
        if (Property && Property->IsValidLowLevel())
        {
            FString PropertyName = Property->GetName();
            
            // Use our new recursive value converter
            TSharedPtr<FJsonValue> JsonValue = UPropertyToJsonValuePascalCaseRecursive(Property, Struct);

            if (JsonValue.IsValid() && !JsonValue->IsNull())
            {
                JsonObject->SetField(PropertyName, JsonValue);
            }
        }
    }
    return JsonObject;
}
bool FMetaTailorMetadata::FromJson(const FString& JsonString)
{
	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid()) return false;

	RootObject->TryGetStringField(TEXT("MetaTailorVersion"), MetaTailorVersion);

	const TArray<TSharedPtr<FJsonValue>>* MeshMaterialsArray;
	if (!RootObject->TryGetArrayField(TEXT("MeshMaterials"), MeshMaterialsArray)) return false;

	for (const auto& MeshVal : *MeshMaterialsArray)
	{
		TSharedPtr<FJsonObject> MeshObj = MeshVal->AsObject();
		if (!MeshObj.IsValid()) continue;

		FMetaTailorMeshInfo MeshInfo;
		MeshObj->TryGetStringField(TEXT("MeshName"), MeshInfo.MeshName);

		const TArray<TSharedPtr<FJsonValue>>* MaterialsArray;
		if (!MeshObj->TryGetArrayField(TEXT("Materials"), MaterialsArray)) continue;

		for (const auto& MatVal : *MaterialsArray)
		{
			TSharedPtr<FJsonObject> MatObj = MatVal->AsObject();
			if (!MatObj.IsValid()) continue;

			FMetaTailorMaterialInfo MatInfo;
			MatObj->TryGetStringField(TEXT("Name"), MatInfo.Name);
			MatObj->TryGetStringField(TEXT("AlbedoName"), MatInfo.AlbedoName);
			MatObj->TryGetStringField(TEXT("NormalName"), MatInfo.NormalName);
			MatObj->TryGetStringField(TEXT("RoughnessName"), MatInfo.RoughnessName);
			MatObj->TryGetStringField(TEXT("MetallicName"), MatInfo.MetallicName);
			MatObj->TryGetStringField(TEXT("EmissiveName"), MatInfo.EmissiveName);
			MatObj->TryGetStringField(TEXT("OcclusionName"), MatInfo.OcclusionName);

			MatObj->TryGetNumberField(TEXT("SmoothnessValue"), MatInfo.SmoothnessValue);
			MatObj->TryGetNumberField(TEXT("MetallicValue"), MatInfo.MetallicValue);

			const TSharedPtr<FJsonObject>* TintObject;
			if (MatObj->TryGetObjectField(TEXT("Tint"), TintObject))
			{
				double R = 0.0, G = 0.0, B = 0.0, A = 1.0;
				(*TintObject)->TryGetNumberField(TEXT("r"), R);
				(*TintObject)->TryGetNumberField(TEXT("g"), G);
				(*TintObject)->TryGetNumberField(TEXT("b"), B);
				(*TintObject)->TryGetNumberField(TEXT("a"), A);

				MatInfo.Tint = FLinearColor(static_cast<float>(R), static_cast<float>(G), static_cast<float>(B), static_cast<float>(A));
			}
			else
			{
				MatInfo.Tint = FLinearColor::White;
			}

			MeshInfo.Materials.Add(MatInfo);
		}
		MeshMaterials.Add(MeshInfo);
	}
	return true;
}


FString FMetaTailorAssetHandler::GetProjectImportPath(bool bIsAvatar)
{
	const UMetaTailorBridgeSettings* Settings = GetDefault<UMetaTailorBridgeSettings>();
	FString BasePath = Settings
		                   ? (bIsAvatar ? Settings->PathForAvatarImports : Settings->PathForOutfitImports)
		                   : (bIsAvatar ? TEXT("MetaTailorImported/Avatars") : TEXT("MetaTailorImported/Outfits"));

	BasePath = BasePath.TrimStartAndEnd();
	if (!BasePath.StartsWith(TEXT("/Game/")))
	{
		BasePath = FPaths::Combine(TEXT("/Game/"), BasePath);
	}
	FPaths::NormalizeDirectoryName(BasePath);
	BasePath.RemoveFromEnd(TEXT("/"));

	return BasePath;
}

bool FMetaTailorAssetHandler::StageReceivedFiles(const FString& SourceModelPath, const FString& TargetDirectory,
                                                 FString& OutCopiedModelPath, FString& OutCopiedTexturePath,
                                                 FString& OutMetadataJsonContent)
{
	IPlatformFile& FileManager = FPlatformFileManager::Get().GetPlatformFile();
	FString SourceDirectory = FPaths::GetPath(SourceModelPath);

	if (!FileManager.DirectoryExists(*TargetDirectory))
	{
		if (!FileManager.CreateDirectoryTree(*TargetDirectory))
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to create target directory: %s"), *TargetDirectory);
			return false;
		}
	}

	FString TargetModelPath = FPaths::Combine(TargetDirectory, FPaths::GetCleanFilename(SourceModelPath));
	if (FileManager.FileExists(*TargetModelPath))
	{
		FileManager.DeleteFile(*TargetModelPath);
	}
	if (!FileManager.CopyFile(*TargetModelPath, *SourceModelPath))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to copy model file from %s to %s"), *SourceModelPath, *TargetModelPath);
		return false;
	}
	OutCopiedModelPath = TargetModelPath; 

	FString SourceTexturesDir = FPaths::Combine(SourceDirectory, TEXT("Textures"));
	FString TargetTexturesDir = FPaths::Combine(TargetDirectory, TEXT("Textures"));
	if (FileManager.DirectoryExists(*SourceTexturesDir))
	{
		if (!FileManager.CopyDirectoryTree(*TargetTexturesDir, *SourceTexturesDir, true))
		{
			UE_LOG(LogTemp, Warning, TEXT("Failed to copy textures directory from %s to %s"), *SourceTexturesDir,
			       *TargetTexturesDir);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Source Textures directory not found: %s"), *SourceTexturesDir);
	}
	
	FString SourceMetadataPath = FPaths::Combine(SourceDirectory, TEXT("metadata.json"));
	if (FileManager.FileExists(*SourceMetadataPath))
	{
		if (!FFileHelper::LoadFileToString(OutMetadataJsonContent, *SourceMetadataPath))
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to read metadata file: %s"), *SourceMetadataPath);
			return false;
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Metadata file not found: %s"), *SourceMetadataPath);
		return false;
	}

	OutCopiedTexturePath = TargetTexturesDir;

	return true;
}

FString AbsolutePathToPackagePath(const FString& AbsolutePath)
{
	FString StandardizedAbsolutePath = AbsolutePath;
	FPaths::NormalizeFilename(StandardizedAbsolutePath);

	FString ProjectContentDir = FPaths::ProjectContentDir();
	ProjectContentDir = FPaths::ConvertRelativePathToFull(ProjectContentDir);;
	FPaths::NormalizeFilename(ProjectContentDir);

	if (StandardizedAbsolutePath.StartsWith(ProjectContentDir))
	{
		FString RelativePath = StandardizedAbsolutePath;
		FPaths::MakePathRelativeTo(RelativePath, *ProjectContentDir);

		FString PackagePath = FPaths::GetPath(RelativePath) / FPaths::GetBaseFilename(RelativePath);

		return TEXT("/Game/") + PackagePath;
	}

	UE_LOG(LogTemp,Error, TEXT("Path %s not within content directory %s"), *StandardizedAbsolutePath, *ProjectContentDir)
	return FString();
}

TArray<UTexture*> FMetaTailorAssetHandler::ImportStagedTextures(const FString& SourceAbsoluteTextureFolderPath, const FString& DestAbsoluteTextureFolderPath)
{
	IFileManager& FileManager = IFileManager::Get();
	TArray<FString> TexturesToImport;
	FString DirectoryToSearch = (SourceAbsoluteTextureFolderPath);
	FileManager.FindFiles(TexturesToImport, *DirectoryToSearch, TEXT("*.png"));

	TArray<UTexture*> ReturnTextures;

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	UE_LOG(LogTemp, Log, TEXT("Found %i textures to import at %s"), TexturesToImport.Num(), *DirectoryToSearch);
	auto TextureFactory = NewObject<UTextureFactory>();
	auto PackagePath = AbsolutePathToPackagePath(DestAbsoluteTextureFolderPath);//FPackageName::FilenameToLongPackageName(DestAbsoluteTextureFolderPath);
	auto PathsArr = TArray<FString>();
	for (auto Texture : TexturesToImport)
	{
		auto FullPath = FPaths::Combine(SourceAbsoluteTextureFolderPath, Texture);
		PathsArr.Add(FullPath);
	}
	// auto ImportedObjects = AssetTools.ImportAssets(
	// 	PathsArr,
	// 	PackagePath,
	// 	TextureFactory
	// );
	
	auto ImportOperation = NewObject<UAutomatedAssetImportData>();
	ImportOperation->Factory = TextureFactory;
	ImportOperation->bReplaceExisting = true;
	ImportOperation->Filenames = PathsArr;
	ImportOperation->bSkipReadOnly = false;
	ImportOperation->DestinationPath = PackagePath;
	auto ImportedObjects = AssetTools.ImportAssetsAutomated(ImportOperation);

	for (const auto Object : ImportedObjects)
	{
		if (auto Casted = Cast<UTexture2D>(Object))
			ReturnTextures.Add(Casted);
	}
	return ReturnTextures;
}

FString GetTargetDirectoryFromName(const FString& TargetProjectFolder, const FString& FileNameWithOrWithoutExtension)
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectContentDir() + TargetProjectFolder.RightChop(6),
		ObjectTools::SanitizeObjectName(FPaths::GetBaseFilename(FileNameWithOrWithoutExtension))));
}
FString GetTargetTextureDirectory(const FString& TargetDirectory)
{
	return FPaths::Combine(TargetDirectory,TEXT("Textures"));
}
FString GetUniqueDirectory(const FString& PotentiallyAlreadyExisting)
{
	FString Ret = PotentiallyAlreadyExisting;
	int Counter = 1;
	while (FPaths::DirectoryExists(Ret))
	{
		Ret = PotentiallyAlreadyExisting+FString::FromInt(++Counter);
	}
	return Ret;
}
UObject* FMetaTailorAssetHandler::ImportModelFromResponse(const FDressedAvatarResponse& Response)
{
	IPlatformFile& FileManager = FPlatformFileManager::Get().GetPlatformFile();
	FString TargetProjectFolder = GetProjectImportPath(
		Response.ParseType() == FDressedAvatarResponse::EResponseType::Avatar);
	

	FString SourceDirectory = FPaths::GetPath(Response.ModelPath);
	FString SourceTexturesDir = FPaths::Combine(SourceDirectory, TEXT("Textures"));
	FString SourceFilename = FPaths::GetCleanFilename(Response.ModelPath);
	FString FbxModelPath;
	//FString MetadataJsonContent;

	FString TargetDirectory = GetUniqueDirectory(GetTargetDirectoryFromName(TargetProjectFolder, SourceFilename));
	FString TargetTexturesDirectory = GetTargetTextureDirectory(TargetDirectory);
	UE_LOG(LogTemp, Warning, TEXT("TargetProject %s\nTargetDirectory %s"), *TargetProjectFolder, *TargetDirectory);
		
	if (!FileManager.DirectoryExists(*TargetTexturesDirectory))
	{
		FileManager.CreateDirectoryTree(*TargetTexturesDirectory);
	}
	else
	{
		UE_LOG(LogTemp,Error,TEXT("Unexpected already existing directory"));
	}
	//FString CopiedModelPath;
	//FString CopiedTexturePath;
	
	/*if (!StageReceivedFiles(Response.ModelPath, TargetDirectory, CopiedModelPath, CopiedTexturePath, MetadataJsonContent))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to stage received files from %s"), *Response.ModelPath);
		return nullptr;
	}*/

	FString SourceMetadataPath = FPaths::Combine(SourceDirectory, TEXT("metadata.json"));
	FString OutMetadataJsonContent;
	if (FileManager.FileExists(*SourceMetadataPath))
	{
		if (!FFileHelper::LoadFileToString(OutMetadataJsonContent, *SourceMetadataPath))
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to read metadata file: %s"), *SourceMetadataPath);
			return nullptr;
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Metadata file not found: %s"), *SourceMetadataPath);
		return nullptr;
	}
	FMetaTailorMetadata Metadata;
	if (!Metadata.FromJson(OutMetadataJsonContent))
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to parse metadata.json. Proceeding without material overrides."));
	}
	
	//auto ImportedTextureObjects = ImportStagedTextures(CopiedTexturePath);
	UObject* ImportedAsset = ImportStagedFBX(Response.ModelPath, AbsolutePathToPackagePath(TargetDirectory));
	auto ImportedTextureObjects = ImportStagedTextures(SourceTexturesDir,TargetTexturesDirectory);

	if (ImportedAsset)
	{
		ApplyMaterialMetadata(ImportedAsset, Metadata, ImportedTextureObjects);
		ImportedAsset->MarkPackageDirty();
	}

	return ImportedAsset;
}

TSharedPtr<FSubobjectEditorTreeNode> FMetaTailorAssetHandler::GetSelectedSubObjectInBlueprint()
{
	if (UBlueprint* Blueprint = GetBlueprint())
	{
		UAssetEditorSubsystem* EditorSys = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		//UNSAFE, the following code could have undefined behavior if the internal structure of stuff changes in a future Unreal version
		//Unable to use Cast since it's not UObject derived
		const auto Editor = static_cast<IBlueprintEditor*>(EditorSys->FindEditorForAsset(Blueprint, false));
		if (!Editor)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to cast editor, internal usage may have changed"));
			return nullptr;
		}
		const auto ComponentsToIterateOver = Editor->GetSelectedSubobjectEditorTreeNodes();
		if (ComponentsToIterateOver.Num() > 0)
			return ComponentsToIterateOver[0];
		return nullptr;
	}
	return nullptr;
}

AActor* FMetaTailorAssetHandler::GetSelectedTargetActor()
{
	USelection* SelectedActors = GEditor->GetSelectedActors();

	AActor* TargetActor = nullptr;

	if (SelectedActors)
	{
		for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
		{
			AActor* Actor = Cast<AActor>(*Iter);
			if (Actor && Actor->FindComponentByClass<USkeletalMeshComponent>())
			{
				TargetActor = Actor;
				break;
			}
		}
	}
	return TargetActor;
}

USkeleton* FMetaTailorAssetHandler::GetSkeletonFromSelectedObject()
{
	auto TargetActor = GetSelectedTargetActor();
	if (!TargetActor) return nullptr;
	auto TargetComponent = TargetActor->FindComponentByClass<USkeletalMeshComponent>();
	if (!TargetComponent) return nullptr;
	auto SkeletalMeshAsset = TargetComponent->GetSkeletalMeshAsset();
	if (!SkeletalMeshAsset) return nullptr;
	return SkeletalMeshAsset->GetSkeleton();
}

USkeletalMeshComponent* FMetaTailorAssetHandler::GetSkeletalMeshFromSelectedObject()
{
	auto TargetActor = GetSelectedTargetActor();
	if (!TargetActor) return nullptr;
	return TargetActor->FindComponentByClass<USkeletalMeshComponent>();
}

UBlueprint* FMetaTailorAssetHandler::GetBlueprint()
{
	UBlueprint* BP = nullptr;
	UAssetEditorSubsystem* EditorSys = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!EditorSys)
		return nullptr;

	for (UObject* Asset : EditorSys->GetAllEditedAssets())
	{
		if (UBlueprint* Candidate = Cast<UBlueprint>(Asset))
		{
			if (EditorSys->FindEditorForAsset(Candidate, false))
			{
				BP = Candidate;
			}
		}
	}
	return BP;
}

bool FMetaTailorAssetHandler::RemoveClothingFromFocusedBlueprint()
{
	auto BP = GetBlueprint();
	if (!BP || !BP->SimpleConstructionScript) return false;

	BP->Modify();
	USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
	auto NodeName = UMetaTailorSkeletalMeshComponent::StaticClass()->GetClassPathName();

	auto Nodes = SCS->GetAllNodes();
	for (auto Node : Nodes)
	{
		if (Node->ComponentClass->GetClassPathName() == NodeName)
		{
			SCS->RemoveNode(Node, false);
		}
	}
	SCS->ValidateSceneRootNodes();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	FKismetEditorUtilities::CompileBlueprint(BP);
	return true;
}

static USkeletalMeshComponent* GetFirstSMC(UBlueprint* Blueprint)
{
	if (!Blueprint || !Blueprint->GeneratedClass)
		return nullptr;

	AActor* CDO = Blueprint->GeneratedClass->GetDefaultObject<AActor>();

	TInlineComponentArray<USkeletalMeshComponent*> SkeletalComps;
	CDO->GetComponents(SkeletalComps);

	return SkeletalComps.Num() ? SkeletalComps[0] : nullptr;
}

static USCS_Node* GetFirstSkeletalNode(UBlueprint* Blueprint)
{
	auto TargetPathName = USkeletalMeshComponent::StaticClass()->GetClassPathName();
	for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		UE_LOG(LogTemp, Log, TEXT("Node iterated: %s"), *Node->ComponentClass->GetClassPathName().ToString());


		if (Node->ComponentClass->GetClassPathName() == TargetPathName)
		{
			UE_LOG(LogTemp, Log, TEXT("Node found"));
			return Node;
		}
	}
	return nullptr;
}

bool FMetaTailorAssetHandler::AddSkeletalMeshComponentToFocusedBlueprint(USkeletalMesh* Mesh)
{
	auto BP = GetBlueprint();
	if (!BP || !BP->SimpleConstructionScript) return false;

	BP->Modify();
	USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
	USCS_Node* MeshNode = SCS->CreateNode(UMetaTailorSkeletalMeshComponent::StaticClass(), TEXT("METATAILOR Clothes"));

	//There are two different approaches used for items in a blueprint, one for C++ based ones and the other for nodes added in the editor
	//this approach is for finding the C++ based ones
	auto LeaderNode = GetFirstSkeletalNode(BP);
	//this approach is for the editor-added ones   
	auto LeaderComponent = GetFirstSMC(BP);

	if (!LeaderNode && !LeaderComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("Unable to find target leader"))
		return false;
	}

	// Saving leader pose DOESN'T WORK, as SetLeaderPoseComponent is....not transient, but still doesn't seem to save properly
	//so we use UMetaTailorSkeletalMeshComponent instead to automatically have it follow with no setup     
	UMetaTailorSkeletalMeshComponent* SkeletalMeshTemplate = CastChecked<UMetaTailorSkeletalMeshComponent>(
		MeshNode->ComponentTemplate);

	if (SkeletalMeshTemplate)
		SkeletalMeshTemplate->SetSkeletalMesh(Mesh);
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to get Skeletal Mesh Component Template"));
	}
	if (LeaderComponent)
	{
		//UE_LOG(LogTemp,Log, TEXT("Leader component is %s"), *LeaderComponent->GetName());
		SCS->AddNode(MeshNode);
		MeshNode->SetParent(LeaderComponent);
	}
	else
	{
		//UE_LOG(LogTemp,Log, TEXT("No leader component"));
		LeaderNode->AddChildNode(MeshNode);
	}
	
	

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	FKismetEditorUtilities::CompileBlueprint(BP);
	return true;
}

FString lastFBXPath;
FString lastTargetImportFolder;
void FMetaTailorAssetHandler::ReimportStagedFBX()
{
	ImportStagedFBX(lastFBXPath,lastTargetImportFolder);
}

UObject* FMetaTailorAssetHandler::ImportStagedFBX(const FString& FBXPath, const FString& TargetImportFolder)
{
	UE_LOG(LogTemp, Error, TEXT("Importing staged FBX from %s to %s"), *FBXPath, *TargetImportFolder);
	lastFBXPath = FBXPath;
	lastTargetImportFolder = TargetImportFolder;
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	UAutomatedAssetImportData* ImportData = NewObject<UAutomatedAssetImportData>();
	

	ImportData->DestinationPath = TargetImportFolder;
	ImportData->Filenames.Add(FBXPath);

	//ImportData->GroupName = TEXT("MetaTailor Import");
	ImportData->bReplaceExisting = true;
	ImportData->bSkipReadOnly = false;
	
	UFbxImportUI* FbxOptions = NewObject<UFbxImportUI>();
	FbxOptions->bImportMaterials = false;
	FbxOptions->bImportTextures = false;
	FbxOptions->bImportAnimations = false;
	FbxOptions->bImportMesh = true;
	FbxOptions->bCreatePhysicsAsset = false;
	FbxOptions->bAutomatedImportShouldDetectType = false;
	FbxOptions->Skeleton = nullptr;
	FbxOptions->bImportAsSkeletal = true;

	//when overwriting assets when there's new meshes, section and material ordering will sometimes get jumbled. This prevents that... sometimes.
	//however, have swapped handling to just renaming things on import, as the edge cases were beyond frustrating
	FbxOptions->TextureImportData->MaterialSearchLocation = EMaterialSearchLocation::DoNotSearch;

	//current exporter in MT doesn't support smoothing groups. Unfortunately this doesn't seem to properly suppress the warning
	//FbxOptions->SkeletalMeshImportData->bPreserveSmoothingGroups = false; 
	
	
	UFbxFactory* FbxFactory = NewObject<UFbxFactory>();
	FbxFactory->ImportUI = FbxOptions;
	ImportData->Factory = FbxFactory;

	bool bIsSkeletal = true;
	if (bIsSkeletal)
	{
		FbxOptions->MeshTypeToImport = FBXIT_SkeletalMesh;
		if (!FbxOptions->SkeletalMeshImportData)
		{
			FbxOptions->SkeletalMeshImportData = NewObject<UFbxSkeletalMeshImportData>(FbxOptions);
		}
		FbxOptions->SkeletalMeshImportData->bImportMorphTargets = true;
	}
	else 
	{
		UE_LOG(LogTemp,Error,TEXT("Static mesh import is not tested in any capacity at the moment, nor is it intended to be used"));
		FbxOptions->MeshTypeToImport = FBXIT_StaticMesh;
	}


	TArray<UObject*> ImportedObjectsResult = AssetTools.ImportAssetsAutomated(ImportData);
	
	UObject* ImportedAsset = nullptr;
	if (ImportedObjectsResult.Num() > 0)
	{
		ImportedAsset = ImportedObjectsResult[0];
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to import asset"));		
	}


	if (ImportedAsset)
	{
		UAssetImportData* AssetImportData = bIsSkeletal
			                                    ? Cast<USkeletalMesh>(ImportedAsset)->GetAssetImportData()
			                                    : Cast<UStaticMesh>(ImportedAsset)->GetAssetImportData();
		if (AssetImportData)
		{
			FAssetImportInfo Info;
			Info.Insert(FAssetImportInfo::FSourceFile(FBXPath));
			AssetImportData->SourceData = Info;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Could not get AssetImportData for %s to set reimport path."),
			       *ImportedAsset->GetName());
		}

		if (bIsSkeletal && !Cast<USkeletalMesh>(ImportedAsset))
		{
			UE_LOG(LogTemp, Warning, TEXT("Unexpected type for imported skeletal mesh"));
		}
		else if (!bIsSkeletal && !Cast<UStaticMesh>(ImportedAsset))
		{
			UE_LOG(LogTemp, Warning, TEXT("Unexpected type for imported static mesh"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Automated FBX Import failed for: %s. Check logs."), *FBXPath);
	}

	return ImportedAsset;
}

UTexture* FindObjectByNameInArray(const TArray<UTexture*>& InObjects, const FString& TargetName)
{
	const FName TargetFName = FName(*TargetName);
	for (UTexture* CurrentObject : InObjects)
	{
		if (CurrentObject)
		{
			if (CurrentObject->GetFName() == TargetFName)
			{
				return CurrentObject;
			}
		}
	}
	return nullptr;
}

void FMetaTailorAssetHandler::ApplyMaterialMetadata(UObject* ImportedAsset, const FMetaTailorMetadata& Metadata,
                                                    const TArray<UTexture*>& TextureCandidates)
{
	if (!ImportedAsset || Metadata.MeshMaterials.Num() == 0)
	{
		return;
	}

	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(ImportedAsset);
	UStaticMesh* StaticMesh = Cast<UStaticMesh>(ImportedAsset);
	int32 NumMaterialSlots = 0;
	TArray<FSkeletalMaterial> ModifiedMaterials; 

	// Get the number of material slots and original materials (for SkeletalMesh)
	if (SkeletalMesh)
	{
		ModifiedMaterials = TArray(SkeletalMesh->GetMaterials());
		NumMaterialSlots = ModifiedMaterials.Num();
	}
	else if (StaticMesh)
	{
		NumMaterialSlots = StaticMesh->GetStaticMaterials().Num();
	}
	else
	{
		return; // Not a mesh asset we can process
	}

	if (NumMaterialSlots == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Asset %s has no material slots."), *ImportedAsset->GetName());
		return;
	}

	//UE_LOG(LogTemp, Log, TEXT("Attempting to apply metadata to %d material slots on asset %s"), NumMaterialSlots,*ImportedAsset->GetName());

	int TargetMaterialIndex = -1;
	bool bSkelMaterialChanged = false;
	//UE_LOG(LogTemp, Log, TEXT("Material Slots: %i, infos: %i"), NumMaterialSlots, Metadata.MeshMaterials.Num());
	const auto BaseMaterial = Cast<UMaterial>(StaticLoadAsset(UMaterial::StaticClass(),
	                                                          FTopLevelAssetPath(
		                                                          "/MetaTailorBridge/BaseMetaTailorMaterial")));
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<
		FAssetRegistryModule>("AssetRegistry");

	for (const auto MeshInfo : Metadata.MeshMaterials)
	{
		//UE_LOG(LogTemp, Log, TEXT("Processing mesh %s has %i materials"), *MeshInfo.MeshName, MeshInfo.Materials.Num())
		if (!BaseMaterial)
		{
			UE_LOG(LogTemp, Error, TEXT("Unable to locate base material"));
		}

		for (auto Mat : MeshInfo.Materials)
		{
			TargetMaterialIndex++;
			if (TargetMaterialIndex >= NumMaterialSlots)
			{
				UE_LOG(LogTemp, Log, TEXT("No more material metadata available after index %d for mesh '%s'"),
				       TargetMaterialIndex - 1, *MeshInfo.MeshName);
				break;
			}
			
			//UE_LOG(LogTemp, Log, TEXT("Processing Material Slot %d, Metadata Name: %s, Base Material: %s"), TargetMaterialIndex, *Mat.Name, *BaseMaterial->GetName());

			FString SanitizedAssetName = ObjectTools::SanitizeObjectName(ImportedAsset->GetName());
			FString SanitizedMatInfoName = ObjectTools::SanitizeObjectName(
				Mat.Name.IsEmpty() ? FString::Printf(TEXT("Mat%d"), TargetMaterialIndex) : Mat.Name);

			UE_LOG(LogTemp, Log, TEXT("Processing material %s"), *SanitizedMatInfoName);
			FString InstanceName = FString::Printf(TEXT("MI_%s_%s"), *SanitizedAssetName, *SanitizedMatInfoName);
			FString PackagePath = FPaths::GetPath(ImportedAsset->GetPathName());
			FString FullInstancePath = FPaths::Combine(PackagePath, InstanceName);

			//assets in object paths have a subobject of the same name that must be referenced when it's gotten
			FString SubObjectPath = FullInstancePath + "." + InstanceName;			
			FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(SubObjectPath));
			UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(AssetData.GetAsset());

			if (!MIC)
			{
				//UE_LOG(LogTemp, Log, TEXT("Creating new MIC: %s"), *FullInstancePath);
				UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
				Factory->InitialParent = BaseMaterial;
				MIC = Cast<UMaterialInstanceConstant>(
					AssetTools.CreateAsset(InstanceName, PackagePath, UMaterialInstanceConstant::StaticClass(), Factory));
				if (!MIC)
				{
					UE_LOG(LogTemp, Error, TEXT("Failed to create Material Instance Constant: %s"), *FullInstancePath);
					continue;
				}
			}
			else
			{
				//UE_LOG(LogTemp, Log, TEXT("Using existing MIC: %s"), *FullInstancePath);
				if (MIC->Parent != BaseMaterial)
				{
					UE_LOG(LogTemp, Log, TEXT("Parent mismatch for existing MIC %s. Resetting parent."),
					       *FullInstancePath);
					MIC->SetParentEditorOnly(BaseMaterial);
				}
			}
			FName ParamNameBaseColor(TEXT("BaseColorTexture"));
			FName ParamNameNormal(TEXT("NormalTexture"));
			FName ParamNameMetallicTex(TEXT("MetallicTexture"));
			FName ParamNameRoughnessTex(TEXT("RoughnessTexture"));
			FName ParamNameEmissionTex(TEXT("EmissionTexture"));
			FName ParamNameOcclusionTex(TEXT("OcclusionTexture"));
			FName ParamNameSmoothnessMultiplier(TEXT("SmoothnessScalar"));
			FName ParamNameMetallicMultiplier(TEXT("MetallicScalar"));
			FName ParamNameBaseColorTint(TEXT("BaseColorTint"));

			if (auto AlbedoTex = FindObjectByNameInArray(TextureCandidates, Mat.AlbedoName))
				MIC->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(ParamNameBaseColor), AlbedoTex);
			if (auto NormalTex = FindObjectByNameInArray(TextureCandidates, Mat.NormalName))
			{
				ConfigureTextureSettings(NormalTex->GetPathName(), true, true);
				MIC->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(ParamNameNormal), NormalTex);
			}
			if (auto RoughnessTex = FindObjectByNameInArray(TextureCandidates, Mat.RoughnessName))
			{
				ConfigureTextureSettings(RoughnessTex->GetPathName(), false, true);
				MIC->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(ParamNameRoughnessTex), RoughnessTex);
			}
			if (auto MetallicTex = FindObjectByNameInArray(TextureCandidates, Mat.MetallicName))
			{
				ConfigureTextureSettings(MetallicTex->GetPathName(), false, true);
				MIC->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(ParamNameMetallicTex), MetallicTex);
			}
			if (auto EmissiveTex = FindObjectByNameInArray(TextureCandidates, Mat.EmissiveName))
				MIC->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(ParamNameEmissionTex), EmissiveTex);
			if (auto OcclusionTex = FindObjectByNameInArray(TextureCandidates, Mat.OcclusionName))
			{
				ConfigureTextureSettings(OcclusionTex->GetPathName(), false, true);
				MIC->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(ParamNameOcclusionTex), OcclusionTex);
			}
			MIC->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(ParamNameSmoothnessMultiplier), Mat.SmoothnessValue);
			MIC->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(ParamNameMetallicMultiplier), Mat.MetallicValue);			
			MIC->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(ParamNameBaseColorTint), Mat.Tint.ToLinearColor());

			MIC->PostEditChange(); 
			MIC->MarkPackageDirty(); 

			if (SkeletalMesh)
			{
				if (ModifiedMaterials[TargetMaterialIndex].MaterialInterface != MIC)
				{
					ModifiedMaterials[TargetMaterialIndex].MaterialInterface = MIC;
					bSkelMaterialChanged = true;
				}
			}
			else if (StaticMesh)
			{
				if (StaticMesh->GetMaterial(TargetMaterialIndex) != MIC)
				{
					StaticMesh->SetMaterial(TargetMaterialIndex, MIC);
					StaticMesh->MarkPackageDirty();
				}
			}
		}
		
		if (SkeletalMesh && bSkelMaterialChanged)
		{
			SkeletalMesh->SetMaterials(ModifiedMaterials);
			SkeletalMesh->MarkPackageDirty();
		}
	}
}

void FMetaTailorAssetHandler::ConfigureTextureSettings(const FString& TexturePathInProject, bool bIsNormalMap, bool bIsLinear)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<
		FAssetRegistryModule>("AssetRegistry");
	FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(TexturePathInProject));
	UTexture2D* Texture = Cast<UTexture2D>(AssetData.GetAsset());

	if (Texture)
	{
		bool bMadeChanges = false;
		if (bIsNormalMap && Texture->CompressionSettings != TC_Normalmap)
		{
			Texture->CompressionSettings = TC_Normalmap;
			Texture->LODGroup = TEXTUREGROUP_WorldNormalMap;
			bMadeChanges = true;
		}
		if (bIsLinear && Texture->SRGB)
		{
			Texture->SRGB = false;
			bMadeChanges = true;
		}

		if (bMadeChanges)
		{
			Texture->UpdateResource();
			Texture->PostEditChange();
			Texture->MarkPackageDirty();
		}
	}
}

template <typename T>
void GetComponentsInChildren(AActor* ParentActor, TArray<T*>& OutComponents, bool bIncludeSelf = true)
{
	if (!ParentActor)
		return;

	if (bIncludeSelf)
	{
		TArray<T*> SelfComponents;
		ParentActor->GetComponents<T>(SelfComponents);
		OutComponents.Append(SelfComponents);
	}

	// Get child actors recursively
	TArray<AActor*> ChildActors;
	ParentActor->GetAllChildActors(ChildActors, true);

	for (AActor* Child : ChildActors)
	{
		if (!Child) continue;

		TArray<T*> ChildComponents;
		Child->GetComponents<T>(ChildComponents);
		OutComponents.Append(ChildComponents);
	}
}

template TArray<UActorComponent*> FMetaTailorAssetHandler::GetComponentsInChildrenSimple(AActor* ParentActor);

template <typename T>
TArray<T*> FMetaTailorAssetHandler::GetComponentsInChildrenSimple(AActor* ParentActor)
{
	TArray<T*> Components;
	GetComponentsInChildren(ParentActor, Components, true);
	return Components;
}

bool FMetaTailorAssetHandler::ExportActorToFBX(AActor* ActorToExport, FString& OutFbxPath, FString& OutMaterialInfoPath)
{
	if (!ActorToExport)
	{
		UE_LOG(LogTemp, Error, TEXT("No actor to export"));
		return false;
	}

	const auto AbsoluteIntermediatePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectIntermediateDir());	
	const auto ModelGuid = FGuid::NewGuid();
	FString TempPath = FPaths::Combine(AbsoluteIntermediatePath, TEXT("MetaTailorExport"),ModelGuid.ToString());
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*TempPath))
	{
		PlatformFile.CreateDirectory(*TempPath);
	}
	FString ModelFileName = TEXT("model.fbx");
	FString MeshMaterialInfoFileName = TEXT("metadata.json");
	FString ModelPath = FPaths::Combine(TempPath, ModelFileName);
	FString MeshMaterialInfoPath = FPaths::Combine(TempPath, MeshMaterialInfoFileName);

	auto Options = NewObject<UFbxExportOption>();
	Options->bExportMorphTargets = false;
	Options->VertexColor = true;
	Options->LevelOfDetail = false;
	Options->BakeMaterialInputs = EFbxMaterialBakeMode::Disabled; //simple doesn't seem to actually work for FBX, as of 5.6.0, so it's handled manually elsewhere
	Options->Collision = false;

	UAssetExportTask* ExportTask = NewObject<UAssetExportTask>();

	//the Map exporter automatically handles child components and multiple skeletal meshes on the same Actor
	//so we simply export the world but only the selected objects to take advantage of that builtin handling
	GEditor->SelectNone(false, true);
	GEditor->SelectActor(ActorToExport, true, true);

	ExportTask->Object = ActorToExport->GetWorld();
	ExportTask->Filename = ModelPath;
	ExportTask->bSelected = true;
	ExportTask->bReplaceIdentical = true;
	ExportTask->bPrompt = false;
	ExportTask->bUseFileArchive = false;
	ExportTask->bWriteEmptyFiles = false;
	ExportTask->Options = Options;
	ExportTask->bAutomated = true;
	bool bExportSuccess = UExporter::RunAssetExportTask(ExportTask);

	GEditor->SelectNone(false, true);

	
	if (!bExportSuccess)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to export %s to FBX."), *ActorToExport->GetName());
		return false;
	}

	
	auto ToMetaTailorInfo = GetMeshMaterialInfoFromActor(ActorToExport,FPaths::Combine(TempPath,TEXT("Textures")));
	
	auto JsonObject = UStructToJsonObjectPascalCaseRecursive(FUnrealToMetaTailorData::StaticStruct(), &ToMetaTailorInfo);
	
	if (!JsonObject.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to serialize material info."));
		return false;
	}
	FString OutputJsonString;
	TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&OutputJsonString);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), JsonWriter);
	if (!FFileHelper::SaveStringToFile(OutputJsonString,*MeshMaterialInfoPath))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to save material info."));
		return false;
	}	

	UE_LOG(LogTemp, Log, TEXT("Successfully exported %s to %s"), *ActorToExport->GetName(), *ModelPath);
	OutFbxPath = ModelPath;
	OutMaterialInfoPath = MeshMaterialInfoPath;
	return true;
}


bool FMetaTailorAssetHandler::ApplyClothingToActor(UObject* SourceClothingAsset, AActor* TargetActor,
                                                   bool bReplaceExisting)
{
	if (!SourceClothingAsset || !TargetActor) return false;

	USkeletalMeshComponent* TargetSkelComp = TargetActor->FindComponentByClass<USkeletalMeshComponent>();
	if (!TargetSkelComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("ApplyClothing: Target actor %s has no SkeletalMeshComponent."),
		       *TargetActor->GetName());
		return false;
	}

	USkeletalMesh* ClothingSkelMesh = Cast<USkeletalMesh>(SourceClothingAsset);
	UStaticMesh* ClothingStaticMesh = Cast<UStaticMesh>(SourceClothingAsset);

	if (!ClothingSkelMesh && !ClothingStaticMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("ApplyClothing: Source asset %s is not a Skeletal or Static Mesh."),
		       *SourceClothingAsset->GetName());
		return false;
	}

	if (ClothingSkelMesh)
	{
		if (!ClothingSkelMesh->GetSkeleton())
		{
			UE_LOG(LogTemp, Warning, TEXT("ApplyClothing: Clothing mesh %s has no skeleton assigned."),
				   *ClothingSkelMesh->GetName());
		}
	}
	if (bReplaceExisting)
	{
		ClearMetaTailorClothes(TargetActor);
	}

	FName NewComponentName = MakeUniqueObjectName(TargetActor, USceneComponent::StaticClass(),
	                                              FName(
		                                              *FString::Printf(
			                                              TEXT("MT_%s"), *SourceClothingAsset->GetName())));

	if (ClothingSkelMesh)
	{
		USkeletalMeshComponent* NewClothingComp = NewObject<USkeletalMeshComponent>(TargetActor, NewComponentName);
		if (NewClothingComp)
		{
			NewClothingComp->RegisterComponent();
			NewClothingComp->AttachToComponent(TargetSkelComp, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			NewClothingComp->SetSkeletalMesh(ClothingSkelMesh);
			NewClothingComp->SetLeaderPoseComponent(TargetSkelComp);
			NewClothingComp->ComponentTags.Add(FName("MetaTailorClothing")); //originally going to be used for easy removal, but using custom component now, leaving it in case that changes
			TargetActor->AddInstanceComponent(NewClothingComp);
			UE_LOG(LogTemp, Log, TEXT("Applied Skeletal Mesh clothing %s to %s"), *SourceClothingAsset->GetName(), *TargetActor->GetName());
			return true;
		}
	}
	else if (ClothingStaticMesh)
	{
		UStaticMeshComponent* NewClothingComp = NewObject<UStaticMeshComponent>(TargetActor, NewComponentName);
		if (NewClothingComp)
		{
			NewClothingComp->RegisterComponent();
			NewClothingComp->AttachToComponent(TargetSkelComp,
			                                   FAttachmentTransformRules::SnapToTargetNotIncludingScale
			                                   /*, TargetSocketName */);
			NewClothingComp->SetStaticMesh(ClothingStaticMesh);
			NewClothingComp->ComponentTags.Add(FName("MetaTailorClothing"));
			TargetActor->AddInstanceComponent(NewClothingComp);
			UE_LOG(LogTemp, Log, TEXT("Applied Static Mesh clothing %s to %s"), *SourceClothingAsset->GetName(),
			       *TargetActor->GetName());
			return true;
		}
	}

	return false;
}

void FMetaTailorAssetHandler::ClearMetaTailorClothes(AActor* TargetActor)
{
	if (!TargetActor) return;

	UE_LOG(LogTemp, Log, TEXT("Attempting to clear clothing"));
	TArray<UActorComponent*> CurrentComponents = TargetActor->GetComponents().Array();
	bool bClearedAny = false;

	for (UActorComponent* Component : CurrentComponents)
	{
		if (Component && Component->ComponentHasTag(FName("MetaTailorClothing")))
		{
			Component->UnregisterComponent();
			Component->DestroyComponent();
			bClearedAny = true;
		}
	}
	if (bClearedAny)
	{
		UE_LOG(LogTemp, Log, TEXT("Cleared MetaTailor clothing components from %s"), *TargetActor->GetName());
	}
}

UTexture* FindTextureFromExpression(UMaterialExpression* Expression)
{
	if (!Expression)
	{
		UE_LOG(LogTemp,Log, TEXT("No expression found"));
		return nullptr;
	}

	if (UMaterialExpressionTextureSample* TextureSample = Cast<UMaterialExpressionTextureSample>(Expression))
		return TextureSample->Texture;

	auto InputCount = Expression->CountInputs();
	for (int32 i = 0; i < InputCount; i++)
	{
		if (UTexture* FoundTexture = FindTextureFromExpression(Expression->GetInput(i)->Expression))
			return FoundTexture;
	}

	return nullptr;
}
void SaveTextureAsPNG(UTexture2D* Texture, const FString& FilePath)
{
    if (!Texture)
    {
        UE_LOG(LogTemp, Warning, TEXT("SaveTextureAsPNG: Texture is null."));
        return;
    }

    if (!Texture->GetPlatformData() || !Texture->GetPlatformData()->Mips.IsValidIndex(0))
    {
        UE_LOG(LogTemp, Warning, TEXT("SaveTextureAsPNG: Texture has no valid platform data or mips."));
        return;
    }

    const FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
    const void* PixelData = Mip.BulkData.LockReadOnly();

    if (!PixelData)
    {
        UE_LOG(LogTemp, Error, TEXT("SaveTextureAsPNG: Failed to lock texture bulk data for reading."));
        return;
    }

    // Load the image wrapper module
    IImageWrapperModule& WrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
    TSharedPtr<IImageWrapper> ImageWrapper = WrapperModule.CreateImageWrapper(EImageFormat::PNG);

    if (!ImageWrapper.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("SaveTextureAsPNG: Failed to create PNG image wrapper."));
        Mip.BulkData.Unlock();
        return;
    }

    // Get the dimensions of the texture
    const int32 Width = Mip.SizeX;
    const int32 Height = Mip.SizeY;

    // Set the raw pixel data on the image wrapper
    TArray<uint8> RawData;
    const int64 RawDataSize = Width * Height * 4; // Assuming 4 channels (BGRA)
    RawData.SetNumUninitialized(RawDataSize);
    FMemory::Memcpy(RawData.GetData(), PixelData, RawDataSize);
    
    Mip.BulkData.Unlock();

    if (ImageWrapper->SetRaw(RawData.GetData(), RawData.Num(), Width, Height, ERGBFormat::BGRA, 8))
    {
        // Compress the image to the PNG format
        const TArray64<uint8>& CompressedData = ImageWrapper->GetCompressed(100);

        // Save the compressed data to a file
        if (FFileHelper::SaveArrayToFile(CompressedData, *FilePath))
        {
            UE_LOG(LogTemp, Log, TEXT("Successfully saved texture to: %s"), *FilePath);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to save texture to: %s"), *FilePath);
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to set raw data on image wrapper."));
    }
}
bool SavePixelDataAsPNG(const TArray<FColor>& PixelData, int32 Width, int32 Height, const FString& FilePath)
{
    if (PixelData.Num() != Width * Height)
    {
        UE_LOG(LogTemp, Error, TEXT("SavePixelDataAsPNG: Pixel data size does not match dimensions. Expected %d pixels, but got %d."), (Width * Height), PixelData.Num());
        return false;
    }

    if (Width <= 0 || Height <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("SavePixelDataAsPNG: Invalid dimensions. Width and Height must be greater than 0."));
		return false;
	}
    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
    
    TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

    if (!ImageWrapper.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("SavePixelDataAsPNG: Failed to create a PNG image wrapper."));
        return false;
    }
    if (ImageWrapper->SetRaw(PixelData.GetData(), PixelData.Num() * sizeof(FColor), Width, Height, ERGBFormat::BGRA, 8))
    {
        const TArray64<uint8>& CompressedData = ImageWrapper->GetCompressed(100);
        if (FFileHelper::SaveArrayToFile(CompressedData, *FilePath))
        {
            //UE_LOG(LogTemp, Log, TEXT("Successfully saved pixel data to: %s"), *FilePath);
        	return true;
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to save pixel data to file: %s"), *FilePath);
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to set raw data on the image wrapper. Image may not have been saved."));
    }
	return false;
}
const TArray PropertiesToBake = {	
	MP_BaseColor,
	MP_Normal,
	MP_Roughness,
	MP_Metallic,
	MP_AmbientOcclusion,
	MP_EmissiveColor,
	MP_Opacity,
};
TArray<UMaterialInterface*> GetMaterials(USkeletalMeshComponent* SMC)
{
	TArray<UMaterialInterface*> Materials;
	for (auto Mat : SMC->GetMaterials())
	{
		Materials.Add(Mat);
	}
	return Materials;
}
TArray<UMaterialInterface*> GetMaterialsReferencedByLod0(USkeletalMeshComponent* SMC)
{
	TArray<UMaterialInterface*> Materials;
	if (!SMC || !SMC->GetSkeletalMeshAsset())
		return Materials;

	auto SkeletalMesh = SMC->GetSkeletalMeshAsset();
	TArray<UMaterialInterface*> SmcMaterials = SMC->GetMaterials();
	if (!SkeletalMesh->IsValidLODIndex(0))
	{
		UE_LOG(LogTemp,Error, TEXT("Unexpected missing lod 0"));
		return Materials;
	}

	for (const auto& Section : SkeletalMesh->GetImportedModel()->LODModels[0].Sections)
	{
		Materials.Add(SmcMaterials[Section.MaterialIndex]);
	}
	return Materials;
}
TArray<UMaterialInterface*> GetStaticMaterialsReferencedByLod0(UStaticMeshComponent* SMC)
{
	TArray<UMaterialInterface*> Materials;
	if (!SMC || !SMC->GetStaticMesh())
		return Materials;

	auto StaticMesh = SMC->GetStaticMesh();
	TArray<UMaterialInterface*> SmcMaterials = SMC->GetMaterials();
	if (StaticMesh->GetNumLODs() <= 0)
	{
		UE_LOG(LogTemp,Error, TEXT("Unexpected missing lod 0"));
		return Materials;
	}

	const FStaticMeshLODResources& Lod = StaticMesh->GetLODForExport(0);
	for (const auto& Section : Lod.Sections)
	{
		Materials.Add(SmcMaterials[Section.MaterialIndex]);
	}
	return Materials;
}
bool IsMetaHumanSkinMaterial(UMaterialInterface* MI)
{
	FString Name;
	if (!MI)
		return false;
	auto BaseMaterial = MI->GetBaseMaterial();
	if (!BaseMaterial)
		return false;
	return BaseMaterial->GetName().Contains("skin_unified_baked");	
}
void SwapMetaHumanLodMaterials(TArray<UMaterialInterface*>& InOutMaterials, const TArray<UMaterialInterface*>& PossibleOptions)
{
	//MetaHumans sometimes use grooms for eyelashes for example, and have Lod 0 be hidden entirely using a transparent material. This swaps those out for the visible materials
	for (auto& PotentiallySwapOut: InOutMaterials)
	{
		if (!PotentiallySwapOut)
			continue;
		auto MaterialName = PotentiallySwapOut->GetName();
		//pattern for eyelashes specifically
		auto TargetName= MaterialName + TEXT("HiLODs");
		for (const auto& TargetMaterial : PossibleOptions)
		{
			if (!TargetMaterial)
				continue;
			if (TargetMaterial->GetName() == TargetName)
				PotentiallySwapOut = TargetMaterial;
		}
	}
	
}
FUnrealToMetaTailorData FMetaTailorAssetHandler::GetMeshMaterialInfoFromActor(AActor* Actor, const FString& SaveTexturesIn)
{
	FUnrealToMetaTailorData Ret;
    Ret.BridgeVersion = IPluginManager::Get().FindPlugin("MetaTailorBridge")->GetDescriptor().VersionName;
    if (!Actor)
	    return Ret;
	
	IMaterialBakingModule& Module = FModuleManager::Get().LoadModuleChecked<IMaterialBakingModule>("MaterialBaking");
	
	TArray<FMeshData> MeshSettingsArray;
	TArray<FMaterialDataEx> MaterialSettingsArray;
	TArray<FBakeOutputEx> BakeOutputs;
	TArray<FString> TextureNames;

	TMap<UMaterialInterface*, TMap<EMaterialProperty, FString>> MaterialToPropertyToTextureName;
	
	int TextureNameIndex = 0;		
	int DIMS = GetDefault<UMetaTailorBridgeSettings>()->ExportTextureResolution;
	const float DEFAULT_SMOOTHNESS = 0.5f;
	TArray<UObject*> MeshComponents;
	for (auto SkeletalMC : GetComponentsInChildrenSimple<USkeletalMeshComponent>(Actor))
		MeshComponents.Add(SkeletalMC);
	for (auto StaticMC : GetComponentsInChildrenSimple<UStaticMeshComponent>(Actor))
		MeshComponents.Add(StaticMC);
	
	for (UObject* Obj : MeshComponents)
	{		
		if (!Obj)
			continue;
		FMetaTailorMeshInfo MeshInfo;
		
		//as of Unreal 5.6.0, the retrieved asset name does not match the exported object name if it's a single item nested in a blueprint, seems to be a bug in the fbx exporter
		//three cases for mesh handling:
		//1. The item exists directly. Unreal creates the node with the Name of the actor component directly
		//2. The item is a blueprint with more than one child. The node has each actor component as a child, with the names of the Actor components used directly
		//3. Odd one out. If there's a blueprint with exactly one actor component child, the fbx node's name is the label of the blueprint, seemingly offset by an ever increasing number
		//  for example, a blueprint called MySmallBlueprint is instanced with the name TheBlueprintInstance in the parent blueprint,  has a single StaticMeshComponent called MyChildMesh,
		//  it will have the name MySmallBlueprint1, whereas in the other circumstances it would be called MyChildMesh
		//some bandage solutions for this end up conflicting with other bandage solutions, and run into other issues like generally incrementing numbers for names
		//for now, just ignoring the case 3 of a blueprint only containing a single MeshComponent
		
		TArray<UMaterialInterface*> MaterialsToIterate;
		if (auto SkeletalMC = Cast<USkeletalMeshComponent>(Obj))
		{
			USkeletalMesh* MeshAsset = SkeletalMC->GetSkeletalMeshAsset();
			if (!MeshAsset)
				continue;
			MeshInfo.MeshName = SkeletalMC->GetName();
			MaterialsToIterate = GetMaterialsReferencedByLod0(SkeletalMC);
			SwapMetaHumanLodMaterials(MaterialsToIterate, SkeletalMC->GetMaterials());			
		}
		else if (auto StaticMC = Cast<UStaticMeshComponent>(Obj))
		{
			if (!StaticMC->GetStaticMesh())
				continue;
			FString NameString = StaticMC->GetName();
			auto ActorName = StaticMC->GetOwner()->GetName();
			UE_LOG(LogTemp,Error,TEXT("Actor name was %s"), *NameString);
			MeshInfo.MeshName = NameString;
			MaterialsToIterate = GetStaticMaterialsReferencedByLod0(StaticMC);
		}
		else
		{
			UE_LOG(LogTemp,Error,TEXT("Unexpected object type when serializing materials"));
			continue;
		}		
		
		for (auto MaterialInterface : MaterialsToIterate)
		{
			if (MaterialInterface)
			{
				auto EntryKey = MaterialInterface;
				FMetaTailorMaterialInfo MaterialInfo;
				
				MaterialInfo.Tint = FMetaTailorLinearColor(FColor::White);				
				MaterialInfo.SmoothnessValue = DEFAULT_SMOOTHNESS; //this and metallic get overridden below if a baked texture is present
				MaterialInfo.MetallicValue = 0.0f;
				
				MaterialInfo.Name = MaterialInterface->GetName();
				for (auto MaterialProperty : PropertiesToBake)
				{
					if (!MaterialInterface->GetMaterial()->IsPropertyConnected(MaterialProperty)) //note that this doesn't seem to catch complex material setups where the material attributes are abstracted behind other nodes
						continue;
					if (!MaterialToPropertyToTextureName.Contains(EntryKey))
						MaterialToPropertyToTextureName.Add(EntryKey);
					FString TextureName;
					if (MaterialToPropertyToTextureName[EntryKey].Contains(MaterialProperty))
					{
						TextureName = MaterialToPropertyToTextureName[EntryKey][MaterialProperty];	
					}
					else if (MaterialProperty != MP_Opacity || !IsMetaHumanSkinMaterial(MaterialInterface)) //special handling, early out for MH opacity is treated as subsurface scattering input, so we don't want to bake it
					{
						const FMaterialPropertyEx& Property = FMaterialPropertyEx(MaterialProperty);
						FMaterialDataEx MatData;
						MatData.Material = MaterialInterface;
						MatData.PropertySizes.Add(Property, FIntPoint(DIMS,DIMS));
						MatData.bTangentSpaceNormal = true;

						MeshSettingsArray.Add(FMeshData());
						MaterialSettingsArray.Add(MatData);
						TextureName = FString::FromInt(TextureNameIndex);
						TextureNames.Add(TextureName);
						TextureNameIndex++;
						MaterialToPropertyToTextureName[EntryKey].Add(MaterialProperty, TextureName);
					}


					switch (MaterialProperty)
					{
					case MP_BaseColor:
						MaterialInfo.AlbedoName = TextureName;
						break;
					case MP_Normal:
						MaterialInfo.NormalName = TextureName;
						break;
					case MP_Roughness:
						MaterialInfo.SmoothnessValue = DEFAULT_SMOOTHNESS;
						MaterialInfo.RoughnessName = TextureName;
						break;
					case MP_Metallic:
						MaterialInfo.MetallicValue = 1.0f;
						MaterialInfo.MetallicName = TextureName;
						break;
					case MP_AmbientOcclusion:
						MaterialInfo.OcclusionName = TextureName;
						break;
					case MP_EmissiveColor:
						MaterialInfo.EmissiveName = TextureName;
						break;
					case MP_Opacity:
						MaterialInfo.OpacityName = TextureName;
						break;
					default:
						UE_LOG(LogTemp, Error, TEXT("Unhandled property"));
						break;							
					}						
				}				
				MeshInfo.Materials.Add(MaterialInfo);
			}
		}		
		Ret.MeshInfos.Add(MeshInfo);
	}
	
	TArray<FMeshData*> MeshSettingsArrayPointers;
	MeshSettingsArrayPointers.Reserve(MeshSettingsArray.Num());			
	for (auto& MeshSettings : MeshSettingsArray) {MeshSettingsArrayPointers.Add(&MeshSettings);}
			
	TArray<FMaterialDataEx*> MaterialSettingsArrayPointers;
	MaterialSettingsArrayPointers.Reserve(MaterialSettingsArray.Num());
	for (auto& MaterialSettings : MaterialSettingsArray) {MaterialSettingsArrayPointers.Add(&MaterialSettings);}

	Module.SetLinearBake(true);
	Module.BakeMaterials(MaterialSettingsArrayPointers,MeshSettingsArrayPointers,BakeOutputs);
	Module.SetLinearBake(false);
	
	ensure(BakeOutputs.Num() == MaterialSettingsArrayPointers.Num()
		&& MeshSettingsArrayPointers.Num() == MaterialSettingsArrayPointers.Num()
		&& TextureNames.Num() == MaterialSettingsArrayPointers.Num());

	int TextureNumber = 0;
	for (auto BakeOutput : BakeOutputs)
	{
		FString TextureName = FString::FromInt(TextureNumber);
		TextureNumber++;
		
		ensure(BakeOutput.PropertyData.Num() == 1); //unordered nature of the map means we rely on single elements to maintain pairings between the earlier arrays and the texture names used hereafter
		
		for (auto PropertyTuple : BakeOutput.PropertyData)				
		{
			//UE_LOG(LogTemp,Log,TEXT("PropertyData %s, islinear: %s"), *UEnum::GetValueAsString(PropertyTuple.Key.Type), (BakeOutput.PropertyIsLinearColor[PropertyTuple.Key]?TEXT("true"):TEXT("false")));
			//UE_LOG(LogTemp,Log,TEXT("Wanting to save %s to file"), *TextureName);					
			auto ColorArray = PropertyTuple.Value;

			if (ColorArray.Num()<=1) //this is typically if baking fails for some reason, like no texture being used
			{
				FColor ConstantColor = FColor::White;
				if (ColorArray.Num() == 1)
				{
					ConstantColor = ColorArray[0];
					ConstantColor.A = 1.0f;
					UE_LOG(LogTemp,Log,TEXT("Color is %s"), *ConstantColor.ToString())					
				}
				UE_LOG(LogTemp,Warning, TEXT("Found bad data for property %s on texture %s"), *UEnum::GetValueAsString(PropertyTuple.Key.Type), *TextureName);
				for (auto &MeshInfo : Ret.MeshInfos)
				for (auto &Entry : MeshInfo.Materials)
				{
					switch (PropertyTuple.Key.Type)
					{
					case MP_BaseColor:
						if (Entry.AlbedoName==TextureName)
						{
							Entry.AlbedoName = TEXT("");
							//assign while preserving A just in case a later algorithm change allows for opacity being assigned first
							//the tint is also a LinearColor while pixel color is standard, so need to divide
							Entry.Tint.r = ConstantColor.R/255.0f;
							Entry.Tint.g = ConstantColor.G/255.0f;
							Entry.Tint.b = ConstantColor.B/255.0f;
						}
						break;
					case MP_Normal:
						if (Entry.NormalName==TextureName)
							Entry.NormalName = TEXT("");
						break;
					case MP_Roughness:
						if (Entry.RoughnessName==TextureName)
						{
							Entry.SmoothnessValue = 0.0f;
							Entry.RoughnessName = TEXT("");
						}
						break;
					case MP_Metallic:
						if (Entry.MetallicName==TextureName)
						{
							Entry.MetallicValue = 0.0f;
							Entry.MetallicName = TEXT("");
						}
						break;
					case MP_AmbientOcclusion:
						if (Entry.OcclusionName==TextureName)
							Entry.OcclusionName = TEXT("");
						break;
					case MP_EmissiveColor:
						if (Entry.EmissiveName==TextureName)
							Entry.EmissiveName = TEXT("");
						break;
					case MP_Opacity:
						if (Entry.OpacityName==TextureName)
						{
							Entry.OpacityName = TEXT("");
							if (Entry.Name.Contains("EyeShell") || Entry.Name == "M_Hide") //MetaHuman eye stuff is ultra weird, doing some manual shenanigans
							{
								Entry.Tint.a = 0.0f;
							}
							else
								Entry.Tint.a = ConstantColor.R/255.0f; //intentional R use, opacity data is in the rgb
						}
						break;
					default:
						UE_LOG(LogTemp,Error,TEXT("Unhandled property"));
						break;
					}
				}
			}
			else
			{
				for (FColor& Pixel: ColorArray)
					Pixel.A = 255;
				FString FilePath = FPaths::Combine(SaveTexturesIn,TextureName + TEXT(".png"));
				if (SavePixelDataAsPNG(ColorArray,DIMS,DIMS, FilePath))
				{
					UE_LOG(LogTemp,Log,TEXT("Successfully saved pixel data to: %s"), *FilePath);
				}
				else
				{
					UE_LOG(LogTemp,Error,TEXT("Failed to save pixel data to file: %s"), *FilePath);	
				}
			}
		}
	}
	
    return Ret;
}
