# R1 relay in the DLL: live checks still needed

Offline results: tests\net\parity.py and tests\net\udp_test.py (both PASS, see OFFLINE_REPORT "Port R1 relay"). Everything below needs the real game or two PCs.

1. Two PCs over Radmin VPN, both cpp_net=1 netfiles=1 netpeer=<other VPN IP>. The ports come from the role: host listens on 7779 and sends to 7778, client the reverse. Check:
   - each log has `relay v2 listen=...` once;
   - `relay stats 10s` has recv>0 and bad_peer=0 on both sides;
   - the avatar moves;
   - fight bench rows A1-A6 pass.
2. Mixed pair, as in the field: one PC runs the DLL (cpp_net=1), the other runs relay_v2.py (cpp_net=0). Run it both ways. It must play the same as row 1.
3. Firewall, first run on a PC with no "S2Coop relay" rule:
   - does Windows show a "Windows Security Alert" for Stalker2-Win64-Shipping.exe when the DLL binds 0.0.0.0?
   - after "Allow" (Private), does the peer get datagrams?
   - repeat with only the port rule from install.ps1 (`netsh ... localport=7779`, no program). Does that rule alone suppress the prompt and let traffic in? Not verified offline.
   - repeat with the program-scoped rule proposed for the installer.
   - the LAN case: the "Rede 2" network is Public on this PC. Check a Private-only rule there.
4. Port conflict: start relay_v2.py on the same port while cpp_net=1. Expect `RELAY ERROR: bind UDP ... (WSA 10048)` every 5 s. Stop relay.py: expect `relay: UDP ... bound after N failed tries` within 5 s, then sync resumes. Also toggle cpp_net 0<->1 in flags.txt with the game running; expect one S2NET line per change, and the port freed when off.
5. PEER SILENT: quit the peer game or its relay. Expect `NOLA PEER SILENT` after peersilent s on the other side (driven by in.json "t", unchanged path). Restart the peer: expect PEER up, and NPC copies cleared on the sid change.
6. sxjoin join with the DLL relay on both sides, then mixed (DLL host + Python client, and the reverse):
   - sx_req.txt / sx_out.pkg / sx_in.pkg flow;
   - `relay sx` lines;
   - a save of the real size (offline used 300 KB with 20% loss);
   - resume after killing the client game halfway (sx_in.map / sx_in.part kept).
7. fakehost --net against the DLL: fakehost.py sends HDR+APP only and reads HDRs. Profiles internet-good / internet / internet-bad against a game with cpp_net=1.
8. Latency and cadence in game. Offline file mode, out.json written to in.json visible on localhost: median 27-35 ms, p95 42-55 ms, same as relay_v2.py. Measure in game with the shots/ping rows of the fight bench. Check the game frame time does not change with cpp_net=1: the net thread does zlib + JSON for 21 KB headers at 10 Hz, about 1 ms per frame on this PC.
9. Mailbox mode (netfiles=0) needs the X0 bridge seam first (PORT_LOG "R1 net"). Then repeat rows 1, 5 and 6 with netfiles=0.
10. Hot reload / uninstall (s2dev plugin reload): x0_stop must run from ~S2CoopNative. Expect no crash on reload, and the port free for the reloaded DLL.
