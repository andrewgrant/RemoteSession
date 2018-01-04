// Copyright 2017 Andrew Grant
// This file is part of RemoteViewer and is freely licensed for commercial and 
// non-commercial use under an MIT license
// See https://github.com/andrewgrant/RemoteViewer for more info

#include "RemoteViewerHost.h"
#include "BackChannel/Transport/IBackChannelTransport.h"
#include "MessageHandler/RecordingMessageHandler.h"
#include "FrameGrabber.h"
#include "Widgets/SViewport.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"

static int32 FramerateMasterSetting = 0;
static FAutoConsoleVariableRef CVarFramerateOverride(
	TEXT("remote.framerate"), FramerateMasterSetting,
	TEXT("Sets framerate"),
	ECVF_Default);

static int32 QualityMasterSetting = 0;
static FAutoConsoleVariableRef CVarQualityOverride(
	TEXT("remote.quality"), QualityMasterSetting,
	TEXT("Sets quality (1-100)"),
	ECVF_Default);

FRemoteViewerHost::FRemoteViewerHost(int32 InQuality, int32 InFramerate)
{
	LastImageTime = 0;
	bScreenSharingEnabled = true;
	Quality = InQuality;

	// Set our framerate and quality cvars, if the user hasn't modified them
	if (FramerateMasterSetting == 0)
	{
		CVarFramerateOverride->Set(InFramerate);
	}

	if (QualityMasterSetting == 0)
	{
		CVarQualityOverride->Set(InQuality);
	}
}

FRemoteViewerHost::~FRemoteViewerHost()
{
}

void FRemoteViewerHost::SetScreenSharing(const bool bEnabled)
{
	bScreenSharingEnabled = bEnabled;
}

void FRemoteViewerHost::SetConsumeInput(const bool bConsume)
{
	if (PlaybackMessageHandler.IsValid())
	{
		PlaybackMessageHandler->SetConsumeInput(bConsume);
	}
}

 void FRemoteViewerHost::Close()
{
	 while (AsyncTasks.GetValue() > 0)
	 {
		 FPlatformProcess::SleepNoStats(0.001);
	 }

	 if (FrameGrabber.IsValid())
	 {
		 FrameGrabber->StopCapturingFrames();
		 FrameGrabber = nullptr;
	 }

	 FRemoteViewerRole::Close();
}

bool FRemoteViewerHost::StartListening(const uint16 InPort)
{
	if (Listener.IsValid())
	{
		return false;
	}

	if (IBackChannelTransport* Transport = IBackChannelTransport::Get())
	{
		Listener = Transport->CreateListener(IBackChannelTransport::TCP);

		Listener->GetOnConnectionRequestDelegate().BindLambda([this](auto NewConnection)->bool
		{
			return OnIncomingConnection(NewConnection);
		});

		Listener->Listen(InPort);
	}

	if (PlaybackMessageHandler.IsValid() == false)
	{
		TSharedRef<FGenericApplicationMessageHandler> DefaultHandler = FSlateApplication::Get().GetPlatformApplication()->GetMessageHandler();

		PlaybackMessageHandler = MakeShareable(new FRecordingMessageHandler(DefaultHandler));
	}

	return Listener.IsValid();
}


bool FRemoteViewerHost::OnIncomingConnection(TSharedRef<IBackChannelConnection> NewConnection)
{
	Close();

	OSCConnection = MakeShareable(new FBackChannelOSCConnection(NewConnection));

	TWeakPtr<FRemoteViewerHost> WeakThis(AsShared());

	OSCConnection->GetDispatchMap().GetAddressHandler(TEXT("/MessageHandler/")).AddRaw(this, &FRemoteViewerHost::OnRemoteMessage);

	OSCConnection->Start();

	TWeakPtr<SWindow> InputWindow;
	TSharedPtr<FSceneViewport> SceneViewport;

#if WITH_EDITOR
	if (GIsEditor)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE)
			{
				FSlatePlayInEditorInfo* SlatePlayInEditorSession = GEditor->SlatePlayInEditorMap.Find(Context.ContextHandle);
				if (SlatePlayInEditorSession && SlatePlayInEditorSession->SlatePlayInEditorWindowViewport.IsValid())
				{
					SceneViewport = SlatePlayInEditorSession->SlatePlayInEditorWindowViewport;
				}

				InputWindow = SlatePlayInEditorSession->SlatePlayInEditorWindow;
			}
		}
		
	}
	else
