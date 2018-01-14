// Copyright 2017 Andrew Grant
// This file is part of RemoteViewer and is freely licensed for commercial and 
// non-commercial use under an MIT license
// See https://github.com/andrewgrant/RemoteViewer for more info


#include "RemoteSessionRole.h"
#include "RemoteSession.h"
#include "Channels/RemoteSessionChannel.h"

DEFINE_LOG_CATEGORY(LogRemoteSession);

FRemoteSessionRole::~FRemoteSessionRole()
{
	StopBackgroundThread();
	Channels.Empty();
	Close();
}

void FRemoteSessionRole::Tick(float DeltaTime)
{
	if (OSCConnection.IsValid())
	{
		if (OSCConnection->IsConnected())
		{
			if (ThreadRunning == false)
			{
				OSCConnection->ReceivePackets();
			}

			for (auto& Channel : Channels)
			{
				Channel->Tick(DeltaTime);
			}
		}
		else
		{
			UE_LOG(LogRemoteSession, Warning, TEXT("Connection %s has disconnected."), *OSCConnection->GetDescription());
			OSCConnection = nullptr;
		}
	}
}

void FRemoteSessionRole::SetReceiveInBackground(bool bValue)
{
	if (bValue && !ThreadRunning)
	{
		StartBackgroundThread();
	}
	else if (!bValue && ThreadRunning)
	{
		StopBackgroundThread();
	}
}

void FRemoteSessionRole::StartBackgroundThread()
{
	check(ThreadRunning == false);
	ThreadExitRequested = false;
	ThreadRunning = true;

	FRunnableThread* Thread = FRunnableThread::Create(this, TEXT("RemoteSessionClientThread"));
}

uint32 FRemoteSessionRole::Run()
{
	double LastTick = FPlatformTime::Seconds();

	while (ThreadExitRequested == false)
	{
		const double DeltaTime = FPlatformTime::Seconds() - LastTick;

		if (IsConnected() == false)
		{
			FPlatformProcess::SleepNoStats(0);
			continue;
		}

		OSCConnection->ReceivePackets(1);
		LastTick = FPlatformTime::Seconds();
	}

	return 0;
}

void FRemoteSessionRole::StopBackgroundThread()
{
	if (ThreadRunning == false)
	{
		return;
	}

	ThreadExitRequested = true;

	while (ThreadRunning)
	{
		FPlatformProcess::SleepNoStats(0);
	}
}

TSharedPtr<IRemoteSessionChannel> FRemoteSessionRole::GetChannel(const FString& InType)
{
	TSharedPtr<IRemoteSessionChannel> Channel;

	TSharedPtr<IRemoteSessionChannel>* FoundChannel = Channels.FindByPredicate([InType](auto Item) {
		return Item->GetType() == InType;
	});

	if (FoundChannel)
	{
		Channel = *FoundChannel;
	}

	return Channel;
}
