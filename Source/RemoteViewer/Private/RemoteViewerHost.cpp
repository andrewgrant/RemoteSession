// Copyright 2017 Andrew Grant
// Licensed under BSD License 2.0. 
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


void FRemoteViewerHost::Tick()
{
	FRemoteViewerRole::Tick();

	static float ImageFPS = 1 / 20.0f;

	const float TimeSinceLastImage = FPlatformTime::Seconds() - LastImageTime;

	if (TimeSinceLastImage >= ImageFPS)
	{
		LastImageTime = FPlatformTime::Seconds();

		TSharedPtr<SViewport> port;
		TWeakPtr<ISlateViewport> SViewport = FSlateApplication::Get().GetGameViewport()->GetViewportInterface();

		UGameViewportClient* ViewportClient = GEngine->GameViewport;
		FViewport* Viewport = ViewportClient->Viewport;

		// todo - make non-shitty, chek PIE etc
		if (FSlateApplication::IsInitialized())
		{
			FIntVector Size(Viewport->GetSizeXY().X, Viewport->GetSizeXY().Y, 0);

			TArray<FColor> Bitmap;

			TSharedRef<SWidget> WindowRef = ViewportClient->GetWindow().ToSharedRef();
			if (FSlateApplication::Get().TakeScreenshot(WindowRef, Bitmap, Size))
			{
				for (FColor& Pixel : Bitmap)
				{
					Pixel.A = 255;
				}

				SendImageToClients(Size.X, Size.Y, Bitmap);
			}
			else
			{
				UE_LOG(LogRemoteViewer, Error, TEXT("Failed to take screenshot!"));
			}
		}

#if 0
		if (SViewport.IsValid())
		{
			FSlateShaderResource* ViewportRTT = SViewport.Pin()->GetViewportRenderTargetTexture();

			FViewport* Viewport = GEngine->GameViewport->Viewport;

			if (Viewport && Viewport->GetRenderTargetTexture().IsValid())
			{
				TArray<FColor> Bitmap;

				// Read the contents of the viewport into an array.
				if (Viewport->ReadPixels(Bitmap, FReadSurfaceDataFlags(), FIntRect()))
				{
					check(Bitmap.Num() == Viewport->GetSizeXY().X * Viewport->GetSizeXY().Y);
				}
			}
			else
			{
				bool bScreenshotSuccessful = false;
				FIntVector Size(InViewport->GetSizeXY().X, InViewport->GetSizeXY().Y, 0);
				if (bShowUI && FSlateApplication::IsInitialized())
				{
					TSharedRef<SWidget> WindowRef = WindowPtr.ToSharedRef();
					bScreenshotSuccessful = FSlateApplication::Get().TakeScreenshot(WindowRef, Bitmap, Size);
					GScreenshotResolutionX = Size.X;
					GScreenshotResolutionY = Size.Y;
				}

			}

			if (ViewportRTT)
			{
				ESlateShaderResource::Type Type = ViewportRTT->GetType();
				return;
			}
		}
#endif
	}
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