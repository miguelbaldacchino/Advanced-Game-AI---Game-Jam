#include "SMetaTailorMainWindow.h"
#include "MetaTailorAssetHandler.h"
#include "MetaTailorBridgeSettings.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Images/SImage.h"
#include "Editor/EditorEngine.h"
#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "ISettingsModule.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "BlueprintEditorModule.h"
#include "Widgets/Input/SNumericEntryBox.h"


#define LOCTEXT_NAMESPACE "SMetaTailorMainWindow"

class FAssetRegistryModule;
class ISettingsModule;

bool SMetaTailorMainWindow::IsDockedInBlueprint()
{
	auto OptionalActiveWindow = FSlateApplication::Get().FindWidgetWindow(SharedThis(this));

	if (UAssetEditorSubsystem* EditorSys = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
	{
		for (UObject* Asset : EditorSys->GetAllEditedAssets())
		{
			if (UBlueprint* Candidate = Cast<UBlueprint>(Asset))
			{
				IBlueprintEditor* Editor = static_cast<IBlueprintEditor*>(EditorSys->FindEditorForAsset(
					Candidate, false));
				auto EditorWidget = Editor->GetToolkitHost()->GetParentWidget();
				auto BpWindow = FSlateApplication::Get().FindWidgetWindow(EditorWidget);
				if (BpWindow == OptionalActiveWindow)
				{
					return true;
				}
			}
		}
	}
	return false;
}

void SMetaTailorMainWindow::Construct(const FArguments& InArgs)
{
	bReplaceClothing = false;
	bLimitBrowserToCompatible = false;
	
	const UMetaTailorBridgeSettings* Settings = GetDefault<UMetaTailorBridgeSettings>();

	const auto FullOutfitPath = "/All" + Settings->PathForOutfitImports;
	UE_LOG(LogTemp, Log, TEXT("Full Outfit path: %s"), *FullOutfitPath);
	auto ContentBrowserConfig = CreateAssetPickerConfiguration(FullOutfitPath);
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(
		"ContentBrowser");
	auto ContentBrowser = ContentBrowserModule.Get().CreateAssetPicker(ContentBrowserConfig);

	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(5, 5, 5, 5)
			[
				SNew(SHorizontalBox)
				// Settings Button
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.HAlign(HAlign_Left)
				[
					SNew(SButton)
					.Text(LOCTEXT("SettingsButton", "Settings"))
					.OnClicked(this, &SMetaTailorMainWindow::OnSettingsClicked)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						if (this->IsDockedInBlueprint())
							return FText::FromString(
								"Undock this from a Blueprint Editor to link it to the selected actor in the Map");
						return FText::FromString(
							"Dock this window to a Blueprint Editor to link it to the edited Blueprint instead of the Map");
					})
				]
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.Text(LOCTEXT("Blah", "?"))
					.OnClicked(this, &SMetaTailorMainWindow::OnHelpClicked)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					FString ActorName = FString("None");
					if (this->IsDockedInBlueprint())
					{
						auto BP = FMetaTailorAssetHandler::GetBlueprint();
						if (BP)
							ActorName = BP->GetFriendlyName();
					}
					else
					{
						auto Actor = FMetaTailorAssetHandler::GetSelectedTargetActor();
						if (Actor)
							ActorName = Actor->GetName();
					}
					return FText::FromString(FString::Printf(TEXT("Linked to: %s"), *ActorName));
				})
			]

			//just for testing things as they come up
			// + SVerticalBox::Slot()
			// .AutoHeight()
			// [
			//     SNew(SButton)
			//     .Text(LOCTEXT("Do not use", "Test Stuff"))
			//     .OnClicked_Lambda([this]
			//     {
			//         FMetaTailorAssetHandler::ReimportStagedFBX();
			//         return FReply::Handled();
			//     })
			// ]
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)				
				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Export Resolution", "Export Resolution"))
				]
				+SHorizontalBox::Slot()				
				.AutoWidth()
				[
					SNew(SNumericEntryBox<int32>)
					.Value_Lambda([]()
					{
						const UMetaTailorBridgeSettings* Settings = GetDefault<UMetaTailorBridgeSettings>();
						return Settings ? Settings->ExportTextureResolution : 1024;
					})
					.MinValue(UMetaTailorBridgeSettings::MIN_TEXTURE_RESOLUTION)
					.MaxValue(UMetaTailorBridgeSettings::MAX_TEXTURE_RESOLUTION)
					.OnValueCommitted_Lambda([](int32 Value, ETextCommit::Type CommitType)
					{
						UMetaTailorBridgeSettings* Settings = GetMutableDefault<UMetaTailorBridgeSettings>();
						Settings->ExportTextureResolution = Value;
						Settings->SaveConfig();
					})
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(5)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(FMargin(4.0f, 4.0f))
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SAssignNew(ReplaceToggle, SCheckBox)
                            .Content()
                            [
                                SNew(STextBlock).Text(
                                    LOCTEXT("ReplaceToggle",
                                            "Replace Existing Clothing"))
                            ]
                            .IsChecked(false) // Default state
                            .OnCheckStateChanged(
                                this, &SMetaTailorMainWindow::OnReplaceToggleChanged)
					]

					+ SVerticalBox::Slot()
					.MaxHeight(0.01)
					//.AutoHeight() //temporarily disabled display of this since it's unimplemented
					[
						SAssignNew(LimitCompatToggle, SCheckBox)
						.Content()
						[
							SNew(STextBlock).Text(LOCTEXT("LimitCompatToggle", "Limit Browser to Compatible Skeletons"))
						]
						.IsChecked(false)
						.OnCheckStateChanged(this, &SMetaTailorMainWindow::OnLimitCompatToggleChanged)
					]
				]
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				ContentBrowser
			]

		] // End Main Vertical Box
	]; // End Scrollbox

	UpdateWidgetStates();
}


