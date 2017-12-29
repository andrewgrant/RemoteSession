// Copyright 2017 Andrew Grant
// Licensed under BSD License 2.0. 
// See https://github.com/andrewgrant/RemoteViewer for more info

#include "MessageTypes.h"


FBackChannelOSCMessage& operator << (FBackChannelOSCMessage& OSCMsg, FRecordedKeyCharInput& Input)
{
	return OSCMsg << Input.KeyChar << Input.IsRepeat;
}