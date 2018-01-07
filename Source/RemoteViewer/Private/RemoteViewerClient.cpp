// Copyright 2017 Andrew Grant
// This file is part of RemoteViewer and is freely licensed for commercial and 
// non-commercial use under an MIT license
// See https://github.com/andrewgrant/RemoteViewer for more info

#include "RemoteViewerClient.h"
#include "BackChannel/Transport/IBackChannelTransport.h"
#include "Framework/Application/SlateApplication.h"	
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Sockets.h"

bool InGameThread()
{
	if (GIsGameThreadIdInitialized)
	{
		return FPlatformTLS::GetCurrentThreadId() == GGameThreadId;
	}
	else
	{
		return true;
	}
}

#define RV_CLIENT_THREADED 0

DECLARE_CYCLE_STAT(TEXT("RVClientTick"), STAT_RVClientTick, STATGROUP_Game);
DECLARE_CYCLE_STAT(TEXT("RVImageDecompression"), STAT_ImageDecompression, STATGROUP_Game);

FRemoteViewerClient::FRemoteViewerClient(const TCHAR* InHostAddress)
{
	HostAddress = InHostAddress;
	ConnectionAttemptTimer = FLT_MAX;		// attempt a connection asap
	LastConnectionAttemptTime = 0;
	RemoteImage = nullptr;
	IsConnecting = false;

	if (HostAddress.Contains(TEXT(":")) == false)
	{
		HostAddress += FString::Printf(TEXT(":%d"), (int32)IRemoteViewerModule::kDefaultPort);
	}

	DefaultHandler = FSlateApplication::Get().GetPlatformApplication()->GetMessageHandler();

	RecordingHandler = MakeShareable(new FRecordingMessageHandler(DefaultHandler.Pin()));

	RecordingHandler->SetRecordingHandler(this);

	FSlateApplication::Get().GetPlatformApplication()->SetMessageHandler(RecordingHandler.ToSharedRef());

	UE_LOG(LogRemoteViewer, Display, TEXT("Will attempt to connect to %s.."), *HostAddress);

	StartConnection();
}

FRemoteViewerClient::~FRemoteViewerClient()
{
	Close();

	// todo - is this ok? Might other things have changed the handler like we do?
	if (DefaultHandler.IsValid())
	{
		FSlateApplication::Get().GetPlatformApplication()->SetMessageHandler(DefaultHandler.Pin().ToSharedRef());
	}

	// should restore handler? What if something else changed it...
	if (RecordingHandler.IsValid())
	{
		RecordingHandler->SetRecordingHandler(nullptr);
	}

	if (RemoteImage)
	{
		RemoteImage->RemoveFromRoot();
		RemoteImage = nullptr;
	}
}

void FRemoteViewerClient::SetRecording(bool bRecord)
{
	if (RecordingHandler.IsValid())
	{
		RecordingHandler->SetRecordingHandler(bRecord ? this : nullptr);
	}
}

void FRemoteViewerClient::SetConsumeInput(bool bConsume)
{
	if (RecordingHandler.IsValid())
	{
		RecordingHandler->SetConsumeInput(bConsume);
	}
}

FRemoteViewerReceivedImageDelegate& FRemoteViewerClient::GetClientImageReceivedDelegate()
{
	return ReceivedImageDelegate;
}


void FRemoteViewerClient::Tick(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_RVClientTick);

	FScopeLock Lock(&ConnectionMutex);

	FRemoteViewerRole::Tick(DeltaTime);

	if (IsConnected() == false)
	{
		if (IsConnecting == false)
		{
			const double TimeSinceLastAttempt = FPlatformTime::Seconds() - LastConnectionAttemptTime;

			if (TimeSinceLastAttempt >= 5.0)
			{
				StartConnection();
			}
		}

		if (IsConnecting)
		{
			CheckConnection();
		}
		return;
	}

	TSharedPtr<FQueuedImage> QueuedImage;

	{
		FScopeLock ImageLock(&ImageMutex);

		if (QueuedImages.Num())
		{
			QueuedImage = QueuedImages.Last();
			QueuedImages.Empty();
		}
	}

	if (QueuedImage.IsValid())
	{
		if (RemoteImage == nullptr || QueuedImage->Width != RemoteImage->GetSizeX() || QueuedImage->Height != RemoteImage->GetSizeY())
		{
			CreateRemoteImage(QueuedImage->Width, QueuedImage->Height);
		}

		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, QueuedImage->Width, QueuedImage->Height);

		TArray<uint8>* TextureData = new TArray<uint8>(MoveTemp(QueuedImage->ImageData));

		RemoteImage->UpdateTextureRegions(0, 1, Region, 4 * QueuedImage->Width, 8, TextureData->GetData(), [](auto TextureData, auto Regions) {
			delete TextureData;
			delete Regions;
		});

		ReceivedImageDelegate.ExecuteIfBound(RemoteImage);
	}
}

