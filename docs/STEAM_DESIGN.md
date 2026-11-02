# Steam multiplayer route (worker S1, 2026-10-17)

Goal: two players connect through Steam. Nobody types an IP, needs a VPN or forwards a port. The host invites a Steam friend, or the friend uses Join Game in the Steam friends list. The IP/UDP route (R1, `netpeer`) stays as the other option and as the fallback.

## 1. Recon (evidence)

| Fact | Evidence |
|---|---|
| Steam API dll | `<G>\Engine\Binaries\ThirdParty\Steamworks\Steamv157\Win64\steam_api64.dll`: FileVersion 06.91.21.57 (SDK 1.57), md5 500475b2…, 1059 exports. No steam_api64 in `Stalker2\Binaries\Win64`. AppID 1643320 is in `Stalker2\Binaries\Win64\steam_appid.txt`. |
| How the exe uses it | Stalker2-Win64-Shipping.exe (md5 a4fc3575…, PE 5.5.4.0) DELAY-imports steam_api64.dll. Imports: GetHSteamUser, Init, RestartAppIfNecessary, RunCallbacks, Register/UnregisterCallback, Register/UnregisterCallResult, Shutdown, SteamGameServer_*, SteamInternal_*. No ManualDispatch import. The exe uses no SteamNetworkingMessages or Sockets, only the old "SteamNetworking006" string. |
| Who initialises and pumps | SteamAPI_Init is called in FUN_146d43708 (SteamShared FSteamClientInstanceHandler, which logs "SteamAPI initialized"). The only RunCallbacks call is in FUN_146d467bc (FOnlineAsyncTaskManagerSteam::OnlineTick): `call qword ptr [0x149f0d170]` (delay IAT slot in .data, RW), guarded by bSteamworksClientInitialized. |
| Steam is up in practice | The crash contexts in `%LOCALAPPDATA%\Stalker2\Saved\Crashes` list steam_api64.dll loaded and a thread "OnlineAsyncTaskThreadSteam DefaultInstance". So OSS Steam is initialised and pumps the legacy callbacks on its own thread. |
| Callbacks the game registers | 333 GameLobbyJoinRequested (size 16), 337 (264), 504 LobbyEnter (24), 505, 506 (32), 331, 304 and others (b_006d7.c; sizes from vtable slot 2 at 0x148eed3a0..). |
| steam_api64 dispatch internals (disassembly) | RegisterCallback inserts into a std::multimap without a lock. Object layout: vptr@0, flags@8 (bit 0 registered, bit 1 game server), id@0xc. Callbacks are dispatched through vtable[1] = Run(void*). Call results: size from vtable[2], then vtable[0] = Run(p, io, hCall). The dll reads the id at +0xc for call results too, so the id must be set. |
| Unexpected LobbyEnter in UE OSS | UE OSS Steam only logs "Entered lobby %s, but not found in sessions list". It does not LeaveLobby. It also parses `+connect_lobby` itself (CheckPendingSessionInvite). |
| Needed flat exports | All present in the 1.57 dll. SteamUser is v021 (no v023 flat export), Friends v017, Matchmaking v009, Utils v010, NetworkingMessages v002, NetworkingUtils v004. |
| Other stores | No GOG or Game Pass copy on this PC. The route is Steam-only: a copy without the dll falls back to ip after `netsteamwait`. |
| Bandwidth to carry | C:\s2coop_inst relay.log: host sends median 62 KB/s, p95 72.5 KB/s, largest datagram 3921 B; client about 3 KB/s. |
| Unknown (live only) | What the game does with the OSS invite-accepted delegate or with `+connect_lobby`. Whether the Steam launcher passes `+connect_lobby` to the shipping exe. Whether SDR carries 62-72 KB/s without loss. Exact LobbyCreated_t size (16 is from the SDK; not seen in the binary). Thread safety of Matchmaking calls made from the OSS thread (that is the thread the game itself uses for them). |

## 2. Design

The simplest design that leaves s2r_net.cpp (R1) untouched:

```
game relay (s2r::Net) 127.0.0.1:listen  <-UDP loopback->  shim 127.0.0.1:ephemeral  <-ISteamNetworkingMessages (SDR)->  peer shim  <-loopback->  peer relay
```

