// Copyright 2017 Andrew Grant
// This file is part of RemoteViewer and is freely licensed for commercial and 
// non-commercial use under an MIT license
// See https://github.com/andrewgrant/RemoteViewer for more info

#pragma once

#include "RemoteSessionChannel.h"
#include "MessageHandler/RecordingMessageHandler.h"

class FBackChannelOSCMessage;
class FBackChannelOSCDispatch;

class REMOTESESSION_API FRemoteSessionInputChannel : public IRemoteSessionChannel, public IRecordingMessageHandlerWriter
{
public:

	FRemoteSessionInputChannel(ERemoteSessionChannelMode InRole, TSharedPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> InConnection);

	~FRemoteSessionInputChannel();

	virtual void Tick(const float InDeltaTime) override;

	virtual void RecordMessage(const TCHAR* MsgName, const TArray<uint8>& Data) override;

	void OnRemoteMessage(FBackChannelOSCMessage& Message, FBackChannelOSCDispatch & Dispatch);

	void SetPlaybackWindow(TWeakPtr<SWindow> InWindow, TWeakPtr<FSceneViewport> InViewport);

	static FString StaticType();
	virtual FString GetType() const override { return StaticType(); }

protected:

	TWeakPtr<FGenericApplicationMessageHandler> DefaultHandler;

	TSharedPtr<FRecordingMessageHandler> RecordingHandler;

	TSharedPtr<FRecordingMessageHandler> PlaybackHandler;


	TSharedPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> Connection;


	ERemoteSessionChannelMode Role;
};