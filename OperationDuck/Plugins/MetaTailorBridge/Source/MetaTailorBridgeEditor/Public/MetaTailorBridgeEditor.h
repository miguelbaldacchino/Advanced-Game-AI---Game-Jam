#pragma once

#include "MetaTailorAssetHandler.h"
#include "MetaTailorIPC.h"
#include "Modules/ModuleManager.h"

class FToolBarBuilder;
class FMenuBuilder;

class FMetaTailorBridgeEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	void PluginButtonClicked();
	void SendToMtButtonClicked();
	void ReceiveFromMtClicked();
	void StartReceiveSequence();
	void OnResponseReceived(bool bSuccess, const FDressedAvatarResponse& Response, const FString& ErrorMessage);

private:
	void RegisterMenus();
	void RegisterBlueprintButtons();

	TSharedRef<SDockTab> OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs);

	TSharedPtr<FUICommandList> PluginCommands;
	TSharedPtr<FMetaTailorIPC> IPCManager;
};
