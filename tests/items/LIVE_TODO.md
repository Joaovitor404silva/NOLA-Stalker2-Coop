# M4 items: live checks pending (cpp_items=1, Lua guard applied)

| # | check | setup | pass when |
|---|-------|-------|-----------|
| 1 | C12 | release gate scenario C12 (container loot both sides) | same items on both sides, no dup, LOCK_DENY on the second opener, TAKEN kept after reload |
| 2 | C10 | release gate scenario C10 | same result as the Lua run in the gate log |
| 3 | fakehost 05 | fakehost run 05 | same NOLA LOCK_/TAKEN/LOOT log lines as the Lua baseline |
| 4 | fakehost 06 | fakehost run 06 | same as above |
| 5 | corpse loot, host side | host kills an NPC, loots the cloot body | body loot opens, items arrive, client copy updated (#CL), no second body |
| 6 | corpse loot, client side | client loots a host-killed NPC | LOOT_GOT / cloot copy correct, the host window closes for a given body |
| 7 | ptake 0.5 s (b189) | take part of a box on one side, open it on the other within 0.5 s | "ptake: aplicado na minha copia" on the other side, no item twice |
| 8 | 215 m teleport then loot (b192) | teleport the client 215 m, loot a corpse right after | no crash; status "items" shows the inv guard count instead of a read of [actor+0x650] |
| 9 | M3 gap | host corpse with cpp_items=1 before M3 provides npcUid | #CL uid 0 on host: confirm client skips clcopy (known gap until M3 npcUid) |
| 10 | catch-up | cpp_items=1, cpp_save=1, cpp_quest=1, client rejoins | "NOLA quest: catch-up aplicado" line matches the Lua text and counts |
