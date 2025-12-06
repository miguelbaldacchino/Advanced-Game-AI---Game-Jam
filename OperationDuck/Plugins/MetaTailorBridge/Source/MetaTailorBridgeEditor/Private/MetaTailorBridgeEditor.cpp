#include "MetaTailorBridgeEditor.h"
#include "MetaTailorAssetHandler.h"
#include "MetaTailorBridgeStyle.h"
#include "MetaTailorBridgeCommands.h"
#include "MetaTailorIPC.h"
#include "SMetaTailorMainWindow.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "ToolMenus.h"
#include "Framework/Notifications/NotificationManager.h"

class FBlueprintEditorModule;
static const FName MetaTailorBridgeTabName("MetaTailorBridge");

#define LOCTEXT_NAMESPACE "FMetaTailorBridgeEditorModule"

TSharedPtr<SWindow> FindBlueprintEditorWindow()
{
	auto BP = FMetaTailorAssetHandler::GetBlueprint();
	if (!BP) return nullptr;
	UAssetEditorSubsystem* EditorSys = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!EditorSys) return nullptr;
	IAssetEditorInstance* Ed = EditorSys->FindEditorForAsset(BP, false);
	if (!Ed) return nullptr;
	TSharedPtr<SDockTab> DockTab = Ed->GetAssociatedTabManager()->GetOwnerTab();
	if (!DockTab) return nullptr;
	return FSlateApplication::Get().FindWidgetWindow(DockTab.ToSharedRef());
}

void FMetaTailorBridgeEditorModule::StartupModule()
{
	FMetaTailorBridgeStyle::Initialize();
	FMetaTailorBridgeStyle::ReloadTextures();
	FMetaTailorBridgeCommands::Register();
	
	IPCManager = MakeShared<FMetaTailorIPC>();
	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FMetaTailorBridgeCommands::Get().OpenPluginWindow,
		FExecuteAction::CreateRaw(this, &FMetaTailorBridgeEditorModule::PluginButtonClicked),
		FCanExecuteAction());
	PluginCommands->MapAction(
		FMetaTailorBridgeCommands::Get().SendToMtButton,
		FExecuteAction::CreateRaw(this, &FMetaTailorBridgeEditorModule::SendToMtButtonClicked),
		FCanExecuteAction());
	PluginCommands->MapAction(
		FMetaTailorBridgeCommands::Get().ReceiveFromMtButton,
		FExecuteAction::CreateRaw(this, &FMetaTailorBridgeEditorModule::ReceiveFromMtClicked),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(
			this, &FMetaTailorBridgeEditorModule::RegisterMenus));


	UE_LOG(LogTemp, Log, TEXT("Registering Nomad Tab Spawner in MT Startup Module"))
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(MetaTailorBridgeTabName,
	                                                  FOnSpawnTab::CreateRaw(
		                                                  this, &FMetaTailorBridgeEditorModule::OnSpawnPluginTab))
	                        .SetDisplayName(LOCTEXT("FMetaTailorBridgeTabTitle", "METATAILOR Bridge"))
	                        .SetMenuType(ETabSpawnerMenuType::Hidden);	
}

void FMetaTailorBridgeEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FMetaTailorBridgeStyle::Shutdown();

	FMetaTailorBridgeCommands::Unregister();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(MetaTailorBridgeTabName);
}

TSharedRef<SDockTab> FMetaTailorBridgeEditorModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	UE_LOG(LogTemp, Log, TEXT("In OnSpawnPluginTab"))
	return SNew(SDockTab)
		.TabRole(NomadTab)
		[
			SNew(SMetaTailorMainWindow)
		];
}

void FMetaTailorBridgeEditorModule::PluginButtonClicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(MetaTailorBridgeTabName);
}

void FMetaTailorBridgeEditorModule::SendToMtButtonClicked()
{
	UBlueprint* BP = FMetaTailorAssetHandler::GetBlueprint();
	if (!BP)
		return;
	FScopedSlowTask SlowTask(1.0f,FText::FromString("Sending to MetaTailor"));
	SlowTask.MakeDialog();
	auto GeneratedClass = BP->GeneratedClass;

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags = RF_Transient;
	auto TargetActor = GEditor->GetEditorWorldContext().World()->SpawnActor<AActor>(
		GeneratedClass, FVector::Zero(), FRotator::ZeroRotator, SpawnParams);

	auto SMCs = FMetaTailorAssetHandler::GetComponentsInChildrenSimple<USkeletalMeshComponent>(TargetActor);
	for (auto SMC : (SMCs))
	{
		if (!SMC->IsVisible())
			SMC->DestroyComponent();
	}
	
	FString FBXPath, MaterialInfoPath;
	auto bSuccess = FMetaTailorAssetHandler::ExportActorToFBX(TargetActor,FBXPath, MaterialInfoPath);
	TargetActor->Destroy();
	if (!bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("FBXPath is empty"));
		return;
	}


	IPCManager->SendExportedModel(FBXPath, [this](bool bSuccess, const FString& ErrorMsg)
	{
		if (bSuccess)
		{
			StartReceiveSequence();
		}
		else
		{
			FNotificationInfo Info(FText::Format(
				LOCTEXT("SendFailedError", "Failed to send to METATAILOR: {0}"), FText::FromString(ErrorMsg)));
			Info.ExpireDuration = 8.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
		}
	});
}

