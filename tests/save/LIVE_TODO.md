# M9 save / join / death: live checks pending

Prerequisites: run `uv run --no-project python -I tests\save\patch_save.py --apply` (it adds the s2n_save_api_* / s2n_join_api_* exports). Add src\s2m_save.cpp and src\s2m_save_x0.cpp to CMake. Flags: cpp_save=1, cpp_quest=1, cpp_travel=1, cpp_items=1, cpp_io=1. Apply the tests\save\lua_guard.patch.

1. Boot log: "NOLA x0 module save registered (port=1)" and no "modulo parado" line.
2. Row C2 (client saves with the host, b165/b167): the host saves, then the client logs the forced autosave, n7sv_pares.txt gets a pair row, and the client's #N count goes up on the host.
3. Row C7 (host death leads to a host load): the client follows (n7 restore). The client keeps only its own items, quest rewards are kept, and it has no duplicate items.
4. Row C13 (sxjoin join / rejoin): first set sxjoin=1. Release flags_client.txt has no sxjoin line, so it defaults to 0. Then check want -> host pack -> install -> XLoadLastSave -> kit restore -> mark installed.
5. Fakehost 07 / 08 / 10 / 11. With csold=0, the 08 and 11 save-timer paths are gone (DELETE). Mark those steps obsolete. Do not count them as failures.
6. UFunction parameter names (not verified; Lua passes them by position):
   - XCreateItemInInventoryByID(PrototypeID, ObjUID, Count, Durability);
   - ModifyMoney(Delta);
   - the type GetMoney returns (read as float with rf).
   - If a name is wrong, the call fails safely: Call is bad_ and run() is false. The kit/join restore then logs an error and gives nothing.
7. +0x650 guard: with a pawn whose +0x650 is null (teleport race b192), itemCount refuses and logs "+0x650 nulo" up to 5 times. It must not crash.
8. em.noSave: s2m_session_em_nosave is an extern "C" weak symbol (fallback false) until M11 exports it. Check that the client does not save during an emission.
9. Row order: the save100 row must run after quest100 (M7 rbTick) and before travel's catchupIn. Check this in the x0 status row list.