- **Loopback shim.** `steam_route` sets the relay's peer to `127.0.0.1:<shim port>`, so Net binds 127.0.0.1 only (no firewall rule, no Windows prompt). The shim forwards each datagram's exact bytes as one Steam message: flags 33 (UnreliableNoNagle | AutoRestartBrokenSession), channel 0. It adds no framing. Datagrams that come back are written to the relay's port. relay_v2 stays end to end, including the save transfer.
- **Pump hook.** The shim never calls Matchmaking, Friends, User, Utils or callback (un)registration from its own thread. Those go through a trampoline written into the exe's own `SteamAPI_RunCallbacks` import slot (found generically by walking delay imports, then normal imports). It runs our pre-pump on the game's Steam thread, then the real RunCallbacks. This avoids the unlocked multimap race and keeps every call on the thread the game already uses. The slot is written only after the game has bound it (`*slot == GetProcAddress(RunCallbacks)`), so the delay-load helper can never overwrite it. The shim thread only calls the NetworkingMessages and Identity functions.
- **Callbacks without stealing.** These are legacy callback objects (`CbBase`, same vtable as CCallbackBase). The dll multicasts each callback to every registered object, so the game's own handlers still run.
- **Host.** When Steam is ready (GetHSteamUser != 0, interfaces present, BLoggedOn), the host creates a friends-only lobby, max 2, with lobby data `nola=2`. The peer is the other lobby member, polled every 0.5 s. The Home key opens ActivateGameOverlayInviteDialog. A friends-only lobby shows "Join Game" in the friends list and raises GameLobbyJoinRequested on the friend (documented GameLobbyJoinRequested_t behaviour). No rich presence.
- **Client.** It joins on 333, or on `+connect_lobby <id>` / `-connect_lobby <id>` in GetCommandLineW. On LobbyEnter it checks the response, `nola=2` and that another user owns the lobby; otherwise it leaves. It goes back to waiting when the host leaves or ownership moves to itself.
- **Sessions.** Sending implicitly accepts. A SessionRequest (1251) is accepted only for the lobby peer. Others are logged ("not accepted: not the lobby peer") and expire after 30 s. Messages from anyone but the peer are dropped and counted (`foreign`). Loopback datagrams from any port other than the relay's are dropped (`local_foreign`).
- **Fallback.** Flags `net=steam|ip` (default steam) and `netsteamwait=60`. If steam_api64 is never loaded, the slot is never bound or pumped, or the user is not logged on within the wait, the link fails once. It logs "<reason>: using the IP route instead" and restarts the relay on `netpeer`. A change to `net` or `netsteamwait` retries. `net=ip` never touches Steam.
- **Stop.** The shim thread is joined. The pump thread is asked to unregister everything and leave the lobby (up to 2 s), then the slot is restored and in-flight trampolines are drained. If the game stopped pumping, the callback objects are left registered but inert (never freed), so a later dispatch is safe.
- **Dev PCs.** Two instances on one PC have the same Steam account, so the lobby cannot pair them: dev flags must set `net=ip`.

## 3. Files

- `src\s2r_steam.h`, `src\s2r_steam.cpp`: the Link (shim, pump hook, lobby state machine, sessions). It has no x0 or UObject dependency.
- `src\s2r_steam_x0.cpp`: x0 glue. `steam_route(Config&)`, `steam_stop`, `steam_invite`, the Home key, the `steam` command (status | invite | join <id> | leave), the status line, and on_flag for net / netsteamwait.
- `tests\steam\patch_s2r_x0.py`: the four-line hook into s2r_x0.cpp (not applied; integration step).
- `tests\steam\fake_steam_api.cpp`, `fake_backend.h`, `fake_world.h`: a fake steam_api64.dll that reproduces the dll's raw dispatch (vtable[1] for callbacks; vtable[2] then vtable[0] for call results; flags/id checks), session-request semantics, and a lobby world.
- `tests\steam\steam_test.cpp`, `glue_steam_test.cpp`, `build_test.bat`, `out\steam_result.txt`, `out\glue_result.txt`.
- `tests\steam\LIVE_TODO.md`: the live checks.

## 4. Status line and logs

`steam <state> self lobby peer(name) port sent recv foreign nopeer send_err last_err sess_ok sess_rej pumps`. The states are waiting_for_steam, creating_lobby, hosting, waiting_for_invite, joining, joined and failed. All log lines start with `S2NET steam: `, and the route choice is logged as `S2NET route: steam|ip (...)`.
