// Copyright 2017 Andrew Grant
// Licensed under BSD License 2.0. 
// See https://github.com/andrewgrant/RemoteViewer for more info

#include "RemoteViewerController.h"
#include "IInputDeviceModule.h"
#include "Tickable.h"
#include "BackChannel/Transport/IBackChannelTransport.h"

#define LOCTEXT_NAMESPACE "FRemoteViewerControllerModule"

class FRemoteViewerControllerPlugin : public IInputDeviceModule, public FTickableGameObject
{

	virtual void StartupModule() override
	{
		IInputDeviceModule::StartupModule();
	}

	virtual TSharedPtr< class IInputDevice > CreateInputDevice(const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler) override
	{
#if 0 // WITH_STEAM_CONTROLLER
		return TSharedPtr< class IInputDevice >(new FSteamController(InMessageHandler));
#else

		//DefaultHandler = InMessageHandler;

		//MessageHandler = MakeShareable(new FProxyMessageHandler(InMessageHandler));
		return nullptr;
#endif
	}

	virtual void Tick(float DeltaTime) override
	{
		
	}

	bool Init()
	{

		return true;
	}

	virtual bool IsTickable() const override
	{
		return true;
	}

	virtual TStatId GetStatId() const override
	{
		return TStatId();	// todo
	}
};

IMPLEMENT_MODULE(FRemoteViewerControllerPlugin, RemoteViewerController)

#undef LOCTEXT_NAMESPACE