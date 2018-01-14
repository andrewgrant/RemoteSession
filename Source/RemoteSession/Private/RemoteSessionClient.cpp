// Copyright 2017 Andrew Grant
// This file is part of RemoteViewer and is freely licensed for commercial and 
// non-commercial use under an MIT license
// See https://github.com/andrewgrant/RemoteViewer for more info

#include "RemoteSessionClient.h"
#include "BackChannel/Transport/IBackChannelTransport.h"
#include "Framework/Application/SlateApplication.h"	
#include "Channels/RemoteSessionInputChannel.h"
#include "Channels/RemoteSessionFrameBufferChannel.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Sockets.h"
#include "RemoteSession.h"

bool InGameThread()
{
	if (GIsGameThreadIdInitialized)
	{
		return FPlatformTLS::GetCurrentThreadId() == GGameThreadId;
	}
	else
	{
		return true;
	}
}

#define RV_CLIENT_THREADED 0

DECLARE_CYCLE_STAT(TEXT("RVClientTick"), STAT_RVClientTick, STATGROUP_Game);


FRemoteSessionClient::FRemoteSessionClient(const TCHAR* InHostAddress)
{
	HostAddress = InHostAddress;
	ConnectionAttemptTimer = FLT_MAX;		// attempt a connection asap
	LastConnectionAttemptTime = 0;

	IsConnecting = false;

	if (HostAddress.Contains(TEXT(":")) == false)
	{
		HostAddress += FString::Printf(TEXT(":%d"), (int32)IRemoteSessionModule::kDefaultPort);
	}

	UE_LOG(LogRemoteSession, Display, TEXT("Will attempt to connect to %s.."), *HostAddress);
}

FRemoteSessionClient::~FRemoteSessionClient()
{
	Close();
}

void FRemoteSessionClient::Tick(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_RVClientTick);

	if (IsConnected() == false)
	{
		if (IsConnecting == false)
		{
			const double TimeSinceLastAttempt = FPlatformTime::Seconds() - LastConnectionAttemptTime;

			if (TimeSinceLastAttempt >= 5.0)
			{
				StartConnection();
			}
		}

		if (IsConnecting)
		{
			CheckConnection();
		}
	}

	FRemoteSessionRole::Tick(DeltaTime);
}

void  FRemoteSessionClient::StartConnection()
{
	check(IsConnecting == false);

	Close();

	if (IBackChannelTransport* Transport = IBackChannelTransport::Get())
	{
		Connection = Transport->CreateConnection(IBackChannelTransport::TCP);

		if (Connection.IsValid())
		{
			if (Connection->Connect(*HostAddress))
			{
				IsConnecting = true;
			}
		}
	}
}

void FRemoteSessionClient::CheckConnection()
{
	check(IsConnected() == false && IsConnecting);

	bool Success = Connection->WaitForConnection(0, [this](auto InConnection) {
		int32 WantedSize = 4 * 1024 * 1024;
		int32 ActualSize(0);

		Connection->GetSocket()->SetReceiveBufferSize(WantedSize, ActualSize);

		OSCConnection = MakeShareable(new FBackChannelOSCConnection(Connection.ToSharedRef()));

		Channels.Add(MakeShareable(new FRemoteSessionInputChannel(ERemoteSessionChannelMode::Send, OSCConnection)));
		Channels.Add(MakeShareable(new FRemoteSessionFrameBufferChannel(ERemoteSessionChannelMode::Receive, OSCConnection)));

		OSCConnection->Start();

		UE_LOG(LogRemoteSession, Log, TEXT("Connected to host at %s (ReceiveSize=%dkb)"), *HostAddress, ActualSize / 1024);

		IsConnecting = false;

		return true;
	});

	if (Success == false)
	{
		IsConnecting = false;
		LastConnectionAttemptTime = FPlatformTime::Seconds();
	}
}
