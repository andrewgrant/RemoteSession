// Copyright 2017 Andrew Grant
// This file is part of RemoteViewer and is freely licensed for commercial and 
// non-commercial use under an MIT license
// See https://github.com/andrewgrant/RemoteViewer for more info

#include "RemoteViewerClient.h"
#include "BackChannel/Transport/IBackChannelTransport.h"
#include "Framework/Application/SlateApplication.h"	
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"

FRemoteViewerClient::FRemoteViewerClient(const TCHAR* InHostAddress)
{
	HostAddress = InHostAddress;
	ConnectionAttemptTimer = FLT_MAX;		// attempt a connection asap
	LastConnectionAttemptTime = 0;
	RemoteImage = nullptr;

	if (HostAddress.Contains(TEXT(":")) == false)
	{
		HostAddress += FString::Printf(TEXT(":%d"), (int32)IRemoteViewerModule::kDefaultPort);
	}

	DefaultHandler = FSlateApplication::Get().GetPlatformApplication()->GetMessageHandler();

	RecordingHandler = MakeShareable(new FRecordingMessageHandler(DefaultHandler.Pin()));

	RecordingHandler->SetRecordingHandler(this);

	FSlateApplication::Get().GetPlatformApplication()->SetMessageHandler(RecordingHandler.ToSharedRef());

	UE_LOG(LogRemoteViewer, Display, TEXT("Will attempt to connect to %s.."), *HostAddress);
}

FRemoteViewerClient::~FRemoteViewerClient()
{
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
	FRemoteViewerRole::Tick(DeltaTime);

	if (IsConnected() == false)
	{
		ConnectionAttemptTimer += DeltaTime;

		const double TimeSinceLastConnection = FPlatformTime::Seconds() - LastConnectionAttemptTime;

		if (TimeSinceLastConnection >= 5.0f)
		{
			Connect();

			LastConnectionAttemptTime = FPlatformTime::Seconds();
		}
	}
}


bool FRemoteViewerClient::Connect()
{
	check(OSCConnection.IsValid() == false);

	UE_LOG(LogRemoteViewer, Display, TEXT("Attempting to connect to %s.."), *HostAddress);

	if (IBackChannelTransport* Transport = IBackChannelTransport::Get())
	{
		TSharedPtr<IBackChannelConnection> Connection = Transport->CreateConnection(IBackChannelTransport::TCP);

		if (Connection.IsValid() && Connection->Connect(*HostAddress))
		{
			OSCConnection = MakeShareable(new FBackChannelOSCConnection(Connection.ToSharedRef()));

			OSCConnection->GetDispatchMap().GetAddressHandler(TEXT("/Screen")).AddRaw(this, &FRemoteViewerClient::UpdateRemoteImage);

			OSCConnection->SetMessageOptions(TEXT("/Screen"), 1);

			OSCConnection->Start();

			UE_LOG(LogRemoteViewer, Log, TEXT("Connected to host at %s"), *HostAddress);
		}		
	}

	return OSCConnection.IsValid();
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

	ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER(UpdateTexture,
		FTexture2DResource*, TextureResource, (FTexture2DResource*)RemoteImage->Resource,
		{
			FTexture2DRHIRef RHIRef = TextureResource->GetTexture2DRHI();
			uint32 Stride = 0;

			FLinearColor* TextureBuffer = (FLinearColor*)RHILockTexture2D(RHIRef, 0, RLM_WriteOnly, Stride, false);

			for (uint32 i = 0; i < TextureResource->GetSizeX() * TextureResource->GetSizeY(); i++)
			{
				//TextureBuffer[i] = FLinearColor::Green;
			}

			RHIUnlockTexture2D(RHIRef, 0, false);
		});
}

void UpdateTextureRegions(UTexture2D* Texture, int32 MipIndex, uint32 NumRegions, FUpdateTextureRegion2D* Regions, uint32 SrcPitch, uint32 SrcBpp, uint8* SrcData, bool bFreeData)
{
	if (Texture->Resource)
	{
		struct FUpdateTextureRegionsData
		{
			FTexture2DResource* Texture2DResource;
			int32 MipIndex;
			uint32 NumRegions;
			FUpdateTextureRegion2D* Regions;
			uint32 SrcPitch;
			uint32 SrcBpp;
			uint8* SrcData;
		};

		FUpdateTextureRegionsData* RegionData = new FUpdateTextureRegionsData;

		RegionData->Texture2DResource = (FTexture2DResource*)Texture->Resource;
		RegionData->MipIndex = MipIndex;
		RegionData->NumRegions = NumRegions;
		RegionData->Regions = Regions;
		RegionData->SrcPitch = SrcPitch;
		RegionData->SrcBpp = SrcBpp;
		RegionData->SrcData = SrcData;

		ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER(
			UpdateTextureRegionsData,
			FUpdateTextureRegionsData*, RegionData, RegionData,
			bool, bFreeData, bFreeData,
			{
				for (uint32 RegionIndex = 0; RegionIndex < RegionData->NumRegions; ++RegionIndex)
				{
					int32 CurrentFirstMip = RegionData->Texture2DResource->GetCurrentFirstMip();
					if (RegionData->MipIndex >= CurrentFirstMip)
					{
						RHIUpdateTexture2D(
							RegionData->Texture2DResource->GetTexture2DRHI(),
							RegionData->MipIndex - CurrentFirstMip,
							RegionData->Regions[RegionIndex],
							RegionData->SrcPitch,
							RegionData->SrcData
							+ RegionData->Regions[RegionIndex].SrcY * RegionData->SrcPitch
							+ RegionData->Regions[RegionIndex].SrcX * RegionData->SrcBpp
						);
					}
				}
				if (bFreeData)
				{
					FMemory::Free(RegionData->Regions);
					FMemory::Free(RegionData->SrcData);
				}
				delete RegionData;
			}
		);
	}
}

void FRemoteViewerClient::UpdateRemoteImage(FBackChannelOSCMessage& Message, FBackChannelOSCDispatch& Dispatch)
{
	static int32 Width(0);
	static int32 Height(0);

	TArray<uint8> CompressedData;

	Message << Width;
	Message << Height;
	Message << CompressedData;

	IImageWrapperModule* ImageWrapperModule = FModuleManager::GetModulePtr<IImageWrapperModule>(FName("ImageWrapper"));

	if (ImageWrapperModule != nullptr)
	{
		TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule->CreateImageWrapper(EImageFormat::JPEG);

		ImageWrapper->SetCompressed(CompressedData.GetData(), CompressedData.Num());

		const TArray<uint8>* RawData = nullptr;

		if (ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawData))
		{
			if (RemoteImage == nullptr || Width != RemoteImage->GetSizeX() || Height != RemoteImage->GetSizeY())
			{
				CreateRemoteImage(Width, Height);
			}

			const size_t DataLen = RawData->Num();
			uint8* DataCopy = (uint8*)FMemory::Malloc(DataLen);
			FMemory::Memcpy(DataCopy, RawData->GetData(), DataLen);

			FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Width, Height);
			
			RemoteImage->UpdateTextureRegions(0, 1, Region, 4 * Width, 8, DataCopy, [](auto TextureData, auto Regions) {
				FMemory::Free(TextureData);
				delete Regions;
			});

			ReceivedImageDelegate.ExecuteIfBound(RemoteImage);
		}
	}
}