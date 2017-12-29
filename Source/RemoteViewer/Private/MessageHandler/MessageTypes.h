// Copyright 2017 Andrew Grant
// Licensed under BSD License 2.0. 
// See https://github.com/andrewgrant/RemoteViewer for more info

#pragma once

#include "CoreMinimal.h"
#include "BackChannel/Protocol/OSC/BackChannelOSC.h"


struct FRecordedKeyCharInput
{
	FRecordedKeyCharInput(TCHAR Inkey, bool InRepeat)
	{
		KeyChar = Inkey;
		IsRepeat = InRepeat;
	}

	TCHAR	KeyChar;
	bool	IsRepeat;
};

FBackChannelOSCMessage& operator << (FBackChannelOSCMessage& OSCMsg, FRecordedKeyCharInput& Input);