# L1 live all-C++ - resume state (2026-10-17 17:30, waiting for games after A1's probes)

## Installed now
- Both sides (Mods and ModsClient): S2CoopNative main.dll md5 8DE41D0F8B443AC153B3C33257E14E61 (backups main.dll.bak_914f76a3).
- mods.txt: S2CoopAlife 0, S2CoopNative 1 (backups mods.txt.bak_l1).
- C:\s2coop_inst\A|B\flags.txt: cpp_net=1, devcmd=1 (backups flags.txt.bak_l1). Client dmgin is back to 1.
- The pair was handed to A1 at 17:20.
- cppbuild\Output is back on S2X_RELEASE=OFF.

## Done
- See OFFLINE_REPORT §238. Everything below passed:
  - P2, join, damage x4, fightbench 13/13;
  - time sync (after the fix), PMC Deactivate, s2q, wshot.
- Fixes: bridge nonce (s2x_bridge.cpp), @time owner (s2m_session_x0.cpp).
- Cover evidence: tests\L1_cover_evidence.md.
- s2rag: tooling_s2dev_console_exec_and_aim_calls.md and engine_crash_fun_1401acfe8_alifepopulation_stack.md are new; anim_npc_cover_crouch_pose_inputs.md is updated.

## Left: S2X_RELEASE=ON one-game smoke
1. Release DLL (md5 7D50091E7C560D3E530EBE0151C74C3C): C:\Users\USER\AppData\Local\Temp\claude\C--Users-USER\377b20f2-cf5e-4ecd-9724-1dbc2995ff02\scratchpad\S2CoopNative_rel.dll
2. Copy it over ModWork\s2coop\Mods\S2CoopNative\dlls\main.dll. Launch the host only: l1_launch.ps1 -Pair 1 -NoClient.
3. Check the log: x0 ready role=host; role defaults loaded (flags_role_host); no devcmd poll; no s2x_data reads; stable in world.
4. Put the dev DLL back afterwards: copy D:\modding\cpp\S2CoopPort\cppbuild\Output\S2CoopNative\Game__Shipping__Win64\S2CoopNative.dll (dev, same source as 8DE41D0F).
