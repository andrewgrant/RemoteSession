// Copyright 2017 Andrew Grant
// This file is part of RemoteViewer and is freely licensed for commercial and 
// non-commercial use under an MIT license
// See https://github.com/andrewgrant/RemoteViewer for more info

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

DECLARE_CYCLE_STAT(TEXT("RVImageDecompression"), STAT_ImageDecompression, STATGROUP_Game);

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
	LastImageTime = 0.0;
	Connection = InConnection;
	RemoteImage[0] = nullptr;
	RemoteImage[1] = nullptr;
	RemoteImageIndex = 0;
	RemoteImageWidth = RemoteImageHeight = 0;
	Role = InRole;

	if (Role == ERemoteSessionChannelMode::Receive)
	{
		Connection->GetDispatchMap().GetAddressHandler(TEXT("/Screen")).AddRaw(this, &FRemoteSessionFrameBufferChannel::ReceiveRemoteImage);
		Connection->SetMessageOptions(TEXT("/Screen"), 1);
	}
}

FRemoteSessionFrameBufferChannel::~FRemoteSessionFrameBufferChannel()
{
	if (FrameGrabber.IsValid())
	{
		FrameGrabber->StopCapturingFrames();
		FrameGrabber = nullptr;
	}

	for (int32 i = 0; i < 2; i++)
	{
		if (RemoteImage[i])
		{
			RemoteImage[i]->RemoveFromRoot();
			RemoteImage[i] = nullptr;
		}
	}
}

FString FRemoteSessionFrameBufferChannel::StaticType()
{
	return TEXT("rv.framebuffer");
}

void FRemoteSessionFrameBufferChannel::SetQuality(int32 InQuality, int32 InFramerate)
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

void FRemoteSessionFrameBufferChannel::CaptureViewport(TSharedRef<FSceneViewport> Viewport)
{
	FrameGrabber = MakeShareable(new FFrameGrabber(Viewport, Viewport->GetSize()));
	FrameGrabber->StartCapturingFrames();
}

UTexture2D* FRemoteSessionFrameBufferChannel::GetRemoteImage() const
{
	return RemoteImage[RemoteImageIndex];
}

void FRemoteSessionFrameBufferChannel::Tick(const float InDeltaTime)
{
	if (FrameGrabber.IsValid())
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

				TArray<FColor>* ColorData = new TArray<FColor>(MoveTemp(LastFrame.ColorBuffer));

				FIntPoint Size = LastFrame.BufferSize;

				AsyncTask(ENamedThreads::AnyBackgroundHiPriTask, [this, Size, ColorData]()
				{
					for (FColor& Color : *ColorData)
					{
						Color.A = 255;
					}

					SendImageToClients(Size.X, Size.Y, *ColorData);

					delete ColorData;
				});

				LastImageTime = FPlatformTime::Seconds();
			}
		}
	}
	
	if (Role == ERemoteSessionChannelMode::Receive)
	{
		TSharedPtr<FQueuedFBImage> QueuedImage;

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
			int32 NextImage = RemoteImageIndex == 0 ? 1 : 0;

			if (RemoteImage[NextImage] == nullptr || QueuedImage->Width != RemoteImage[NextImage]->GetSizeX() || QueuedImage->Height != RemoteImage[NextImage]->GetSizeY())
			{
				CreateRemoteImage(NextImage, QueuedImage->Width, QueuedImage->Height);
			}


			FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, QueuedImage->Width, QueuedImage->Height);

			TArray<uint8>* TextureData = new TArray<uint8>(MoveTemp(QueuedImage->ImageData));

			RemoteImage[NextImage]->UpdateTextureRegions(0, 1, Region, 4 * QueuedImage->Width, 8, TextureData->GetData(), [this, NextImage](auto InTextureData, auto InRegions) {
				RemoteImageIndex = NextImage;
				delete InTextureData;
				delete InRegions;
			});
		}
	}
}

void FRemoteSessionFrameBufferChannel::SendImageToClients(int32 Width, int32 Height, const TArray<FColor>& ImageData)
{
	static bool SkipImages = FParse::Param(FCommandLine::Get(), TEXT("remote.noimage"));

	// Can be released on the main thread at anytime
	TSharedPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> LocalConnection = Connection;

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

void FRemoteSessionFrameBufferChannel::ReceiveRemoteImage(FBackChannelOSCMessage& Message, FBackChannelOSCDispatch& Dispatch)
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

			TSharedPtr<FQueuedFBImage> QueuedImage = MakeShareable(new FQueuedFBImage);
			QueuedImage->Width = Width;
			QueuedImage->Height = Height;
			QueuedImage->ImageData = MoveTemp(*((TArray<uint8>*)RawData));
			QueuedImages.Add(QueuedImage);
		}
	}
}

void FRemoteSessionFrameBufferChannel::CreateRemoteImage(const int32 InSlot, const int32 InWidth, const int32 InHeight)
{
	if (RemoteImage[InSlot])
	{
		RemoteImage[InSlot]->RemoveFromRoot();
		RemoteImage[InSlot] = nullptr;
	}

	RemoteImage[InSlot] = UTexture2D::CreateTransient(InWidth, InHeight);

	RemoteImage[InSlot]->AddToRoot();
	RemoteImage[InSlot]->UpdateResource();
}


