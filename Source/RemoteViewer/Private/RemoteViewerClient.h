// Copyright 2017 Andrew Grant
// Licensed under BSD License 2.0. 
// See https://github.com/andrewgrant/RemoteViewer for more info

#pragma once

#include "RemoteViewerRole.h"
#include "MessageHandler/RecordingMessageHandler.h"

class FGenericApplicationMessageHandler;
class FRecordingMessageHandler;
class FBackChannelOSCMessage;
class FBackChannelOSCDispatch;




class FRemoteViewerClient : public FRemoteViewerRole, public IRecordingMessageHandlerWriter
{
public:

	FRemoteViewerClient();
	~FRemoteViewerClient();

	bool Connect(const TCHAR* Address);

	void SetRecording(bool bRecord);
	void SetConsumeInput(bool bConsume);

	FRemoteViewerReceivedImageDelegate& GetClientImageReceivedDelegate();

protected:

	virtual void RecordMessage(const TCHAR* MsgName, const TArray<uint8>& Data) override;

	void ReceivedImage(FBackChannelOSCMessage& Message, FBackChannelOSCDispatch& Dispatch);

protected:

	TWeakPtr<FGenericApplicationMessageHandler> DefaultHandler;
	TSharedPtr<FRecordingMessageHandler> RecordingHandler;
	FRemoteViewerReceivedImageDelegate ReceivedImageDelegate;
	TArray<uint8> HostImage;
};