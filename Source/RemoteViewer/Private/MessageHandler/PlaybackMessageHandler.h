// Copyright 2017 Andrew Grant
// Licensed under BSD License 2.0. 
// See https://github.com/andrewgrant/RemoteViewer for more info

#pragma once

#include "CoreMinimal.h"

class FBackChannelOSCDispatch;

class FPlaybackMessageHandler : public TSharedFromThis<FPlaybackMessageHandler>
{
public:

	FPlaybackMessageHandler(const TSharedPtr<FGenericApplicationMessageHandler>& InTargetHandler);

	virtual ~FPlaybackMessageHandler() {};

public:

	void BindDispatchAddresses(FBackChannelOSCDispatch& Dispatch);

	virtual void PlayOnKeyChar(FBackChannelOSCMessage& Msg);

protected:

	const TSharedPtr<FGenericApplicationMessageHandler> TargetHandler;
};