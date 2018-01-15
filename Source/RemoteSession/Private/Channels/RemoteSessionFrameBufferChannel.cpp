// Copyright 2017 Andrew Grant
// This file is part of RemoteSession and is freely licensed for commercial and 
// non-commercial use under an MIT license
// See https://github.com/andrewgrant/RemoteSession for more info

#include "Channels/RemoteSessionFrameBufferChannel.h"
#include "Protocol/OSC/BackChannelOSCConnection.h"
#include "Protocol/OSC/BackChannelOSCMessage.h"
#include "IConsoleManager.h"
#include "FrameGrabber.h"
#include "Async.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Engine/Texture2D.h"
#include "ModuleManager.h"

DECLARE_CYCLE_STAT(TEXT("RSFrameBufferCap"), STAT_FrameBufferCapture, STATGROUP_Game);
DECLARE_CYCLE_STAT(TEXT("RSTextureUpdate"), STAT_TextureUpdate, STATGROUP_Game);
DECLARE_CYCLE_STAT(TEXT("RSImageCompression"), STAT_ImageCompression, STATGROUP_Game);
DECLARE_CYCLE_STAT(TEXT("RSImageDecompression"), STAT_ImageDecompression, STATGROUP_Game);

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


FRemoteSessionFrameBufferChannel::FRemoteSessionFrameBufferChannel(ERemoteSessionChannelMode InRole, TSharedPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> InConnection)
	: IRemoteSessionChannel(InRole, InConnection)
{
	LastSentImageTime = 0.0;
	Connection = InConnection;
	IncomingImage[0] = nullptr;
	IncomingImage[1] = nullptr;
	CurrentImageIndex = 0;
	Role = InRole;

	if (Role == ERemoteSessionChannelMode::Receive)
	{
		InConnection->GetDispatchMap().GetAddressHandler(TEXT("/Screen")).AddRaw(this, &FRemoteSessionFrameBufferChannel::ReceiveHostImage);
		InConnection->SetMessageOptions(TEXT("/Screen"), 1);
	}
}

FRemoteSessionFrameBufferChannel::~FRemoteSessionFrameBufferChannel()
{
	while (NumAsyncTasks.GetValue() > 0)
	{
		FPlatformProcess::SleepNoStats(0);
	}

	if (FrameGrabber.IsValid())
	{
		FrameGrabber->StopCapturingFrames();
		FrameGrabber = nullptr;
	}

	for (int32 i = 0; i < 2; i++)
	{
		if (IncomingImage[i])
		{
			IncomingImage[i]->RemoveFromRoot();
			IncomingImage[i] = nullptr;
		}
	}
}

FString FRemoteSessionFrameBufferChannel::StaticType()
{
	return TEXT("rs.framebuffer");
}

