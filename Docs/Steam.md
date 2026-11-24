# Steam switch-on (not active today)

Today: OnlineSubsystem Null. Co-op is LAN list + direct IP / invite code. All online calls go through `SQSession` (Source/SoulQuota/SQSession.*). No project code includes a Steam header, so the project compiles without touching the Steam SDK. The OnlineSubsystemSteam and SteamSockets plugins are already enabled in SoulQuota.uproject; they stay idle while DefaultPlatformService is Null.

Nothing below has been run. It is the standard Unreal setup as I know it for UE 5.x; check each key against the UE 5.8 OnlineSubsystemSteam docs before relying on it.

## What you need first
1. A Steam app id (Steamworks partner account, app fee paid).
2. The trademark check on the name (Docs/IP_Audit.md: the itch.io "SOUL QUOTA" risk) before the store page.

## Config switch (Config/DefaultEngine.ini)
Replace the `[OnlineSubsystem]` block and add the rest:
```ini
[OnlineSubsystem]
DefaultPlatformService=Steam

[OnlineSubsystemSteam]
bEnabled=true
SteamDevAppId=<your app id>
bInitServerOnClient=true

[/Script/Engine.GameEngine]
!NetDriverDefinitions=ClearArray
+NetDriverDefinitions=(DefName="GameNetDriver",DriverClassName="/Script/SteamSockets.SteamSocketsNetDriver",DriverClassNameFallback="/Script/OnlineSubsystemUtils.IpNetDriver")

[/Script/SteamSockets.SteamSocketsNetDriver]
NetConnectionClassName="/Script/SteamSockets.SteamSocketsNetConnection"
```
For local testing before you have your own id, Valve's public test id 480 works with the Steam client running.

## What changes by itself after the switch
- `SQSession::Host` creates a Steam lobby (presence on, invites on, join in progress on) instead of a LAN beacon.
- PLAY CO-OP lists lobbies (heading becomes FRIENDS' LOBBIES). Note: the search is not yet filtered to friends or to this game's build; add a build-id filter before release.
- Steam overlay invites and "Join game" arrive through `SQSession::ListenForInvites` and travel from the main menu.
- Joins go through Steam relay, so no port forwarding and no IP on screen.

## Still to write when the app id exists
- An INVITE button that opens the Steam friends overlay (IOnlineExternalUI::ShowInviteUI), in place of the invite code line.
- Launch-by-invite when the game is not running (command line `+connect_lobby`).
- Player names from Steam persona instead of the computer name.
- Hide the IP / invite code line and the IP box when `SQSession::IsLan()` is false.
- Steam Input (optional): the game already reads XInput pads; Steam Deck works through that. Publish an official Deck layout in Steamworks.
- Achievements, cloud saves (Steam Auto-Cloud on Saved/SaveGames needs no code), rich presence.
- Shipping builds need steam_appid.txt removed and the app id set in the depot.

## Test once switched
- Two Steam accounts on two PCs: host a lobby, see it listed, join, drop the client's network, rejoin within 5 minutes and get the reaper back.
