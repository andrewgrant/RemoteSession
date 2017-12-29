// Copyright 2017 Andrew Grant
// Licensed under BSD License 2.0. 
// See https://github.com/andrewgrant/RemoteViewer for more info

#pragma once

#include "BackChannel/Protocol/OSC/BackChannelOSCConnection.h"

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

	virtual void Tick();

protected:
	
	TSharedPtr<FBackChannelOSCConnection> OSCConnection;

};