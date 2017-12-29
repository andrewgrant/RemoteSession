// Copyright 2017 Andrew Grant
// Licensed under BSD License 2.0. 
// See https://github.com/andrewgrant/RemoteViewer for more info

#include "PlaybackMessageHandler.h"
#include "BackChannel/Protocol/OSC/BackChannelOSCDispatch.h"

FPlaybackMessageHandler::FPlaybackMessageHandler(const TSharedPtr<FGenericApplicationMessageHandler>& InTargetHandler)
	: TargetHandler(InTargetHandler)
{

}

#define BIND_ADDRESS(Dispatch, Address, Func) \
	TWeakPtr<FPlaybackMessageHandler> WeakThis(AsShared());\
	Dispatch.GetAddressHandler(Address).AddLambda([WeakThis](FBackChannelOSCMessage& InMsg, FBackChannelOSCDispatch& InDispatch)\
	{\
		if (WeakThis.IsValid()) { WeakThis.Pin()->Func(InMsg); }\
	});\


void FPlaybackMessageHandler::BindDispatchAddresses(FBackChannelOSCDispatch& Dispatch)
{
	BIND_ADDRESS(Dispatch, TEXT("/Keyboard/OnKeyChar"), PlayOnKeyChar);
}

void FPlaybackMessageHandler::PlayOnKeyChar(FBackChannelOSCMessage& Msg)
{
	TCHAR Character;
	bool IsRepeat;

	Msg << Character;
	Msg << IsRepeat;

	TargetHandler->OnKeyChar(Character, IsRepeat);
}