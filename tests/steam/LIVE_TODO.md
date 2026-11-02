# Steam route: live checks (S1)

Nothing below was run. Offline proof: out\steam_result.txt (56 PASS) and out\glue_result.txt (fake steam_api64).

## One PC, one Steam account (only after X0 integrates; set `net=steam`, role host)
1. UE4SS.log shows these, in this order:
   - `S2NET steam: started`
   - `steam_api64.dll is loaded`
   - `hooked the game's SteamAPI_RunCallbacks`
   - `ready: user <name> (<id>), app 1643320, role host, overlay on`
   - `friends-only lobby <id> ready`
   This proves the hook reaches the real slot 0x149f0d170, and that the game's OSS pumps it.
2. If the log has `waiting for the game's first SteamAPI_RunCallbacks` but no `hooked` line, the slot is not called through. Example: the OSS dispatch never ran, or the dev exe differs. Record the two pointers it prints.
3. `steam` (s2q console) status line: state `hosting`, pumps rising, last_err 0.
4. In game, press Home. The Steam overlay invite dialog opens and lists friends. With the overlay off, the log says so.
5. The friend list shows the host as "Playing S.T.A.L.K.E.R. 2" with Join Game. This needs the second account to see it.
6. Firewall: no Windows prompt from our side, because the relay binds 127.0.0.1. Check whether steamclient's SDR socket triggers a prompt (not verified).
7. With `net=ip`, no `S2NET steam` line appears at all.
8. With Steam offline (Steam started in offline mode), the log shows `Steam user not ready within 60 s ... using the IP route instead`, then `S2NET route: ip`.

## Two PCs, two Steam accounts that are friends (the real test)
1. Host: role host, `net=steam`. Client: role client, `net=steam`. Neither has a netpeer, VPN or open port.
2. Invite path: the host presses Home and invites the friend, and the friend accepts in the overlay. The client log shows `joining lobby`, then `joined the lobby of <host>`, then `session with the lobby peer ... accepted`. The host log shows `<friend> joined the lobby: co-op link up`.
3. Join Game path: the friend uses Join Game in the friends list while the game is running. The same lines appear.
4. Cold start path: the friend's game is closed, and the friend clicks Join Game or accepts the invite. Steam starts the game. Check:
   - whether the shipping exe gets `+connect_lobby <id>`: the log should say `launched by Steam to join lobby ... (+connect_lobby)`;
   - what the launcher (Stalker2.exe) does with it. Unknown.
5. Game reaction (unknown, important): when the invite is accepted, does UE OSS / the game try to travel, show a dialog or kick to the menu? The OSS only logs "Entered lobby ..., but not found in sessions list". Watch for a map change or a popup.
6. Relay over SDR. Play 10 min in a busy area, then run `steam` and `net` status on both sides. Check:
   - send_err 0 (if not 0, note last_err, e.g. 25 LimitExceeded);
   - the recv counts follow the sent counts;
   - PEER SILENT never appears;
   - NPC copies are as smooth as on the IP route.
   The host sends 62-72 KB/s (relay.log); whether SDR keeps that rate without loss or delay is not verified.
7. The host quits. The client log says `the host left the lobby; waiting for a new invite`. When the host comes back and invites again, the client rejoins.
8. A stranger: a third account that is not the lobby peer sends to the host. The log shows `session request from ... not accepted`.
9. Fallback: on the client set `net=ip`, with a netpeer. The relay switches to UDP without a game restart (on_flag).
10. Save transfer (sx) over Steam: the client joins fresh (sxjoin=1) and the save arrives with a matching crc.
