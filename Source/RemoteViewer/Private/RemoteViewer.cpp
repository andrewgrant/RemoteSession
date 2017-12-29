// Copyright 2017 Andrew Grant
// Licensed under BSD License 2.0. 
// See https://github.com/andrewgrant/RemoteViewer for more info

#include "RemoteViewer.h"
#include "Framework/Application/SlateApplication.h"
#include "BackChannel/Transport/IBackChannelTransport.h"
#include "RemoteViewerHost.h"
#include "RemoteViewerClient.h"

#define LOCTEXT_NAMESPACE "FRemoteViewerModule"


#define REMOTEVIEWER_AUTOINIT_HOST 0 // !UE_BUILD_SHIPPING
#define REMOTEVIEWER_TEST 0 //(1 && !UE_BUILD_SHIPPING)


class FRemoteViewerModule : public IRemoteViewerModule, public FTickableGameObject
{
protected:

	FString								HostAddress;

	TSharedPtr<FRemoteViewerHost>		Host;
	TSharedPtr<FRemoteViewerClient>		Client;

	// todo - icky
	FRemoteViewerReceivedImageDelegate		ClientReceivedDelegate;
	float									ClientConnectionTimer;
	bool									ClientIsConnected;

public:
	void StartupModule()
	{
#if REMOTEVIEWER_AUTOINIT_HOST
		// Change this to test to create a loop back connection
		if (PLATFORM_DESKTOP)
		{
			InitHost();
		}
#endif

		ClientConnectionTimer = 0.0f;
		ClientIsConnected = false;
	}

	void ShutdownModule()
	{
		// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
		// we call this function before unloading the module.
	}

	virtual void InitClient(const TCHAR* RemoteAddress) override
	{
		if (Client.IsValid())
		{
			Client = nullptr;
		}

		Client = MakeShareable(new FRemoteViewerClient());

		HostAddress = RemoteAddress;
		if (HostAddress.Contains(TEXT(":")) == false)
		{
			HostAddress += TEXT(":1313");
		}

		UE_LOG(LogRemoteViewer, Display, TEXT("Will attempt to connect to %s.."), *HostAddress);
	}

	virtual bool IsClientConnected() const override
	{
		return Client.IsValid() && Client->IsConnected();
	}

	virtual void StopClient() override
	{
		Client = nullptr;
	}

	FRemoteViewerReceivedImageDelegate& GetClientImageReceivedDelegate() override
	{
		return ClientReceivedDelegate;
	}

	virtual void InitHost(const int16 Port=0) override
	{
		if (Host.IsValid())
		{
			Host = nullptr;
		}

		TSharedPtr<FRemoteViewerHost> NewHost = MakeShareable(new FRemoteViewerHost());

		int16 SelectedPort = Port ? Port : 1313;

		if (NewHost->StartListening(SelectedPort))
		{
			Host = NewHost;
			UE_LOG(LogRemoteViewer, Log, TEXT("Started listening on port %d"), SelectedPort);
		}
		else
		{
			UE_LOG(LogRemoteViewer, Error, TEXT("Failed to start host listening on port %d"), SelectedPort);
		}
	}

	virtual bool IsHostRunning() const override
	{
		return Host.IsValid();
	}

	virtual bool IsHostConnected() const override
	{
		return Host.IsValid() && Host->IsConnected();
	}

	virtual void StopHost() override
	{
		Host = nullptr;
	}

	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FRemoteViewer, STATGROUP_Tickables);
	}
	
	virtual bool IsTickable() const override
	{
		return true;
	}

	virtual void Tick(float DeltaTime) override
	{
		bool ClientIsValid = Client.IsValid();
		bool NewClientIsConnected = ClientIsValid && Client->IsConnected();

		if (NewClientIsConnected == false && ClientIsConnected)
		{
			UE_LOG(LogRemoteViewer, Warning, TEXT("Closing Client due to disconnection."));
			ClientIsConnected = false;
		}

		if (ClientIsValid)
		{	
			if (HostAddress.Len() && ClientIsConnected == false)
			{
				ClientConnectionTimer += DeltaTime;

				if (ClientConnectionTimer >= 5.0f)
				{
					UE_LOG(LogRemoteViewer, Display, TEXT("Attempting to connect to %s.."), *HostAddress);

					if (Client->Connect(*HostAddress))
					{
						// if loopback, tell the client not to respond directly
						if (Host.IsValid())
						{
							Client->SetConsumeInput(true);
						}

						UE_LOG(LogRemoteViewer, Log, TEXT("Connected to host at %s"), *HostAddress);
						ClientIsConnected = true;
					}

					ClientConnectionTimer = 0.0f;
				}
			}

			Client->GetClientImageReceivedDelegate() = ClientReceivedDelegate;
			Client->Tick();
			ClientIsConnected = NewClientIsConnected;
		}
	
		if (Host.IsValid())
		{
			// disable client if it exists, so it doesn't record what we're about to send...
			if (ClientIsConnected)
			{
				Client->SetRecording(false);
			}

			Host->Tick();

			if (ClientIsConnected)
			{
				Client->SetRecording(true);
			}
		}
	}	
};
	
IMPLEMENT_MODULE(FRemoteViewerModule, RemoteViewer)

#undef LOCTEXT_NAMESPACE
