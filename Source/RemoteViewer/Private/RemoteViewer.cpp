// Copyright 2017 Andrew Grant
// This file is part of RemoteViewer and is freely licensed for commercial and 
// non-commercial use under an MIT license
// See https://github.com/andrewgrant/RemoteViewer for more info

#include "RemoteViewer.h"
#include "Framework/Application/SlateApplication.h"
#include "BackChannel/Transport/IBackChannelTransport.h"
#include "RemoteViewerHost.h"
#include "RemoteViewerClient.h"

#if WITH_EDITOR
#include "Editor.h"
#endif
#include "CoreGlobals.h"

#define LOCTEXT_NAMESPACE "FRemoteViewerModule"

#define REMOTEVIEWER_AUTOINIT_HOST 0 // !UE_BUILD_SHIPPING
#define REMOTEVIEWER_TEST 0 //(1 && !UE_BUILD_SHIPPING)


class FRemoteViewerModule : public IRemoteViewerModule, public FTickableGameObject
{
protected:

	TSharedPtr<FRemoteViewerHost>		Host;
	TSharedPtr<FRemoteViewerClient>		Client;

	int32								DefaultPort;
	int32								Quality;
	int32								Framerate;

	// todo - icky
	FRemoteViewerReceivedImageDelegate		ClientReceivedDelegate;

	bool bAutoHostWithPIE;
	FDelegateHandle PostPieDelegate;
	FDelegateHandle EndPieDelegate;

public:

	void SetAutoStartWithPIE(bool bEnable)
	{
		bAutoHostWithPIE = bEnable;
	}

	void StartupModule()
	{
		bool bAutoHostWithGame = true;
		DefaultPort = IRemoteViewerModule::kDefaultPort;
		Quality = 85;
		Framerate = 30;
		bAutoHostWithPIE = true;

		GConfig->GetBool(TEXT("RemoteViewer"), TEXT("bAutoHostWithGame"), bAutoHostWithGame, GEngineIni);
		GConfig->GetBool(TEXT("RemoteViewer"), TEXT("bAutoHostWithPIE"), bAutoHostWithPIE, GEngineIni);
		GConfig->GetInt(TEXT("RemoteViewer"), TEXT("HostPort"), DefaultPort, GEngineIni);
		GConfig->GetInt(TEXT("RemoteViewer"), TEXT("Quality"), Quality, GEngineIni);
		GConfig->GetInt(TEXT("RemoteViewer"), TEXT("Framerate"), Framerate, GEngineIni);

		if (PLATFORM_DESKTOP)
		{
			if (GIsEditor)
			{
#if WITH_EDITOR
				PostPieDelegate = FEditorDelegates::PostPIEStarted.AddRaw(this, &FRemoteViewerModule::OnPIEStarted);
				EndPieDelegate = FEditorDelegates::EndPIE.AddRaw(this, &FRemoteViewerModule::OnPIEEnded);
#endif
			}
			else if (bAutoHostWithGame)
			{
				InitHost();
			}
		}
	}

	void ShutdownModule()
	{
		// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
		// we call this function before unloading the module.
#if WITH_EDITOR
		if (PostPieDelegate.IsValid())
		{
			FEditorDelegates::PostPIEStarted.Remove(PostPieDelegate);
		}

		if (EndPieDelegate.IsValid())
		{
			FEditorDelegates::EndPIE.Remove(EndPieDelegate);
		}
#endif
	}

	virtual void InitClient(const TCHAR* RemoteAddress) override
	{
		if (Client.IsValid())
		{
			Client = nullptr;
		}

		Client = MakeShareable(new FRemoteViewerClient(RemoteAddress));

		// if the host is already running then this is a loopback connection so we want
		// to consume all input by default, and disable screen sharing
		if (IsHostRunning())
		{
			Host->SetConsumeInput(true);
			Client->SetConsumeInput(true);
			Host->SetScreenSharing(false);
		}
	}

