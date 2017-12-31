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



class FRemoteViewerClient : public FRemoteViewerRole, public IRecordingMessageHandlerWriter
{
public:

	FRemoteViewerClient(const TCHAR* InHostAddress);
	~FRemoteViewerClient();

	void SetRecording(bool bRecord);
	void SetConsumeInput(bool bConsume);

	FRemoteViewerReceivedImageDelegate& GetClientImageReceivedDelegate();

	virtual void Tick(float DeltaTime) override;

protected:

	bool			Connect();
	virtual void	RecordMessage(const TCHAR* MsgName, const TArray<uint8>& Data) override;

	void			UpdateRemoteImage(FBackChannelOSCMessage& Message, FBackChannelOSCDispatch& Dispatch);
	void			CreateRemoteImage(const int32 InWidth, const int32 InHeight);

protected:

	FString		HostAddress;
	double		ConnectionAttemptTimer;
	double		LastConnectionAttemptTime;

	TWeakPtr<FGenericApplicationMessageHandler> DefaultHandler;
	TSharedPtr<FRecordingMessageHandler> RecordingHandler;
	FRemoteViewerReceivedImageDelegate ReceivedImageDelegate;
	TArray<uint8> HostImage;

	UTexture2D*		RemoteImage;
};