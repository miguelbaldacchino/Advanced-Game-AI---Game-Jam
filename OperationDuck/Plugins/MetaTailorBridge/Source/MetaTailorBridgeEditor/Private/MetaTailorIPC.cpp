#include "MetaTailorIPC.h"
#include "MetaTailorBridgeSettings.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Containers/Ticker.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Networking.h"
#include "TickableEditorObject.h"
#include "Framework/Notifications/NotificationManager.h"

TSharedPtr<FJsonObject> FImportAvatarRequest::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());
	JsonObject->SetStringField(TEXT("filePath"), FilePath);
	JsonObject->SetStringField(TEXT("sourceName"), SourceName);
	return JsonObject;
}

bool FDressedAvatarResponse::FromJson(const TSharedPtr<FJsonObject>& JsonObject)
{
	if (!JsonObject.IsValid()) return false;

	if (!JsonObject->TryGetStringField(TEXT("ModelPath"), ModelPath)) return false;
	JsonObject->TryGetStringField(TEXT("MetaTailorVersion"), MetaTailorVersion);
	if (!JsonObject->TryGetStringField(TEXT("DataType"), DataType)) return false;

	return true;
}


FMetaTailorIPC::FMetaTailorIPC() : bIsListening(false), ListenerSocket(nullptr)
{
	TickDelegate = FTickerDelegate::CreateRaw(this, &FMetaTailorIPC::CheckForConnections);
}

FMetaTailorIPC::~FMetaTailorIPC()
{
	StopListening();
}

int32 FMetaTailorIPC::GetMetaTailorPort() const
{
	const UMetaTailorBridgeSettings* Settings = GetDefault<UMetaTailorBridgeSettings>();
	return Settings ? Settings->MetaTailorPort : 53485;
}

int32 FMetaTailorIPC::GetUnrealBridgePort() const
{
	const UMetaTailorBridgeSettings* Settings = GetDefault<UMetaTailorBridgeSettings>();
	return Settings ? Settings->UnrealBridgePort : 53487;
}

void FMetaTailorIPC::SendExportedModel(const FString& ExportedFBXPath,
                                       TFunction<void(bool bSuccess, const FString& ErrorMessage)> OnComplete)
{
	FHttpModule& HttpModule = FHttpModule::Get();

	FImportAvatarRequest RequestData;
	RequestData.FilePath = ExportedFBXPath;
	RequestData.SourceName = TEXT("METATAILOR Bridge for Unreal");

	TSharedPtr<FJsonObject> RequestJson = RequestData.ToJson();
	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(RequestJson.ToSharedRef(), Writer);

	FString URL = FString::Printf(TEXT("http://127.0.0.1:%d/importavatar"), GetMetaTailorPort());

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = HttpModule.CreateRequest();
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetURL(URL);
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetContentAsString(RequestBody);
	HttpRequest->OnProcessRequestComplete().BindRaw(this, &FMetaTailorIPC::HandleSendModelResponse, OnComplete);

	if (!HttpRequest->ProcessRequest())
	{
		OnComplete(false, TEXT("Failed to start HTTP request."));
	}
}

void FMetaTailorIPC::HandleSendModelResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful,
                                             TFunction<void(bool, const FString&)> OnComplete)
{
	FString ErrorMessage;
	bool bSuccess = false;

	if (!bWasSuccessful || !Response.IsValid())
	{
		ErrorMessage = TEXT("HTTP Request Failed or No Response.");
	}
	else if (EHttpResponseCodes::IsOk(Response->GetResponseCode()))
	{
		bSuccess = true;
	}
	else
	{
		ErrorMessage = FString::Printf(TEXT("HTTP Error %d: %s"), Response->GetResponseCode(),
		                               *Response->GetContentAsString());
	}

	OnComplete(bSuccess, ErrorMessage);
}

