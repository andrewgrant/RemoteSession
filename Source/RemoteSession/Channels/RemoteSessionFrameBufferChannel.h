// Copyright 2017 Andrew Grant
// This file is part of RemoteViewer and is freely licensed for commercial and 
// non-commercial use under an MIT license
// See https://github.com/andrewgrant/RemoteViewer for more info

#pragma once

#include "RemoteSessionChannel.h"
#include "ThreadSafeBool.h"

struct FQueuedFBImage
{
	int32				Width;
	int32				Height;
	TArray<uint8>		ImageData;
};


class FBackChannelOSCMessage;
class FBackChannelOSCDispatch;
class FFrameGrabber;
class FSceneViewport;
class UTexture2D;

class REMOTESESSION_API FRemoteSessionFrameBufferChannel : public IRemoteSessionChannel
{
public:

	FRemoteSessionFrameBufferChannel(ERemoteSessionChannelMode InRole, TSharedPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> InConnection);

	~FRemoteSessionFrameBufferChannel();

	void CaptureViewport(TSharedRef<FSceneViewport> Viewport);

	virtual void Tick(const float InDeltaTime) override;

	void SetQuality(int32 InQuality, int32 InFramerate);

	UTexture2D* GetRemoteImage() const;

	static FString StaticType();
	virtual FString GetType() const override { return StaticType(); }

protected:

	void		SendImageToClients(int32 Width, int32 Height, const TArray<FColor>& ImageData);

	void ReceiveRemoteImage(FBackChannelOSCMessage & Message, FBackChannelOSCDispatch & Dispatch);

	void CreateRemoteImage(const int32 InSlot, const int32 InWidth, const int32 InHeight);

	TSharedPtr<FFrameGrabber>				FrameGrabber;

	FCriticalSection					ImageMutex;
	TArray<TSharedPtr<FQueuedFBImage>>	QueuedImages;
	int32					RemoteImageWidth;
	int32					RemoteImageHeight;
	FThreadSafeBool			RemoteImageCreationRequested;
	UTexture2D*				RemoteImage[2];
	int32					RemoteImageIndex;

	double LastImageTime;

	TSharedPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> Connection;

	ERemoteSessionChannelMode Role;

};