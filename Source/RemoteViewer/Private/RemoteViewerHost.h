// Copyright 2017 Andrew Grant
// This file is part of RemoteViewer and is freely licensed for commercial and 
// non-commercial use under an MIT license
// See https://github.com/andrewgrant/RemoteViewer for more info

#pragma once

#include "RemoteViewerRole.h"
#include "BackChannel/Utils/BackChannelThreadedConnection.h"

class FBackChannelThreadedConnection;
class FBackChannelListener;
class IBackChannelConnection;
class FRecordingMessageHandler;
class FFrameGrabber;
class IImageWrapper;

class FRemoteViewerHost : public FRemoteViewerRole, public TSharedFromThis<FRemoteViewerHost>, public IBackChannelThreadedConnectionDelegate
{
public:

	FRemoteViewerHost(int32 InQuality, int32 InFramerate);
	~FRemoteViewerHost();

	bool StartListening(const uint16 Port);

	void SetScreenSharing(const bool bEnabled);

	void SetConsumeInput(const bool bConsume);

	virtual void Close() override;

	virtual void Tick(float DeltaTime) override;

protected:

	virtual bool		OnIncomingConnection(TSharedRef<IBackChannelConnection>& NewConnection) override;
	bool				ProcessIncomingConnection(TSharedRef<IBackChannelConnection> NewConnection);
		
	void		OnRemoteMessage(FBackChannelOSCMessage& Message, FBackChannelOSCDispatch& Dispatch);

	void		SendImageToClients(int32 Width, int32 Height, const TArray<FColor>& ImageData);

	TSharedPtr<FBackChannelThreadedConnection> Listener;

	TSharedPtr<FRecordingMessageHandler>	PlaybackMessageHandler;

	TSharedPtr<FFrameGrabber>				FrameGrabber;

	FDelegateHandle		EndFrameDelegate;

	FThreadSafeCounter	AsyncTasks;

	double LastImageTime;
	bool bScreenSharingEnabled;

	int32		Quality;
	int32		Framerate;
};