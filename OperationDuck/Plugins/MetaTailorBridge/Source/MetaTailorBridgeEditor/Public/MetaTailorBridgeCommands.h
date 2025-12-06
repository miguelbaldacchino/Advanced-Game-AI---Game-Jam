#pragma once

#include "Framework/Commands/Commands.h"
#include "MetaTailorBridgeStyle.h"

class FMetaTailorBridgeCommands : public TCommands<FMetaTailorBridgeCommands>
{
public:
	FMetaTailorBridgeCommands()
		: TCommands(TEXT("MetaTailorBridge"), NSLOCTEXT("Contexts", "MetaTailorBridge", "MetaTailorBridge Plugin"),
		            NAME_None, FMetaTailorBridgeStyle::GetStyleSetName())
	{
	}

	virtual void RegisterCommands() override;

	TSharedPtr<FUICommandInfo> OpenPluginWindow;
	TSharedPtr<FUICommandInfo> SendToMtButton;
	TSharedPtr<FUICommandInfo> ReceiveFromMtButton;
};
