// Copyright 2017 Andrew Grant
// This file is part of RemoteViewer and is freely licensed for commercial and 
// non-commercial use under an MIT license
// See https://github.com/andrewgrant/RemoteViewer for more info

#pragma once

#include "RemoteViewerRole.h"

class FBackChannelListener;
class IBackChannelConnection;
class FRecordingMessageHandler;

class FRemoteViewerHost : public FRemoteViewerRole, public TSharedFromThis<FRemoteViewerHost>
{
public:

	FRemoteViewerHost();
	~FRemoteViewerHost();

	bool StartListening(const uint16 Port);

	void SetScreenSharing(const bool bEnabled);

	void SetConsumeInput(const bool bConsume);

	virtual void Close() override;

	virtual void Tick(float DeltaTime) override;

protected:

	bool		OnIncomingConnection(TSharedRef<IBackChannelConnection> NewConnection);
		
	void		OnRemoteMessage(FBackChannelOSCMessage& Message, FBackChannelOSCDispatch& Dispatch);

	void		SendImageToClients(int32 Width, int32 Height, const TArray<FColor>& ImageData);

	void		OnEndFrame();

	TSharedPtr<IBackChannelListener> Listener;

	TSharedPtr<FRecordingMessageHandler>	PlaybackMessageHandler;

	FDelegateHandle		EndFrameDelegate;

	FThreadSafeCounter	AsyncTasks;

	double LastImageTime;
	bool bScreenSharingEnabled;
};