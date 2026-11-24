// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQSession.h"

#include "Engine/World.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "IPAddress.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "SocketSubsystem.h"

namespace
{
	const TCHAR* const SesAlphabet = TEXT("ABCDEFGHJKLMNPQRSTUVWXYZ23456789"); // 32 symbols, none that read alike
	const uint64 SesScramble = 0x5A17C0DE9B31ULL;                              // 48 bits
	const FName SesHostKey(TEXT("SQHOST"));

	TSharedPtr<FOnlineSessionSearch> SesSearch;
	TArray<SQSession::FFound> SesFound;
	bool bSesSearching = false;
	bool bSesHasConnect = false;
	FString SesPendingConnect;
	TWeakObjectPtr<UWorld> SesWorld;
	FDelegateHandle SesFindHandle;
	FDelegateHandle SesJoinHandle;
	FDelegateHandle SesInviteHandle;

	IOnlineSessionPtr SesInterface(UWorld* World)
	{
		IOnlineSubsystem* Sub = World ? Online::GetSubsystem(World) : nullptr;
		return Sub ? Sub->GetSessionInterface() : IOnlineSessionPtr();
	}

	int32 SesCodeCheck(uint64 V)
	{
		int32 Sum = 0;
		for (int32 i = 0; i < 6; ++i)
		{
			Sum += static_cast<int32>((V >> (8 * i)) & 0xFF);
		}
		return Sum & 3;
	}

	void SesOnFindComplete(bool bOk)
	{
		UWorld* World = SesWorld.Get();
		if (IOnlineSessionPtr Sessions = SesInterface(World))
		{
			Sessions->ClearOnFindSessionsCompleteDelegate_Handle(SesFindHandle);
		}
		bSesSearching = false;
		SesFound.Reset();
		if (bOk && SesSearch.IsValid())
		{
			for (const FOnlineSessionSearchResult& R : SesSearch->SearchResults)
			{
				SQSession::FFound F;
				FString Named;
				F.Host = R.Session.SessionSettings.Get(SesHostKey, Named) && !Named.IsEmpty() ? Named : R.Session.OwningUserName;
				F.PingMs = R.PingInMs;
				SesFound.Add(F);
			}
		}
	}

	void SesOnJoinComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
	{
		UWorld* World = SesWorld.Get();
		IOnlineSessionPtr Sessions = SesInterface(World);
		if (!Sessions.IsValid())
		{
			return;
		}
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(SesJoinHandle);
		FString Resolved;
		if (Result == EOnJoinSessionCompleteResult::Success && Sessions->GetResolvedConnectString(SessionName, Resolved) && !Resolved.IsEmpty())
		{
			SesPendingConnect = Resolved;
			bSesHasConnect = true;
		}
	}

	void SesJoinResult(UWorld* World, const FOnlineSessionSearchResult& Result)
	{
		IOnlineSessionPtr Sessions = SesInterface(World);
		if (!Sessions.IsValid())
		{
			return;
		}
		SesWorld = World;
		if (Sessions->GetNamedSession(NAME_GameSession))
		{
			Sessions->DestroySession(NAME_GameSession);
		}
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(SesJoinHandle);
		SesJoinHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(FOnJoinSessionCompleteDelegate::CreateStatic(&SesOnJoinComplete));
		Sessions->JoinSession(0, NAME_GameSession, Result);
	}

	void SesOnInviteAccepted(const bool bOk, const int32 /*LocalUser*/, FUniqueNetIdPtr /*User*/, const FOnlineSessionSearchResult& Invite)
	{
		if (bOk && Invite.IsValid())
		{
			SesJoinResult(SesWorld.Get(), Invite);
		}
	}
}

FString SQSession::ServiceName()
{
	IOnlineSubsystem* Sub = IOnlineSubsystem::Get();
	return Sub ? Sub->GetSubsystemName().ToString().ToUpper() : FString(TEXT("NONE"));
}

bool SQSession::IsLan()
{
	return ServiceName() != TEXT("STEAM");
}

FString SQSession::LocalAddress()
{
	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!Sockets)
	{
		return FString();
	}
	bool bCanBind = false;
	const TSharedRef<FInternetAddr> Addr = Sockets->GetLocalHostAddr(*GLog, bCanBind);
	return Addr->IsValid() ? Addr->ToString(false) : FString();
}

FString SQSession::InviteCode(const FString& Address, int32 Port)
{
	TArray<FString> Parts;
	Address.ParseIntoArray(Parts, TEXT("."), true);
	if (Parts.Num() != 4)
	{
		return FString();
	}
	uint64 V = 0;
	for (const FString& Part : Parts)
	{
		if (!Part.IsNumeric())
		{
			return FString();
		}
		const int32 Octet = FCString::Atoi(*Part);
		if (Octet < 0 || Octet > 255)
		{
			return FString();
		}
		V = (V << 8) | static_cast<uint64>(Octet);
	}
	V = (V << 16) | static_cast<uint64>(FMath::Clamp(Port, 0, 65535));
	V ^= SesScramble;
	const uint64 Bits = (V << 2) | static_cast<uint64>(SesCodeCheck(V));
	FString Code;
	for (int32 i = 9; i >= 0; --i)
	{
		Code.AppendChar(SesAlphabet[(Bits >> (5 * i)) & 31]);
		if (i == 5)
		{
			Code.AppendChar(TEXT('-'));
		}
	}
	return Code;
}

