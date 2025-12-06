#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Widgets/Notifications/SNotificationList.h"

struct FDressedAvatarResponse
{
	FString ModelPath;
	FString MetaTailorVersion;
	FString DataType;

	enum class EResponseType : uint8
	{
		Unknown,
		Outfit,
		Avatar
	};

	EResponseType ParseType() const
	{
		if (DataType.Equals(TEXT("Outfit"), ESearchCase::IgnoreCase)) return EResponseType::Outfit;
		if (DataType.Equals(TEXT("Avatar"), ESearchCase::IgnoreCase)) return EResponseType::Avatar;
		return EResponseType::Unknown;
	}

	bool FromJson(const TSharedPtr<FJsonObject>& JsonObject);
};


struct FImportAvatarRequest
{
	FString FilePath;
	FString SourceName;
	TSharedPtr<FJsonObject> ToJson() const;
};


class FMetaTailorIPC
{
public:
	FMetaTailorIPC();
	~FMetaTailorIPC();

	void SendExportedModel(const FString& ExportedFBXPath,
	                       TFunction<void(bool bSuccess, const FString& ErrorMessage)> OnComplete);
	void StartListeningForResponse(
		TFunction<void(bool bSuccess, const FDressedAvatarResponse& Response, const FString& ErrorMessage)>
		OnResponseReceived, TSharedPtr<SWindow> TargetWindow);
	void StopListening();

private:
	void HandleSendModelResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful,
	                             TFunction<void(bool, const FString&)> OnComplete);
	bool bIsListening;
	FSocket* ListenerSocket;
	TFunction<void(bool bSuccess, const FDressedAvatarResponse& Response, const FString& ErrorMessage)>
	CurrentResponseCallback;
	bool CheckForConnections(float val);
	FTickerDelegate TickDelegate;
	FTSTicker::FDelegateHandle TickHandle;

	int32 GetMetaTailorPort() const;
	int32 GetUnrealBridgePort() const;
	TSharedPtr<SNotificationItem> NotificationItem;
	FDelegateHandle WindowCloseDelegateHandle;
	TSharedPtr<SWindow> NotificationWindow;
};
