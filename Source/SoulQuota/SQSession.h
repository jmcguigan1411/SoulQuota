// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * Platform pass (Nov 2026): the ONE place the game talks to the online service. Today that service is OnlineSubsystem Null
 * (LAN beacon + direct IP); switching DefaultPlatformService to Steam in DefaultEngine.ini makes the same calls create a
 * Steam lobby, search lobbies and accept overlay invites (Docs/Steam.md). Nothing here names the Steam SDK.
 * Menu and HUD only ever call this namespace; the join itself stays a plain ClientTravel to the resolved address.
 */
namespace SQSession
{
	struct FFound
	{
		FString Host;
		int32 PingMs = 0;
	};

	/** "NULL" today, "STEAM" once the config switch is made. */
	FString ServiceName();
	bool IsLan();

	/** This machine's address on the local network ("192.168.1.20"); empty if the socket layer can't say. */
	FString LocalAddress();
	/** Short friendly form of an IPv4 address and port ("ABCDE-FGHJK"). A way to read an address out loud, not a secret. */
	FString InviteCode(const FString& Address, int32 Port);
	/** True and "a.b.c.d:port" when the text is a valid invite code (case, spaces and dashes ignored). */
	bool DecodeInvite(const FString& Code, FString& OutAddress);
	void CopyText(const FString& Text);

	/** Host side: advertise this listen server (LAN beacon on Null, lobby on Steam). Safe to call again; it keeps the session. */
	void Host(UWorld* World, int32 MaxPlayers, const FString& HostName);
	/** Drop any session this machine made or joined (called whenever the main menu opens). */
	void Leave(UWorld* World);

	/** Start a search; Results() fills when it lands. */
	void Find(UWorld* World);
	bool IsSearching();
	const TArray<FFound>& Results();
	/** Join a search result; TakeConnectString returns the address to travel to once the service has resolved it. */
	void Join(UWorld* World, int32 Index);
	bool TakeConnectString(FString& Out);
	/** Steam overlay "Join game" / accepted invites end up in TakeConnectString too. Harmless on Null. */
	void ListenForInvites(UWorld* World);
}
