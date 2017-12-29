// Copyright 2017 Andrew Grant
// Licensed under BSD License 2.0. 
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

	bool StartListening(const uint16 Port);

	virtual void Tick() override;

protected:

	bool		OnIncomingConnection(TSharedRef<IBackChannelConnection> NewConnection);
		
	void		OnRemoteMessage(FBackChannelOSCMessage& Message, FBackChannelOSCDispatch& Dispatch);

	void		SendImageToClients(int32 Width, int32 Height, const TArray<FColor>& ImageData);

	TSharedPtr<IBackChannelListener> Listener;

	TSharedPtr<FRecordingMessageHandler>	PlaybackMessageHandler;

	double LastImageTime;
};