bool SQSession::DecodeInvite(const FString& Code, FString& OutAddress)
{
	const FString Clean = Code.ToUpper().Replace(TEXT("-"), TEXT("")).Replace(TEXT(" "), TEXT(""));
	if (Clean.Len() != 10)
	{
		return false;
	}
	uint64 Bits = 0;
	for (int32 i = 0; i < 10; ++i)
	{
		int32 Symbol = -1;
		for (int32 s = 0; s < 32; ++s)
		{
			if (SesAlphabet[s] == Clean[i])
			{
				Symbol = s;
				break;
			}
		}
		if (Symbol < 0)
		{
			return false;
		}
		Bits = (Bits << 5) | static_cast<uint64>(Symbol);
	}
	uint64 V = Bits >> 2;
	if (SesCodeCheck(V) != static_cast<int32>(Bits & 3))
	{
		return false;
	}
	V ^= SesScramble;
	const int32 Port = static_cast<int32>(V & 0xFFFF);
	const uint32 Ip = static_cast<uint32>((V >> 16) & 0xFFFFFFFFULL);
	OutAddress = FString::Printf(TEXT("%d.%d.%d.%d:%d"), static_cast<int32>((Ip >> 24) & 0xFF), static_cast<int32>((Ip >> 16) & 0xFF),
		static_cast<int32>((Ip >> 8) & 0xFF), static_cast<int32>(Ip & 0xFF), Port);
	return true;
}

void SQSession::CopyText(const FString& Text)
{
	FPlatformApplicationMisc::ClipboardCopy(*Text);
}

void SQSession::Host(UWorld* World, int32 MaxPlayers, const FString& HostName)
{
	IOnlineSessionPtr Sessions = SesInterface(World);
	if (!Sessions.IsValid() || Sessions->GetNamedSession(NAME_GameSession))
	{
		return;
	}
	const bool bLan = IsLan();
	FOnlineSessionSettings Settings;
	Settings.bIsLANMatch = bLan;
	Settings.NumPublicConnections = FMath::Clamp(MaxPlayers, 1, 8);
	Settings.bShouldAdvertise = true;
	Settings.bAllowJoinInProgress = true;
	Settings.bAllowInvites = true;
	Settings.bUsesPresence = !bLan;
	Settings.bAllowJoinViaPresence = !bLan;
	Settings.bUseLobbiesIfAvailable = !bLan;
	Settings.Set(SesHostKey, HostName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Sessions->CreateSession(0, NAME_GameSession, Settings);
}

void SQSession::Leave(UWorld* World)
{
	IOnlineSessionPtr Sessions = SesInterface(World);
	if (Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession))
	{
		Sessions->DestroySession(NAME_GameSession);
	}
}

void SQSession::Find(UWorld* World)
{
	IOnlineSessionPtr Sessions = SesInterface(World);
	if (!Sessions.IsValid() || bSesSearching)
	{
		return;
	}
	SesWorld = World;
	SesSearch = MakeShared<FOnlineSessionSearch>();
	SesSearch->bIsLanQuery = IsLan();
	SesSearch->MaxSearchResults = 20;
	if (!SesSearch->bIsLanQuery)
	{
		SesSearch->QuerySettings.Set(FName(TEXT("LOBBYSEARCH")), true, EOnlineComparisonOp::Equals);
	}
	Sessions->ClearOnFindSessionsCompleteDelegate_Handle(SesFindHandle);
	SesFindHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(FOnFindSessionsCompleteDelegate::CreateStatic(&SesOnFindComplete));
	bSesSearching = Sessions->FindSessions(0, SesSearch.ToSharedRef());
}

bool SQSession::IsSearching()
{
	return bSesSearching;
}

const TArray<SQSession::FFound>& SQSession::Results()
{
	return SesFound;
}

void SQSession::Join(UWorld* World, int32 Index)
{
	if (SesSearch.IsValid() && SesSearch->SearchResults.IsValidIndex(Index))
	{
		SesJoinResult(World, SesSearch->SearchResults[Index]);
	}
}

bool SQSession::TakeConnectString(FString& Out)
{
	if (!bSesHasConnect)
	{
		return false;
	}
	bSesHasConnect = false;
	Out = SesPendingConnect;
	SesPendingConnect.Reset();
	return true;
}

void SQSession::ListenForInvites(UWorld* World)
{
	IOnlineSessionPtr Sessions = SesInterface(World);
	if (!Sessions.IsValid())
	{
		return;
	}
	SesWorld = World;
	Sessions->ClearOnSessionUserInviteAcceptedDelegate_Handle(SesInviteHandle);
	SesInviteHandle = Sessions->AddOnSessionUserInviteAcceptedDelegate_Handle(FOnSessionUserInviteAcceptedDelegate::CreateStatic(&SesOnInviteAccepted));
}

void SQSession::SubmitScore(const UObject* WorldContext, FName Board, int32 Score)
{
	// Deliberately a stub: the Null service has no boards, and the Steam leaderboard interface is wired in the Steam pass
	// (Docs/Steam.md, "Leaderboards"). Every caller already keeps its own local best in USQSaveGame.
	if (!WorldContext || Board.IsNone() || Score <= 0) return;
	UE_LOG(LogTemp, Verbose, TEXT("SQSession::SubmitScore %s = %d (%s: local only)"), *Board.ToString(), Score, *ServiceName());
}
