// Copyright 2017 Andrew Grant
// Licensed under BSD License 2.0. 
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

	virtual void InitClient(const TCHAR* RemoteAddress) = 0;
	virtual bool IsClientConnected() const = 0;
	virtual void StopClient() = 0;
	virtual FRemoteViewerReceivedImageDelegate& GetClientImageReceivedDelegate() = 0;

	virtual void InitHost(const int16 Port=0) = 0;
	virtual bool IsHostRunning() const = 0;
	virtual bool IsHostConnected() const = 0;
	virtual void StopHost() = 0;

};