void FRemoteSessionFrameBufferChannel::SetCaptureQuality(int32 InQuality, int32 InFramerate)
{
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

void FRemoteSessionFrameBufferChannel::SetCaptureViewport(TSharedRef<FSceneViewport> Viewport)
{
	FrameGrabber = MakeShareable(new FFrameGrabber(Viewport, Viewport->GetSize()));
	FrameGrabber->StartCapturingFrames();
}

UTexture2D* FRemoteSessionFrameBufferChannel::GetHostScreen() const
{
	return IncomingImage[CurrentImageIndex];
}

void FRemoteSessionFrameBufferChannel::Tick(const float InDeltaTime)
{
	if (FrameGrabber.IsValid())
	{
		SCOPE_CYCLE_COUNTER(STAT_FrameBufferCapture);

		FrameGrabber->CaptureThisFrame(FFramePayloadPtr());

		TArray<FCapturedFrameData> Frames = FrameGrabber->GetCapturedFrames();

		if (Frames.Num())
		{
			const double ElapsedImageTimeMS = (FPlatformTime::Seconds() - LastSentImageTime) * 1000;
			const int32 DesiredFrameTimeMS = 1000 / FramerateMasterSetting;

			if (ElapsedImageTimeMS >= DesiredFrameTimeMS)
			{
				FCapturedFrameData& LastFrame = Frames.Last();

				TArray<FColor>* ColorData = new TArray<FColor>(MoveTemp(LastFrame.ColorBuffer));

				FIntPoint Size = LastFrame.BufferSize;

				NumAsyncTasks.Increment();

				AsyncTask(ENamedThreads::AnyBackgroundHiPriTask, [this, Size, ColorData]()
				{
					SCOPE_CYCLE_COUNTER(STAT_ImageCompression);

					for (FColor& Color : *ColorData)
					{
						Color.A = 255;
					}

					SendImageToClients(Size.X, Size.Y, *ColorData);

					delete ColorData;

					NumAsyncTasks.Decrement();
				});

				LastSentImageTime = FPlatformTime::Seconds();
			}
		}
	}
	
	if (Role == ERemoteSessionChannelMode::Receive)
	{
		SCOPE_CYCLE_COUNTER(STAT_TextureUpdate);

		TSharedPtr<FQueuedFBImage> QueuedImage;

		{
			// Check to see if there are any queued images. We just care about the last
			FScopeLock ImageLock(&ImageMutex);
			if (ReceivedImageQueue.Num())
			{
				QueuedImage = ReceivedImageQueue.Last();
				ReceivedImageQueue.Empty();
			}
		}

		// If an image was waiting...
		if (QueuedImage.IsValid())
		{
			int32 NextImage = CurrentImageIndex == 0 ? 1 : 0;

			// create a texture if we don't have a suitable one
			if (IncomingImage[NextImage] == nullptr || QueuedImage->Width != IncomingImage[NextImage]->GetSizeX() || QueuedImage->Height != IncomingImage[NextImage]->GetSizeY())
			{
				CreateTexture(NextImage, QueuedImage->Width, QueuedImage->Height);
			}

			// Update it on the render thread. There shouldn't (...) be any harm in GT code using it from this point
			FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, QueuedImage->Width, QueuedImage->Height);
			TArray<uint8>* TextureData = new TArray<uint8>(MoveTemp(QueuedImage->ImageData));

			IncomingImage[NextImage]->UpdateTextureRegions(0, 1, Region, 4 * QueuedImage->Width, 8, TextureData->GetData(), [this, NextImage](auto InTextureData, auto InRegions) {
				CurrentImageIndex = NextImage;
				delete InTextureData;
				delete InRegions;
			});
		}
	}
}

void FRemoteSessionFrameBufferChannel::SendImageToClients(int32 Width, int32 Height, const TArray<FColor>& ImageData)
{
	static bool SkipImages = FParse::Param(FCommandLine::Get(), TEXT("remote.noimage"));

	// Can be released on the main thread at anytime so hold onto it
	TSharedPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> LocalConnection = Connection.Pin();

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

void FRemoteSessionFrameBufferChannel::ReceiveHostImage(FBackChannelOSCMessage& Message, FBackChannelOSCDispatch& Dispatch)
{
	int32 Width(0);
	int32 Height(0);

	TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe> CompressedData = MakeShareable(new TArray<uint8>());

	Message << Width;
	Message << Height;
	Message << *CompressedData;

	NumAsyncTasks.Increment();

	AsyncTask(ENamedThreads::AnyBackgroundHiPriTask, [this, Width, Height, CompressedData]()
	{
		SCOPE_CYCLE_COUNTER(STAT_ImageDecompression);

		IImageWrapperModule* ImageWrapperModule = FModuleManager::GetModulePtr<IImageWrapperModule>(FName("ImageWrapper"));

		if (ImageWrapperModule != nullptr)
		{
			TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule->CreateImageWrapper(EImageFormat::JPEG);

			ImageWrapper->SetCompressed(CompressedData->GetData(), CompressedData->Num());

			const TArray<uint8>* RawData = nullptr;

			if (ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawData))
			{
				FScopeLock ImageLock(&ImageMutex);

				TSharedPtr<FQueuedFBImage> QueuedImage = MakeShareable(new FQueuedFBImage);
				QueuedImage->Width = Width;
				QueuedImage->Height = Height;
				QueuedImage->ImageData = MoveTemp(*((TArray<uint8>*)RawData));
				ReceivedImageQueue.Add(QueuedImage);
			}
		}

		NumAsyncTasks.Decrement();
	});
}

void FRemoteSessionFrameBufferChannel::CreateTexture(const int32 InSlot, const int32 InWidth, const int32 InHeight)
{
	if (IncomingImage[InSlot])
	{
		IncomingImage[InSlot]->RemoveFromRoot();
		IncomingImage[InSlot] = nullptr;
	}

	IncomingImage[InSlot] = UTexture2D::CreateTransient(InWidth, InHeight);

	IncomingImage[InSlot]->AddToRoot();
	IncomingImage[InSlot]->UpdateResource();
}


