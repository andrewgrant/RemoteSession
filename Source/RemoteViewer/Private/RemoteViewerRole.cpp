// Copyright 2017 Andrew Grant
// This file is part of RemoteViewer and is freely licensed for commercial and 
// non-commercial use under an MIT license
// See https://github.com/andrewgrant/RemoteViewer for more info

#include "RemoteViewerRole.h"

DEFINE_LOG_CATEGORY(LogRemoteViewer);

void FRemoteViewerRole::Tick(float DeltaTime)
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
