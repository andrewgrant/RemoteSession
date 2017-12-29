// Copyright 2017 Andrew Grant
// Licensed under BSD License 2.0. 
// See https://github.com/andrewgrant/RemoteViewer for more info

#include "RemoteViewerRole.h"

DEFINE_LOG_CATEGORY(LogRemoteViewer);

void FRemoteViewerRole::Tick()
{
	if (OSCConnection.IsValid())
	{
		if (OSCConnection->IsConnected())
		{
			OSCConnection->DispatchMessages();
		}
		else
		{
			UE_LOG(LogRemoteViewer, Warning, TEXT("Connection %s has disconnected."), *OSCConnection->Description());
			OSCConnection = nullptr;
		}
	}
}
