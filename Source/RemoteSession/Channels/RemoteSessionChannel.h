// Copyright 2017 Andrew Grant
// This file is part of RemoteSession and is freely licensed for commercial and 
// non-commercial use under an MIT license
// See https://github.com/andrewgrant/RemoteSession for more info

#pragma once

#include "CoreMinimal.h"

enum class ERemoteSessionChannelMode
{
	Receive,
	Send
};

class FBackChannelOSCConnection;

class REMOTESESSION_API IRemoteSessionChannel
{

public:

	IRemoteSessionChannel(ERemoteSessionChannelMode InRole, TSharedPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> InConnection) {}

	virtual ~IRemoteSessionChannel() {}

	virtual void Tick(const float InDeltaTime) = 0;

	virtual FString GetType() const = 0;

};
