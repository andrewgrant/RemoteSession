// Copyright 2017 Andrew Grant
// This file is part of RemoteViewer and is freely licensed for commercial and 
// non-commercial use under an MIT license
// See https://github.com/andrewgrant/RemoteViewer for more info

#pragma once

#include "RemoteSessionRole.h"


class FRemoteSessionClient : public FRemoteSessionRole
{
public:

	FRemoteSessionClient(const TCHAR* InHostAddress);
	~FRemoteSessionClient();

	virtual void Tick(float DeltaTime) override;

protected:

	void StartConnection();
	void CheckConnection();

	FString				HostAddress;
	
	bool				IsConnecting;

	double				ConnectionAttemptTimer;
	double				LastConnectionAttemptTime;
};