void FMetaTailorBridgeEditorModule::ReceiveFromMtClicked()
{
	StartReceiveSequence();
}

void FMetaTailorBridgeEditorModule::StartReceiveSequence()
{
	IPCManager->StartListeningForResponse(
		[this](bool bSuccess, const FDressedAvatarResponse& Response, const FString& ErrorMsg)
		{
			this->OnResponseReceived(bSuccess, Response, ErrorMsg);
		},
		FindBlueprintEditorWindow()
	);
}

void FMetaTailorBridgeEditorModule::OnResponseReceived(bool bSuccess, const FDressedAvatarResponse& Response,
                                                 const FString& ErrorMessage)
{

	//as of 5.5 using the AsyncTask approach ends up with an internal recursive call when Reimporting an FBX asset, using the ticker approach prevents this
	//AsyncTask(ENamedThreads::GameThread, [this, bSuccess, Response, ErrorMessage]()
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this, bSuccess, Response, ErrorMessage](float DeltaTime)->bool
	{
		if (bSuccess)
		{
			UObject* ImportedAsset = FMetaTailorAssetHandler::ImportModelFromResponse(Response);
			if (ImportedAsset)
			{
				FNotificationInfo Info(FText::Format(
					LOCTEXT("ImportedAsset", "Successfully imported: {0}"),
					FText::FromString(ImportedAsset->GetName())));
				Info.ExpireDuration = 5.0f;
				FSlateNotificationManager::Get().AddNotification(Info);
				
				if (Response.ParseType() == FDressedAvatarResponse::EResponseType::Outfit)
				{
					if (FMetaTailorAssetHandler::AddSkeletalMeshComponentToFocusedBlueprint(
						Cast<USkeletalMesh>(ImportedAsset)))
					{
						FNotificationInfo ApplyInfo(
							LOCTEXT("AppliedClothing", "Applied clothing to selected actor."));
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
			else
			{
				FNotificationInfo Info(LOCTEXT("ImportProcessFailed",
				                               "Failed during import processing. Check logs."));
				Info.ExpireDuration = 8.0f;
				FSlateNotificationManager::Get().AddNotification(Info);
			}
		}
		else
		{
			FNotificationInfo Info(FText::Format(
				LOCTEXT("ReceiveError", "Error receiving from METATAILOR: {0}"), FText::FromString(ErrorMessage)));
			Info.ExpireDuration = 8.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
		}
		return false;
	}));
}

void FMetaTailorBridgeEditorModule::RegisterMenus()
{
	UE_LOG(LogTemp, Log, TEXT("In Register Menus"))
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddMenuEntryWithCommandList(FMetaTailorBridgeCommands::Get().OpenPluginWindow, PluginCommands);
		}
	}

	RegisterBlueprintButtons();
}

void FMetaTailorBridgeEditorModule::RegisterBlueprintButtons()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* ButtonToolBar = UToolMenus::Get()->ExtendMenu("AssetEditor.BlueprintEditor.ToolBar");
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("AssetEditor.BlueprintEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddMenuEntryWithCommandList(FMetaTailorBridgeCommands::Get().OpenPluginWindow, PluginCommands);
		}
	}


	FSlateIcon MtIcon(FMetaTailorBridgeStyle::Get().GetStyleSetName(), "MetaTailorBridge.PluginIcon");
	FSlateIcon ExportIcon(FMetaTailorBridgeStyle::Get().GetStyleSetName(), "MetaTailorBridge.SendToMtIcon");
	FSlateIcon ImportIcon(FMetaTailorBridgeStyle::Get().GetStyleSetName(), "MetaTailorBridge.ReceiveFromMtIcon");
	FToolMenuSection& Section = ButtonToolBar->FindOrAddSection("METATAILOR");
	UE_LOG(LogTemp, Log, TEXT("Toolbarring SendToMtButtonClicked"))

	const auto OpenWindowCommand = FMetaTailorBridgeCommands::Get().OpenPluginWindow;
	const auto SendCommand = FMetaTailorBridgeCommands::Get().SendToMtButton;
	const auto ReceiveCommand = FMetaTailorBridgeCommands::Get().ReceiveFromMtButton;

	auto ContentBrowserEntry = FToolMenuEntry::InitToolBarButton(OpenWindowCommand,
	                                                             FText::FromString("Content Browser"),
	                                                             FText::FromString(
		                                                             "Opens the main METATAILOR Bridge window"),
	                                                             MtIcon);
	auto SendButtonEntry = FToolMenuEntry::InitToolBarButton(
		SendCommand,
		FText::FromString("Send to MT"),
		FText::FromString("Sends the blueprint to METATAILOR"),
		ExportIcon);
	auto ReceiveButtonEntry = FToolMenuEntry::InitToolBarButton(
		ReceiveCommand,
		FText::FromString("Receive from MT"),
		FText::FromString("Receives and applies clothing from METATAILOR"),
		ImportIcon);
	ContentBrowserEntry.SetCommandList(PluginCommands);
	SendButtonEntry.SetCommandList(PluginCommands);
	ReceiveButtonEntry.SetCommandList(PluginCommands);

	Section.AddEntry(ContentBrowserEntry);
	Section.AddEntry(SendButtonEntry);
	Section.AddEntry(ReceiveButtonEntry);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMetaTailorBridgeEditorModule, MetaTailorBridgeEditor)
