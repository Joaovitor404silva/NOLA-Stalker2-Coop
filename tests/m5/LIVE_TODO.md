# M5 world objects: live checks pending (nothing here was run in the game)

Offline state: 26 Lua-vs-C++ parity scenarios identical, 6 C++-only fast-mode scenarios pass, TSV round trip 1278/1278 rows, 39 flag defaults checked (run tests\m5\run_all.bat). The ProdWorld adapter (src\s2m_world_x0.cpp) only compiles; every UE call in it is unverified.

## Adapter calls whose names are guesses (verify with one log line each)
- NiagaraFunctionLibrary.SpawnSystemAtLocation param names: WorldContextObject, SystemTemplate, Location, Rotation, Scale, bAutoDestroy, bAutoActivate, PoolingMethod, bPreCullCheck.
- AkGameplayStatics.PostEventAtLocation param names: AkEvent, Location, Orientation, WorldContextObject.
- SetStaticMesh param NewMesh, SetSimulatePhysics bSimulate, SetActorHiddenInGame bNewHidden, SetActorEnableCollision bNewActorEnableCollision, SetEnabled bInEnabled, SetBroken bInBroken, PauseSkeletalComponent bPause, SetInteractionActive bActive. The Lua calls pass them positionally; X0 Call is by name.
- The BndEvt__... delegate functions are called with zero args from the s2u_actions table (Lua does the same); confirm X0 Call::run with no params invokes them.
- DoorState write: the adapter tries int32, uint8 and float on the anim instance; the real type is not verified.
- K2_GetComponentRotation is read as (pitch, yaw, roll) through Call::rr; order not verified.

## Behaviour to check on a real run
- NullMesh: when the StaticMeshComponent.StaticMesh read fails or is null the adapter reports NullMesh (meshsafe, b77tp). Confirm no crash on a streamed-out prop.
- asset(): find_path only. Lua falls back to LoadAsset when the mesh is not loaded; X0 has no such call, so a destroyed prop whose replacement mesh is not in memory skips the mesh step ("malha ok=false").
- gate: key is addr|fullname of BP_Stalker2Character_C, open = iface().body_gate or true. Confirm doors/dobj stay quiet while the remote body is out of the world.
- is_a in the adapter ignores the Lua depth limit; check an InteractableObject subclass deeper than the Lua limit is accepted on purpose.
- Table-load logs name the .tsv files (Lua names .lua); ientab step failures print "!erro" (Lua prints the Lua error text).

## Fast detection (the point of this port)
- The 1.2 to 1.8 s host-side delay (OFFLINE_REPORT section 213) is NOT explained: section 203 measured 2.7 to 3.0 s of 3.1 s as the 3 scans x 1 s poll, section 213 never split the A->B time. The C++ removes the poll cadence (about 0.15 s), the 3-miss debounce (1 miss plus the DG.alive check) and requests an immediate out flush. Measure on a live break: the report line now reads "sumiu N varreduras T s, ultimo visto ha X s" and gives the detection time; compare it with the time the other side applies it.
- Cost: the poll validates tracked Refs only (no actor list). Measure ms per frame row and ms per 1 s full scan on the fight bench with dobjfast=1 versus dobjfast=0.
- A bottle on a shelf destroyed by a thrown object, a plank pushed (physics nudge), a crate shot: each must show one #Z entry on the other PC within about 0.3 s. Run one of each.
- Re-streamed copies (walk away 150 m and back after a break) must stay hidden on the receiver and must not be re-reported by the breaker.
- dobjfmiss=2 and dobjfast=0 are the rollback switches; confirm dobjfast=0 behaves like the Lua (3 scans of 1 s).

## X0 gaps this module depends on (see PORT_LOG, M5 block)
- flush_out: an immediate out.json flush after a report. Without it the report waits for the normal out cadence; env_.flush_out() is an empty default, so detection works but the second hop is not shortened.
- Flag defaults for dobjfast (1), dobjfsec (0.15), dobjfmiss (1) in flags_x0.tsv, and HC_KEYS membership if hostcfg should carry them.
- cpp_world is the switch name in s2x_sched.cpp (the brief said cpp_m5).
