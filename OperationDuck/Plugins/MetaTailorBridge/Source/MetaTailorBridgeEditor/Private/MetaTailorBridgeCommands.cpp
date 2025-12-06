#include "MetaTailorBridgeCommands.h"

#define LOCTEXT_NAMESPACE "FMetaTailorBridgeModule"

void FMetaTailorBridgeCommands::RegisterCommands()
{
	UI_COMMAND(OpenPluginWindow, "MetaTailorBridge", "Bring up MetaTailorBridge window", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(SendToMtButton, "MetaTailorBridge", "Sends to MT", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ReceiveFromMtButton, "MetaTailorBridge", "Receives from MT", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