void FMetaTailorIPC::StartListeningForResponse(
	TFunction<void(bool bSuccess, const FDressedAvatarResponse& Response, const FString& ErrorMessage)>
	OnResponseReceived, TSharedPtr<SWindow> TargetWindow)
{
	if (bIsListening)
	{
		OnResponseReceived(false, FDressedAvatarResponse(), TEXT("Already listening."));
		return;
	}
	this->NotificationWindow = TargetWindow;
	auto CancelButtonInfo = FNotificationButtonInfo(FText::FromString("Cancel"),
	                                                FText::FromString("Stop listening for a response from MT"),
	                                                FSimpleDelegate::CreateLambda([this] { StopListening(); }));
	auto ButtonArray = TArray<FNotificationButtonInfo>();
	ButtonArray.Add(CancelButtonInfo);
	FNotificationInfo Info(FText::FromString("Listening for MT..."));
	Info.FadeInDuration = 1.f;
	Info.FadeOutDuration = 1.f;
	Info.ExpireDuration = 0.0f; // 0 means it stays until manually removed
	Info.bUseThrobber = true;
	Info.bUseSuccessFailIcons = true;
	Info.bUseLargeFont = false;
	Info.bFireAndForget = false;
	Info.bAllowThrottleWhenFrameRateIsLow = false;
	Info.ButtonDetails = ButtonArray;
	Info.ForWindow = TargetWindow;

	if (NotificationItem)
		NotificationItem->ExpireAndFadeout();
	this->NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);

	if (NotificationItem)
		NotificationItem->SetCompletionState(SNotificationItem::CS_Pending);


	CurrentResponseCallback = OnResponseReceived;
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		CurrentResponseCallback(false, FDressedAvatarResponse(), TEXT("Failed to get Socket Subsystem."));
		return;
	}

	FIPv4Address Address;
	FIPv4Address::Parse(TEXT("127.0.0.1"), Address);
	TSharedRef<FInternetAddr> ListenAddr = SocketSubsystem->CreateInternetAddr();
	ListenAddr->SetIp(Address.Value);
	ListenAddr->SetPort(GetUnrealBridgePort());

	ListenerSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("MetaTailorListener"), false);
	if (!ListenerSocket)
	{
		CurrentResponseCallback(false, FDressedAvatarResponse(), TEXT("Failed to create listener socket."));
		return;
	}

	ListenerSocket->SetReuseAddr(true);
	ListenerSocket->SetNonBlocking(true);

	if (!ListenerSocket->Bind(*ListenAddr))
	{
		SocketSubsystem->DestroySocket(ListenerSocket);
		ListenerSocket = nullptr;
		CurrentResponseCallback(false, FDressedAvatarResponse(),
		                        FString::Printf(TEXT("Failed to bind to port %d."), GetUnrealBridgePort()));
		return;
	}

	if (!ListenerSocket->Listen(1))
	{
		SocketSubsystem->DestroySocket(ListenerSocket);
		ListenerSocket = nullptr;
		CurrentResponseCallback(false, FDressedAvatarResponse(), TEXT("Failed to listen on socket."));
		return;
	}

	bIsListening = true;
	UE_LOG(LogTemp, Log, TEXT("METATAILOR Bridge: Started listening on port %d"), GetUnrealBridgePort());
	
	if (!TickHandle.IsValid())
	{
		TickHandle = (FTSTicker::GetCoreTicker().AddTicker(TickDelegate, 0.1f));
	}
	if (TargetWindow)
	{
		ensure(!WindowCloseDelegateHandle.IsValid());
		WindowCloseDelegateHandle = TargetWindow->GetOnWindowClosedEvent().AddLambda(
			[this](TSharedRef<SWindow> WindowRef)
			{
				StopListening();
			});
	}
}

void FMetaTailorIPC::StopListening()
{
	if (ListenerSocket)
	{
		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (SocketSubsystem)
		{
			SocketSubsystem->DestroySocket(ListenerSocket);
		}
		ListenerSocket = nullptr;
	}
	bIsListening = false;
	CurrentResponseCallback = nullptr;

	if (NotificationItem)
	{
		if (NotificationItem->GetCompletionState() == SNotificationItem::CS_Pending)
			NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
		NotificationItem->ExpireAndFadeout();
		NotificationItem = nullptr;
	}
	if (WindowCloseDelegateHandle.IsValid())
	{
		if (NotificationWindow)
			NotificationWindow->GetOnWindowClosedEvent().Remove(WindowCloseDelegateHandle);
		WindowCloseDelegateHandle.Reset();
	}
	UE_LOG(LogTemp, Log, TEXT("MetaTailor Bridge: Stopped listening."));
}