#endif
	{
		UGameEngine* GameEngine = Cast<UGameEngine>(GEngine);
		SceneViewport = GameEngine->SceneViewport;
		InputWindow = GameEngine->GameViewportWindow;
	}

	if (SceneViewport.IsValid())
	{
		FrameGrabber = MakeShareable(new FFrameGrabber(SceneViewport.ToSharedRef(), SceneViewport->GetSize()));
		FrameGrabber->StartCapturingFrames();
	}

	PlaybackMessageHandler->SetPlaybackWindow(InputWindow, SceneViewport);

	return true;
}

void FRemoteViewerHost::OnRemoteMessage(FBackChannelOSCMessage& Message, FBackChannelOSCDispatch& Dispatch)
{
	FString MessageName = Message.GetAddress();
	MessageName.RemoveFromStart(TEXT("/MessageHandler/"));

	TArray<uint8> MsgData;
	Message << MsgData;

	PlaybackMessageHandler->PlayMessage(*MessageName, MsgData);
}

void FRemoteViewerHost::Tick(float DeltaTime)
{
	if (FrameGrabber.IsValid() && bScreenSharingEnabled)
	{
		FrameGrabber->CaptureThisFrame(FFramePayloadPtr());

		TArray<FCapturedFrameData> Frames = FrameGrabber->GetCapturedFrames();

		if (Frames.Num())
		{	
			const double ElapsedImageTimeMS = (FPlatformTime::Seconds() - LastImageTime) * 1000;
			const int32 DesiredFrameTimeMS = 1000 / FramerateMasterSetting;

			if (ElapsedImageTimeMS >= DesiredFrameTimeMS)
			{
				FCapturedFrameData& LastFrame = Frames.Last();

				TSharedPtr<TArray<FColor>> ColorData = MakeShareable(new TArray<FColor>(MoveTemp(LastFrame.ColorBuffer)));

				FIntPoint Size = LastFrame.BufferSize;

				AsyncTask(ENamedThreads::AnyBackgroundHiPriTask, [this, Size, ColorData]()
				{
					for (FColor& Color : *ColorData)
					{
						Color.A = 255;
					}

					SendImageToClients(Size.X, Size.Y, *ColorData);
				});

				LastImageTime = FPlatformTime::Seconds();
			}
		}
	}
	FRemoteViewerRole::Tick(DeltaTime);
}

void FRemoteViewerHost::SendImageToClients(int32 Width, int32 Height, const TArray<FColor>& ImageData)
{
	static bool SkipImages = FParse::Param(FCommandLine::Get(), TEXT("remote.noimage"));

	// Can be released on the main thread at anytime
	TSharedPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> LocalConnection = OSCConnection;

	if (LocalConnection.IsValid() && SkipImages == false)
	{
		// created on demand because there can be multiple SendImage requests in flight
		IImageWrapperModule* ImageWrapperModule = FModuleManager::GetModulePtr<IImageWrapperModule>(FName("ImageWrapper"));

		if (ImageWrapperModule != nullptr)
		{
			TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule->CreateImageWrapper(EImageFormat::JPEG);

			ImageWrapper->SetRaw(ImageData.GetData(), ImageData.GetAllocatedSize(), Width, Height, ERGBFormat::BGRA, 8);

			TArray<uint8> JPGData = ImageWrapper->GetCompressed(QualityMasterSetting);

			FBackChannelOSCMessage Msg(TEXT("/Screen"));
			Msg.Write(Width);
			Msg.Write(Height);
			Msg.Write(JPGData);
			LocalConnection->SendPacket(Msg);
		}
	}
}