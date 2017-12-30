// Copyright 2017 Andrew Grant
// This file is part of RemoteViewer and is freely licensed for commercial and 
// non-commercial use under an MIT license
// See https://github.com/andrewgrant/RemoteViewer for more info

#pragma once

#include "CoreMinimal.h"
#include "ModuleManager.h"

REMOTEVIEWER_API DECLARE_LOG_CATEGORY_EXTERN(LogRemoteViewer, Log, All);

DECLARE_DELEGATE_OneParam(FRemoteViewerReceivedImageDelegate, UTexture2D*)


class IRemoteViewerModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override = 0;
	virtual void ShutdownModule() override = 0;
	/** End IModuleInterface implementation */

public:

	enum
	{
		kDefaultPort = 1010
	};

public:
	/** Client implementation */

	/** Initialize a client that will attempt to connect to the provided address */
	virtual void InitClient(const TCHAR* RemoteAddress) = 0;

	/** Returns true/false based on the connection state of the client */
	virtual bool IsClientConnected() const = 0;

	/** Stops the client. After this InitClient() must be called if a new connection is desired */
	virtual void StopClient() = 0;

	/** Returns the client image delegate which can be bound to */
	virtual FRemoteViewerReceivedImageDelegate& GetClientImageReceivedDelegate() = 0;

public:
	/** Server implementation */

	/** Starts a RemoteViewer server that listens for clients on the provided port */
	virtual void InitHost(const int16 Port=0) = 0;

	/** Returns true/false based on the running state of the host server */
	virtual bool IsHostRunning() const = 0;

	/** Returns true/false if a client is connected */
	virtual bool IsHostConnected() const = 0;

	/** Stops the server, after this InitHost() must be called if a new connection is desired */
	virtual void StopHost() = 0;

};