SMetaTailorMainWindow::~SMetaTailorMainWindow()
{
}

FAssetPickerConfig SMetaTailorMainWindow::CreateAssetPickerConfiguration(FString FullPath)
{
	if (FullPath.EndsWith("/")) //otherwise checks/ensures fail internally
		FullPath.LeftChopInline(1);
	FAssetPickerConfig Config;
	Config.InitialAssetViewType = EAssetViewType::Tile;
	Config.bAllowNullSelection = true;
	Config.Filter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());
	Config.bCanShowDevelopersFolder = true;
	Config.Filter.PackagePaths.Add(FName(FullPath));
	Config.Filter.bRecursivePaths = true;
	Config.OnAssetSelected.BindLambda([this](const FAssetData& AssetData)
	{
		UE_LOG(LogTemp, Log, TEXT("OnAssetSelected for %s"), *AssetData.AssetName.ToString());
		if (!this->IsDockedInBlueprint())
		{
			const auto TargetActor = FMetaTailorAssetHandler::GetSelectedTargetActor();
			if (!TargetActor)
				return;

			if (!AssetData.IsValid())
				FMetaTailorAssetHandler::ClearMetaTailorClothes(TargetActor);
			else
				FMetaTailorAssetHandler::ApplyClothingToActor(AssetData.GetAsset(), TargetActor, this->bReplaceClothing);
		}
		else
		{
			if (!AssetData.IsValid())
			{
				FMetaTailorAssetHandler::RemoveClothingFromFocusedBlueprint();
			}			
			else
			{
				if (this->bReplaceClothing)
					FMetaTailorAssetHandler::RemoveClothingFromFocusedBlueprint();
				if (FMetaTailorAssetHandler::AddSkeletalMeshComponentToFocusedBlueprint(Cast<USkeletalMesh>(AssetData.GetAsset())))
				{
					FNotificationInfo ApplyInfo(LOCTEXT("AppliedClothing", "Applied clothing to selected actor."));
					ApplyInfo.ExpireDuration = 5.0f;
					FSlateNotificationManager::Get().AddNotification(ApplyInfo);
				}
				else
				{
					FNotificationInfo ApplyFailInfo(
						LOCTEXT("ApplyFail", "Failed to apply clothing (check compatibility?)."));
					ApplyFailInfo.ExpireDuration = 5.0f;
					FSlateNotificationManager::Get().AddNotification(ApplyFailInfo);
				}
			}
		}
		GEditor->RedrawAllViewports();
	});

	Config.SelectionMode = ESelectionMode::Single;
	return Config;
}


FReply SMetaTailorMainWindow::OnSettingsClicked()
{
	FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Editor", "Plugins", "METATAILOR Bridge");
	return FReply::Handled();
}

FReply SMetaTailorMainWindow::OnHelpClicked()
{
	FPlatformProcess::LaunchURL(
		TEXT("https://docs.google.com/document/d/1x7a5NJ_XAwv_BWaJdIVhvXw3_R-8hapgnokqrQmz4Ok/edit?usp=sharing"),
		nullptr, nullptr);
	return FReply::Handled();
}

void SMetaTailorMainWindow::OnReplaceToggleChanged(ECheckBoxState NewState)
{
	bReplaceClothing = (NewState == ECheckBoxState::Checked);
}

void SMetaTailorMainWindow::OnLimitCompatToggleChanged(ECheckBoxState NewState)
{
	bLimitBrowserToCompatible = (NewState == ECheckBoxState::Checked);
	UpdateWidgetStates();
}

void SMetaTailorMainWindow::UpdateWidgetStates()
{
	//some old legacy code went here, leaving it in case we need to manually update in the future if other bind approaches are insufficient for updates
}


#undef LOCTEXT_NAMESPACE