void FRemoteViewerClient::StartConnectionThread()
{
	check(ThreadRunning == false);

	ThreadExitRequested = false;
	ThreadRunning = true;

	FRunnableThread* Thread = FRunnableThread::Create(this, TEXT("RemoteViewerClientThread"));
}

void FRemoteViewerClient::StopConnectionThread()
{
	if (ThreadRunning == false)
	{
		return;
	}

	ThreadExitRequested = true;

	while (ThreadRunning)
	{
		FPlatformProcess::SleepNoStats(0);
	}
}

uint32 FRemoteViewerClient::Run()
{
	double LastTickTime = FPlatformTime::Seconds();

	while (ThreadExitRequested == false)
	{
		FPlatformProcess::SleepNoStats(0);

		const double TimeNow = FPlatformTime::Seconds();
		const double ElapsedTime = TimeNow - LastTickTime;
		LastTickTime = TimeNow;

		if (IsConnected() == false)
		{
			continue;
		}

		{
			FScopeLock Lock(&ConnectionMutex);
			FRemoteViewerRole::Tick(ElapsedTime);
		}
	}
	
	return 0;
}

void  FRemoteViewerClient::StartConnection()
{
	check(IsConnecting == false);

	Close();

	if (IBackChannelTransport* Transport = IBackChannelTransport::Get())
	{
		Connection = Transport->CreateConnection(IBackChannelTransport::TCP);

		if (Connection.IsValid())
		{
			// lock the connection
			FScopeLock Lock(&ConnectionMutex);

			if (Connection->Connect(*HostAddress))
			{
				IsConnecting = true;
			}
		}
	}
}

void FRemoteViewerClient::CheckConnection()
{
	// lock the connection
	FScopeLock Lock(&ConnectionMutex);

	check(IsConnected() == false && IsConnecting);

	bool Success = Connection->WaitForConnection(0, [this](auto InConnection) {
		int32 WantedSize = 4 * 1024 * 1024;
		int32 ActualSize(0);

		Connection->GetSocket()->SetReceiveBufferSize(WantedSize, ActualSize);

		OSCConnection = MakeShareable(new FBackChannelOSCConnection(Connection.ToSharedRef()));

		OSCConnection->GetDispatchMap().GetAddressHandler(TEXT("/Screen")).AddRaw(this, &FRemoteViewerClient::UpdateRemoteImage);

		OSCConnection->SetMessageOptions(TEXT("/Screen"), 1);

		OSCConnection->Start();

		UE_LOG(LogRemoteViewer, Log, TEXT("Connected to host at %s (ReceiveSize=%dkb)"), *HostAddress, ActualSize / 1024);

		IsConnecting = false;

		return true;
	});

	if (Success == false)
	{
		IsConnecting = false;
		LastConnectionAttemptTime = FPlatformTime::Seconds();
	}

}

void FRemoteViewerClient::Close()
{
	StopConnectionThread();
	FRemoteViewerRole::Close();
	Connection = nullptr;
}

void FRemoteViewerClient::RecordMessage(const TCHAR* MsgName, const TArray<uint8>& Data)
{
	if (OSCConnection.IsValid())
	{
		// send as blobs
		FString Path = FString::Printf(TEXT("/MessageHandler/%s"), MsgName);
		FBackChannelOSCMessage Msg(*Path);

		Msg.Write(Data);

		OSCConnection->SendPacket(Msg);
	}
}

void FRemoteViewerClient::CreateRemoteImage(const int32 InWidth, const int32 InHeight)
{
	if (RemoteImage)
	{
		RemoteImage->RemoveFromRoot();
		RemoteImage = nullptr;
	}

	RemoteImage = UTexture2D::CreateTransient(InWidth, InHeight);

	RemoteImage->AddToRoot();
	RemoteImage->UpdateResource();
}


void FRemoteViewerClient::UpdateRemoteImage(FBackChannelOSCMessage& Message, FBackChannelOSCDispatch& Dispatch)
{
	SCOPE_CYCLE_COUNTER(STAT_ImageDecompression);

	int32 Width(0);
	int32 Height(0);

	TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe> CompressedData = MakeShareable(new TArray<uint8>());

	Message << Width;
	Message << Height;
	Message << *CompressedData;

	IImageWrapperModule* ImageWrapperModule = FModuleManager::GetModulePtr<IImageWrapperModule>(FName("ImageWrapper"));

	if (ImageWrapperModule != nullptr)
	{
		TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule->CreateImageWrapper(EImageFormat::JPEG);

		ImageWrapper->SetCompressed(CompressedData->GetData(), CompressedData->Num());

		const TArray<uint8>* RawData = nullptr;

		if (ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawData))
		{
			FScopeLock ImageLock(&ImageMutex);

			TSharedPtr<FQueuedImage> QueuedImage = MakeShareable(new FQueuedImage);
			QueuedImage->Width = Width;
			QueuedImage->Height = Height;
			QueuedImage->ImageData = MoveTemp(*((TArray<uint8>*)RawData));
			QueuedImages.Add(QueuedImage);
		}
	}
}