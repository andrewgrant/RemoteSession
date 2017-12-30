// Copyright 2017 Andrew Grant
// This file is part of RemoteViewer and is freely licensed for commercial and 
// non-commercial use under an MIT license
// See https://github.com/andrewgrant/RemoteViewer for more info

#pragma once

#include "BackChannel/Protocol/OSC/BackChannelOSCConnection.h"
#include "Tickable.h"

class FRemoteViewerRole
{
public:

	virtual ~FRemoteViewerRole() {}

	virtual void Close()
	{
		OSCConnection = nullptr;
	}

	virtual bool IsConnected() const
	{
		return OSCConnection.IsValid() && OSCConnection->IsConnected();
	}

	virtual void Tick( float DeltaTime );

protected:
	
	TSharedPtr<FBackChannelOSCConnection> OSCConnection;

};