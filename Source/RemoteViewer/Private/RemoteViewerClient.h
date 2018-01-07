// Copyright 2017 Andrew Grant
// This file is part of RemoteViewer and is freely licensed for commercial and 
// non-commercial use under an MIT license
// See https://github.com/andrewgrant/RemoteViewer for more info

#pragma once

#include "RemoteViewerRole.h"
#include "MessageHandler/RecordingMessageHandler.h"

class FGenericApplicationMessageHandler;
class FRecordingMessageHandler;
class FBackChannelOSCMessage;
class FBackChannelOSCDispatch;
class UTexture2D;


struct FQueuedImage
{
	int32				Width;
	int32				Height;
	TArray<uint8>		ImageData;
};


class FRemoteViewerClient : public FRemoteViewerRole, public IRecordingMessageHandlerWriter, FRunnable
{
public:

	FRemoteViewerClient(const TCHAR* InHostAddress);
	~FRemoteViewerClient();

	void SetRecording(bool bRecord);
	void SetConsumeInput(bool bConsume);

	FRemoteViewerReceivedImageDelegate& GetClientImageReceivedDelegate();

	virtual void Tick(float DeltaTime) override;

protected:

	void			StartConnection();
	void			CheckConnection();

	virtual void	Close() override;
	virtual void	RecordMessage(const TCHAR* MsgName, const TArray<uint8>& Data) override;

	void			UpdateRemoteImage(FBackChannelOSCMessage& Message, FBackChannelOSCDispatch& Dispatch);
	void			CreateRemoteImage(const int32 InWidth, const int32 InHeight);

	void			StartConnectionThread();
	void			StopConnectionThread();

	uint32			Run();

protected:

	FString								HostAddress;
	TSharedPtr<IBackChannelConnection>	Connection;
	bool								IsConnecting;


	double		ConnectionAttemptTimer;
	double		LastConnectionAttemptTime;

	TWeakPtr<FGenericApplicationMessageHandler> DefaultHandler;
	TSharedPtr<FRecordingMessageHandler> RecordingHandler;
	FRemoteViewerReceivedImageDelegate ReceivedImageDelegate;

	FThreadSafeBool			ThreadExitRequested;
	FThreadSafeBool			ThreadRunning;
	FThreadSafeBool			ImageReady;
	FCriticalSection		ConnectionMutex;

	int32					RemoteImageWidth;
	int32					RemoteImageHeight;
	FThreadSafeBool			RemoteImageCreationRequested;
	UTexture2D*				RemoteImage;


	FCriticalSection					ImageMutex;
	TArray<TSharedPtr<FQueuedImage>>	QueuedImages;
};