	virtual bool IsClientConnected() const override
	{
		return Client.IsValid() && Client->IsConnected();
	}

	virtual void StopClient() override
	{
		Client = nullptr;
	}

	virtual void InitHost(const int16 Port = 0) override
	{
#if !UE_BUILD_SHIPPING
		if (Host.IsValid())
		{
			Host = nullptr;
		}

		TSharedPtr<FRemoteViewerHost> NewHost = MakeShareable(new FRemoteViewerHost(Quality, Framerate));

		int16 SelectedPort = Port ? Port : (int16)DefaultPort;

		if (NewHost->StartListening(SelectedPort))
		{
			Host = NewHost;
			UE_LOG(LogRemoteViewer, Log, TEXT("Started listening on port %d"), SelectedPort);
		}
		else
		{
			UE_LOG(LogRemoteViewer, Error, TEXT("Failed to start host listening on port %d"), SelectedPort);
		}
#else
		UE_LOG(LogRemoteViewer, Log, TEXT("RemoteViewer is disabled. Shipping=1"), SelectedPort);
#endif
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

	void OnPIEStarted(bool bSimulating)
	{
		if (bAutoHostWithPIE)
		{
			InitHost();
		}
	}

	void OnPIEEnded(bool bSimulating)
	{
		// always stop, incase it was started via the console
		StopHost();
	}

	FRemoteViewerReceivedImageDelegate& GetClientImageReceivedDelegate() override
	{
		return ClientReceivedDelegate;
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
		bool ClientIsConnected = Client.IsValid() && Client->IsConnected();
		bool HostIsConnected = Host.IsValid() && Host->IsConnected();

		bool DisabledClientRecording = false;

		// mostly these ifs() are to deal with the case of loopback testing where the host should only respond
		// to input from the client, the client should not record anything it sends
		
		bool IsLoopBack = HostIsConnected && ClientIsConnected;

		if (IsLoopBack)
		{
			// disable client so it doesn't record what we're about to send...
			Client->SetRecording(false);
			DisabledClientRecording = true;
		}

		if (Client.IsValid())
		{
			Client->Tick(DeltaTime);
			Client->GetClientImageReceivedDelegate() = ClientReceivedDelegate;
		}

		if (Host.IsValid())
		{
			// tick the host, and allow it to pass through input
			Host->SetConsumeInput(false);
			Host->Tick(DeltaTime);
			Host->SetConsumeInput(IsLoopBack);
		}

		if (DisabledClientRecording)
		{
			Client->SetRecording(true);
		}
	}	
};
	
IMPLEMENT_MODULE(FRemoteViewerModule, RemoteViewer)

FAutoConsoleCommand GRemoteHostCommand(
	TEXT("remote.host"),
	TEXT("Starts a remote viewer host"),
	FConsoleCommandDelegate::CreateStatic(
		[]()
	{
		if (FRemoteViewerModule* Viewer = FModuleManager::LoadModulePtr<FRemoteViewerModule>("RemoteViewer"))
		{
			Viewer->InitHost();
		}
	})
);

FAutoConsoleCommand GRemoteDisconnectCommand(
	TEXT("remote.disconnect"),
	TEXT("Disconnect remote viewer"),
	FConsoleCommandDelegate::CreateStatic(
		[]()
	{
		if (FRemoteViewerModule* Viewer = FModuleManager::LoadModulePtr<FRemoteViewerModule>("RemoteViewer"))
		{
			Viewer->StopClient();
			Viewer->StopHost();
		}
	})
);

FAutoConsoleCommand GRemoteAutoPIECommand(
	TEXT("remote.autopie"),
	TEXT("enables remote with pie"),
	FConsoleCommandDelegate::CreateStatic(
		[]()
{
	if (FRemoteViewerModule* Viewer = FModuleManager::LoadModulePtr<FRemoteViewerModule>("RemoteViewer"))
	{
		Viewer->SetAutoStartWithPIE(true);
	}
})
);

#undef LOCTEXT_NAMESPACE
