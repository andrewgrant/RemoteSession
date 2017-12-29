// Copyright 2017 Andrew Grant
// Licensed under BSD License 2.0. 
// See https://github.com/andrewgrant/RemoteViewer for more info

#include "RemoteViewerClient.h"
#include "BackChannel/Transport/IBackChannelTransport.h"
#include "Framework/Application/SlateApplication.h"	
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"

FRemoteViewerClient::FRemoteViewerClient()
{
	
}

FRemoteViewerClient::~FRemoteViewerClient()
{
	// should restore handler? What if something else changed it...
	if (RecordingHandler.IsValid())
	{
		RecordingHandler->SetRecordingHandler(nullptr);
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

bool FRemoteViewerClient::Connect(const TCHAR* Address)
{
	if (OSCConnection.IsValid())
	{
		return false;
	}

	if (RecordingHandler.IsValid() == false && DefaultHandler.IsValid() == false)
	{
		DefaultHandler = FSlateApplication::Get().GetPlatformApplication()->GetMessageHandler();

		RecordingHandler = MakeShareable(new FRecordingMessageHandler(DefaultHandler.Pin()));

		RecordingHandler->SetRecordingHandler(this);

		FSlateApplication::Get().GetPlatformApplication()->SetMessageHandler(RecordingHandler.ToSharedRef());
	}

	if (IBackChannelTransport* Transport = IBackChannelTransport::Get())
	{
		TSharedPtr<IBackChannelConnection> Connection = Transport->CreateConnection(IBackChannelTransport::TCP);

		if (Connection.IsValid() && Connection->Connect(Address))
		{
			OSCConnection = MakeShareable(new FBackChannelOSCConnection(Connection.ToSharedRef()));

			OSCConnection->GetDispatchMap().GetAddressHandler(TEXT("/Screen")).AddRaw(this, &FRemoteViewerClient::ReceivedImage);

			OSCConnection->SetMessageOptions(TEXT("/Screen"), 1);

			OSCConnection->Start();
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

void FRemoteViewerClient::ReceivedImage(FBackChannelOSCMessage& Message, FBackChannelOSCDispatch& Dispatch)
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
			static FTexture2DRHIRef TexRef = nullptr;
			static UTexture2D* DynamicTexture = nullptr;

			if (DynamicTexture == nullptr || Width != DynamicTexture->GetSizeX() || Height != DynamicTexture->GetSizeY())
			{
				DynamicTexture = UTexture2D::CreateTransient(Width, Height);
				DynamicTexture->AddToRoot();
				DynamicTexture->UpdateResource();
			}

			const size_t DataLen = Width * Height * 8;
			uint8* DataCopy = (uint8*)FMemory::Malloc(DataLen);
			FMemory::Memcpy(DataCopy, RawData->GetData(), DataLen);

			//TSharedPtr<TArray<uint8>> DataCopy = MakeShareable(new TArray<uint8>(*RawData));
			
#if 1
			FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Width, Height);
			
			// update and free data
			UpdateTextureRegions(DynamicTexture, 0, 1, Region, 4 * Width, 8, DataCopy, true);
			DynamicTexture->UpdateResource();
#else

			ENQUEUE_UNIQUE_RENDER_COMMAND_THREEPARAMETER(UpdateTexture,
				FTexture2DResource*, TextureResource, (FTexture2DResource*)DynamicTexture->Resource,
				TSharedPtr<TArray<uint8>>, DataCopy, DataCopy,
				int32, DataLen, 0,
				{
					FTexture2DRHIRef RHIRef = TextureResource->GetTexture2DRHI();
					uint32 Stride = 0;
					uint8* TextureBuffer = (uint8*)RHILockTexture2D(RHIRef, 0, RLM_WriteOnly, Stride, false);
					FMemory::Memcpy(TextureBuffer, DataCopy->GetData(), DataCopy->Num());
					RHIUnlockTexture2D(RHIRef, 0, false);
				});
#endif

			ReceivedImageDelegate.ExecuteIfBound(DynamicTexture);

		}
	}

}