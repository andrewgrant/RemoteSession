// Copyright 2017 Andrew Grant
// Licensed under BSD License 2.0. 
// See https://github.com/andrewgrant/RemoteViewer for more info

#pragma once

#include "CoreMinimal.h"
#include "ModuleManager.h"



class FRemoteViewerModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};