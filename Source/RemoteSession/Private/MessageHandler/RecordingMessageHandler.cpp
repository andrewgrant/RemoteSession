// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "RecordingMessageHandler.h"
#include "RemoteSession.h"
#include "BackChannel/Protocol/OSC/BackChannelOSC.h"
#include "BufferArchive.h"
#include "MemoryReader.h"
#include "Engine/GameEngine.h"
#include "Engine/GameViewportClient.h"
#include "Async.h"

PRAGMA_DISABLE_OPTIMIZATION

// helper to serialize out const params
template <typename S, typename T>
S& SerializeOut(S& Ar, const T& Value)
{
	T Tmp = Value;
	Ar << Tmp;
	return Ar;
}

#define BIND_PLAYBACK_HANDLER(Address, Func) \
	DispatchTable.FindOrAdd(Address).BindLambda([this](FArchive& InAr)\
	{\
		Func(InAr);\
	});\



template <typename P1, typename P2>
struct TwoParamMsg
{
	P1	Param1;
	P2	Param2;

	TwoParamMsg(FArchive& Ar)
	{
		Ar << Param1;
		Ar << Param2;
	}

	TwoParamMsg(P1 InParam1, P2 InParam2)
	{
		Param1 = InParam1;
		Param2 = InParam2;
	}

	TArray<uint8> AsData()
	{
		FBufferArchive MemAr;
		MemAr << Param1 << Param2;
		return MemAr;
	}
};

template <typename P1, typename P2, typename P3>
struct ThreeParamMsg
{
	P1	Param1;
	P2	Param2;
	P3	Param3;

	ThreeParamMsg(FArchive& Ar)
	{
		Ar << Param1;
		Ar << Param2;
		Ar << Param3;
	}

	ThreeParamMsg(P1 InParam1, P2 InParam2, P3 InParam3)
	{
		Param1 = InParam1;
		Param2 = InParam2;
		Param3 = InParam3;
	}

	TArray<uint8> AsData()
	{
		FBufferArchive MemAr;
		MemAr << Param1 << Param2 << Param3;
		return MemAr;
	}
};




FRecordingMessageHandler::FRecordingMessageHandler(const TSharedPtr<FGenericApplicationMessageHandler>& InTargetHandler)
	: FProxyMessageHandler(InTargetHandler)
{
	OutputWriter = nullptr;
	ConsumeInput = false;

	BIND_PLAYBACK_HANDLER(TEXT("OnKeyChar"), PlayOnKeyChar);
	BIND_PLAYBACK_HANDLER(TEXT("OnKeyUp"), PlayOnKeyUp);
	BIND_PLAYBACK_HANDLER(TEXT("OnKeyDown"), PlayOnKeyDown);

	BIND_PLAYBACK_HANDLER(TEXT("OnTouchStarted"), PlayOnTouchStarted);
	BIND_PLAYBACK_HANDLER(TEXT("OnTouchMoved"), PlayOnTouchMoved);
	BIND_PLAYBACK_HANDLER(TEXT("OnTouchEnded"), PlayOnTouchEnded);
}

#undef BIND_PLAYBACK_HANDLER

void FRecordingMessageHandler::SetRecordingHandler(IRecordingMessageHandlerWriter* InOutputWriter)
{
	OutputWriter = InOutputWriter;
}

void FRecordingMessageHandler::RecordMessage(const TCHAR* MsgName, const TArray<uint8>& Data)
{
	if (IsRecording())
	{
		OutputWriter->RecordMessage(MsgName, Data);
	}
}

void FRecordingMessageHandler::SetConsumeInput(bool bConsume)
{
	ConsumeInput = bConsume;
}

void FRecordingMessageHandler::SetPlaybackWindow(TWeakPtr<SWindow> InWindow, TWeakPtr<FSceneViewport> InViewport)
{
	PlaybackWindow = InWindow;
	PlaybackViewport = InViewport;
}

FVector2D FRecordingMessageHandler::ConvertToNormalizedScreenLocation(const FVector2D& Location)
{
	const FVector2D ViewportSize = FVector2D(GEngine->GameViewport->Viewport->GetSizeXY());

	return FVector2D(Location.X / ViewportSize.X, Location.Y / ViewportSize.Y);
}

FVector2D FRecordingMessageHandler::ConvertFromNormalizedScreenLocation(const FVector2D& ScreenLocation)
{
	FVector2D OutVector = ScreenLocation;

	TSharedPtr<SWindow> GameWindow = PlaybackWindow.Pin();
	if (GameWindow.IsValid())
	{
		FVector2D WindowOrigin = GameWindow->GetPositionInScreen();
		FVector2D WindowSize = GameWindow->GetSizeInScreen();
		OutVector = WindowOrigin + (ScreenLocation * WindowSize);
	}

	return OutVector;
}


bool FRecordingMessageHandler::PlayMessage(const TCHAR* Message, const TArray<uint8>& Data)
{
	FRecordedMessageDispatch* Dispatch = DispatchTable.Find(Message);

	if (Dispatch != nullptr)
	{
		// todo - can we steal this data in a more elegant way? :)
		TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe> DataCopy = MakeShareable(new TArray<uint8>(MoveTemp(*(TArray<uint8>*)&Data)));

		AsyncTask(ENamedThreads::GameThread, [Dispatch, DataCopy] {
			FMemoryReader Ar(*DataCopy);
			Dispatch->ExecuteIfBound(Ar);
		});
		
	}
	else
	{
		UE_LOG(LogRemoteSession, Warning, TEXT("No Playback Handler registered for message %s"), Message);
	}

	return true;
}

