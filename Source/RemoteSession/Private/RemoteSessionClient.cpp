// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "RemoteSessionClient.h"
#include "BackChannel/Transport/IBackChannelTransport.h"
#include "Framework/Application/SlateApplication.h"	
#include "Channels/RemoteSessionInputChannel.h"
#include "Channels/RemoteSessionFrameBufferChannel.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Sockets.h"
#include "RemoteSession.h"


DECLARE_CYCLE_STAT(TEXT("RSClientTick"), STAT_RDClientTick, STATGROUP_Game);

FRemoteSessionClient::FRemoteSessionClient(const TCHAR* InHostAddress)
{
	HostAddress = InHostAddress;
	ConnectionAttemptTimer = FLT_MAX;		// attempt a connection asap
	LastConnectionAttemptTime = 0;
    ConnectionTimeout = 5;

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
	SCOPE_CYCLE_COUNTER(STAT_RDClientTick);

	if (IsConnected() == false)
	{
		if (IsConnecting == false)
		{
			const double TimeSinceLastAttempt = FPlatformTime::Seconds() - LastConnectionAttemptTime;

			if (TimeSinceLastAttempt >= 5.0)
			{
				StartConnection();
                LastConnectionAttemptTime = FPlatformTime::Seconds();
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
    
    const double TimeSpentConnecting = FPlatformTime::Seconds() - LastConnectionAttemptTime;

	if (Success == false && TimeSpentConnecting >= ConnectionTimeout)
	{
		IsConnecting = false;
        Close();
	}
}