bool FMetaTailorIPC::CheckForConnections(float val)
{
	if (!bIsListening || !ListenerSocket)
	{
		return false;
	}

	bool bHasPendingConnection;
	if (ListenerSocket->HasPendingConnection(bHasPendingConnection) && bHasPendingConnection)
	{
		UE_LOG(LogTemp, Log, TEXT("Had Pending Connection"));
		TSharedRef<FInternetAddr> RemoteAddress = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();

		if (FSocket* ConnectionSocket = ListenerSocket->Accept(*RemoteAddress, TEXT("MetaTailorConnection")))
		{
			UE_LOG(LogTemp, Log, TEXT("MetaTailor Bridge: Accepted connection from %s"),
			       *RemoteAddress->ToString(true));

			TArray<uint8> ReceivedData;
			uint32 PendingDataSize;
			int32 TotalBytesRead = 0;
			const int32 BufferSize = 4096;

			while (ConnectionSocket->HasPendingData(PendingDataSize) && PendingDataSize > 0)
			{
				int32 CurrentOffset = ReceivedData.Num();
				ReceivedData.AddUninitialized(BufferSize);
				int32 BytesRead = 0;
				if (ConnectionSocket->Recv(ReceivedData.GetData() + CurrentOffset, BufferSize, BytesRead))
				{
					if (BytesRead <=0)
						break;
					TotalBytesRead += BytesRead;
					ReceivedData.SetNum(CurrentOffset + BytesRead, EAllowShrinking::No);
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("MetaTailor Bridge: Socket Recv error."));
					TotalBytesRead = -1; // Indicate error
					break;
				}
				if (BytesRead < BufferSize)
					break;
			}

			if (TotalBytesRead > 0)
			{
				// Convert the entire received data (headers + body) to FString
				FString FullResponseString;
				FFileHelper::BufferToString(FullResponseString, ReceivedData.GetData(), ReceivedData.Num());

				// HTTP headers are separated from the body by a double CRLF (\r\n\r\n)
				FString HeaderBodySeparator = TEXT("\r\n\r\n");
				int32 BodyStartIndex = FullResponseString.Find(HeaderBodySeparator, ESearchCase::CaseSensitive);
				FString JsonString;

				if (BodyStartIndex != INDEX_NONE)
				{
					// Extract the body part
					JsonString = FullResponseString.Mid(BodyStartIndex + HeaderBodySeparator.Len());
					//UE_LOG(LogTemp, Verbose, TEXT("MetaTailor Bridge: Extracted HTTP Body: %s"), *JsonString);
				}
				else
				{
					UE_LOG(LogTemp, Warning,
					       TEXT(
						       "MetaTailor Bridge: Could not find HTTP header/body separator '\\r\\n\\r\\n' in response. Assuming entire message is body (might be incorrect)."
					       ));
					// Fallback: Treat the whole thing as JSON (original behavior, less robust)
					JsonString = FullResponseString;
				}

				if (!JsonString.IsEmpty())
				{
					TSharedPtr<FJsonObject> JsonObject;
					TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

					if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
					{
						FDressedAvatarResponse ResponseData;
						if (ResponseData.FromJson(JsonObject))
						{
							UE_LOG(LogTemp, Log, TEXT("MetaTailor Bridge: Received valid response. ModelPath: %s"),
							       *ResponseData.ModelPath);
							if (CurrentResponseCallback)
							{
								CurrentResponseCallback(true, ResponseData, "");
							}
						}
						else
						{
							UE_LOG(LogTemp, Warning,
							       TEXT("MetaTailor Bridge: Failed to parse response JSON structure from body."));
							if (CurrentResponseCallback) CurrentResponseCallback(
								false, FDressedAvatarResponse(), TEXT("Failed to parse response JSON structure."));
						}
					}
					else
					{
						UE_LOG(LogTemp, Warning, TEXT("MetaTailor Bridge: Failed to deserialize JSON body string: %s"),
						       *JsonString);
						if (CurrentResponseCallback) CurrentResponseCallback(
							false, FDressedAvatarResponse(), TEXT("Failed to deserialize JSON body string."));
					}
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("MetaTailor Bridge: Extracted JSON body was empty."));
					if (CurrentResponseCallback) CurrentResponseCallback(
						false, FDressedAvatarResponse(), TEXT("Received empty body."));
				}
			}
			else if (TotalBytesRead == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("MetaTailor Bridge: Received no data from connection."));
				if (CurrentResponseCallback) CurrentResponseCallback(false, FDressedAvatarResponse(),
				                                                     TEXT("Received no data."));
			}

			FString IndividualResponse = TEXT("200 OK");
			// The \r\n sequence is critical for the HTTP protocol
			const FString Response = FString::Printf(TEXT("HTTP/1.1 %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"),*IndividualResponse);

			FTCHARToUTF8 Converter(*Response);
			int32 BytesSent = 0;

			bool bDidSend = ConnectionSocket->Send(
				(uint8*)Converter.Get(),
				Converter.Length(),
				BytesSent
			);
			if (!bDidSend)
			{
				UE_LOG(LogTemp, Warning, TEXT("MetaTailor Bridge: Send failed."));
			}
			else
			{
				UE_LOG(LogTemp,Log,TEXT("Response sent"));
			}
			
			if (NotificationItem)
				NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
			StopListening();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("MetaTailor Bridge: Failed to accept incoming connection."));
			if (CurrentResponseCallback) CurrentResponseCallback(false, FDressedAvatarResponse(),
			                                                     TEXT("Failed to accept connection."));
		}
		

	}
	return true;
}