bool FRecordingMessageHandler::OnKeyChar(const TCHAR Character, const bool IsRepeat)
{
	if (IsRecording())
	{
		TwoParamMsg<TCHAR, bool> Msg(Character, IsRepeat);
		RecordMessage(TEXT("OnKeyChar"), Msg.AsData());
	}

	if (ConsumeInput)
	{
		return true;
	}	

	return FProxyMessageHandler::OnKeyChar(Character, IsRepeat);
}

void FRecordingMessageHandler::PlayOnKeyChar(FArchive& Ar)
{
	TwoParamMsg<TCHAR, bool> Msg(Ar);
	OnKeyChar(Msg.Param1, Msg.Param2);
}

bool FRecordingMessageHandler::OnKeyDown(const int32 KeyCode, const uint32 CharacterCode, const bool IsRepeat)
{
	if (IsRecording())
	{
		ThreeParamMsg<int32, TCHAR, bool> Msg(KeyCode, CharacterCode, IsRepeat);
		RecordMessage(TEXT("OnKeyDown"), Msg.AsData());
	}

	if (ConsumeInput)
	{
		return true;
	}

	return FProxyMessageHandler::OnKeyDown(KeyCode, CharacterCode, IsRepeat);
}

void FRecordingMessageHandler::PlayOnKeyDown(FArchive& Ar)
{
	ThreeParamMsg<int32, TCHAR, bool> Msg(Ar);
	OnKeyDown(Msg.Param1, Msg.Param2, Msg.Param3);
}


bool FRecordingMessageHandler::OnKeyUp(const int32 KeyCode, const uint32 CharacterCode, const bool IsRepeat)
{
	if (IsRecording())
	{
		ThreeParamMsg<int32, TCHAR, bool> Msg(KeyCode, CharacterCode, IsRepeat);
		RecordMessage(TEXT("OnKeyUp"), Msg.AsData());
	}

	if (ConsumeInput)
	{
		return true;
	}

	return FProxyMessageHandler::OnKeyUp(KeyCode, CharacterCode, IsRepeat);
}

void FRecordingMessageHandler::PlayOnKeyUp(FArchive& Ar)
{
	ThreeParamMsg<int32, TCHAR, bool> Msg(Ar);
	OnKeyUp(Msg.Param1, Msg.Param2, Msg.Param3);
}

bool FRecordingMessageHandler::OnTouchStarted(const TSharedPtr< FGenericWindow >& Window, const FVector2D& Location, int32 TouchIndex, int32 ControllerId)
{
	if (IsRecording())
	{
		ThreeParamMsg<FVector2D, int32, int32> Msg(ConvertToNormalizedScreenLocation(Location), TouchIndex, ControllerId);
		RecordMessage(TEXT("OnTouchStarted"), Msg.AsData());
	}

	if (ConsumeInput)
	{
		return true;
	}

	

	return FProxyMessageHandler::OnTouchStarted(Window, Location, TouchIndex, ControllerId);
}

void FRecordingMessageHandler::PlayOnTouchStarted(FArchive& Ar)
{
	ThreeParamMsg<FVector2D, int32, int32 > Msg(Ar);
	FVector2D ScreenLocation = ConvertFromNormalizedScreenLocation(Msg.Param1);

	TSharedPtr<FGenericWindow> Window;

	if (PlaybackWindow.IsValid())
	{
		Window = PlaybackWindow.Pin()->GetNativeWindow();
	}

	OnTouchStarted(Window, ScreenLocation, Msg.Param2, Msg.Param3);
}

bool FRecordingMessageHandler::OnTouchMoved(const FVector2D& Location, int32 TouchIndex, int32 ControllerId)
{
	if (IsRecording())
	{
		ThreeParamMsg<FVector2D, int32, int32> Msg(ConvertToNormalizedScreenLocation(Location), TouchIndex, ControllerId);
		OutputWriter->RecordMessage(TEXT("OnTouchMoved"), Msg.AsData());
	}

	if (ConsumeInput)
	{
		return true;
	}

	return FProxyMessageHandler::OnTouchMoved(Location, TouchIndex, ControllerId);
}

void FRecordingMessageHandler::PlayOnTouchMoved(FArchive& Ar)
{
	ThreeParamMsg<FVector2D, int32, int32 > Msg(Ar);
	FVector2D ScreenLocation = ConvertFromNormalizedScreenLocation(Msg.Param1);
	OnTouchMoved(ScreenLocation, Msg.Param2, Msg.Param3);
}

bool FRecordingMessageHandler::OnTouchEnded(const FVector2D& Location, int32 TouchIndex, int32 ControllerId)
{
	if (IsRecording())
	{
		ThreeParamMsg<FVector2D, int32, int32> Msg(ConvertToNormalizedScreenLocation(Location), TouchIndex, ControllerId);
		OutputWriter->RecordMessage(TEXT("OnTouchEnded"), Msg.AsData());
	}

	if (ConsumeInput)
	{
		return true;
	}

	return FProxyMessageHandler::OnTouchEnded(Location, TouchIndex, ControllerId);
}

void FRecordingMessageHandler::PlayOnTouchEnded(FArchive& Ar)
{
	ThreeParamMsg<FVector2D, int32, int32 > Msg(Ar);
	FVector2D ScreenLocation = ConvertFromNormalizedScreenLocation(Msg.Param1);
	OnTouchEnded(ScreenLocation, Msg.Param2, Msg.Param3);
}

PRAGMA_ENABLE_OPTIMIZATION