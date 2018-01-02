// Copyright 2017 Andrew Grant
// This file is part of RemoteViewer and is freely licensed for commercial and 
// non-commercial use under an MIT license
// See https://github.com/andrewgrant/RemoteViewer for more info

#include "RemoteViewerHost.h"
#include "BackChannel/Transport/IBackChannelTransport.h"
#include "MessageHandler/RecordingMessageHandler.h"
#include "Widgets/SViewport.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"



FRemoteViewerHost::FRemoteViewerHost()
{
	LastImageTime = 0;
	bScreenSharingEnabled = true;

	EndFrameDelegate = FCoreDelegates::OnEndFrame.AddRaw(this, &FRemoteViewerHost::OnEndFrame);
}

FRemoteViewerHost::~FRemoteViewerHost()
{
	FCoreDelegates::OnEndFrame.Remove(EndFrameDelegate);
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
	OSCConnection = MakeShareable(new FBackChannelOSCConnection(NewConnection));

	TWeakPtr<FRemoteViewerHost> WeakThis(AsShared());

	OSCConnection->GetDispatchMap().GetAddressHandler(TEXT("/MessageHandler/")).AddRaw(this, &FRemoteViewerHost::OnRemoteMessage);

	OSCConnection->Start();

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

void FRemoteViewerHost::OnEndFrame()
{
	if (IsConnected() == false || bScreenSharingEnabled == false)
	{
		return;
	}

	UGameViewportClient* ViewportClient = GEngine->GameViewport;
	
	FViewport* Viewport = ViewportClient->Viewport;

	if (Viewport->IsPlayInEditorViewport() == false)
	{
		FTexture2DRHIRef TextureRef = RHIGetViewportBackBuffer(Viewport->GetViewportRHI());

		ENQUEUE_RENDER_COMMAND(CaptureScreen)(
			[this, Viewport](FRHICommandListImmediate& RHICmdList)
		{
			FIntPoint Size = Viewport->GetSizeXY();
			FIntRect Rect = FIntRect(FIntPoint(0, 0), Size);

			TArray<FLinearColor> LinearData;

			FTexture2DRHIRef BackBuffer = RHICmdList.GetViewportBackBuffer(Viewport->GetViewportRHI());
			RHICmdList.ReadSurfaceData(BackBuffer, Rect, LinearData, FReadSurfaceDataFlags());

			AsyncTasks.Increment();

			AsyncTask(ENamedThreads::AnyBackgroundHiPriTask, [this, Size, LinearData]()
			{
				if (OSCConnection.IsValid())
				{
					// Hmm.
					TArray<FColor> ImageData;
					for (const FLinearColor& LinearColor : LinearData)
					{
						FColor Color = LinearColor.ToFColor(false);
						Color.A = 255;
						ImageData.Add(Color);
					}

					SendImageToClients(Size.X, Size.Y, ImageData);
				}

				AsyncTasks.Decrement();
			});
		});
	}
	else
	{
		FTexture2DRHIRef TextureRef = Viewport->GetRenderTargetTexture();

		ENQUEUE_RENDER_COMMAND(CaptureScreen)(
			[this, TextureRef, Viewport](FRHICommandListImmediate& RHICmdList)
		{
			FIntPoint Size = Viewport->GetSizeXY();
			FIntRect Rect = FIntRect(FIntPoint(0, 0), Size);

			TArray<FLinearColor> LinearData;

			RHICmdList.ReadSurfaceData(TextureRef, Rect, LinearData, FReadSurfaceDataFlags());

			AsyncTask(ENamedThreads::AnyBackgroundHiPriTask, [this, Size, LinearData]()
			{
				// Hmm.
				TArray<FColor> ImageData;
				for (const FLinearColor& LinearColor : LinearData)
				{
					FColor Color = LinearColor.ToFColor(false);
					Color.A = 255;
					ImageData.Add(Color);
				}

				SendImageToClients(Size.X, Size.Y, ImageData);
			});
		});
	}
}


void FRemoteViewerHost::Tick(float DeltaTime)
{
	FRemoteViewerRole::Tick(DeltaTime);
}

void FRemoteViewerHost::SendImageToClients(int32 Width, int32 Height, const TArray<FColor>& ImageData)
{
	static bool SkipImages = FParse::Param(FCommandLine::Get(), TEXT("remote.noimage"));

	if (OSCConnection.IsValid() && SkipImages == false)
	{
		IImageWrapperModule* ImageWrapperModule = FModuleManager::GetModulePtr<IImageWrapperModule>(FName("ImageWrapper"));

		if (ImageWrapperModule != nullptr)
		{
			TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule->CreateImageWrapper(EImageFormat::JPEG);

			ImageWrapper->SetRaw(ImageData.GetData(), ImageData.GetAllocatedSize(), Width, Height, ERGBFormat::BGRA, 8);

			TArray<uint8> JPGData = ImageWrapper->GetCompressed(60);

			FBackChannelOSCMessage Msg(TEXT("/Screen"));
			Msg.Write(Width);
			Msg.Write(Height);
			Msg.Write(JPGData);
			OSCConnection->SendPacket(Msg);
		}
	}
}