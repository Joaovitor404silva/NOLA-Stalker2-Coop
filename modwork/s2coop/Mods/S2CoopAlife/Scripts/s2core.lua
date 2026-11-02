-- s2core.lua: the RELOADABLE part of S2CoopAlife. Reload without relaunch:
--     bash /d/modding/luacmd.sh 'return S2.reload("why")'
-- Rules: everything is a field of M or a file-local; state that must survive a reload lives in ST (= S2.state);
-- loops only through S2.loop / S2.delay or the two tickers (tick100 / tick1000); UObjects kept in ST are
-- re-checked with IsValid inside pcall before use. main.lua calls M.shotResolve / M.applyMirrors / M.ownsAvatar /
-- M.snapExtra when they exist (otherwise its built-in code runs).
local M = {}
local S2L = {}   -- b76loc: functions moved off the main chunk's 200-local limit (patch_locals.py); one slot for 34
M.VERSION = "core-25-b85-b83-b81kz-b80-b78v-b20-b21-b22-b23-b24-b25-b26-b27-b28-b29-b31-b32-b33-b34-b35-b36-b38-b39-b40-b42-b43-b44-b45-b46-b47-b49-b50-b51-b52-b53"
-- new flags (merged into FL on load when missing): corepuppet=1 the module drives the npcpuppet body;
-- avgain = smoothing gain (1/s), avlead = velocity extrapolation in ms, pupweapon=1 show the remote weapon on the puppet
M.FLAGS = {corepuppet = 1, avgain = 30, avlead = 200, pupweapon = 1, coretarget = 1,   -- coretarget=1: hidden AI-target NPC at the remote position
    pwrescan = 0, pwscan = 3,   -- b184g19 (sender): 1 = re-read the own weapon's attachments every pwscan s (1 s while none was seen), not only
                     -- when the weapon component changes. Measured 19:27: the host's cache held SK_pm with 0 attachments (read in the draw
                     -- frame, before slide/frame/mag were attached) while 11 were attached; the puppet showed no pistol.
    pupnogun = 0,    -- b184g19 (receiver): 1 = the puppet NPC's OWN weapon (prototype item generator) is taken out of its hands each s
    pwsnap = 0,      -- b184g19 (receiver): 1 = re-place the remote gun on the hand socket every pupTick (10 Hz). An attached component does
                     -- not follow CharacterMesh0's animated bones here (measured: aim socket z 1347 -> idle 1295, gun stayed 1347 =
                     -- gun floating at the hip, 'broken arm'); a relative-location write makes it re-read the socket.
    ffc = 1, ffcttl = 1, ffcwin = 0.05,   -- b143perf (both): FindFirstOf/FindAllOf shared inside one loop body; one-per-game classes kept ffcttl s while valid
    x1c = 1,         -- b143perf (both): x1part cuts/splits the remote x1 once per snapshot (was: every call); 0 = old path
    acache = 1, acneg = 5,   -- b149hitch (both): StaticFindObject/LoadAsset of assets/classes/default objects kept per path (IsValid + same class); a /Game miss kept acneg s
    mshys = 500, msstay = 4,   -- b149hitch (host, mslot=1): a mirror-set NPC keeps its place msstay s (and its class), then only a newcomer mshys cm nearer pushes it out
    meshsafe = 1,    -- b77tp: null-safe StaticMesh name reads (UE4SS GetFName on a null object = native AV 0x18, pcall cannot catch)
    tgtdiag = 0,     -- b72: 1 = read-only target diagnostics: the last 10 samples (drift since last tick, vertical speed, hp) are logged when the
                     -- hidden target dies, plus a "target 10s" line while shots are heard or the target loses hp
    tgtnofall = 0,   -- b72: 1 = the hidden target's movement is stopped (StopMovementImmediately) after each per-tick teleport, so no fall
                     -- speed builds up between teleports (hypothesis for the one-tick deaths 5000 -> 0 of the friend session; unmeasured)
    tgtsafe = 0,     -- b72: 1 = no NEW target spawn request while a shot was heard in the last tgtcalm s (crash 0x658 came 1.5 s after
                     -- a target re-created into a fight); after tgtwaitmax s of waiting the request goes out anyway
    tgtcalm = 8, tgtwaitmax = 60,
    tgtgod = 0, tgtgodwait = 2,   -- b112 (review #1): 1 = the hidden target is Unkillable (XSetEntityUnkillableByUID, the nodeath call) once per adopted
                     -- target, re-asserted every 5 s; a lethal hit is held tgtgodwait s (damage still sent, HP refilled) instead of MORREU + respawn
    pupanimbp = 0,   -- b80crash: 1 = the puppet NPC is never put in single-node animation mode (PlayAnimation); crash 0x658 (exe +530066):
                     -- the game casts a Character's Mesh.AnimScriptInstance to AnimInstanceHuman with no null check (2/2 dumps: AnimSingleNodeInstance)
    kzsafe = 0, killz = -20000, kztop = 1000000, kzxy = 5000000,   -- b81kz: 1 = positions below killz cm (or above kztop, beyond kzxy, NaN) are never
                     -- used to place our actors (puppet, target, far body, items, leash) nor sent as NPCs; stored bodies and S2.io.pawn are
                     -- checked with IsValid before any property read (crash 22:12:49: P.pawn.Mesh on a freed puppet, UE4SS IsA on garbage)
    pupanimfix = 0,  -- b83anim: 1 = every s, puppet (needs pupanimbp=1) + target without an AnimInstanceHuman instance -> SetAnimationMode(0) / SetAnimInstanceClass; 2 = also drop a body still broken after 5 s
    pupabpstill = 0, -- b80crash: with pupanimbp=1, also DisableMovement on the puppet so its own AI cannot walk it between placements
    shotattr = 0,    -- b72: 1 = (host) a client shot that hits an NPC is dealt with AttackerUID = the client's hidden target, so the NPC
                     -- turns on the client's body (measured once: 18 hits in 30 s after one attributed hit); falls back to the old call
    repsync = 0,     -- b104rep (N4): HOST: 1 = a client bullet that hits an NPC is dealt in the HOST player's name (AttackerUID = host uid),
                     -- so the game's own relation rules change the host's standing (the master). Client: story changes come by the Rel paks
    repecho = 1, repechocd = 5,   -- b104rep: with shotattr=1 too: damage of a follow-up hit in the client target's name (the victim also
                     -- fights the client; 0 = no echo) / s per NPC between two echoes
    repfoe = 1,      -- b145aggro: HOST: 1 = a client hit on an ALREADY hostile victim (mutant, or a human focused on the host / the client's
                     -- target / puppet) is dealt in the client's hidden target's name, so it fights the client's body; calm humans, the
                     -- killing hit and unreadable hp stay in the host's name (N4, kill credit); 2 = log only; 0 = off
    relfoe = 0,      -- b177g17: HOST: 1 = repfoe's "hostile" = the game's relation victim -> host is Enemy (S2CoopNative s2n_rel)
    relmir = 0, relmirs = 2, relmirr = 6000, relmirtol = 50,   -- b177g17: HOST: 1 = every relmirs s the client's hidden target takes
                     -- the host's relation to every live NPC within relmirr cm (s2n_rel_copy, when they differ by > relmirtol)
    cfollow = 0, cfollowm = 40, cfollowstep = 3,
    ftfollow = 0,      -- b100ft (N6): 1 = a guide fast travel by either player takes the other one along (#FT); set on both sides
    ftjump = 30000, ftrad = 10000, ftsettle = 3, ftkeep = 30, ftmaxage = 120, ftnear = 5000,   -- b100ft: own jump cm / cm to a guide arrival point /
                       -- s after the jump before sending / s the #FT is sent / s max age of a received #FT / cm = 'already there'
    ftleash = 30, ftwait = 45, ftoff = 200, ftfly = 40, ftgrace = 15, ftqmax = 300, ftpawn = 0,   -- b100ft: client follow leash m / s per try /
                       -- host offset cm / host max s in fly mode / client leash park s after a dialog / s a held follow waits / 1 = a jump across a pawn change counts
    tpfloor = 0, tpfback = 100, tpfwait = 40,   -- b108tpfloor: 1 = every pull to the other player lands on ITS floor (S2L.tpfSpot): tpfback cm behind it
                       -- at its z (0 = its exact spot) / s max a far pull waits for its floor to load (then its spot in fly mode)
    tpfit = 0, tpfring = 150,   -- b113tpfit: 1 = the pull places with K2_TeleportTo (false = the capsule does not fit, nothing moves) trying spots around
                       -- the other player (behind, left, right, front at tpfback cm, then a ring of 8 at tpfring cm, then its spot); none fits -> stay
    idrop = 0, idropr = 400, idropm = 8000,   -- batch 73: 1 = a ground item I drop appears on the other PC (#DR; spawn by SID); idropr = cm around me where the dropped item is looked for; idropm = cm: the copy is made when the receiver is this close
    anomsync = 0, anomr = 6000, anomcd = 3,   -- batch 73: 1 = an anomaly I walk into also fires on the other PC (#AN; ForceActivateOnce); anomr = cm listing radius; anomcd = s minimum per anomaly
    ssbox = 0,   -- batch 73: client with sstash=1 and csave=1: the per-pawn box fill adds only ledger - ss_box.txt (what my box already holds) instead of the whole ledger
    iactlog = 1,   -- b70f probe: log PC interaction state changes (in progress, interacted actor class@pos, length type, move ignored, cinematic)
    keyfix = 0,      -- b71kf: 1 = a remote 10 cm key also matches a local key one unit off on any axis (half-to-even / sub-cm edge).
    keytol = 1,      -- b71kf: tolerance in key units (10 cm each) per axis for keyfix
    iamir = 0,       -- b71ia: interaction mirror. 1 = an interaction with a player animation on one PC is replayed on the other PC:
                     -- teleport to the starter's stand point, target the same object, press Interact (stock anim + handlers). ia71.lua
    iaunk = 1,       -- b71ia: also mirror classes missing from s2u_interact.lua when the game says the interaction has an animation (len 2)
    iainj = 1,       -- b71ia: 1 = continuous IA_Interact injection until the interaction starts; 2 = one InjectInputForAction call
    iaface = 1,      -- b71ia: turn the camera to the object before pressing (SetControlRotation; unmeasured)
    iaq = 10, iapress = 1.5, iamax = 20, iaecho = 10, iahold = 6, iafind = 150, iareach = 200, ialift = 20, iaset = 0.3,
    qskipcatch = 1,   -- b70e: s2c_skip.txt applies only to catch-up/replay; a LIVE host event runs on the client (E01_MQ01_SequenceStart is reused after the intro)
    qnodlg = 1,   -- b70d: quest dialog nodes (SID contains "Dialog") are not replayed on the client: its NPC copy would start the same talk and nothing ends it
    dmir = 0, dmirint = 0, dmirwait = 20, dmirend = 15, dmirkeep = 10, dmirstart = 2, dmirnpcm = 500, dmirtxt = 1, dmirblk = 0, dmirclick = 0,   -- b70dlg dialog mirror: 1 = the client sees the dialog, the host picks the answers (dm70.lua, s2u_dlg.lua); dmirint = also open player-started talks on the client (XForceActorInteract)
    cfall = 1, cfallsec = 3, cfallm = 30, cfollowlift = 100,   -- b70b: each leash step lifts the client cfollowlift cm, next step after it lands   -- b70 client fall rescue: falling cfallsec s and cfallm m below the host -> placed beside the host   -- client leash: the client steps toward the host, cfollowstep m per 100 ms tick, pulling only when farther than cfollowm m
    vendone = 1,     -- b124vendone: 1 = a vendor (vendmir) is only the single nearest live NPC pawn to each s2u_vendor.lua row within vendr (both sides); 0 = any NPC within vendr
    vendmir = 0, vendr = 4000, vendtrade = 0, vendtr = 1500,   -- b78fix D3: 1 = vendors (s2u_vendor.lua, within vendr cm) are never mirrored: the host leaves them out of its scan, the client keeps its own copy (2 = the host also leaves out every hub/trade-place NPC); vendtrade (client) 1 = a talk with a vendor (row within vendtr cm) opens only the trade screen (2 = dry run)
    orphvia = 1, bodyoffdestroy = 0,   -- b84alife: orphrm=3 = hide + remove the orphan from the A-Life by its GUID (orphvia 1 = AI2:XDestroyModel(uid), 2 = DC:XKillNpcByUID + AA:XDespawnDeadNPCsAtRadius if no other corpse that close); a removed GUID seen again -> hide only. bodyoffdestroy 1 = target+puppet spawned with bDestroyOnOffline=true, 2 = target only
    orphsweep = 0, orphrm = 2, orphsec = 3, orphsave = 0, orphmax = 4,   -- b82orph: 1 = bodies of ours (RemoteTarget/RemotePlayer) that are not the adopted target/puppet are hidden, lose collision and interaction; orphrm=2 also K2_DestroyActor; 2 = dry run (log only); every orphsec s
    tgtnoint = 0,    -- b78fix D4: 1 = the hidden target and the puppet take no interaction (NPCComponent:SetInteractionActive(false), re-set each second)
    iprobe = 0, ipr = 2500,   -- batch 59 probe (dev): 1 = log the interactables near the local player (Lever/DoorView/Door/InteractableObject/InteractableMonitorSection) and each state change; ipr = radius cm
    abpguard = 0, abpsec = 2, abpmax = 4, abpmax3 = 6,   -- b85crash: crash 0x588 (exe+f19a42): an NPC AI move-to reads its null HumanAnimController (Agent+0xAC8,
                     -- built only at BeginPlay when the mesh has an AnimInstanceHuman). 1 = log every live human without it (UE4SS custom Int64
                     -- property), 2 = also hide+K2_DestroyActor our puppet/target in that state (fresh body = fresh BeginPlay; abpmax per role per
                     -- 600 s), 3 = host: also any other human in that state (A-Life re-creates it; abpmax3 per 600 s). abpsec = scan period s
    pupai = 1, pupais = 90, pupaimax = 2,   -- b134pupai: the puppet is a pure puppet by DATA (avatar pak v4: faction S2CoopPuppet, threat
                     -- prototype with every stimulus 0, NoVision). 1 = log every threat on/off of the adopted puppet and recycle a body still in
                     -- threat after pupais s (older body / pak v3; hide + XDestroyModel / K2_DestroyActor), at most pupaimax per 600 s; 2 = log only
    pupnoreinit = 0, pupnoai = 0, emparkpup = 0, emgrace = 15,   -- b86pupctl: pupnoreinit 1 = pupanimbp no longer re-inits a puppet that already
                     -- runs AnimInstanceHuman in mode 0 (that SetAnimationMode(0,true) killed the instance + Agent+0xAC8 controller 0.2-0.8 s later).
                     -- pupnoai 1 = once per adopted puppet: BrainComponent:StopLogic + controller tick off + path following off; 2 = also UnPossess.
                     -- emparkpup 1 = during the emission and emgrace s after it no puppet body (XDestroyModel(uid) / K2_DestroyActor, no adopt/spawn)
    pupmove = 0, pupmvrm = 1, pupmvmode = 5, pupmvgain = 3, pupmvgainz = 6, pupmvsnap = 300, pupmvdead = 8, pupmvvmax = 1500,   -- b89pupmove: puppet
                     -- locomotion (needs pupanimbp=1). 1 = engine velocity: the CharacterMovement moves the puppet every frame (MovementMode
                     -- pupmvmode: 5 Flying, 2 NavWalking) from a Velocity steered at 10 Hz toward the smoothed remote position (gain pupmvgain/s,
                     -- z pupmvgainz/s, teleport beyond pupmvsnap cm, zero inside pupmvdead cm, max pupmvvmax cm/s); 2 = idle/walk/run clips
                     -- through the AnimBP slot pupmvslot + the old teleport; 3 = both. pupmvrm 1 = RootMotionMode 1 (IgnoreRootMotion) on the
                     -- puppet's AnimBP while pupmove>0 (its idle root motion was the 2nd writer). The sender adds #MV:<crouch>,<aim> to x1.
    pupmvslot = "DefaultSlot", pupmvtag = 0, pupmvcr = 0, pupmvsprint = 520, pupmvlog = 0,   -- b89pupmove: pupmvtag 1 = XNpcSetStateTag gait
                     -- (walk/run/sprint by remote speed, sprint above pupmvsprint cm/s), crouch and aim bits from #MV; pupmvcr 1 = Character:Crouch /
                     -- UnCrouch from #MV; pupmvlog 1 = read-only 10 s diag line of the puppet (also with pupmove=0: the 2nd-writer baseline)
    pupanim = 0, pafall = 1, pamaxr = 2.5, patrn = 45, pahold = 2,   -- b119pupanim: 1 = pupmove clips from the state table S2L.PA (idle/aim/
                     -- crouch/turn/walk/run/sprint/strafe/back/fall by speed, direction vs the sender's yaw and #MV crouch,aim,falling); pafall 0 =
                     -- no fall clip; pamaxr = max clip rate; patrn = deg/s of sender yaw = turn in place; pahold = samples a new state must win
    pupfeet = 0,       -- b123pupcrouch: 1 = the sender adds x1 #FH:<capsule half-height>; the receiver puts the puppet's capsule at the remote FEET
                       -- (z - FH) + its own half-height (crouch no longer sinks it ~32 cm); no #FH from the other side = the old centre z. Set on A AND B
    pabh = 0,          -- b123pupcrouch (needs pupanim=1): 1 = bare-hands rows: squat as crouch idle, bh walk_/run_ l|r 90/135/180 loops for
                       -- strafe/back (run_ at or above WALK_MAX); 0 = the b119 rows (rifle-arm strafe/back/crouch for bare hands too)
    takenown = 1,  -- batch 50: TAKEN_DENY only for containers the OTHER player emptied (own takes: taken_mine.txt on each PC)
    taken = 0,   -- core-19: 1 = containers a player opened are 'taken' for good (#T client->host, #X list host->client, taken.txt on the host)
    loot = 1, lootreq = 0,   -- core-16: loot a host corpse from the client (key G near the corpse, or lootreq=<new number>)
    farbody = 1, pupfar = 6000,   -- core-21: beyond pupfar cm the remote player is a plain mesh actor (the NPC puppet is pooled by the A-Life past ~85 m)
    wshot = 1,         -- b144combat (HOST): 1 = NPC shots (OnShotSoundStart, owner = an NPC within wsr cm of the client) ride x1 "#WS"; 0 = off
    wsfx = 1,          -- b144combat (CLIENT): 1 = a relayed NPC shot / voice plays at its mirror (fire sound + TP muzzle flash), 2 = + NPC tracer; 0 = off
    wvoice = 1,        -- b144combat (both): 1 = NPC voice lines (host: AkVoiceoverSpeakerComponent.AkAudioEvent polled 0.2 s) relayed and played; 0 = off
    wsmax = 30,        -- b144combat (CLIENT): at most this many relayed shot effects per second in total (N16 cap)
    wsnmax = 4,        -- b144combat (CLIENT): at most this many shots played per 0.2 s entry of one NPC
    wsr = 6000,        -- b144combat (HOST): only NPCs within this many cm of the client player are relayed
    wvmaxn = 12,       -- b144combat (HOST): at most this many NPCs polled for voice lines
    wvface = 1,        -- b146voice (HOST): 1 = a new lip-sync face montage (AS_FAC_*) of an NPC = a voice line -> its VO_ event relayed; 0 = off
    wvspk = 1,         -- b146voice (HOST): 1 = also the AkAudioEvent of the VoiceoverSpeechComponents' speakers (FindAllOf each 1 s); 0 = off
    wvsw = 1,          -- b146voice (CLIENT): 1 = a voice plays on a one-shot AkComponent with the NPC's Voice-<X> switch; 0 = b144 PostEventAtLocation
    clammo = 1,        -- b140lootfix (both, needs cloot=1): 1 = a loot body never holds MORE than the host list (created guns come with a full
                       -- magazine that is counted: rounds filled after the guns; a gun whose magazine alone is more rounds than the list has
                       -- is held back, it stays in the list); takes are deltas. 2 = every gun made (surplus <= 1 magazine per gun). 0 = b138
    b164off = 0, cllease = 3, clholdmax = 120,   -- b164: 0 = corpse leases + one body + quest items to both ON (1 = b162); lease s / max hold s
    cloot = 1, clr = 2500, clsend = 4000, clmax = 6, clgone = 4, clrc = 5, clvis = 0, clcopy = 1, clsid = "S2Coop_RemoteTarget", cllog = 1,   -- b138loot
                     -- (both): a host-killed corpse within clr cm of the client gets a loot body there (clsid spawned, killed, filled with the
                     -- host's list); takes go back to the host (one shared list, host wins); clsend = host list radius (cm); clmax bodies;
                     -- clgone s; clrc = host recount s; clvis 0 = body mesh hidden under the mirror; clcopy 1 = the client's own live copy killed
    lootshare = 0, lsnog = 0, lsr = 3000, lsper = 1,   -- b90loot (N1): one shared item list per DeadBody body (the first opener's list;
                     -- the other PC's copy is made equal); lsnog = the host's G list skips those bodies; lsr = cm; lsper = s
    lscont = 0, lsctake = 0.3, lscwait = 0.6, lscr = 150,   -- b93cont (N1 stage 2, needs lootshare=1): one shared list per item container
                     -- of s2u_contk.tsv (first reader's roll; take-all read lsctake s after the open, read lscwait s later; row within lscr cm)
    qitem = 0, qirm = 1, qigive = 1, qisec = 0.5, qiper = 80, qire = 20, qir = 400,   -- b87qitem (N2): quest items stay with the host; qirm = client
                     -- removes with S2QI_<sid> (QuestItemGuard client pak); qigive = host receives relays; qir = cm host-keep radius
    qreward = 1, ptake = 1, rwtest = 0, cinelog = 0,   -- core-22: quest rewards of the host go to the client too; partial take for containers listed in s2u_cont.tsv
    ground = 1,     -- core-20: ground items picked up by one player are hidden on the other PC (#G)
    locks = 1,      -- roadmap (e): container locks (one player at a time per container)
    locktest = 0,   -- 1 = test: pretend the other player already holds every container (DENY path)
    pupretry = 1,   -- batch 43: 1 = the body spawn never gives up while the other player is up (rounds of 4 requests pupback s apart, later 2x);
                    --           the counters reset on a new session of the other side; a body waiting to be adopted as the puppet is not hidden
    pupadopt = 1,   -- batch 45: 1 = the puppet adopts any live body of its prototype near the local player, at any time; no new request while one exists
    pupwin = 60,    -- batch 45: seconds a puppet request waits for its body (r37: bodies came later than 20 s)
    pupadoptr = 5000, -- batch 45: cm around the local player where a body of the puppet's prototype is adopted
    pupback = 60,   -- batch 43: seconds after the first round of 4 failed requests (later rounds wait 2x)
    bodysafe = 1,   -- batch 44: 1 = bodies are spawned, adopted, moved and hidden only while the local pawn has been the same live object for
                    --           bodywait s; when it goes away or changes (a load) every stored body reference is dropped untouched
    tpdrop = 5000,  -- batch 52: cm; an own move larger than this between two ticks drops the body references like a load (0 = off)
    bodywait = 5,   -- batch 44: seconds the local pawn must be stable (also after a body of ours turned invalid) before bodies are touched
    spawngap = 10,  -- batch 44: at most one XSpawnObjBySID per body every spawngap s
    lockfirst = 0,  -- batch 42: 1 = the first to open keeps the container, the host too is denied (0 = b25 rule: the host takes it from the client)
    alifekeepquest = 1,  -- core-17: with alifeoff=1 also keep NPCs that have a placeholder GUID (placed, named NPCs: traders, quest givers; found with QuestBindingFunctionLibrary:GetPlaceholderGuidByUObject)
    alifekeephubs = 1,   -- core-15: with alifeoff=1 the NPCs inside a hub (traders, quest givers; FastTravelLocationPrototypes coords) stay visible and usable
    alifeoff = 0,   -- 1 = no local A-Life (roadmap a, for the CLIENT): A-Life features off + every NPC/mutant that is not one of our bodies is despawned
    aimask = 203647, -- batch 53: AI features the CLIENT switches off with alifeoff=1 (NPC brains only; 4194303 = All, the old behaviour that also killed hunger/radiation/hit processing)
    doors = 1, doorsec = 1, doorr = 2000, doorhold = 6,   -- batch 53: host reports moved doors within doorr cm (#O, held doorhold s); client ForceOpen/CloseDoor
    doorre = 0, nrdoor = 0, qdivfix = 0,   -- b187c17 (C17): doorre 1 = a client SAVE LOAD in the same session gets the host's door states again
                       -- (host: the client's #N load counter changed -> b181 re-queue; client: new pawn -> door ledger reset, so the resend applies);
                       -- nrdoor 1 = #O door events ride the b128 netrel ack/resend layer (a link gap > doorhold s, < peersilent s, lost them);
                       -- qdivfix 1 = the qdiv line 'o host avancou N passos e nenhum chegou aqui' also asks the host's catch-up again (qgap path)
    dobjfx = 0,     -- batch 60: how the OTHER player's break is applied: 0 = K2_DestroyActor (vanish, b58), 1 = the prop's own StartDestructActions() (stock VFX+sound+destroy, UNMEASURED), 2 = replay the plank VFX+sound (NS_Destruct_WoodenPlanks + SFX_..._Destruct_BigBox) then K2_DestroyActor
    dobjgen = 0, dobjexp = 0, dobjfxmax = 4, dobjstat = 1,   -- b74world: 1 = GENERIC destructible mirror (every DestructibleObject class, s2u_destruct.lua table: vanish OR phase/mesh step, home key per actor, movable props never reported); 2 = same detection, log only (dry). dobjexp=1: a mirrored explosive really explodes (0 = VFX+sound+mesh only, no second damage). dobjfxmax = replays with VFX per second. dobjstat=1: one scan-cost line per 60 s
    dobj = 0, dobjr = 2500, dobjsec = 1, dobjhold = 6, dobjmiss = 3,   -- batch 58: BOTH sides report broken destructible props within dobjr cm (#Z, held dobjhold s); a miss must repeat dobjmiss scans AND be unfindable to count; the permanent broken set re-enforces removal when World Partition streams the actor back; reset on level change
    choice = 0, n3kill = 2, n3id = 1, n3r = 20000, n3max = 60, n3cache = 10, n3trig = 2, n3use = 1, n3ur = 2500, n3usesec = 2,   -- b91choice (N3):
                     -- 1 = story choices outside dialogs stay with the host (s2u_choice.lua): n3kill = highest NPC tier the client cannot
                     -- damage (0 off, 1 clear choices, 2 also path-cancelling); n3id 1 GetActorByGuid / 2 GetPlaceholderGuidByUObject;
                     -- n3trig = highest trigger tier the host refuses for the client (0 off); n3use = branch usables: client prompts off +
                     -- host refuses the client's #P change; n3r/n3ur = cm, n3max = guid lookups per new pawn, n3cache/n3usesec = s
    lead = 0, ldtrig = 3, ldkill = 3, ldhold = 1, ldclose = 4,   -- b92lead (N14 first starter leads): 1 = ONE story lock (#LK); the side that
                     -- starts a non-vendor dialog / branch trigger leads, the other follows (mirrored dialog, answers disabled; other
                     -- story dialogs held: hidden, closed after ldclose s); tie = host; overrides the client limits of choice and qitem
                     -- (gates = lock checks, quest items copied to both). ldtrig/ldkill = s a trigger/client story shot holds it, ldhold = s after a dialog
    ldblk = 0, ldblkr = 3000, mqpull = 0, mqwin = 15,   -- b109dlgblock (N14 "Block, no mirror", N15): ldblk 1 (needs lead=1) = while the OTHER side
                     -- holds the lock, living non-vendor human NPCs within ldblkr cm take no interaction here (no story talk), given back when it
                     -- ends; vendor talks stay free. mqpull 1 = the client is pulled/frozen/joined to the host's dialog/cinematic (cinedlg,
                     -- cjoin, cine follow, cj68 scenes) ONLY for main quest ones (S2L.isMain; host: a main node within mqwin s before/during)
    qrelay = 1, qnsrc = 0,   -- batch 56 (ported b62): #QN node relay, origin c = the client's own trigger -> host XEndQuestNodeBySID once
    ctrig = 1, trigmargin = 150, trigre = 30, trignear = 40000,   -- batch 55 (ported b62): client position detection of quest trigger volumes (s2u_triggers.tsv)
    trigself = 1,      -- b98trig (lead=1, host): a client #QN for a trigger the host itself entered within trigre s is not fired again, and a host
                       -- entry into a trigger the client's #QN already fired takes no lock; 0 = old behaviour
    qpick = 0, qplead = 5, qpwait = 8, qpmax = 120, qpkeep = 600, qplkeep = 900,   -- b99qpick (needs lead=1 + QuestPick pak on both):
                       -- 1 = a dialog pin fired while the CLIENT leads is sent (#QP) and the host runs what it launches; the client skips a host
                       -- report of a node its own graph already ran. qplead = s after the client's dialog lock, qpwait = s the host waits for
                       -- its own graph (follower), qpmax = longest wait while the host has a dialog open, qpkeep/qplkeep = s own runs count
    cinedlg = 0,     -- batch 69d: 1 = a host dialog (W_DialogueView_C on screen) counts as a cinematic for #V: client frozen + protected, joined, host body NOT hidden
    cjoin = 0, cjoinm = 1500, cjoff = 150, cjcool = 20,   -- batch 69: cutscene join: teleport beside the other player (farther than cjoinm cm) before a scene node runs here
    ientab = 0,      -- batch 69: 1 = apply the other side's change with the object's own handlers from s2u_actions.lua (b68 decompile table, cj68.lua actApply)
    ienprompt = 0,   -- batch 68g: 1 = send/apply the actor's interaction-component active bits with its state (a used one-shot lever loses its prompt on both PCs)
    ienanim = 0,     -- batch 67c: 1 = also set the anim BP DoorState (lever handle, hatch lid) to the mirrored state (on=0, off=1)
    ienlamp = 0,     -- batch 67: 1 = a lamp switched on the other side runs LightOn()/LightOff() here (the light itself, not only bEnabled)
    ienhatch = 0,    -- batch 66: 1 = a hatch valve opened on the other side runs this side's own click handler (bound event 4) instead of Toggle
    ientoggle = 1,   -- batch 64: apply the other side's lever/hatch/lamp state with InteractableObject:Toggle() (0 = SetEnabled only)
    ien = 0, ienr = 2500, iensec = 1, ienhold = 6,       -- batch 59: host reports InteractableObject bEnabled changes within ienr cm (#P, held ienhold s); client SetEnabled
    kit = 1, kitdelay = 12, kitwait = 90,   -- batch 53: client: post-tutorial kit (s2u_kit.tsv) once per campaign; kitdelay s after a new pawn; campaign unknown after kitwait s = '-'
    mslot = 1, mslotsec = 2, mslr = 7000,   -- b131combat F1 (host, scan=1): the client's NPC mirror slots (maxnpcs) go to live NPCs in threat
                       -- within mslr cm of the CLIENT, then alive, then corpses there, then the rest, nearest to the client first (was: nearest
                       -- to the host, corpses and calm NPCs included); rebuilt every mslotsec s
    shotlag = 300, shotpad = 10,   -- b131combat F2: a client shot is tested against the NPC capsule (+ shotpad cm, never below 45/95) now and up to
                       -- shotlag ms back along its velocity (the client aimed at a late mirror); head/legs only for humans; shotlag 0 = old test
    tgtpark = 1,       -- b131combat F3: the parked hidden target (remote far / out of the world) drops its HP reference: no phantom hit on return
    headsafe = 1, headmax = 32,   -- b133head (HOST): the NPC look sent to the client's mirrors is complete (up to headmax skeletal parts)
                       -- and head first (/_fac/ meshes, then headgear S2L.HL_GEAR); 0 = main.lua's first 12 parts in component order
    hidnpc = 1, hidnpcd = 0,    hidnpcvia = 1, hidnpchp = 4999,   -- b135fixes 1 (CLIENT, alifeoff=1): a HIDDEN live local NPC in threat, focused
                       -- on me or within hidnpcd cm is killed (hidnpcvia 1 = DC:XKillNpcByUID, 2 = AI2:XDestroyModel); never our bodies (max HP
                       -- >= hidnpchp), npcsync pairs, kept/vendor NPCs, s2u_choice story NPCs
    qdead = 1,         -- b135fixes 2: no quest-item count is sent (qimirror / lead / qitem) while the local player is dead or downed
    qfall = 1, qfallr = 400, qfalldz = 500, qfallwin = 8, qfallup = 5,   -- b135fixes 3: after a drop (inventory open / qdrop hold) a NEW item
                       -- container within qfallr cm that is > qfalldz cm below the player = physics off + put back on the ground
    lootr = 700,    -- batch 53: cm; a corpse of the host's list within this distance can be looted with G (was 400)
     dmgscale = 1,   -- batch 53: multiplier on the HP the hidden target lost before it is sent to the client
    dmgmin = 1,     -- batch 53: was 5 (every real hit of the friend session was 2.7-3.4 HP and got dropped). Target HP drops below this are forwarded only within 2 s after a real hit (>= dmgmin); bleeding/after-effect ticks are dropped (0 = forward all)
    -- damage to the LOCAL player from the other side's #D events (offline batch 3, block DAMAGE IN below):
    dmgin = 0,      -- 1 = apply them (role client)
    dmgtest = 0,    -- 1 = also apply them when role = host (loopback: the host receives its own events)
    dmgmode = 1,    -- 1 = Obj:ReceiveDamageStruct (the player's armor counts)   2 = Obj:ReceiveDamage (exact HP points, no armor)
    dmgap10 = 10,   -- ArmorPiercing x10 used with dmgmode=1 (10 = 1.0)
    dmgmax = 200,   -- cap per event (HP points)
    dmgcap = 300,   -- cap per second (HP points, refilled continuously); what does not fit is dropped and counted
    dmgspawn = 3,   -- seconds without damage after the local pawn appears (load, respawn)
    dmgfloor = 0,   -- 0 = the player can die. N > 0 = HP never goes below N: the player is 'downed' instead
    dmgdown = 10,   -- seconds a downed player takes no damage (the host stops hitting the body meanwhile)
    -- session (batch 6): the other side counts as silent after this many seconds without a new snapshot
    peersilent = 10,   -- batch 53: was 5; Radmin/internet gaps of 5-10 s released locks and hid the bodies
    hostcfg = 0, hcblk = 128,   -- b105ver (N11): 1 = #HC: both sides send the md5 of their s2core.lua (a different one is logged, never swapped);
                       -- the host sends its shared flags (S2L.HC_KEYS) and the client takes them; set on BOTH. hcblk = md5 blocks per tick
    -- life guard (batch 6): downed instead of dead, for BOTH players, whatever hurt them
    lifeguard = 0,     -- 1 = on
    downhp = 15,       -- HP at or below this = downed (HP is held here)
    downsecs = 20,     -- gets up alone after this many seconds
    reviveradius = 300,-- cm: the other (alive) player this close ...
    revivesecs = 3,    -- ... for this many seconds = revived early
    revivehp = 30,     -- HP after getting up
    downfreeze = 1,    -- 1 = a downed player cannot walk
    downmsg = 1,       -- b147downmsg: 1 = a downed player sees 'DOWNED - you get up in N s, or when the other player comes within X m' (needs lifeguard=1); 0 = log only
    -- quest sync (batch 7): host = the quest observer pak + console handler "s2q" -> #J events; client applies them to its journal
    qsync = 0,         -- 1 = on (both sides)
    qtest = 0,         -- 1 = the host also applies its own events (loopback test only)
    qnode = 0,         -- client: 1 = replay through the client pak's isolated copies (XStartQuestNodeBySID <node>_S2C; Int variables too)
                       --         0 = journal debug calls + XSetGlobalBool (no client pak needed; Int/Add and N events skipped)
    qcatchup = 60,     -- host: seconds the full quest log is offered after a client session starts
    itake = 0,         -- batch 9: 1 = item take-back on (#U events; needs the S2U paks on BOTH PCs)
    itest = 0,         -- 1 = also apply our own #U events (loopback test only)
    inodes = 4,        -- quest nodes started per 100 ms tick at most
    icontr = 300,      -- cm: a container matches a placeholder of s2u_cont.tsv within this distance
    ionly1 = 1,        -- batch 12: 1 = the container paks hold count-1 nodes only (no _A node): an "A" request is refused
    isame = 0,         -- 1 = the same node may be started several times in one tick (0 = once per tick, as before)
    icmax = 1000,      -- most count-1 starts queued for one container request
    cine = 0,          -- batch 12: 1 = #V host cinematic / teleport signal on
    cinetest = 0,      -- 1 = the client also polls and sends #V (loopback / solo test)
    cinejump = 3000,   -- cm: a move of more than this between two ticks is a teleport; also the distance that makes the client follow
    cinefollow = 25,   -- m: leash distance used while following (main.lua teleports the client when it is farther than this)
    cinehold = 6,      -- s: how long the short leash stays
    leashown = 0,      -- b115leash: 1 = one leash owner: cine/join/nodeath/ft only REQUEST a leash (ST.leash.req), S2L.lrApply writes FL.leashm once a tick
    leashpullwin = 20, -- b115leash: s the first pull owns the leash; another feature's pull meanwhile is refused (it retries by itself)
    cinefreeze = 1,    -- batch 12b: 1 = the client is frozen and protected while the host is in a cinematic (needs cine=1)
    cinemax = 180,     -- s: one freeze never lasts longer than this
    nopause = 0, npwait = 3, npmax = 10,   -- b88nopause (user N8): 1 = a paused world (Esc menu / P) is un-paused at once on this PC (tick100),
                       -- not while loading, npwait s after a new pawn, dead, in our nodeath death or a game cinematic; npmax catches in 5 s -> 30 s off
    cineinput = 0,     -- 1 = also pawn:DisableInput(controller) while frozen (NOT measured in game)
    cinegod = 1,       -- 1 = also XSetEntityUnkillableByUID on the local player while frozen (NOT measured in game)
    cpersist = 0,      -- batch 13: 1 = client persistence (client_state.txt in the bridge folder)
    cptest = 0,        -- 1 = persistence also runs on the host (solo probe)
    cpsecs = 30,       -- seconds between saves
    cpdelay = 8,       -- seconds after a new local pawn before the restore
    cpper = 5,         -- item stacks created per 100 ms tick during a restore
    nodeath = 0,       -- batch 13: 1 = the client never really dies: Unkillable + respawn next to the host
    ndtest = 0,        -- 1 = nodeath also runs on the host (solo probe)
    ndhp = 0.5,        -- HP at or below this = death
    ndwait = 5,        -- seconds frozen before the respawn
    ndresphp = 50,     -- HP after the respawn
    ndmoney = 0,       -- money lost at each death, in percent
    ndnear = 1500,     -- cm: farther than this from the host = teleported to it at the respawn
    ndleash = 10,      -- m: leash distance used for that teleport
    emsync = 0,        -- batch 14: 1 = emissions follow the host (#E); the client's own emission schedule is paused
    emtest = 0,
    qworld = 1,        -- batch 53: on (was 0; World paks of b53). batch 16: client: 1 = also replay the "world" events (kind W: data layers, restrictors, zones, anomalies)
    qrollback = 0,     -- batch 16: 1 = #B: the host announces a rollback of its quest log, the client reloads and replays
    newgame = 1,       -- batch 46: host: the game's new-game node (EQ101_P_..._NewStart) with the clock < newgamesecs starts a new co-op campaign record
    newgamesecs = 600, -- batch 46: game seconds
    campaign = "",     -- batch 46: host: a word; changing it starts a new co-op campaign record (use it when switching to another existing campaign)
    qrbmode = 1,       -- client: 1 = reload with XLoadLastSave; 0 = only log the rollback (the client's journal stays ahead)
    qrbwindow = 300,   -- host: seconds the catch-up is offered again after a rollback
    qrbdelay = 12,     -- client: seconds after its new pawn before the replay
    qrbmax = 240,      -- client: gives up waiting for the reload / the catch-up after this many seconds
    csold = 0, qlsettle = 60,   -- b165: 0 = ONE client save rule (user 2026-10-16 "save client whenever the host saves": the client saves right after
                     -- each host save, tagged with its kh/campaign, retried until it sticks; no timer/steps/leave saves, no n7/n7sv gates); the real
                     -- save folder from C++ s2n_save_info (-saveddirsuffix, STEAM and NULL); host #B/catch-up wait until its log matches the loaded
                     -- save (qlsettle s at most); a client booted into another campaign's / an ahead save loads its pair. 1 = old behaviour
    qnohold = 0, qrbnl = 30,   -- b163quest: 0 = fixes ON: a new host campaign is not an n7 rollback; a client rbWait whose save never loaded
                       -- (same pawn after qrbnl s) is released; n7 keeps Q.seen (no double apply); qdiv flags held/unreceived host steps. 1 = old
    n7sv = 0, n7svloc = 30, n7svdir = 1,   -- b101n7save (client, needs n7=1 csave=1): 1 = after a host load the client loads its save PAIRED
                     -- with the host's loaded save (ledger of its saves by host steps; the pair is copied over its newest save file);
                     -- n7svloc s: no client save while its own graph ran a qpick node the host has not reported; n7svdir 1 = 'dir' allowed
    sxjoin = 0, sxwait = 180, sxsave = 25, sxtries = 3,   -- b167 (both, needs n7=1 csave=1 qsync=1 + DLL b167 + relay b167): 1 = a client booted into a world
                     -- that is not the host's (other campaign / ahead / no record) and with no paired save loads a COPY OF THE HOST'S SAVE
                     -- (sent over the relay, written over a fresh auto save of the client) and gets back its own snapshot (items, money);
                     -- sxwait s for the transfer, sxsave s per forced auto save try, sxtries tries; then it stays in its own world
    cswc = 1,          -- b167 (client): 1 = the client saves/pairs only while its LOADED world is the host's campaign (W line of
                     -- client_applied.txt); rows/records with an unknown or other world campaign are never pairs. 0 = b165 behaviour
    n7eq = 0,          -- b103n7ammo (client, needs n7=1): 1 = the n7 restore does not judge slot-capable items (s2u_equip.tsv: weapons,
                     -- armor, artifacts, detectors, nvg, binoculars): a worn item reads count 0, so 'missing' may be 'worn'; re-creating
                     -- it gave a duplicate plus a full magazine the trim cannot take back (live 12:09 A918D sobra 8, A919D sobra 30)
    n7 = 0, n7shared = 0, n7wait = 5, n7per = 4, n7conf = 5, n7poll = 2,   -- b97n7 (N7): 1 = host death / older save: the client reloads its own
                     -- paired save, replays the host log and gets back exactly its own items (both sides; needs qrollback=1, client csave=1);
                     -- n7shared 1 = allow it on ONE save folder for both games (D13 open); n7wait s after the catch-up; n7per adds per tick;
                     -- host: n7conf s same pawn after its save index changed = a save; n7poll s between index reads
    qdiv = 1, qdivs = 2, qdivm = 20, qdivkeep = 600, qdivn = 12, qdivroll = 1, qdivj = 0, qdivlog = 30,   -- b129qdiv A: #QD quest digest both ways;
                       -- a client trigger step the host has not run for qdivm s: the host replays it (XEndQuestNodeBySID) once its own story lock
                       -- is free, or, if the host's log moved the same quest after the step (tie), the host wins and the client returns (n7Follow;
                       -- qdivroll 0 = log only). qdivj 1 = also compare journal counts (log only). One line every qdivlog s: in-sync/diverged/repaired
    ldsave = 1, ldsavew = 8, ldsavekeep = 604800,   -- b129qdiv B: ledger (bridge s2_blk_ledger.txt, GUIDs) of NPCs/usables our blocks switched
                       -- off; ldsavew s (+25 s) after every load: count what is off and not ours; 2 = also switch the ledger ones back on
    qmload = 1, qmloadw = 8,   -- b127softlock A (qimirror): a new local pawn (death reload / own load / respawn) = a new quest-item baseline,
                       -- nothing sent; after qmloadw s the CLIENT tops each item back up to the count agreed before (never down); the host only re-reads
    qnkeep = 1,        -- b127softlock B: a client trigger report (#QN) rides peersilent+2 s (was 3 s; the host dedupes sid:seq): a relay gap does not lose it
    ldwatch = 1, ldwatchs = 600,   -- b127softlock C (lead=1): my DIALOG story lock is released when the dialog reader dm70 is not loaded
                       -- (the lock could never end by itself) or after ldwatchs s (the other player's story NPCs come back)
    qimirror = 0, qmset = 3, qmre = 1, qmrmhost = 0,   -- b97n7 part B (needs lead=1): quest-item counts mirrored both ways (absolute count after
                     -- qmset s stable; receiver sets its count; S2QI removal then +rest after qmre s); qmrmhost 1 = the host has the S2QI pak
    qdrop = 0, qdrophold = 1000, qdropfn = 1, qdropwin = 4,   -- b132qhold: 1 = HOLD D qdrophold ms on a quest item in the open inventory
                       -- drops it (pak S2QD + s2u_qdrop.lua, BOTH); a tap stays the stock refusal; no message. qdropfn 1 = UIInventoryDropItem,
                       -- 2 = OnDropItemHoldTriggered, 0 = no call; if nothing drops: droppable until qdropwin s after D is released. Set on BOTH
    qextra = 0,        -- batch 16 (v1; switch on after its probe): client: 1 = also replay the "extra" events (kind X: PDA notes, markers, quest givers, technician unlocks)
    cinesleep = 0,     -- batch 16 (v1; switch on after its probe): 1 = a sleeping host counts as "in a cinematic" for #V        -- 1 = the host also runs the client side on its own flag (loopback / solo probe)
    sleepsync = 0,     -- b102sleep (N5): 1 = a sleep by either player takes the other along (#SL); refused / no answer = the sleeper is restored; set on BOTH
    sleeppull = 0, sleeppullr = 300,   -- b110: 1 = the side that follows a sleep (#SL accepted) is first pulled next to the sleeper (tpfloor's spot
                       -- rule, S2L.tpfNow); not when already within sleeppullr cm. Set on BOTH
    pupfix = 0, pupfixcm = 150, pupfixn = 3, pupfixmax = 3,   -- b110: 1 = the puppet found pupfixcm+ cm off our last write (another writer
                       -- put it back on a stale anchor = the idle flicker) on pupfixn samples in 10 s -> body swapped (hidden + XDestroyModel(uid),
                       -- new body adopted/requested); at most pupfixmax swaps per 600 s. Set on BOTH
    sleepmode = 1, sleepans = 15, sleepwait = 60, sleepnat = 4, sleepvirt = 0, sleepskip = 1,   -- b102sleep: 1 = the other follows at once, 2 = it must
                       -- lie down in a bed within sleepwait s / s for an answer / s the game's sleep may take to start before the scripted one /
                       -- sleepiness after a scripted sleep / 1 = host skips with XSkipTimeHours (checked), 0 = S2_setGameTime
    sleepkeep = 30, sleepmaxage = 120, sleepcool = 30, sleeprate = 24, sleepfight = 10,   -- b102sleep: s #SL kept / max age s / s no new request
                       -- after a sleep / tod-seconds per real second (undo) / s after an HP loss = in a fight
    sstash = 0,        -- batch 17: 1 = one stash shared by both players (ledger + #Y); needs the Stash pak on BOTH PCs
    ssremove = 0,      -- 1 = also remove the other player's takes from MY box copy (needs the StashRm pak, built with the box GUID)
    ssstrict = 1,      -- 1 = the HOST too loses what it takes beyond the ledger (right for a campaign started with the mod; 0 if the host's box held items before)
    ssdelay = 10,      -- client: seconds after a new pawn before its box copy is filled from the ledger
    tsync = 0,         -- batch 17: 1 = trader take-back: my buys leave the other PC's copy of that trader (needs the Trade pak + itake=1 on both)
    trradius = 800,    -- cm: an NPC within this distance of a row of s2u_trade.tsv is that trader
    artsync = 0,       -- batch 18: 1 = artifact mirror (host's artifacts shown to the client as pickups; one taker). Needs itake-style paks: Player pak + NoArtifacts on the client
    artradius = 6000,  -- cm: the host lists artifacts this close to the CLIENT
    artnear = 500,     -- cm: a count that goes up this close to an artifact's spot is a pickup of it
    artmax = 6,        -- most artifacts listed at once
    artclass = "",     -- client: class name of the spawned pickup, if it is not "Artifact" (from the probe)
    cpequip = 1,       -- batch 19: persistence keeps an equip ledger for slot items (s2u_equip.tsv); 0 = inventory counts only, as before
    cpammo = 1,        -- batch 19: after a restore remove the loaded-ammo surplus of created weapons
    ndgrace = 10,      -- batch 19: seconds without damage after a nodeath respawn
    emretry = 20,      -- batch 19: client: seconds between two XStartEmission attempts
    trwindow = 3600,   -- batch 21: game-seconds (24 game hours = one trader restock) a slot item bought by the other player blocks my buying the same one
    qonce = 1,         -- batch 22: 1 = every host quest event is applied at most once per client world (0 = replay all, as before)
    artcool = 5400,    -- batch 22 (artsync=2): host game-seconds a place stays taken (the spawner cooldown of the kit)
    artrad = 2500,     -- batch 22 (artsync=2): cm around a spawner of s2u_artspawn.tsv that count as its place
    csave = 0,         -- batch 23: 1 = the CLIENT saves its own game (remove its NoSave pak); the mod keeps the applied quest events in step with that save
    cssecs = 60,       -- seconds between two saves the mod triggers
    cssteps = 5,       -- batch 63 (user 2026-10-14): a save for applied host steps only once this many steps were applied since the last save
    cstimer = 300,     -- seconds: the client's game is also saved this often when nothing was applied (the character must survive a crash)
    csdelay = 0,       -- seconds after triggering a save before its event counts are written (0 = at once)
    cswait = 10,       -- batch 24: seconds to wait for the save index file to change after asking for a save
    csretry = 20,      -- batch 24: seconds before a refused save is tried again
    cscalm = 15,       -- batch 25: and seconds without a drop of the client's HP before that new try
    cssettle = 3,      -- batch 25: seconds after applying a host quest step before a save is asked for
    alifeguid = 0,     -- batch 29: 1 = the old GetPlaceholderGuidByUObject test in the A-Life sweep (crashed the game during quest squad moves; tests only)
    alifetrader = 12000, -- batch 29: cm; alifekeepquest=1 keeps NPCs this close to a named trader/medic/bartender place of s2u_trade.tsv
    emskew = 15,       -- batch 39: seconds my emission may run without the host's (start early / end late) before the protection arms
    autoload = 0,      -- batch 39: 1 = at the main menu with no pawn, XLoadLastSave every 20 s until a pawn exists (the title key must come from outside)
    wxlock = 1,        -- batch 38: client: the current weather never ends by itself (RequestedWeatherDuration = 1e9)
    wxinstant = 1,     -- batch 38: client: XForceWeather for the first switch of a session / pawn and for drift corrections
    wxfollow = 1,      -- batch 36: client: switch to the host's weather type as soon as it changes (2 s apart at least); emission weather (7) on one side only is fixed at once
    gsnap = 0,         -- batch 36: 1 = after a teleport, a client stuck more than 300 cm below the host for 3 s is put at host z + 150
    emreset = 0,       -- batch 35: client: pause the own emission schedule again at every new pawn and at every host emission start
    qbootdedupe = 1,   -- batch 34: host: drop a quest event already in the log during the first qbootwin s after a new host pawn (load re-fires)
    qbootwin = 30,     -- batch 34: seconds
    emprotect = 120,   -- batch 33: client unkillable and no csave while its own emission runs without the host's, and this many seconds after
    artreach = 600,    -- batch 33: cm; an artifact counts as taken by me only when I stood this close to it at its last sighting
    smooth = 1,        -- batch 49: the puppet moves by the remote velocity each tick (dead reckoning) and pulls the error in at smoothgain/s
    smoothgain = 4,    -- batch 49: 1/s
    smoothsnap = 300,  -- batch 49: cm of error beyond which the puppet is put on the target at once
    smoothlog = 1,     -- batch 49: every 10 s, while it moves, a histogram of the puppet's per-tick steps and of the sample intervals
    nsmooth = 0,       -- b114smooth: 1 = ONE per-frame motion path for every actor placed from network data (puppet + client NPC mirrors): a
                       -- ProjectileMovementComponent moves it EVERY FRAME by a velocity the core sets at 10 Hz (dead reckoning + error pull)
    nsgain = 7, nssnap = 300, nscap = 0, nsvmax = 1500, nsdead = 3, nsmax = 8,   -- b114: error pull 1/s (~140 ms), snap cm, extrapolation (b148pose: 250 -> 0)
    nsip = 1, nsnoback = 1, nsippad = 0.05, nsipmax = 0.5, nsbhold = 0.5, nsbmax = 60,   -- b148pose: actors follow the sample path one sample interval
                       -- (+nsippad s, max nsipmax) behind, never past the newest sample; nsnoback: never step back along the path while the
                       -- target moved in the last nsbhold s, later back at most nsbmax cm/s. Old b114: nsip=0 nsnoback=0 nscap=250
                       -- cap ms, speed cap cm/s, dead zone cm, mirrors driven per tick (nearest first; the rest keep main.lua's placement)
    nsyaw = 0, nsyawgain = 10,   -- b114: nsyaw 1 = per-frame yaw by a RotatingMovementComponent (rate = yaw error x nsyawgain); 0 = 10 Hz Lua yaw
    nsuro = 0,         -- b114: 1 (with nsmooth=1) = the puppet mesh animates every frame: URO off, VisibilityBasedAnimTickOption 0, tick interval 0
    nslog = 1,         -- b114: 10 s line "S2CORE nsmooth 10 s" while nsmooth=1
    offtick = 30,      -- b116motion: K>0 = gsc.Mid/FarDistanceComponentsMaxTickRate kept at ceil(K*4/3) (30 -> 40) so a visible Agent body (puppet)
                       -- moves >= ~K times/s (the game: 30 / 10 -> ~20-26/s); game values saved, put back once when the flag becomes 0
    fpscap = 0,        -- b118fpscap (user N16): 1 = tick-interval caps on every Agent's Mesh + CharacterMovement and every b114 mover:
                       -- hostile (IsInThreat; mirrors by the host's #FE list) 1/fpsenemy, everything else 1/fpsother; own values back at 0
    fpsenemy = 45, fpsother = 30, fpsver = 5, fpsfe = 12,   -- b118: caps per s; s between read-backs; max names in #FE
    aiwatch = 0,       -- b121aiwatch: 1 = HOST (alifeoff ~= 1) re-asserts the AI feature mask (XActivateAIFeature(aiwatchm) + XALifeEnable)
                       -- every aiwatchs s while free (no own cinematic/dialog/freeze/sleep/death) and once at each new local pawn
    aiwatchs = 10, aiwatchm = 4194303,   -- b121: s between re-asserts; EAIFeature bits OR-ed back (4194303 = All; 4161535 = game constructor default)
    rvder = 1,         -- b116b: 1 = the bodies (puppet, its PMC, far body) use the snapshot-derived remote velocity when the sent one is ~0
                       -- but the snapshots move (live: getRemote().vx = 0 while the host walked); 0 = the sent velocity as before
    pupcmoff = 0,      -- b114 (coordinator 17:20, measured): 1 = the puppet's CharacterMovement is stopped and its tick switched off once per pawn
                       -- (PoseSearchLocomotion CMC regrew Velocity and moved the body between our writes); re-checked every tick; not with pupmove 1/3
    npcsync = 0,       -- b122npcsync: 1 on the HOST = x1 "#NP" list of its NPCs near either player; 1 on the CLIENT (needs nsmooth=1) = its kept
                       -- NPCs paired with the host's (uid, then prototype + nearest) get no brain and are driven from the host's samples
    npsr = 15000, npsbud = 24, npslist = 1,   -- b122 (b125: 15000, was 10000): host radius cm around EACH player; NPCs per snapshot; s between NPC lists (both sides)
    npsmatch = 2, npsmr = 1500, npslost = 2, npsmax = 16,   -- b122: 1 uid only, 2 + prototype within npsmr cm; s silent -> released; driven per tick
    npsai = 1, npsanim = 1, npsdlgr = 400, npsdlghold = 5, npslog = 1,   -- b122: per-NPC brain off; 1 walk/run clips (2 + idle); dialog cm / hold s; 10 s line
    npsmove = 1,       -- b125npcmove (shared flag; needs npcsync=1): 1 = the host sends only MOVING / threat NPCs (+ one 'stopped' entry, + npskey calm
                       -- keyframes); the client drives only those and releases the rest to its own AI (snapped when > npssnap cm off); 0 = b122
    npsmode = 2, npsmstop = 1,   -- b125: 2 = PMC mover + CMC Velocity/Acceleration written for the AnimBP; 1 = CMC tick on drives it; 0 = b122 PMC + npsanim clips; stop own montage on take
    npsv = 30, npsd = 50, npsstill = 1, npskey = 1,   -- b125 host: moving = 2D speed > npsv cm/s or > npsd cm from its last sent spot; held npsstill s; calm keyframes/snapshot
    npssnap = 300, npsidle = 60,   -- b125 client: idle NPC > npssnap cm from the host's spot is placed there (at release + every npslist s); pair kept npsidle s silent
    artmute = 1,       -- batch 47: an artifact hidden at a closed place also has its AkComponent, NiagaraComponent and PhysicsInteractionComponent stopped
    -- batch 51: artaway/artupz/artparkx/artparky removed (r42: the game deletes parked copies; the click at an emptied place is stock)
    artquiet = 20,     -- batch 33: seconds after a teleport of mine or a new pawn in which no artifact take is counted
    emstart = 0,       -- batch 32: 0 = the client never calls XStartEmission (the synced weather type 7 starts it); 1 = ONE call per host emission, no retry
                       -- batch 40: 2 = ONE call emwait s after the host's emission began, only if mine was never active during it
    emwait = 20,       -- batch 40: seconds after the host's emission began before the client looks at its own (and, emstart=2, starts it)
    emgrace = 10,      -- batch 32: seconds the client's emission may run on after the host's flag went off before the one stop
    emstopgap = 60,    -- batch 32: seconds before a stop is tried again (3 at most per emission)
    joinport = 1,      -- batch 31: 1 = the client is teleported to the host once at every host session start and after every client load
    joinm = 10,        -- batch 31: metres; no teleport when the host is nearer than this
    netrel = 1,        -- b128net: 1 = ack + resend layer for the one-shot x1 events (S2L.NRTAG): a link gap longer than an event's window no
                       -- longer loses it (resent in #RS until the peer acks; the peer applies it once). Both sides; a side without it = no #RS
    netrelmax = 120, netrelhold = 6, netrelseen = 300, netrelper = 40,   -- b128: give up s; receiver hold s; receiver dedupe memory s; #RS entries/snapshot (and <= 3000 chars)
    netlockkeep = 60,  -- b128net: s the peer's story lock #LK / container lock #K keep their last value while the peer is silent (0 = dropped at once)
    resend = 6,        -- batch 28: seconds the one-shot messages #W #R #T #U ride in the snapshots (longer than peersilent)
    qgap = 1,          -- batch 27: 1 = the client asks for the catch-up again when the host's log holds more steps than its world
    csconfirm = 5}     -- batch 25: seconds the same pawn must still be there after the save index changed (a world load rewrites it too)

local api, FL, log
local seqs = {}                     -- animation asset cache (rebuilt after every reload)
local PUP_ANIM = {
    idle = "/Game/_STALKER2/Animations/Characters/AnimSequences/bh/idles/AS_tp_bh_idle.AS_tp_bh_idle",
    walk = "/Game/_STALKER2/Animations/Characters/AnimSequences/bh/locomotion/stand/AS_tp_bh_walk_fwd.AS_tp_bh_walk_fwd",
    run = "/Game/_STALKER2/Animations/Characters/AnimSequences/bh/locomotion/stand/AS_tp_bh_run_fwd.AS_tp_bh_run_fwd",
}
local PUP_NAT = {walk = 114, run = 285}   -- cm/s of the clips, measured with the game's own AI
local WALK_MAX = 230                      -- remote speed below this = walk clip, above = run clip
local T0 = {Translation = {X = 0, Y = 0, Z = 0}, Rotation = {X = 0, Y = 0, Z = 0, W = 1}, Scale3D = {X = 1, Y = 1, Z = 1}}

local function valid(o)
    if not o then return false end
    local ok, v = pcall(function() return o:IsValid() end)
    return ok and v
end
local function bodyOk(B)                      -- B = ST.pup or ST.tgt
    if not valid(B.pawn) then return false end
    if not B.path then return true end
    -- StaticFindObject(<level actor path>) costs 21-25 ms per call (measured): use the live pawn list (0.2 ms per tick) + same full name
    local ok, same = pcall(function() return S2_isLivePawn(B.pawn) and (B.pawn:GetFullName():gsub("^%S+ ", "")) == B.path end)
    return ok and same
end
function M.inWorld(_, x, y, z)                 -- b81kz: false = out of the world (main.lua calls it through S2_coreCall)
    if (FL.kzsafe or 0) ~= 1 then return true end
    if type(x) ~= "number" or type(y) ~= "number" or type(z) ~= "number" then return false end
    if x ~= x or y ~= y or z ~= z then return false end                    -- NaN
    local xy = FL.kzxy or 5000000
    if math.abs(x) > xy or math.abs(y) > xy then return false end            -- also +-inf
    return z >= (FL.killz or -20000) and z <= (FL.kztop or 1000000)
end
function S2L.remoteIn(what)                     -- b81kz: the remote snapshot for PLACING things; nil while the remote is out of the world
    local r = api.getRemote and api.getRemote()
    if not r or (FL.kzsafe or 0) ~= 1 or M.inWorld(nil, r.x, r.y, r.z) then
        if r and S2L.kzOut then S2L.kzOut = nil; log(string.format("S2CORE kzsafe: remoto de volta ao mundo (z=%.0f) depois de %d recusas", r.z or 0, S2L.kzN or 0)); S2L.kzN = 0 end
        return r
    end
    S2L.kzN = (S2L.kzN or 0) + 1
    if not S2L.kzOut then
        S2L.kzOut = true
        log(string.format("S2CORE kzsafe: remoto FORA DO MUNDO (%s,%s,%s; killz=%s) -> nada nosso vai para la (%s); boneco/alvo parados e escondidos",
            tostring(r.x), tostring(r.y), tostring(r.z), tostring(FL.killz), tostring(what)))
    end
    return nil
end
function S2L.ioPawn()                           -- b81kz: S2.io.pawn is cached by main.lua between steps: IsValid first (null- and dangling-safe)
    local p = S2.io and S2.io.pawn
    if (FL.kzsafe or 0) ~= 1 or valid(p) then return p end
end
function S2L.kzHold(ST)                         -- b81kz: remote out of the world: our bodies stay where they are, hidden
    local P, F = ST.pup, ST.far
    if P and P.pawn and not P.hidden and valid(P.pawn) then P.hidden = true; pcall(function() P.pawn:SetActorHiddenInGame(true) end) end
    if F and F.actor and not F.hidden then S2L.farHide(F) end
end
function S2L.pawnOut(o)                         -- b81kz (host): an NPC out of the world is not scanned/sent
    if (FL.kzsafe or 0) ~= 1 or api.ROLE ~= "host" or not valid(o) then return false end
    local q; pcall(function() q = o:K2_GetActorLocation() end)
    return q ~= nil and not M.inWorld(nil, q.X, q.Y, q.Z)
end
local function asset(path)
    local a = StaticFindObject(path)
    if not valid(a) then pcall(function() a = LoadAsset(path) end) end
    if valid(a) then return a end
end

function M.onLoad(ST, a)
    api = a; FL = a.FL; log = a.log
    ST.loads = (ST.loads or 0) + 1
    ST.pup = ST.pup or {}
    ST.tgt = ST.tgt or {}
    ST.dmg = ST.dmg or {seq = 0, list = {}, total = 0}
    ST.din = ST.din or {seen = {}, n = {ok = 0, cap = 0, dead = 0, prot = 0, down = 0}}   -- receiving side (survives reloads: no event is applied twice)
    ST.shotSeen = ST.shotSeen or {}
    ST.peer = ST.peer or {state = "none", ups = 0, sessions = 0}
    ST.me = ST.me or {nonce = string.format("%x%x", os.time() % 0xFFFFFF, math.random(0xFFFFF))}   -- identifies this game launch to the other side
    ST.life = ST.life or {}
    ST.qs = ST.qs or {seq = 0, list = {}, seen = {}, got = 0, sent = 0, applied = 0, skipped = 0}
    if not rawget(_G, "S2_QHANDLER") then           -- registered ONCE per Lua state; the call is looked up in the current module each time
        S2_QHANDLER = true
        local okR, eR = pcall(RegisterConsoleCommandHandler, "s2q", function(full, params, ar)
            local m = S2.mod
            if m and m.questCmd then pcall(m.questCmd, S2.state, full, params) end
            return true                                -- handled: the engine does not see "s2q"
        end)
        log("S2CORE quest: RegisterConsoleCommandHandler s2q ok=" .. tostring(okR) .. " " .. (okR and "" or tostring(eR)))
    end
    log("S2CORE onLoad ver=" .. M.VERSION .. " loads=" .. ST.loads .. " puppet=" .. tostring(valid(ST.pup.pawn)))
end
function M.onUnload(ST)
    if M.cineRestore then pcall(M.cineRestore, ST, "modulo descarregado") end if M.emRelease then pcall(M.emRelease, ST, "modulo descarregado") end
    if M.cpFlush then pcall(M.cpFlush, ST, "modulo descarregado") end          -- batch 13: last save of the session
    if M.ndRelease then pcall(M.ndRelease, ST, "modulo descarregado") end
    pcall(S2L.ffUninstall)                                           -- b143perf: originals back
    pcall(S2L.acUninstall)                                           -- b149hitch: originals back
end   -- batch 12b: never leave the client frozen

function M.ownsAvatar(ST) return FL.avatarmode == "npcpuppet" and FL.corepuppet == 1 end

-- ===== sender side: extra snapshot field "x1" (no quotes allowed inside) =====
-- "W1~<weapon part: path@mats@name@socket>~<statics list>" taken from main.lua's appearance scan of the LOCAL player
local function dmgStr(ST)                      -- damage taken by the remote player's target body; each event is repeated for 2 s
    if FL.cine == 1 and ST.cv and ST.cv.on then ST.dmg.list = {}; return "#D:" end   -- batch 12: no damage to the client while the host is in a cinematic
    local now, keep, t = os.clock(), {}, {}
    for _, e in ipairs(ST.dmg.list) do if now - e.at < 2 then keep[#keep + 1] = e; t[#t + 1] = string.format("%d,%.1f", e.seq, e.amount) end end
    ST.dmg.list = keep
    return "#D:" .. table.concat(t, "|")
end
local function weaponStr(ST, pawn)
    if FL.pupweapon ~= 1 then return "" end
    local w = pawn:GetWeaponInHandsMeshComponent()
    if not valid(w) then return "W1~~" end
    local wname = w:GetFName():ToString()
    local c = ST.selfLook
    local now = os.clock()
    if not (c and c.wname == wname) or ((FL.pwrescan or 0) == 1 and now >= (c.at or 0)) then   -- b184g19: pwrescan=1 also re-reads every pwscan s
        local part, statics = "", {}
        local wa = w:GetSkeletalMeshAsset()
        if valid(wa) then part = (wa:GetFullName():gsub("^%S+ ", "")) .. "@@" .. wname .. "@" .. w:GetAttachSocketName():ToString() end
        -- static attachments whose parent chain ends at the weapon (magazine, sights...)
        local known = {[wname] = true}
        local list = api.compList(pawn, "/Script/Engine.StaticMeshComponent")
        for _ = 1, 4 do
            for _, sc in ipairs(list) do pcall(function()
                if not valid(sc) or not sc:IsVisible() then return end
                local nm = sc:GetFName():ToString()
                if known[nm] then return end
                local par = sc:GetAttachParent(); if not valid(par) then return end
                local pn = par:GetFName():ToString(); if not known[pn] then return end
                local sm = sc.StaticMesh; if not valid(sm) then return end
                local l, r, k = sc.RelativeLocation, sc.RelativeRotation, sc.RelativeScale3D
                known[nm] = true
                if #statics < 14 then statics[#statics + 1] = string.format("%s>%s>%s>%s>%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f", nm, pn, sc:GetAttachSocketName():ToString(),
                    (sm:GetFullName():gsub("^%S+ ", "")), l.X, l.Y, l.Z, r.Pitch, r.Yaw, r.Roll, k.X, k.Y, k.Z) end
            end) end
        end
        if (FL.pwrescan or 0) == 1 then table.sort(statics) end   -- b184g19: same weapon -> same text (no rebuild on the receiver)
        local old = c
        c = {wname = wname, str = "W1~" .. part .. "~" .. table.concat(statics, "|"), at = now + ((#statics == 0 and part ~= "") and 1 or (FL.pwscan or 3))}
        ST.selfLook = c
        if not (old and old.str == c.str) then log("S2CORE snapExtra: arma local " .. (part:match("[^/%.@]+@") or "?") .. " estaticos=" .. #statics .. " bytes=" .. #c.str) end
    end
    if api.lookPut then return "W2~" .. api.lookPut(c.str) end   -- protocol v2: only the hash travels every tick; the text goes once ("apps")
    return c.str
end

-- ===== x1 layout:  <weapon>#D:<seq>,<hp points>|...#L:<a|w|d>,<hp>,<maxhp>     (no '"' and no '#' inside a part)
--   <weapon> = W1~<part>~<statics> (text inline) or W2~<hash> (protocol v2: text sent once through main.lua's look cache)
--   #D = damage the SENDER's hidden target body took = damage for the player who RECEIVES this snapshot (host -> client)
--   #L = life of the player who SENDS this snapshot (both directions): a alive, w downed, d dead
local function x1part(tag)                      -- "D" or "L" part of the REMOTE x1 (text after "#D:" / "#L:"), or nil
    local P = S2.state and S2.state.peer
    if P and P.state == "silent" and not S2L.nrKeep(tag, P) then return nil end   -- batch 6: a peer that went silent holds no lock, sends no damage, has no life state (b128: #LK/#K kept netlockkeep s)
    if P and P.live == false then return nil end        -- batch 28: only a snapshot file that has CHANGED since this game started carries messages
    local head = S2.io and S2.io.head
    if (FL.x1c or 0) == 1 then return S2L.x1Get(head, tag) end   -- b143perf: cut + split once per remote snapshot
    local x1 = head and head:match('"x1"%s*:%s*"(.-)"') or ""
    if S2L.nrX1 then x1 = S2L.nrX1(x1) end              -- b128net: + resent (#RS) entries not seen before
    return x1:match("#" .. tag .. ":([^#]*)")
end
local function lifeStr(ST, pawn)
    local hp, mx, alive = -1, -1, true
    pcall(function() hp = pawn:GetHP() end); pcall(function() mx = pawn:GetMaxHP() end); pcall(function() alive = pawn:IsAlive() and true or false end)
    if type(hp) ~= "number" then hp = -1 end; if type(mx) ~= "number" then mx = -1 end
    local st = "a"
    if not alive or hp == 0 then st = "d" elseif os.clock() < (ST.din.downUntil or 0) then st = "w" end
    return string.format("#L:%s,%.0f,%.0f", st, hp, mx)
end
-- life of the OTHER player: "a" alive, "w" downed, "d" dead (+ hp, maxhp); nil = the other side does not report (older module)
function M.remoteLife()
    local sg = x1part("L"); if not sg then return nil end
    local st, hp, mx = sg:match("^(%a),([%-%d%.]+),([%-%d%.]+)")
    return st, tonumber(hp), tonumber(mx)
end
function M.remoteDown() local st = M.remoteLife(); return st ~= nil and st ~= "a" end

-- ===== b115leash: one leash owner (SIMPLIFY_REVIEW #7; flag leashown). Requests ST.leash.req[name] = {m, untilT, hard, why}; with leashown=1 no
-- feature writes FL.leashm: S2L.lrApply (tick100, after ftTick) writes the effective value there ONCE a tick; main.lua's leash keeps reading it.
-- Effective (the rule each old site followed): a live HARD park (join_park: partner absent / frozen in the host's cutscene; batch 33/36 kept it
-- parked when the cine follow lowered it) -> 1e9; else the shortest live pull (cine 25 m 6 s, join 10 m 1 s, nodeath 10 m 6 s, ft 30 m 1 s),
-- never above flags.txt; else a live SOFT park (ft_park: dialog / cinematic / ftgrace after it / own travel; ftPark left another switch's window
-- alone, so a pull beats it) -> 1e9; else flags.txt leashm (LR.base: the S2_loadFlags wrapper keeps the reload from undoing a request).
-- One pull at a time: the first puller owns the leash for leashpullwin s (and a hard park refuses every pull); a refused pull is logged once and
-- its feature retries by itself (cine within its 3 s window, join keeps J.want, ft keeps F.pend, nodeath respawns without the pull).
function S2L.lrFmt(m) return (type(m) ~= "number" and tostring(m)) or (m >= 1e9 and "parada") or string.format("%g m", m) end
function S2L.lrOn(ST)                              -- leashown=1: the owner's state; the first call takes over from the old switches
    local LR = ST.leash
    if not LR then LR = {req = {}, ref = {}}; ST.leash = LR end
    if not LR.on then
        LR.on = true; LR.req = {}; LR.ref = {}; LR.pull = nil; LR.last = nil
        local J, V, N, F = ST.join or {}, ST.cv or {}, ST.nd or {}, ST.ft or {}
        local base
        for _, v in ipairs({J.parked or false, F.lsave or false, J.leashSave or false, V.leashSave or false, N.leashSave or false, (F.fol and F.fol.real) or false, FL.leashm or false}) do
            if type(v) == "number" and v < 1e9 then base = v; break end   -- the real value an old switch was holding, else the current one
        end
        LR.base = base or 350
        J.parked, J.parkWhy, J.leashSave, J.leashTemp, V.leashSave, N.leashSave, F.lsave, F.park = nil, nil, nil, nil, nil, nil, nil, nil
        if F.fol then F.fol.real, F.fol.temp = nil, nil end
        log("S2CORE coleira: dono unico ligado (leashown=1): base " .. S2L.lrFmt(LR.base) .. " (os outros interruptores soltos)")
    end
    return LR
end
function S2L.lrEff(LR, now)                        -- the effective leash (pure)
    local hard, soft, pull = false, false, nil
    for _, q in pairs(LR.req) do
        if not q.untilT or now < q.untilT then
            if q.m >= 1e9 then if q.hard then hard = true else soft = true end
            elseif not pull or q.m < pull then pull = q.m end
        end
    end
    local base = LR.base or 350
    if hard then return 1e9 end
    if pull then return math.min(pull, base) end
    if soft then return 1e9 end
    return base
end
function S2L.lrSet(ST, name, m, secs, why, hard)   -- file / renew a request (secs nil = until lrDrop)
    if (FL.leashown or 0) ~= 1 then return end
    local LR, now = S2L.lrOn(ST), os.clock()
    local q = LR.req[name]
    if q and not secs and not q.untilT and q.m == m then q.why = why; return end   -- a park asked again each tick: silent
    LR.req[name] = {m = m, untilT = secs and now + secs or nil, hard = hard and true or nil, why = why}
    log(string.format("S2CORE coleira: pedido %s %s%s%s -> efetiva %s", name, S2L.lrFmt(m), secs and string.format(" (%g s)", secs) or "",
        why and (" [" .. tostring(why) .. "]") or "", S2L.lrFmt(S2L.lrEff(LR, now))))
end
function S2L.lrDrop(ST, name, why)                 -- the feature is done with its request
    local LR = ST.leash
    if not (LR and LR.req and LR.req[name]) then return end
    LR.req[name] = nil
    if (FL.leashown or 0) == 1 then log("S2CORE coleira: fim " .. name .. " (" .. tostring(why) .. ") -> efetiva " .. S2L.lrFmt(S2L.lrEff(LR, os.clock()))) end
end
function S2L.lrPull(ST, name, m, secs, why)        -- a pull: true = filed; false = refused (hard park, or another feature owns the pull)
    if (FL.leashown or 0) ~= 1 then return false end
    local LR, now = S2L.lrOn(ST), os.clock()
    local by, txt
    for n, q in pairs(LR.req) do if q.hard and q.m >= 1e9 and (not q.untilT or now < q.untilT) then by, txt = n, "coleira parada por " .. n end end
    local Pu = LR.pull
    if not by and Pu and Pu.name ~= name and now < Pu.untilT then by, txt = Pu.name, string.format("%s puxa por mais %.0f s", Pu.name, Pu.untilT - now) end
    if by then
        if LR.ref[name] ~= by then LR.ref[name] = by; log("S2CORE coleira: pedido " .. name .. " " .. S2L.lrFmt(m) .. " RECUSADO (" .. txt .. ")") end
        return false
    end
    LR.ref[name] = nil
    LR.pull = {name = name, untilT = now + math.max(FL.leashpullwin or 20, secs or 0)}
    S2L.lrSet(ST, name, m, secs, why)
    return true
end
function S2L.lrWrap()                              -- main.lua's 5 s flags reload compares with flags.txt's value, then the effective one is put back
    if rawget(_G, "S2_loadFlags_lr") ~= nil or type(rawget(_G, "S2_loadFlags")) ~= "function" then return end
    local prev, fl = rawget(_G, "S2_loadFlags"), FL
    rawset(_G, "S2_loadFlags_lr", prev)
    rawset(_G, "S2_loadFlags", function(quiet)
        local LR = S2.state and S2.state.leash
        local on = LR ~= nil and LR.on == true and LR.base ~= nil and (fl.leashown or 0) == 1
        if on then fl.leashm = LR.base end
        local ok, r = pcall(prev, quiet)
        if on then
            if fl.leashm ~= LR.base then log("S2CORE coleira: flags.txt leashm " .. tostring(LR.base) .. " -> " .. tostring(fl.leashm)); LR.base = fl.leashm end
            local e = S2L.lrEff(LR, os.clock()); fl.leashm = e; LR.last = e
        end
        if not ok then error(r, 0) end
        return r
    end)
end
function S2L.lrApply(ST)                           -- tick100: the ONE writer of FL.leashm (leashown=1)
    local LR = ST.leash
    if (FL.leashown or 0) ~= 1 then
        if LR and LR.on then                       -- switched off: flags.txt's value back; the old switches start from scratch
            LR.on = nil; LR.req = {}; LR.ref = {}; LR.pull = nil; LR.last = nil
            if LR.base then FL.leashm = LR.base end
            log("S2CORE coleira: dono unico desligado (leashown=0): coleira " .. S2L.lrFmt(FL.leashm))
        end
        return
    end
    LR = S2L.lrOn(ST)
    S2L.lrWrap()
    local now = os.clock()
    if LR.last ~= nil and FL.leashm ~= LR.last and type(FL.leashm) == "number" then   -- written outside the owner (reload without our wrapper, luacmd): new base
        log("S2CORE coleira: leashm mudou por fora " .. S2L.lrFmt(LR.last) .. " -> " .. S2L.lrFmt(FL.leashm) .. ": nova base")
        LR.base = FL.leashm
    end
    local gone
    for n, q in pairs(LR.req) do
        if q.untilT and now >= q.untilT then LR.req[n] = nil; gone = (gone and gone .. "," or "") .. n end
    end
    if LR.pull and now >= LR.pull.untilT then LR.pull = nil end
    local e = S2L.lrEff(LR, now)
    if gone then log("S2CORE coleira: fim " .. gone .. " (tempo) -> efetiva " .. S2L.lrFmt(e)) end
    if FL.leashm ~= e then FL.leashm = e end
    LR.last = e
end
-- ===== end b115leash
-- ===== batch 12: #V = the host is in a cinematic / the host was teleported =====
local function cvInit(ST) ST.cv = ST.cv or {on = false, tseq = 0} return ST.cv end
function M.remoteCine(ST)                              -- client: hide the host body now?
    local V = ST.cv
    if V and V.rk == "d" then return false end            -- batch 69d: a dialog keeps the host's body visible (the host just stands and talks)
    return FL.cine == 1 and V ~= nil and (V.rc == true or os.clock() < (V.hideUntil or 0))
end
local function cineOut(ST)
    if FL.cine ~= 1 or not (api.ROLE == "host" or FL.cinetest == 1) then return "" end
    local V = cvInit(ST)
    local okM, mf = pcall(S2L.mqFlag, ST, V)   -- b109dlgblock: 4th field 1 = main quest (old clients read the first 3)
    return string.format("#V:%d,%d,%s,%d", V.on and 1 or 0, V.tseq, V.kind or "c", (okM and mf == 1) and 1 or 0)
end
local function cineTick0(ST)
    local V, now = cvInit(ST), os.clock()
    if FL.cine ~= 1 then V.on = false; V.rc = false; return end
    local pawn = S2.io and S2.io.pawn
    if api.ROLE == "host" or FL.cinetest == 1 then     -- the two proven signals
        local on = false
        pcall(function()                                   -- core-23: read on the game class "PC" (through UEHelpers.GetPlayerController() the property reads as a TrivialObject, never true: measured)
            local pc = V.pc; if not valid(pc) then pc = FindFirstOf("PC"); V.pc = pc end
            -- core-23b: a cinematic counts only while the player is also locked out (bInCinematicSequence alone is true for 15-18 s after every load of the new-game save, with the player in control)
            on = pc.bInCinematicSequence == true and pc:IsMoveInputIgnored() == true end)
        if FL.cinesleep == 1 then                              -- batch 16: a sleeping host freezes and protects the client the same way
            local slp, sp = false, pawn
            if not valid(sp) then pcall(function() sp = V.pc.Pawn end) end
            if valid(sp) then pcall(function() slp = sp:IsSleeping() == true end) end
            if slp ~= (V.sleep or false) then V.sleep = slp; log("S2CORE cine: host " .. (slp and "DORMINDO (conta como cinematica para o cliente)" or "acordou")) end
            on = on or slp
        end
        local dlg = false
        if FL.cinedlg == 1 and not on and ((FL.lead or 0) ~= 1 or S2L.ldMine(ST) ~= nil) then                    -- batch 69d: a dialog (measured: W_DialogueView_C in the viewport only while talking)
            if not valid(V.dw) or now - (V.dwAt or 0) > 2 then
                V.dwAt = now; V.dw = nil
                for _, w in ipairs(FindAllOf("W_DialogueView_C") or {}) do pcall(function() if w:IsInViewport() then V.dw = w end end) end   -- 69d2: stays in the viewport after the dialog, only hidden
            end
            if valid(V.dw) then pcall(function() dlg = V.dw:IsInViewport() == true and V.dw:IsVisible() == true end) end   -- measured after a dialog: inVP=true vis=false (Collapsed)
            on = dlg
        end
        V.kind = dlg and "d" or "c"
        if on ~= V.on then
            V.on = on
            log("S2CORE cine: " .. (on and (dlg and "DIALOGO do host comecou" or "CINEMATICA do host comecou (tiros e dano em pausa)") or "cinematica/dialogo do host terminou"))
        end
        if valid(pawn) then
            local q = pawn:K2_GetActorLocation()
            if V.px and now - V.pt < 1 then
                local d = math.sqrt((q.X - V.px) ^ 2 + (q.Y - V.py) ^ 2 + (q.Z - V.pz) ^ 2)
                if d > FL.cinejump then
                    V.tseq = V.tseq + 1
                    log(string.format("S2CORE cine: TELEPORTE do host (%.0f m num passo) #%d", d / 100, V.tseq))
                end
            end
            V.px, V.py, V.pz, V.pt = q.X, q.Y, q.Z, now
        end
    end
    if api.ROLE == "host" and FL.cinetest ~= 1 then return end
    -- client side
    if V.leashSave and (now > (V.leashUntil or 0)) then
        FL.leashm = V.leashSave; V.leashSave = nil
        log("S2CORE cine: coleira de volta a " .. tostring(FL.leashm) .. " m")
    end
    local c, ts, rk, rm = (x1part("V") or ""):match("^(%d),(%d+),?(%a?),?(%d?)")
    if c then V.rk = (rk ~= "" and rk) or "c"; V.rm = rm == "1"; if c == "1" and V.rm then V.mEp = true end end   -- b109dlgblock: main quest episode
    if not c then
        if V.rc then V.rc = false; V.hideUntil = now + 1 end   -- the host went silent or stopped sending: show the body again
        return
    end
    c, ts = c == "1", tonumber(ts)
    if c ~= (V.rc or false) then
        V.rc = c
        if c then V.mEp = V.rm; V.cjWant = (V.rk == "d") or nil end   -- b109dlgblock: a new episode; the join waits until it is main (mqpull)
        if c and V.rk == "d" then
            log(S2L.mqOk(V) and "S2CORE cine: host em DIALOGO -> cliente congelado ao lado" or "S2CORE mq: host em DIALOGO fora da missao principal: sem puxar nem congelar (mqpull)")
        elseif c then log("S2CORE cine: host em cinematica -> corpo do host escondido" .. (S2L.mqOk(V) and "" or " (fora da missao principal: sem congelar, mqpull)"))
        else V.hideUntil = now + 1; V.cjWant = nil; V.followUntil = S2L.mqOk(V) and now + 3 or nil; log("S2CORE cine: cinematica do host terminou -> corpo de volta em 1 s" .. (S2L.mqOk(V) and "" or " (fora da missao principal: sem seguir, mqpull)")) end
    end
    if V.cjWant and S2L.mqOk(V) then                     -- b109dlgblock: once per episode, as soon as it is main quest (mqpull=0: at its start, as before)
        V.cjWant = nil
        local r = S2L.remoteIn("cjoin")
        if FL.cjoin == 1 and r and valid(pawn) and now - (r.recv or 0) < 1 then   -- batch 69d: join the talker (same rule as cjoin)
            local q = pawn:K2_GetActorLocation()
            local d = math.sqrt((r.x - q.X) ^ 2 + (r.y - q.Y) ^ 2 + (r.z - q.Z) ^ 2)
            if d > (FL.cjoinm or 1500) then
                local ok   -- b108 CJ1
                if (FL.tpfloor or 0) == 1 then ok = S2L.tpfNow(ST, r, "cjoin dialogo")
                else ok = pcall(function() pawn:K2_SetActorLocation({X = r.x + (FL.cjoff or 150), Y = r.y, Z = r.z + 50}, false, {}, true) end) end
                log(string.format("S2CORE cjoin: dialogo do host a %.0f m -> teleporte ok=%s", d / 100, tostring(ok)))
            end
        end
    end
    if V.rts ~= nil and ts ~= V.rts then V.followUntil = now + 3; log("S2CORE cine: host teleportado (#" .. ts .. ")") end
    V.rts = ts
    if V.followUntil and not c and not V.leashSave and not V.fz then   -- 12b: follow only once the freeze is released (the leash teleport walks the pawn)
        if now > V.followUntil then V.followUntil = nil
        else
            local r = S2L.remoteIn("cine segue")
            if r and valid(pawn) and now - (r.recv or 0) < 1 then
                local q = pawn:K2_GetActorLocation()
                local d = math.sqrt((r.x - q.X) ^ 2 + (r.y - q.Y) ^ 2)
                if d > FL.cinejump then
                    local own = (FL.leashown or 0) == 1          -- b115leash: a pull request; refused = tried again while followUntil lasts
                    if not own or S2L.lrPull(ST, "cine", FL.cinefollow, FL.cinehold, string.format("host a %.0f m", d / 100)) then
                        if not own then V.leashSave = FL.leashm; FL.leashm = FL.cinefollow; V.leashUntil = now + FL.cinehold end
                        V.followUntil = nil
                        log(string.format("S2CORE cine: host a %.0f m -> sigo ja (coleira %s m por %s s)", d / 100, tostring(FL.cinefollow), tostring(FL.cinehold)))
                    end
                end
            end
        end
    end
end

-- ===== DAMAGE IN: apply the other side's #D events to the local player =====
-- Each event once (dedup by seq with a 5 s window: a relaunched host that starts again at seq 1 is accepted; an in.json that
-- stopped changing is re-read every tick and keeps its marks alive), capped per event (dmgmax) and per second (dmgcap),
-- never while dead, downed or in the first dmgspawn seconds of a pawn. Runs on the client; on the host only with dmgtest=1.
-- Nothing goes the other way: the client has no A-Life, so nothing on the client can hurt the host player's body there.
-- ----- batch 12b: client frozen and protected while the host is in a cinematic -----
local cvFresh = true                                   -- first tick of this module load
local function cvPawn()
    local pw = S2.io and S2.io.pawn
    if not valid(pw) then pcall(function() pw = api.UEHelpers.GetPlayerController().Pawn end) end
    return pw
end
function M.cineRestore(ST, why)                        -- undo the freeze; safe to call at any time
    local V = ST.cv; local F = V and V.fz
    if not F then return false end
    V.fz = nil
    local pw, r = cvPawn(), {}
    if valid(pw) then
        if F.move and not (ST.life and ST.life.down and ST.life.frozen) then      -- a downed player stays frozen by the lifeguard
            r[#r + 1] = "mov=" .. tostring(pcall(function() pw.CharacterMovement:SetMovementMode(1, 0) end))
        end
        if F.input then r[#r + 1] = "input=" .. tostring(pcall(function() pw:EnableInput(api.UEHelpers.GetPlayerController()) end)) end
        if F.god and not (ST.nd and ST.nd.armed) then r[#r + 1] = "god=" .. tostring(pcall(function() FindFirstOf("CustomConsoleManagerBuild"):XSetEntityUnkillableByUID(F.god, false) end)) end
    end
    log(string.format("S2CORE cine: cliente LIBERADO (%s) depois de %.1f s %s", tostring(why), os.clock() - F.at, table.concat(r, " ")))
    return true
end
local function cineFreeze(ST)
    local V, now = cvInit(ST), os.clock()
    if cvFresh then cvFresh = false; if V.fz then M.cineRestore(ST, "modulo carregado") end end
    if api.ROLE == "host" and FL.cinetest ~= 1 then return end
    local P = ST.peer or {}
    local want, why = false, "cinematica terminou"
    if FL.cine ~= 1 or FL.cinefreeze ~= 1 then why = "cine/cinefreeze desligado"
    elseif P.state ~= "up" then why = "outro lado em silencio"
    elseif not S2L.mqOk(V) then why = "fora da missao principal (mqpull)"   -- b109dlgblock
    elseif V.rc == true or now < (V.hideUntil or 0) then want = true end
    if V.rc ~= true then V.fzBlock = nil end           -- the host left the cinematic: a later one may freeze again
    if want and V.fzBlock then return end
    if want and V.fz and now - V.fz.at > FL.cinemax then
        M.cineRestore(ST, "tempo maximo de " .. tostring(FL.cinemax) .. " s"); V.fzBlock = true; return
    end
    if not want then if V.fz then M.cineRestore(ST, why) end return end
    local pw = cvPawn(); if not valid(pw) then return end
    if not V.fz then V.fz = {at = now}; log("S2CORE cine: cliente CONGELADO e protegido (host em cinematica)") end
    local F = V.fz
    local addr = pw:GetAddress()
    if F.addr ~= addr then                             -- first tick of the freeze, or a new pawn (load / respawn) during it
        F.addr = addr
        local hp; pcall(function() hp = pw:GetHP() end)
        F.hp = (type(hp) == "number" and hp > 0) and hp or nil
        F.move = pcall(function() pw.CharacterMovement:DisableMovement() end)
        if FL.cineinput == 1 then F.input = pcall(function() pw:DisableInput(api.UEHelpers.GetPlayerController()) end) end
        if FL.cinegod == 1 then
            pcall(function()
                local uid = StaticFindObject("/Script/Stalker2.Default__CppMediator"):GetGUID(pw)
                FindFirstOf("CustomConsoleManagerBuild"):XSetEntityUnkillableByUID(uid, true); F.god = uid
            end)
        end
        log(string.format("S2CORE cine: congelado mov=%s input=%s god=%s hp=%s", tostring(F.move), tostring(F.input), tostring(F.god), tostring(F.hp)))
    end
    pcall(function() if pw.CharacterMovement.MovementMode ~= 0 then pw.CharacterMovement:DisableMovement() end end)   -- something put it back
    if F.hp then
        pcall(function()
            local hp = pw:GetHP()
            -- core-23: with Unkillable on (cinegod) a lethal hit leaves the pawn alive at HP 0 (measured: it then died at the release); refill from 0 too
            if type(hp) == "number" and (hp > 0 or F.god ~= nil) and hp < F.hp then pw:ForceSetHP(F.hp); F.held = (F.held or 0) + 1 end
        end)
    end
    pcall(function() local b = pw:GetBleeding(); if type(b) == "number" and b > 0 then pw:ForceSetBleeding(0.0) end end)
    ST.din.protectUntil = math.max(ST.din.protectUntil or 0, now + 1)     -- #D events are refused while frozen (counted as "prot")
end
local function cineTick(ST)
    local ok, e = pcall(cineTick0, ST)
    cineFreeze(ST)                                     -- runs even if the signal part failed: the restore paths must always run
    if not ok then error(e, 0) end
end
local function dmgApply(pawn, amount)
    if FL.dmgmode == 2 then pawn:ReceiveDamage(amount + 0.0, 6, 0.0, 0.0, 0.0, 0.0, FName("None"))
    else pawn:ReceiveDamageStruct({Damage = amount + 0.0, ArmorDamage = 0.0, ArmorPiercing = FL.dmgap10 / 10, Bleeding = 0.0, BleedingChance = 0.0, BoneHit = FName("None"), DamageType = 6, DamageSource = 2}) end
end
local function dmgIn(ST)
    if FL.dmgin ~= 1 or not (api.ROLE == "client" or FL.dmgtest == 1) then return end
    local pawn = S2.io and S2.io.pawn
    if not valid(pawn) then return end
    local D, now = ST.din, os.clock()
    local addr = pawn:GetAddress()
    if addr ~= D.pawnAddr then                 -- new pawn (first tick, load, respawn): no damage for dmgspawn seconds
        D.pawnAddr = addr; D.protectUntil = now + FL.dmgspawn; D.downUntil = 0
    end
    D.budget = math.min(FL.dmgcap, (D.budget or FL.dmgcap) + FL.dmgcap * (now - (D.budgetAt or now))); D.budgetAt = now
    if now - (D.seen.pruneAt or -10) >= 1 then
        D.seen.pruneAt = now
        for k, at in pairs(D.seen) do if type(k) == "number" and now - at > 5 then D.seen[k] = nil end end
    end
    for ev in (x1part("D") or ""):gmatch("[^|]+") do
        local seq, amt = ev:match("^(%d+),([%d%.]+)$")
        seq, amt = tonumber(seq), tonumber(amt)
        if seq and amt then
            local seenAt = D.seen[seq]
            D.seen[seq] = now                   -- refreshed while the event is still in the snapshot
            if not seenAt then                   -- first sight: decided once, never retried
                local hp, alive = pawn:GetHP(), pawn:IsAlive()
                if not alive or hp <= 0 then D.n.dead = D.n.dead + 1
                elseif now < (D.protectUntil or 0) then D.n.prot = D.n.prot + 1
                elseif now < (D.downUntil or 0) then D.n.down = D.n.down + 1
                else
                    local a = math.min(amt, FL.dmgmax, D.budget)
                    if a < amt then D.n.cap = D.n.cap + 1 end
                    if a > 0 then
                        D.budget = D.budget - a
                        local ok, e = pcall(dmgApply, pawn, a)
                        local hp1 = pawn:GetHP()
                        if FL.dmgfloor > 0 and type(hp1) == "number" and hp1 < FL.dmgfloor then   -- downed instead of dead
                            pcall(function() pawn:ForceSetHP(FL.dmgfloor) end)
                            D.downUntil = now + FL.dmgdown
                            log(string.format("S2CORE dmgin: CAIDO (hp %.1f -> piso %d) por %d s", hp1, FL.dmgfloor, FL.dmgdown))
                            hp1 = pawn:GetHP()
                        end
                        D.n.ok = D.n.ok + 1
                        log(string.format("S2CORE dmgin: seq=%d pedido=%.1f aplicado=%.1f hp %.1f -> %s ok=%s %s", seq, amt, a, hp, tostring(hp1), tostring(ok), ok and "" or tostring(e)))
                    end
                end
            end
        end
    end
    if now - (D.logAt or 0) >= 10 then
        D.logAt = now
        local n = D.n
        if n.ok + n.cap + n.dead + n.prot + n.down > 0 then log(string.format("S2CORE dmgin 10s: aplicados=%d cortados_pelo_teto=%d ignorados morto=%d protecao=%d caido=%d", n.ok, n.cap, n.dead, n.prot, n.down)) end
        D.n = {ok = 0, cap = 0, dead = 0, prot = 0, down = 0}
    end
end

-- ===== SESSION (batch 6, section 15) =====
-- Every snapshot carries "#H:<role>,<mod version>,<module version>,<difficulty>,<launch id>" (state, not an event: a lost packet does not
-- matter). The receiving side keeps ST.peer = {state = none|up|silent, role, mod, core, diff, nonce, sid}:
--   up      a new snapshot arrived in the last FL.peersilent seconds
--   silent  none for longer: x1part() returns nothing (locks released, no life state), main.lua's mirrors are removed once, and the
--           body/target code already hides on "no snapshot for 5 s". The local world is left alone.
--   new session = the header "sid" (main.lua, one per game launch) or the launch id changed: the seen-sequence windows of shots and damage are
--           emptied at once (the 5 s windows would do it anyway), pending lock denial is cleared. Looks need nothing: they are keyed by content
--           hash and the side that restarted asks for what it lacks ("need").
-- Checks, logged once per session: same role on both sides (unless it is our own snapshot coming back = loopback), different mod or module
-- version, the other side's difficulty.
local function helloStr(ST)
    return string.format("#H:%s,%s,%s,%s,%s", tostring(api.ROLE), (tostring(rawget(_G, "MOD_VER") or "?"):gsub("[^%w%.%-_]", "")), (M.VERSION:gsub("[^%w%.%-_]", "")), (tostring(FL.difficulty):gsub("[^%w]", "")), ST.me.nonce)
end
function M.peer(ST) return (ST or S2.state).peer end
local function peerTick(ST)
    local P, now = ST.peer, os.clock()
    local r = api.getRemote and api.getRemote()
    local fresh = r ~= nil and (now - r.recv) < FL.peersilent
    if not fresh then
        if P.state == "up" then
            P.state = "silent"; P.silentAt = now
            log(string.format("S2CORE PEER SILENT: sem snapshot novo ha %d s (travas soltas, corpo/alvo escondidos, espelhos removidos)", FL.peersilent))
            if api.mirrorsClear then pcall(api.mirrorsClear) end
            if ST.lock then ST.lock.denied = nil end
        end
        return
    end
    if not P.live then                                   -- batch 28: the first snapshot seen may be a file left by an earlier session
        if P.firstT == nil then P.firstT = r.t or false; P.live = false elseif (r.t or false) ~= P.firstT then P.live = true end
    end
    local head = S2.io and S2.io.head or ""
    local sid = head:match('"sid"%s*:%s*"(.-)"') or ""
    local x1 = (FL.x1c or 0) == 1 and S2L.x1Raw(head) or head:match('"x1"%s*:%s*"(.-)"') or ""   -- b143perf x1c
    local role, mod, core, diff, nonce = (x1:match("#H:([^#]*)") or ""):match("^([^,]*),([^,]*),([^,]*),([^,]*),([^,]*)$")
    local id = sid .. "/" .. tostring(nonce)
    local was = P.state
    if was ~= "up" then
        P.state = "up"; P.ups = P.ups + 1
        log(string.format("S2CORE PEER UP (#%d)%s", P.ups, was == "silent" and string.format(" depois de %.0f s em silencio", now - (P.silentAt or now)) or ""))
    end
    if id ~= P.id then
        local first = P.id == nil
        P.id, P.sid, P.nonce, P.role, P.mod, P.core, P.diff = id, sid, nonce, role, mod, core, diff
        P.sessions = P.sessions + 1; P.warned = nil
        ST.din.seen = {}; ST.shotSeen = {}                      -- the other game restarted: its sequence numbers start again
        if ST.qs then ST.qs.seen = {} end                        -- batch 7: so do its quest event numbers
        if ST.it then ST.it.seen = {} end                        -- batch 9: and its item take-back numbers
        if ST.ss then ST.ss.seen = {} end                        -- batch 17: and its stash operations
        if ST.art then ST.art.seen = {} end                      -- batch 18: and its artifact take numbers
        if ST.cv then ST.cv.rts = nil; ST.cv.rc = false; ST.cv.hideUntil = 0 end   -- batch 12/12b: new session: teleport count forgotten, cinematic state cleared (freeze released)
        if ST.lock then ST.lock.denied = nil end
        log(string.format("S2CORE PEER SESSION #%d%s: papel=%s mod=%s modulo=%s dificuldade=%s id=%s", P.sessions, first and "" or " (o outro lado relancou)", tostring(role), tostring(mod), tostring(core), tostring(diff), id))
    end
    if not P.warned then
        P.warned = true
        P.loopback = nonce ~= nil and nonce == ST.me.nonce
        if role == nil then log("S2CORE PEER AVISO: o outro lado nao manda #H (modulo antigo): versoes nao conferidas")
        elseif P.loopback then log("S2CORE PEER: e o nosso proprio snapshot (loopback); papel nao conferido")
        else
            if role == tostring(api.ROLE) then log("S2CORE PEER ERRO: os dois lados estao com papel=" .. role .. " (um tem de ser host e o outro client: role.txt)") end
            if mod ~= (tostring(rawget(_G, "MOD_VER") or "?"):gsub("[^%w%.%-_]", "")) then log("S2CORE PEER AVISO: main.lua diferente (aqui " .. tostring(rawget(_G, "MOD_VER")) .. ", la " .. tostring(mod) .. ")") end
            if core ~= (M.VERSION:gsub("[^%w%.%-_]", "")) then log("S2CORE PEER AVISO: s2core.lua diferente (aqui " .. M.VERSION .. ", la " .. tostring(core) .. ")") end
        end
    end
end

-- ===== batch 31: dropped beside the host on every join (client) =====
local function joinTick(ST)
    if api.ROLE ~= "client" then return end
    local J = ST.join or {}; ST.join = J
    if rawget(_G, "S2_loadFlags_orig") == nil and type(rawget(_G, "S2_loadFlags")) == "function" then   -- batch 36: the 5 s flags reload must not undo the park
        local orig = rawget(_G, "S2_loadFlags")
        rawset(_G, "S2_loadFlags_orig", orig)
        rawset(_G, "S2_loadFlags", function(quiet)
            local JJ = S2.state and S2.state.join
            local parked = JJ and JJ.parked
            if parked then FL.leashm = parked end            -- the reload compares with the real value: no "1e9 -> 350", no reset
            local r = orig(quiet)
            if parked and JJ.parked then JJ.parked = FL.leashm; FL.leashm = 1e9 end   -- a new value in flags.txt is the one to restore
            return r
        end)
    end
    do                                                   -- batch 33/36: no leash toward a silent or absent partner, nor while frozen for a cinematic
        local P0 = ST.peer
        local up = P0 and P0.state == "up" and P0.live ~= false
        local V0 = ST.cv
        local frozen = V0 and V0.fz ~= nil                               -- no teleport at all while frozen, the cinematic's own follow included
        local why = (not up and "parceiro ausente") or (frozen and "congelado na cinematica do host") or nil
        if (FL.leashown or 0) == 1 then                  -- b115leash: a HARD park request (beats every pull); no save/restore/repair here
            if why then S2L.lrSet(ST, "join_park", 1e9, nil, why, true) else S2L.lrDrop(ST, "join_park", "parceiro de volta / descongelado") end
        elseif why then
            if FL.leashm ~= 1e9 then                     -- parked, and kept parked if another switch (cinematic follow) lowers it meanwhile
                if not J.parked then J.parked = FL.leashm end
                FL.leashm = 1e9
            end
            if J.parkWhy ~= why then J.parkWhy = why; log("S2CORE join: " .. why .. ": coleira parada (era " .. tostring(J.parked) .. " m)") end
        elseif J.parked then
            if FL.leashm == 1e9 then FL.leashm = J.parked end
            for _, o in ipairs({ST.cv or false, ST.nd or false, J}) do               -- a switch that started meanwhile took the parked value as "the old one"
                if o and o.leashSave == 1e9 then o.leashSave = J.parked end
            end
            log("S2CORE join: " .. (J.parkWhy or "?") .. " acabou: coleira em " .. tostring(FL.leashm) .. " m"); J.parked = nil; J.parkWhy = nil
        end
    end
    if FL.gsnap == 1 then                                -- batch 36: a teleport that left the client under the ground
        local pw0, r0 = S2L.ioPawn(), S2L.remoteIn("gsnap")
        local p0; pcall(function() p0 = pw0:K2_GetActorLocation() end)
        local now0 = os.clock()
        if p0 then
            if J.gp and ((p0.X - J.gp.X) ^ 2 + (p0.Y - J.gp.Y) ^ 2) > 5000 ^ 2 then J.snapUntil = now0 + 30 end
            J.gp = {X = p0.X, Y = p0.Y}
            if J.snapUntil and now0 < J.snapUntil and r0 and r0.z and p0.Z < r0.z - 300 then
                J.lowAt = J.lowAt or now0
                if now0 - J.lowAt >= 3 then
                    local ok   -- b108 GS
                    if (FL.tpfloor or 0) == 1 then ok = S2L.tpfNow(ST, r0, "gsnap")
                    else
                        ok = pcall(function() pw0:K2_SetActorLocation({X = p0.X, Y = p0.Y, Z = r0.z + 150}, false, {}, true) end)
                        pcall(function() pw0.CharacterMovement:SetMovementMode(5, 0) end)
                        J.walkAt = now0 + 1
                    end
                    J.lowAt = nil; J.snapUntil = nil
                    log(string.format("S2CORE join: o cliente ficou %.0f cm abaixo do host por 3 s depois de um teleporte -> posto em z do host + 150 ok=%s", r0.z - p0.Z, tostring(ok)))
                end
            else J.lowAt = nil end
        end
        if J.walkAt and now0 >= J.walkAt then J.walkAt = nil; pcall(function() pw0.CharacterMovement:SetMovementMode(1, 0) end) end
    end
    local now = os.clock()
    local function restore(why)
        if not J.leashSave then return end
        local back, temp = J.leashSave, J.leashTemp
        J.leashSave = nil; J.leashTemp = nil
        if FL.leashm == temp then FL.leashm = back end                                -- still our value: put the old one back
        for _, o in ipairs({ST.cv or false, ST.nd or false}) do                        -- another switch (cinematic follow, no-death) took OUR value as "the old one": hand it the real one
            if o and o.leashSave == temp then o.leashSave = back end
        end
        log("S2CORE join: coleira de volta a " .. tostring(back) .. " m (" .. why .. ")")
    end
    if FL.joinport ~= 1 then restore("joinport desligado"); J.want = nil; return end
    local pw, P = S2.io and S2.io.pawn, ST.peer
    if not valid(pw) or not P then return end
    local addr = pw:GetAddress()
    if addr ~= J.addr then if J.addr ~= nil then J.want = "save carregado" end; J.addr = addr end
    if P.state ~= "up" or P.live == false then restore("host em silencio"); return end
    if P.sessions ~= J.sess then J.sess = P.sessions; J.want = "sessao do host #" .. tostring(P.sessions) end
    local r, lp = S2L.remoteIn("join"), S2.io.p
    if not (r and r.x and lp) then return end
    local d = math.sqrt((r.x - lp.X) ^ 2 + (r.y - lp.Y) ^ 2) / 100
    if J.leashSave and now > J.untilT then restore("pedido feito") end      -- 1 s is enough: main.lua's leash starts its teleport on the first tick
    if J.waitT then                                      -- the teleport of main.lua loads the place first: measured about 12 s
        if d <= FL.joinm + 5 then log(string.format("S2CORE join: ao lado do host (%.0f m)", d)); restore("chegou"); J.want = nil; J.waitT = nil; J.tries = nil
        elseif now > J.waitT then
            restore("sem efeito"); J.waitT = nil; J.tries = (J.tries or 0) + 1
            if J.tries >= 3 then J.want = nil; J.tries = nil; log("S2CORE join: teleporte ao host nao funcionou em 3 tentativas; desisto ate o proximo join") end
        end
        return
    end
    if not J.want then return end
    local V, N = ST.cv, ST.nd
    if (V and (V.fz ~= nil or V.rc == true or V.leashSave)) or (N and N.leashSave) then return end      -- never during a cinematic freeze or another leash switch: wait
    if d <= FL.joinm then log(string.format("S2CORE join: %s; host a %.0f m: sem teleporte", J.want, d)); J.want = nil; J.tries = nil; return end
    if (FL.leashown or 0) == 1 then                      -- b115leash: a 1 s pull request; refused = J.want waits (asked again next tick)
        if not S2L.lrPull(ST, "join", FL.joinm, 1, J.want) then return end
    else J.leashSave = FL.leashm; J.leashTemp = FL.joinm; FL.leashm = FL.joinm end
    J.untilT = now + 1; J.waitT = now + 60
    log(string.format("S2CORE join: %s; host a %.0f m -> teleporte para junto do host (coleira em %s m por 1 s)", J.want, d, tostring(FL.joinm)))
end
-- ===== batch 36: one weather for both worlds =====
local function wxTick(ST)
    if api.ROLE ~= "client" or FL.wxfollow ~= 1 then return end
    local P = ST.peer
    if not (P and P.state == "up" and P.live ~= false) then return end
    local W = ST.wx or {}; ST.wx = W
    local rwx = tonumber(((S2.io and S2.io.head or ""):match('"wx"%s*:%s*(%-?%d+)')))
    local mine                                                     -- read here, not main.lua's S2_wx (refreshed only every 5 s)
    pcall(function()
        local w = W.wc
        if not (w and w:IsValid()) then w = FindFirstOf("WeatherController"); W.wc = w end
        mine = w.CurrentWeatherData.TargetWeather
    end)
    mine = tonumber(mine)
    if not rwx or rwx < 0 or rwx > 9 or not mine then return end
    local now = os.clock()
    local function lock(why)                                       -- batch 38: the client's own selector must never pick again
        if FL.wxlock ~= 1 then return end
        local before, after
        pcall(function() before = W.wc.CurrentWeatherData.RequestedWeatherDuration end)
        pcall(function() W.wc.CurrentWeatherData.RequestedWeatherDuration = 1e9 end)
        pcall(function() after = W.wc.CurrentWeatherData.RequestedWeatherDuration end)
        W.lockAt = now + 10
        if tonumber(before) ~= tonumber(after) then log(string.format("S2CORE clima: seletor travado (%s): duracao pedida %s -> %s", why, tostring(before), tostring(after))) end
    end
    local pw = S2.io and S2.io.pawn
    local paddr = valid(pw) and pw:GetAddress() or nil
    local fresh = false
    if paddr and paddr ~= W.paddr then W.paddr = paddr; fresh = true; W.done = nil; lock("pawn novo") end
    if P.sessions ~= W.sess then W.sess = P.sessions; fresh = true; W.done = nil end
    if fresh then W.instant = true end                             -- the first switch of a pawn / session is instant
    if now >= (W.lockAt or 0) then
        local cur; pcall(function() cur = W.wc.CurrentWeatherData.RequestedWeatherDuration end)
        if tonumber(cur) and tonumber(cur) < 1e8 and mine ~= 8 and mine ~= 7 then lock("o jogo escolheu de novo") else W.lockAt = now + 10 end
    end
    if W.checkAt and now >= W.checkAt then                       -- batch 38: did the switch take?
        local want = W.checkWant; W.checkAt = nil
        if mine == want then log(string.format("S2CORE clima: troca para %d confirmada (%s)", want, W.checkHow))
        elseif not W.retried then
            W.retried = true
            local ok = pcall(function() FindFirstOf("CustomConsoleManagerDR"):XForceWeather(want) end)
            W.checkAt = now + 2; W.checkHow = "XForceWeather (nova tentativa)"
            log(string.format("S2CORE clima: troca para %d NAO pegou em 2 s (alvo %d) -> XForceWeather(%d) ok=%s", want, mine, want, tostring(ok)))
        else log(string.format("S2CORE clima: troca para %d NAO pegou nem com XForceWeather (alvo %d)", want, mine)) end
    end
    if rwx ~= W.host then W.host = rwx; W.done = nil; W.retried = nil end   -- a new host type: one switch is owed
    if mine ~= W.mine then
        if W.mine ~= nil and mine ~= W.setTo then W.instant = true end   -- batch 38: changed by itself (not by us): drift, corrected at once
        W.mine = mine; W.done = nil
    end
    rawset(_G, "S2_wxAt", now + 60)                                -- batch 38: main.lua's once-a-minute weather sync stays off while this rule runs (it copied the 8)
    if rwx == 8 then rwx = mine end                                -- batch 38: the host's CalmBeforeEmission (8) is not copied (the client would sit in it 120 s)
    if rwx == 7 and mine ~= 7 then W.instant = true end            -- the host's emission weather is taken at once: both emissions start together
    local hard = (mine == 7) ~= (rwx == 7)                         -- emission weather on one side only
    if mine ~= rwx and now >= (W.next or 0) and (not W.done or (hard and now >= (W.retryAt or 0))) then
        W.next = now + 2; W.done = true; W.retryAt = now + 60
        local inst = FL.wxinstant == 1 and W.instant
        local how = inst and "XForceWeather" or "XSwitchToWeather"
        local ok = pcall(function() local dr = FindFirstOf("CustomConsoleManagerDR"); if inst then dr:XForceWeather(rwx) else dr:XSwitchToWeather(rwx) end end)
        W.instant = nil; W.setTo = rwx
        rawset(_G, "S2_wxAt", now + 60)                            -- main.lua's once-a-minute sync waits for it
        log(string.format("S2CORE clima: host em %d, eu em %d -> %s(%d) ok=%s%s", rwx, mine, how, rwx, tostring(ok), hard and " (clima de emissao so de um lado)" or ""))
        W.checkAt = now + 2; W.checkWant = rwx; W.checkHow = how
        if rwx ~= 7 then lock("depois da troca") end
    end
end
-- ===== LIFE GUARD (batch 6, section 18): downed instead of dead =====
-- lifeguard=1: when the local player's HP is at or below FL.downhp (from anything: enemies on the host, events on the client, a fall, an
-- anomaly) the player is DOWNED: HP is held at downhp every tick, walking is switched off (downfreeze=1), the life state sent is "w" (the
-- other side stops forwarding hits to its target and applies no damage here). The player gets up with revivehp after downsecs, or earlier
-- when the other player is alive and within reviveradius for revivesecs. A hit that takes all the HP between two 100 ms ticks still kills:
-- then the game's own death screen follows (client: load its save, the leash pulls it back; host: the session returns to the host's last save).
-- b147downmsg: the downed player's own on-screen line (host AND client). Same UMG TextBlock as S2L.qdrSay (b130). A NEW widget per downed
-- period; removed when the player gets up / dies / lifeguard stops; after a pawn change the old widget is only forgotten, never touched
-- (the old world's viewport is gone). Text changes at most once per second (N is a whole number). L.dm = {w, addr, txt, gone}.
function S2L.downMsg(L, pw, now, off)
    local d = L.dm
    local okp = valid(pw)
    local addr = okp and pw:GetAddress() or nil
    if off or not L.down or not okp then
        if d then
            L.dm = nil
            if d.w and addr and d.addr == addr then pcall(function() if d.w:IsValid() then d.w:RemoveFromParent() end end) end
        end
        return
    end
    if d and (d.gone or d.addr ~= addr) then L.dm = {gone = true, addr = d.addr}; return end   -- pawn changed: forget it, no new widget this period
    local n = math.max(0, math.ceil(FL.downsecs - (now - L.down.at)))
    local txt = string.format("DOWNED - you get up in %d s, or when the other player comes within %g m", n, FL.reviveradius / 100)
    if d and d.txt == txt then return end
    if not d then
        d = {addr = addr}; L.dm = d
        log("S2CORE LIFE: mensagem na tela: " .. txt .. ((FL.downmsg or 1) == 1 and "" or " (downmsg=0: so log)"))
        if (FL.downmsg or 1) ~= 1 then d.txt = txt; return end
        local ok, e = pcall(function()
            local pc = api.UEHelpers.GetPlayerController()
            if not valid(pc) then return end
            local cU, cT, cB = StaticFindObject("/Script/UMG.UserWidget"), StaticFindObject("/Script/UMG.WidgetTree"), StaticFindObject("/Script/UMG.TextBlock")
            if not (valid(cU) and valid(cT) and valid(cB)) then return end
            local w = StaticConstructObject(cU, pc); if not valid(w) then return end
            local tr = StaticConstructObject(cT, w); if not valid(tr) then return end
            local tb = StaticConstructObject(cB, tr); if not valid(tb) then return end
            w.WidgetTree = tr; tr.RootWidget = tb
            tb:SetText(FText(txt))
            pcall(function() tb:SetJustification(1) end)
            pcall(function() tb:SetShadowOffset({X = 2, Y = 2}) end)
            pcall(function() tb:SetShadowColorAndOpacity({R = 0, G = 0, B = 0, A = 1}) end)
            pcall(function() tb:SetRenderTranslation({X = 0, Y = 160}) end)
            w:SetVisibility(3)                                   -- HitTestInvisible
            w:AddToViewport(1000)
            d.w = w; d.tb = tb
        end)
        if not ok then log("S2CORE LIFE: mensagem erro " .. tostring(e)) end
    elseif d.tb then
        pcall(function() if d.tb:IsValid() then d.tb:SetText(FText(txt)) end end)
    end
    d.txt = txt
end
local function lifeGuard(ST)
    if ST.nd and ST.nd.dead then S2L.downMsg(ST.life, S2.io and S2.io.pawn, 0, true); return end   -- batch 13: a 'dead' client is handled by ndTick until its respawn
    local L, D, now = ST.life, ST.din, os.clock()
    local pw = S2.io and S2.io.pawn
    if not valid(pw) then pcall(function() pw = api.UEHelpers.GetPlayerController().Pawn end) end   -- before the first snapshot of the other side arrives
    if FL.lifeguard ~= 1 or not valid(pw) then
        S2L.downMsg(L, pw, now, true)
        if L.down and valid(pw) and L.frozen then pcall(function() pw.CharacterMovement:SetMovementMode(1, 0) end) end
        L.down = nil; L.frozen = nil
        return
    end
    local hp, alive = pw:GetHP(), pw:IsAlive()
    if type(hp) ~= "number" or not alive or hp <= 0 then
        if L.down then log(string.format("S2CORE LIFE: MORREU ENQUANTO CAIDO (hp=%s alive=%s, caido ha %.1f s, ultimo sangramento=%s)", tostring(hp), tostring(alive), now - L.down.at, tostring(L.lastBleed))) end
        S2L.downMsg(L, pw, now, true)
        L.down = nil; L.frozen = nil; return
    end   -- really dead: the game handles it
    if not L.down then
        if hp <= FL.downhp then
            L.down = {at = now, near = nil}; L.downs = (L.downs or 0) + 1
            D.downUntil = now + FL.downsecs                    -- the same field the damage receiver and the life state use
            pcall(function() pw:ForceSetHP(FL.downhp) end)
            pcall(function() local b = pw:GetBleeding(); L.lastBleed = b; if type(b) == "number" and b > 0 then pw:ForceSetBleeding(0.0) end end)
            if FL.downfreeze == 1 then L.frozen = pcall(function() pw.CharacterMovement:DisableMovement() end) end
            log(string.format("S2CORE LIFE: CAIDO #%d (hp %.1f <= %d); levanta em %d s ou com o outro jogador a %d cm por %d s", L.downs, hp, FL.downhp, FL.downsecs, FL.reviveradius, FL.revivesecs))
            S2L.downMsg(L, pw, now)
        end
        return
    end
    if hp < FL.downhp then pcall(function() pw:ForceSetHP(FL.downhp) end) end      -- held: nothing finishes a downed player
    pcall(function() local b = pw:GetBleeding(); L.lastBleed = b; if type(b) == "number" and b > 0 then pw:ForceSetBleeding(0.0) end end)   -- core-19d: no bleeding while down
    S2L.downMsg(L, pw, now)
    local why
    local r, lp = api.getRemote and api.getRemote(), api.getLocal and api.getLocal()
    local st = M.remoteLife()
    if r and lp and st == "a" and (now - r.recv) < 2 and (r.x - lp.X)^2 + (r.y - lp.Y)^2 <= FL.reviveradius^2 then
        L.down.near = L.down.near or now
        if now - L.down.near >= FL.revivesecs then why = "reanimado pelo outro jogador" end
    else L.down.near = nil end
    if not why and now - L.down.at >= FL.downsecs then why = "tempo" end
    if why then
        pcall(function() pw:ForceSetHP(FL.revivehp) end)
        if L.frozen then pcall(function() pw.CharacterMovement:SetMovementMode(1, 0) end) end
        D.downUntil = 0; D.protectUntil = now + FL.dmgspawn     -- a few seconds without event damage after getting up
        log(string.format("S2CORE LIFE: DE PE (%s) depois de %.1f s, hp=%d", why, now - L.down.at, FL.revivehp))
        S2L.downMsg(L, pw, now, true)
        L.down = nil; L.frozen = nil
    else
        D.downUntil = math.max(D.downUntil or 0, now + 1)       -- stays "w" while down
    end
end
-- ===== core-16: loot a host corpse from the client (minimal slice). Messages in x1 (no '#' or '"' inside):
--   #C:<id>@<x>,<y>,<z>|...   host -> client: dead humans near the host player or the client (max 4), refreshed every second
--   #Q:<seq>;<id>             client -> host: "give me the loot of <id>" (repeated 3 s, deduped by seq)
--   #R:<seq>;<id>;<sid>x<n>|...   host -> client: the items found (repeated 3 s); the client adds them to its player
-- Nothing in the game's dump removes an item from a corpse, so the host marks the corpse "given away" and closes its own loot window on it.
local LOOT_SIDS = {"A012A","A012D","A012E","A045A","A045D","A045E","A545A","A545D","A545E","A556A","A556D","A556E","A556S","A762A","A762D","A762E","A762NATOA","A762NATOD","A762NATOS","A762SniperA","A762SniperD","A762SniperS","A918A","A918D","A919A","A919D","A939A","A939D","A939E","A939S","AArtifactWeirdBall","AArtifactWeirdBolt","AArtifactWeirdFlower","AArtifactWeirdKettle","AArtifactWeirdNut","AArtifactWeirdWater","AGA","AHEDP","AK74_DefaultMuz","AKU_DefaultMuz","APG7V","AVOG","Anomaly_Scientific_Armor","Anomaly_Scientific_Armor_PSY_preinstalled","AntiRad","ArmyMedkit","Bandage","BattleExoskeleton_Varta_Armor","Battle_Dolg_Armor","Battle_Dolg_End_Armor","Battle_Military_Helmet","Battle_Monolith_Armor","Battle_Spark_Armor","Battle_Svoboda_Armor","Battle_Varta_Armor","Bear","Beer","Binoculars_01","Binoculars_02","Binoculars_03","BlinddogLoot","BloodsuckerLoot","BoarLoot","BottomRailM701","Bread","BurerLoot","CArtifactBouncyBall","CArtifactBouncyBall_Fake","CArtifactBubble","CArtifactBubble_Fake","CArtifactBun","CArtifactBun_Fake","CArtifactBung","CArtifactBung_Fake","CArtifactChunkMeat","CArtifactChunkMeat_Fake","CArtifactCottonWool","CArtifactCottonWool_Fake","CArtifactCrystalThorn","CArtifactCrystalThorn_Fake","CArtifactDevilsMushroom","CArtifactDevilsMushroom_Fake","CArtifactEchinus","CArtifactEchinus_Fake","CArtifactKryptonite","CArtifactKryptonite_Fake","CArtifactLiquidStone","CArtifactLiquidStone_Fake","CArtifactMica","CArtifactMica_Fake","CArtifactPellicle","CArtifactPellicle_Fake","CArtifactPlasticine","CArtifactPlasticine_Fake","CArtifactRosin","CArtifactRosin_Fake","CArtifactSlime","CArtifactSlime_Fake","CArtifactSlug","CArtifactSlug_Fake","CArtifactThorn","CArtifactThorn_Fake","CPrologArtifactSlug","CannedFood","CatLoot","ChimeraLoot","Cinnamon","ControllerLoot","DeerLoot","DefaultScopeGrim","Default_Military_Armor","Dnipro_DefaultMuz","EArtifactAtom","EArtifactAtom_Fake","EArtifactBattery","EArtifactBattery_Fake","EArtifactChocolate","EArtifactChocolate_Fake","EArtifactCloud","EArtifactCloud_Fake","EArtifactCrystalGlass","EArtifactCrystalGlass_Fake","EArtifactDope","EArtifactDope_Fake","EArtifactDummy","EArtifactDummy_Fake","EArtifactFlash","EArtifactFlash_Fake","EArtifactJellyFish","EArtifactJellyFish_Fake","EArtifactMoonlight","EArtifactMoonlight_Fake","EArtifactRazor","EArtifactRazor_Fake","EArtifactSnowflake","EArtifactSnowflake_Fake","EArtifactSoul","EArtifactSoul_Fake","EArtifactSparkler","EArtifactSparkler_Fake","EArtifactThunderHedgehog","EArtifactThunderHedgehog_Fake","EArtifactTow","EArtifactTow_Fake","EArtifactWorm","EArtifactWorm_Fake","EN_AutMuz_1","EN_AutMuz_2","EN_BuckLaunch_1","EN_BuckLaunch_Data","EN_ColimScope_1","EN_GLaunch_1","EN_GoloScope_1","EN_Grip_1","EN_Grip_2","EN_Grip_3","EN_Laser_1","EN_Silen_1","EN_Silen_2","EN_Silen_3","EN_Silen_4","EN_SinMuz_1","EN_SinMuz_2","EN_SinMuz_3","EN_SinMuz_4","EN_SinMuz_5","EN_X2Scope_1","EN_X3Scope_1","EN_X4Scope_1","EN_X8Scope_1","Echo","EcoMedkit","Energetic","Energetic_Limited","Exoskeleton_Dolg_Armor","Exoskeleton_Mercenaries_Armor","Exoskeleton_Monolith_Armor","Exoskeleton_Neutral_Armor","Exoskeleton_Svoboda_Armor","FArtifactBakedBolts","FArtifactBakedBolts_Fake","FArtifactBurntHunk","FArtifactBurntHunk_Fake","FArtifactCandle","FArtifactCandle_Fake","FArtifactCore","FArtifactCore_Fake","FArtifactCrystal","FArtifactCrystal_Fake","FArtifactDeadSponge","FArtifactDeadSponge_Fake","FArtifactDrops","FArtifactDrops_Fake","FArtifactEye","FArtifactEye_Fake","FArtifactFireBall","FArtifactFireBall_Fake","FArtifactFireworks","FArtifactFireworks_Fake","FArtifactGlass","FArtifactGlass_Fake","FArtifactHellishHedgehog","FArtifactHellishHedgehog_Fake","FArtifactMomsBeads","FArtifactMomsBeads_Fake","FArtifactPlasma","FArtifactPlasma_Fake","FArtifactResin","FArtifactResin_Fake","FArtifactRingOmnipotence","FArtifactRingOmnipotence_Fake","FArtifactSteak","FArtifactSteak_Fake","FleshLoot","Fora_DefaultMuz","FreshBread","G37_BottomRail","G37_DefaultMuz","G37_IronSight","G37_ScopeRail","GArtifactBloodStone","GArtifactBloodStone_Fake","GArtifactBud","GArtifactBud_Fake","GArtifactCompass","GArtifactCompass_Fake","GArtifactGoldFish","GArtifactGoldFish_Fake","GArtifactGraphiteBlock","GArtifactGraphiteBlock_Fake","GArtifactGravy","GArtifactGravy_Fake","GArtifactHedgehog","GArtifactHedgehog_Fake","GArtifactLandSlug","GArtifactLandSlug_Fake","GArtifactNightStar","GArtifactNightStar_Fake","GArtifactPlane","GArtifactPlane_Fake","GArtifactRubiksCube","GArtifactRubiksCube_Fake","GArtifactSplitStone","GArtifactSplitStone_Fake","GArtifactSponge","GArtifactSponge_Fake","GArtifactSpring","GArtifactSpring_Fake","GArtifactStoneDrop","GArtifactStoneDrop_Fake","GArtifactTrunk","GArtifactTrunk_Fake","GArtifactWrenched","GArtifactWrenched_Fake","GP3A_Ironsight","Gauss_DefaultMuz","Gauss_Scope","Gilka","GrenadeF1","GrenadeRGD5","GuardGunAK74_ST","GuardGunAKU_PP","GuardGunAPB_HG","GuardGunBucket_PP","GuardGunD12_SG","GuardGunDnipro_ST","GuardGunFora_ST","GuardGunG37_ST","GuardGunGP3A_DMR","GuardGunGrim_ST","GuardGunGvintar_ST","GuardGunIntegral_PP","GuardGunKharod_ST","GuardGunLavina_ST","GuardGunM10_HG","GuardGunM16_ST","GuardGunM701_SP","GuardGunM860_SG","GuardGunMark_SP","GuardGunObrez_SG","GuardGunPKP_MG","GuardGunPM_HG","GuardGunRam2_SG","GuardGunRhino_HG","GuardGunSKP_DMR","GuardGunSPSA_SG","GuardGunSVDM_SP","GuardGunSVU_SP","GuardGunTOZ_SG","GuardGunUDP_HG","GuardGunViper_PP","GuardGunZubr_PP","GuitarUsable","GunAK74_Korshunov_ST","GunAK74_MagDefault","GunAK74_MagIncreased","GunAK74_Phantom_ST","GunAK74_ST","GunAK74_Strelok_ST","GunAKU_MagDefault","GunAKU_PP","GunAKU_Silence_MagDefault","GunAK_MagPaired","GunAPB_HG","GunAPB_MagDefault","GunAPB_MagIncreased","GunArevPrecise_AR","GunArevPrecise_MagDefault","GunArev_DefaultMuz","GunArev_MagDefault","GunArev_ST","GunBucket_MagDefault","GunBucket_MagIncreased","GunBucket_PP","GunBucket_TopRail","GunD12_MagDefault","GunD12_MagIncreased","GunD12_MagLarge","GunD12_MagPaired","GunD12_SG","GunDecider_MagDefault","GunDnipro_MagDefault","GunDnipro_MagPaired","GunDnipro_ST","GunDrowned_GLaunch","GunDrowned_MagPaired","GunFora230_DefaultMuz","GunFora230_MagDefault","GunFora230_MagIncreased","GunFora230_PP","GunFora_MagDefault","GunFora_MagIncreased","GunFora_ST","GunG37V2_MagPaired","GunG37V2_ST","GunG37V2_Scope","GunG37_ST","GunGP37_MagDefault","GunGP37_MagLarge","GunGP37_MagPaired","GunGP3A_DMR","GunGP3A_DefaultMuz","GunGP3A_MagDefault","GunGP3A_MagIncreased","GunGP3A_MagLarge","GunGauss_MagDefault","GunGauss_MagIncreased","GunGauss_SP","GunGauss_Scar_MagHuge","GunGauss_Scar_SP","GunGonta_SP","GunGrim_GLaunch","GunGrim_MagDefault","GunGrim_MagIncreased","GunGrim_MagLarge","GunGrim_ST","GunGvintar_MagDefault","GunGvintar_MagIncreased","GunGvintar_ST","GunIntegral_FrontExtension","GunIntegral_MagDefault","GunIntegral_MagIncreased","GunIntegral_PP","GunKharod_MagDefault","GunKharod_MagPaired","GunKharod_ST","GunKora_HG","GunKora_MagDefault","GunKora_MagIncreased","GunLavina_MagDefault","GunLavina_MagIncreased","GunLavina_ST","GunM10_HG","GunM10_MagDefault","GunM10_MagIncreased","GunM16_MagDefault","GunM16_MagIncreased","GunM16_ST","GunM701_MagDefault","GunM701_SP","GunM860_MagLarge","GunM860_SG","GunMark_MagDefault","GunMark_MagIncreased","GunMark_SP","GunNightStalker_HG","GunNightStalker_MagIncreased","GunNovator_MagDefault","GunObrez_SG","GunPKP_Korshunov_MG","GunPKP_Korshunov_MagLarge","GunPKP_MG","GunPKP_MagDefault","GunPKP_MagIncreased","GunPKP_MagLarge","GunPM_HG","GunPM_MagDefault","GunPM_MagIncreased","GunPM_PDA_Tutorial_HG","GunPM_Screw","GunRam2_SG","GunRhino_HG","GunRpg7_GL","GunS15_MagDefault","GunSKP_DMR","GunSKP_MagDefault","GunSKP_MagIncreased","GunSPSA_SG","GunSVDM_MagDefault","GunSVDM_SP","GunSVU_MagDefault","GunSVU_MagIncreased","GunSVU_SP","GunSVU_ScopeRail","GunSVU_Sniper_Duga_SP","GunSotnyk_MagDefault","GunSpitfire_MagDefault","GunTOZ_Gonta","GunTOZ_SG","GunTank_MagLarge","GunThreeLine_SP","GunThreeLine_Scope","GunUDP_HG","GunUDP_MagDefault","GunUDP_MagIncreased","GunViper_MagDefault","GunViper_MagIncreased","GunViper_PP","GunZubr_MagDefault","GunZubr_MagIncreased","GunZubr_PP","Gun_Cavalier_SR","Gun_Combatant_AR","Gun_Deadeye_HG","Gun_Decider_AR","Gun_Drowned_AR","Gun_Encourage_HG","Gun_GStreet_HG","Gun_GStreet_MagIncreased","Gun_Krivenko_HG","Gun_Lummox_AR","Gun_Lynx_SR","Gun_Merc_AR","Gun_Partner_SR","Gun_Predator_SG","Gun_ProjectY_HG","Gun_Rail_SG","Gun_RatKiller_MagDefault","Gun_RatKiller_SMG","Gun_S15_AR","Gun_SOFMOD_AR","Gun_Shakh_MagIncreased","Gun_Shakh_SMG","Gun_Sharpshooter_AR","Gun_Silence_ColimScope","Gun_Silence_SMG","Gun_Silence_Silen","Gun_SkifGun_HG","Gun_Sledgehammer_SG","Gun_Sledgehummer_GoloScope","Gun_Sotnyk_AR","Gun_Sotnyk_ColimScope","Gun_Spitfire_SMG","Gun_Spitter_SMG","Gun_Star_HG","Gun_Tank_MG","Gun_Texas_SG","Gun_Trophy_AR","Gun_Unknown_AR","Gun_Whip_SR","Gvintar_Scope","HP_Laser_1","HP_Laser_2","Heavy2_Military_Armor","HeavyAnomaly_Monolith_Armor","HeavyAnomaly_Scientific_Armor","HeavyAnomaly_Spark_Armor","HeavyBattle_Spark_Armor","HeavyExoskeleton_Dolg_Armor","HeavyExoskeleton_Monolith_Armor","HeavyExoskeleton_Svoboda_Armor","Heavy_Dolg_Armor","Heavy_Duty_Helmet","Heavy_Mercenaries_Armor","Heavy_Military_Helmet","Heavy_Svoboda_Armor","Heavy_Svoboda_Helmet","Heavy_Varta_Helmet","Hercules","Ironsights_01","Ironsights_02","Ironsights_03","Ironsights_04","Ironsights_05","Jacket_Bandit_Armor","Jemmy_Neutral_Armor","Kharod_DefaultMuz","Light_Bandit_Helmet","Light_Duty_Helmet","Light_Mercenaries_Armor","Light_Mercenaries_Helmet","Light_Military_Helmet","Light_Neutral_Helmet","M16_DefaultMuz","M701_Colim_Scope","M701_Scope","M860LongGrip","M860ShortGrip","MagFedM860","Mark_DefaultMuz","Medkit","MeleeShovel","MeleeStub","Middle_Bandit_Armor","Milk","MoneyCommon","MoneyEpic","MoneyLegendary","MoneyRare","NPC_Agata_Armor","NPC_Anomaly_Spark_Armor","NPC_Barmen_Armor","NPC_Battle_Noon_Armor","NPC_Batya_Armor","NPC_Cloak_Heavy_Military_Armor","NPC_Cloak_Heavy_Neutral_Armor","NPC_Dalin_Armor","NPC_Dekhtyarev_Armor","NPC_Exoskeleton_Coprs_Armor","NPC_Exoskeleton_Noon_Armor","NPC_Faust_Armor","NPC_Heavy2_Coprs_Armor","NPC_Heavy3Exoskeleton_Coprs_Armor","NPC_Heavy3_Corps_Armor","NPC_HeavyAnomaly_Noon_Armor","NPC_HeavyExoskeleton_Mercenaries_Armor","NPC_HeavyExoskeleton_Noon_Armor","NPC_HeavyExoskeleton_Spark_Armor","NPC_Heavy_Corps_Armor","NPC_Heavy_Military_Armor","NPC_Kaymanov_Armor","NPC_Korshunov_Armor","NPC_Korshunov_Armor_2","NPC_Richter_Armor","NPC_Sci_Armor","NPC_Sel_Armor","NPC_Sel_Neutral_Armor","NPC_Shram_Armor","NPC_Sidorovich_Armor","NPC_SkinCloak_Bandit_Armor","NPC_Spark_Armor","NPC_Tec_Armor","NPC_Tyotya_Armor","NVG_Gen1","NVG_Gen2","NVG_Gen3","NVG_Gen3_WP","NVG_NPC_Gen1","NVG_NPC_Gen2","NVG_NPC_Gen3","Nasos_Neutral_Armor","Newbee_Neutral_Armor","NightStalker_Silen","PArtifactBrain","PArtifactBrain_Fake","PQuestArtifactScraper","PQuestArtifactScraper_Fake","PSYBlocker","PoltergeistLoot","PseudodogLoot","PseudogiantLoot","RU_AutMuz_1","RU_AutMuz_2","RU_AutMuz_3","RU_ColimScope_1","RU_ColimScope_2","RU_GLaunch_1","RU_Grip_1","RU_Grip_2","RU_Grip_3","RU_Laser_1","RU_Silen_1","RU_Silen_2","RU_Silen_3","RU_SinMuz_1","RU_SinMuz_2","RU_SinMuz_3","RU_SinMuz_4","RU_SinMuz_5","RU_X2Scope_1","RU_X4Scope_1","RU_X8Scope_1","RailPicatiniGP37","Ram2_SinMuz_1","Rook_Dolg_Armor","Rook_Svoboda_Armor","SEVA_Dolg_Armor","SEVA_Neutral_Armor","SEVA_Spark_Armor","SEVA_Svoboda_Armor","SVDM_DefaultMuz","SVDM_Scope","SVU_DefaultMuz","SVU_Scope","Sausage","SciSEVA_Scientific_Armor","Sharpshooter_Silen","SkinJacket_Bandit_Armor","SnorkLoot","Sofmod_Silen","SpoiledCannedFood","TopRailAKU","TopRailAPB","TopRailD12","TopRailGP3A","TopRailGrim","TopRailKora","TopRailLavina","TopRailM10","TopRailM860","TopRailMark","TopRailPKP","TopRailRhino","TopRailSKP","TopRailSPSA","TopRailViper","TopRailZubr","TushkanLoot","UA_GLaunch_Weapon_Data_En","UA_GLaunch_Weapon_Data_Ru","UDP_Deadeye_Colim","Veles","Vodka","Water","Zorya_Neutral_Armor","Zubr_SinMuz"}
-- core-18: latch for the G press (a tap shorter than the 100 ms poll was missed). Registered once per game session (global guard), the callback only stamps the time.
if not rawget(_G, "S2_LOOT_KEY") then
    local okB, eB = pcall(function() (rawget(_G, "S2_RegisterKeyBindRaw") or RegisterKeyBind)(Key.G, function() local st = S2 and S2.state; if st then st.lootLatch = os.clock() end end) end)
    rawset(_G, "S2_LOOT_KEY", okB and "ok" or ("erro " .. tostring(eB)))
end

-- ===== core-19: taken list (persistent on the host). Keys: containers = lock id (class@pos in 10 cm), corpses = "corpse@<x m>,<y m>".
local function takenFile() return (rawget(_G, "BRIDGE_DIR") or "") .. "taken.txt" end
local function takenLoad(ST)
    ST.taken = {}; ST.takenList = {}
    if api.ROLE ~= "host" then return end
    local f = io.open(takenFile(), "r")
    if f then for ln in f:lines() do local line = ln:gsub("%s", ""); if line ~= "" and not ST.taken[line] then ST.taken[line] = true; ST.takenList[#ST.takenList + 1] = line end end; f:close() end
    log("S2CORE TAKEN: " .. #ST.takenList .. " chaves lidas de taken.txt")
end
local function mineFile() return (rawget(_G, "BRIDGE_DIR") or "") .. "taken_mine.txt" end
local function takenMineSet(ST)                      -- batch 50: whole-container takes made by THIS player
    if ST.takenMine then return ST.takenMine end
    local M2 = {}; ST.takenMine = M2
    local f = io.open(mineFile(), "r")
    if f then
        for ln in f:lines() do local k = ln:gsub("%s", ""); if k ~= "" then M2[k] = true end end
        f:close()
    elseif api.ROLE == "host" then                   -- migration: before batch 50 nothing recorded who took; the host's list counts as its own
        local g, n, out = io.open(takenFile(), "r"), 0, io.open(mineFile(), "w")
        if g then for ln in g:lines() do local k = ln:gsub("%s", ""); if k ~= "" then M2[k] = true; n = n + 1; if out then out:write(k .. "\n") end end end; g:close() end
        if out then out:close() end
        log("S2CORE TAKEN: taken_mine.txt criado de taken.txt (" .. n .. " chaves contam como minhas: migracao)")
    end
    return M2
end
local function takenMineAdd(ST, key)
    local M2 = takenMineSet(ST); if M2[key] then return end
    M2[key] = true
    local f = io.open(mineFile(), "a"); if f then f:write(key .. "\n"); f:close() end
end
local function takenAdd(ST, key)
    if not ST.taken then takenLoad(ST) end
    if not key or key == "" or ST.taken[key] then return false end
    ST.taken[key] = true; ST.takenList[#ST.takenList + 1] = key
    if api.ROLE == "host" then local f = io.open(takenFile(), "a"); if f then f:write(key .. "\n"); f:close() end end
    log("S2CORE TAKEN +" .. key .. " (total " .. #ST.takenList .. ")")
    return true
end
local function corpseKey(o)
    local q = o:K2_GetActorLocation()
    return string.format("corpse@%.0f,%.0f", q.X / 100, q.Y / 100)
end
local function lootInit(ST) ST.loot = ST.loot or {seq = 0, done = {}, applied = {}, given = {}, corpses = "", reply = nil, req = nil, keyDown = false, lastScan = 0} return ST.loot end
local function findPawnByName(id)
    for _, o in ipairs(FindAllOf("Pawn") or {}) do
        local hit = false
        pcall(function() hit = o:IsValid() and o:GetFullName():find(id, 1, true) ~= nil end)
        if hit then return o end
    end
end
local function remoteField(tag)
    local x = x1part(tag); return x
end
-- ===== b87qitem (flag qitem, default 0; user rule N2): QUEST ITEMS STAY WITH THE HOST. Table s2u_qitems.lua (gen_qitems.py, whole game:
-- 573 items = IsQuestItem + QuestItemPrototypes items a quest node needs + SwitchQuestItemState targets; 476 ItemAdd nodes; 333 placeholders).
-- CLIENT: (1) never replays an ItemAdd node of a quest item (questApply); (2) guard: every qisec s it counts qiper table items in its OWN
-- inventory (CppMediator:GetItemsCountInInventory on the player pawn only); a quest item found there, from any source (own corpse or
-- container, ground, dialog, reward, replay) is removed with the client pak node S2QI_<sid> (qirm=1) and sent to the host: "#QI:<seq>,<sid>,<n>".
-- HOST: (3) a #QI relay gives the host the item up to the count its quest needs (have >= need -> nothing: the host already has it);
-- (4) a corpse the client loots by LOOT_REQ that holds a quest item is never 'given away' and the host's loot window stays allowed, and the
-- host receives those items at once; (5) the host is never TAKEN_DENY'd a container that can hold a quest item (table position within qir cm)
-- or a corpse pawn that still holds one. Nothing here touches a container actor (no inventory call on containers: crash rule).
function S2L.qiTab()
    if S2L.QIT ~= nil then return S2L.QIT end
    S2L.QIT = false
    local ok, T = pcall(dofile, api.BR .. "s2u_qitems.lua")
    if not ok or type(T) ~= "table" or type(T.S) ~= "table" then log("S2CORE qitem: s2u_qitems.lua ausente ou invalido: " .. tostring(T)); return false end
    T.list = {}; for s in pairs(T.S) do T.list[#T.list + 1] = s end; table.sort(T.list)
    T.skip = T.skip or {}; T.norm = T.norm or {}; T.pos = T.pos or {}
    S2L.QIT = T
    log(string.format("S2CORE qitem: %d itens de missao, %d nos ItemAdd pulados no cliente, %d recipientes/corpos com item de missao", #T.list, (function() local n = 0; for _ in pairs(T.skip) do n = n + 1 end return n end)(), #T.pos))
    return T
end
function S2L.qiMed() if not valid(S2L.qiM) then S2L.qiM = StaticFindObject("/Script/Stalker2.Default__CppMediator") end return S2L.qiM end
function S2L.qiCount(pawn, sid)                        -- pawns only (player, NPC corpse): never a container actor
    local n = 0
    pcall(function() local v = S2L.qiMed():GetItemsCountInInventory(pawn, sid); if type(v) == "number" then n = v end end)
    return n
end
function S2L.qiSkip(sid)                               -- client questApply: an ItemAdd of a quest item is never replayed here
    if (FL.b164off or 0) == 0 and sid and rawget(_G, "s2n_qi_npcadd") and s2n_qi_npcadd(sid) == true then return false end   -- b164: into an NPC / corpse / container = replayed (rule: both); into the player = qimirror
    if (FL.qitem ~= 1 and (FL.qimirror or 0) ~= 1) or api.ROLE == "host" or not sid then return false end   -- b97n7: qimirror skips them too
    local T = S2L.qiTab(); return T and T.skip[sid] == 1 or false
end
function S2L.qiGive(sid, n, why, match)                       -- host: receive up to what the quest needs
    local T = S2L.qiTab(); if not T or FL.qigive ~= 1 then return "qigive=0" end
    local pw = S2.io and S2.io.pawn; if not valid(pw) then return "sem pawn" end
    local need = T.S[sid] or 1
    local have = S2L.qiCount(pw, sid)
    local give = match and (math.min(n, need) - have) or math.min(n, need - have)   -- b92lead: match = top up to the other player's count
    if give <= 0 then return string.format("host ja tem %d/%d: nada", have, need) end
    local ok, e = pcall(function() FindFirstOf("CustomConsoleManagerRK"):XCreateItemInInventoryByID(sid, 0, give, 1.0) end)
    local after = S2L.qiCount(pw, sid)
    return string.format("host %d -> %d (+%d pedido, need %d) ok=%s%s [%s]", have, after, give, need, tostring(ok), ok and "" or (" " .. tostring(e)), tostring(why))
end
function S2L.qiCorpse(ST, o, id)                       -- host LOOT_REQ: true = this corpse holds a quest item (keep it open for the host)
    if FL.qitem ~= 1 then return false end
    local T = S2L.qiTab(); if not T then return false end
    local got = {}
    for _, s in ipairs(T.list) do local n = S2L.qiCount(o, s); if n > 0 then got[#got + 1] = {s, n} end end
    if #got == 0 then return false end
    local L = ST.loot; L.qkeep = L.qkeep or {}; L.qkeep[id] = true
    for _, g in ipairs(got) do log("S2CORE qitem: corpo " .. id .. " tem " .. g[1] .. " x" .. g[2] .. " (cliente pediu o saque) -> " .. S2L.qiGive(g[1], g[2], "saque do cliente")) end
    log("S2CORE qitem: corpo " .. id .. " NAO marcado como entregue; o host continua podendo abrir")
    return true
end
function S2L.qiHostKeep(ST, id, pw)                    -- host lockTick: never TAKEN_DENY a container/corpse that can hold a quest item
    if FL.qitem ~= 1 or api.ROLE ~= "host" or not id then return false end
    local T = S2L.qiTab(); if not T then return false end
    local Q = ST.qi or {}; ST.qi = Q; Q.keep = Q.keep or {}
    if Q.keep[id] ~= nil then return Q.keep[id] end
    local keep = false
    local a, b, c = id:match("@([%-%d%.]+),([%-%d%.]+),([%-%d%.]+)$")
    if a then
        local x, y, z, r2 = tonumber(a) * 10, tonumber(b) * 10, tonumber(c) * 10, (FL.qir or 400) ^ 2
        for _, p in ipairs(T.pos) do if (p[1] - x) ^ 2 + (p[2] - y) ^ 2 + (p[3] - z) ^ 2 <= r2 then keep = p[4] .. " " .. p[6]; break end end
    end
    if not keep and id:find("^BP_AI_Human_C") then         -- a corpse PAWN (safe to count); never a container
        local o; pcall(function() o = pw:GetInteractedActor() end)
        if valid(o) then for _, s in ipairs(T.list) do if S2L.qiCount(o, s) > 0 then keep = "corpo com " .. s; break end end end
    end
    Q.keep[id] = keep and true or false
    if keep then log("S2CORE qitem: " .. id .. " pode ter item de missao (" .. tostring(keep) .. "): o host nunca e negado (TAKEN_DENY pulado)") end
    return Q.keep[id]
end
function S2L.qiTick(ST)
    if FL.qitem ~= 1 and (FL.lead or 0) ~= 1 then return end   -- b92lead
    local T = S2L.qiTab(); if not T then return end
    local Q = ST.qi or {}; ST.qi = Q
    Q.out = Q.out or {}; Q.sent = Q.sent or {}; Q.seen = Q.seen or {}; Q.seq = Q.seq or 0; Q.i = Q.i or 0
    local now = os.clock()
    if (FL.lead or 0) == 1 then return S2L.ldQi(ST, T, Q, now) end   -- b92lead: both roles keep + copy
    if api.ROLE == "host" then                          -- (3) relays from the client
        for ent in (x1part("QI") or ""):gmatch("[^|]+") do
            if not Q.seen[ent] then
                Q.seen[ent] = now
                local seq, sid, n = ent:match("^(%d+),([^,]+),(%d+)$")
                if seq and T.S[sid] then log("S2CORE qitem: cliente pegou " .. sid .. " x" .. n .. " (seq " .. seq .. ") -> " .. S2L.qiGive(sid, tonumber(n), "relay do cliente"))
                elseif seq then log("S2CORE qitem: relay ignorado (fora da tabela): " .. ent) end
            end
        end
        for k, t in pairs(Q.seen) do if now - t > 60 then Q.seen[k] = nil end end
        return
    end
    if now < (Q.at or 0) then return end                -- (2) client guard
    Q.at = now + (FL.qisec or 0.5)
    local pw = S2.io and S2.io.pawn; if not valid(pw) then return end
    do local okG, rG = pcall(S2L.qgDead, ST, Q, pw, now, false); if okG and rG then return end end   -- b135fixes 2 (qdead)
    local nL = #T.list; if nL == 0 then return end
    for _ = 1, math.min(FL.qiper or 80, nL) do
        Q.i = Q.i % nL + 1
        local sid = T.list[Q.i]
        local n = S2L.qiCount(pw, sid)
        if n > 0 then
            local s = Q.sent[sid]
            if not s or now - s.at > (FL.qire or 20) or n > s.n then
                Q.sent[sid] = {at = now, n = n}; Q.seq = Q.seq + 1
                Q.out[#Q.out + 1] = {e = Q.seq .. "," .. sid .. "," .. n}   -- untilT is set when it first rides in x1 (qiOut)
                local rm = "remocao desligada (qirm=0)"
                if FL.qirm == 1 and not T.norm[sid] then
                    local aa = FindFirstOf("CustomConsoleManagerAA")
                    local ok, e = false, "CustomConsoleManagerAA ausente"
                    if valid(aa) then ok, e = pcall(function() aa:XStartQuestNodeBySID("S2QI_" .. sid) end) end
                    rm = "S2QI_" .. sid .. " ok=" .. tostring(ok) .. (ok and "" or (" " .. tostring(e)))
                elseif T.norm[sid] then rm = "sem no de remocao (item fora do kit)" end
                log(string.format("S2CORE qitem: item de missao no cliente: %s x%d%s -> %s; enviado ao host (seq %d)", sid, n, s and " (de novo)" or "", rm, Q.seq))
            end
        elseif Q.sent[sid] and Q.sent[sid].n > 0 then
            log("S2CORE qitem: " .. sid .. " saiu do inventario do cliente (" .. string.format("%.1f", now - Q.sent[sid].at) .. " s)")
            Q.sent[sid].n = 0
        end
    end
end
function S2L.qiOut(ST)
    if (FL.lead or 0) ~= 1 and (FL.qitem ~= 1 or api.ROLE == "host") then return "" end   -- b92lead: both roles send with lead=1
    local Q = ST.qi; if not Q or not Q.out or not Q.out[1] then return "" end
    local now, keep, r = os.clock(), {}, {}
    for _, o in ipairs(Q.out) do                        -- at most 8 in one snapshot; the rest wait their turn (a save full of quest items)
        if #r < 8 then o.untilT = o.untilT or (now + (FL.resend or 4)) end
        if not o.untilT then keep[#keep + 1] = o elseif now < o.untilT then keep[#keep + 1] = o; r[#r + 1] = o.e end
    end
    Q.out = keep
    return #r > 0 and ("#QI:" .. table.concat(r, "|")) or ""
end
-- ===== end b87qitem
local function lootTick(ST)
    if FL.loot ~= 1 then return end
    local L = lootInit(ST); local now = os.clock()
    if not ST.taken then takenLoad(ST) end
    local lp = S2.io and S2.io.pawn
    if not valid(lp) then return end
    if api.ROLE == "host" then
        if now - L.lastScan >= 1 then          -- list of corpses near the host player (the client is kept near by the leash)
            L.lastScan = now
            local lpos = lp:K2_GetActorLocation(); local r = {}
            for _, o in ipairs(FindAllOf("Pawn") or {}) do pcall(function()
                if #r < 4 and o ~= lp and not M.isOwnBody(ST, o) and not o:IsAlive() then
                    local nm = o:GetFullName():match("BP_AI_Human_C_%d+")
                    if nm and not (ST.taken and ST.taken[corpseKey(o)]) and not M.LS.noG(ST, o) then   -- b90loot (lsnog)
                        local q = o:K2_GetActorLocation()
                        if (q.X - lpos.X)^2 + (q.Y - lpos.Y)^2 < 3000^2 then r[#r + 1] = string.format("%s@%.0f,%.0f,%.0f", nm, q.X, q.Y, q.Z) end
                    end
                end end) end
            L.corpses = table.concat(r, "|")
        end
        local tk = remoteField("T")
        if tk and tk ~= "" and FL.taken == 1 then for k in tk:gmatch("[^|]+") do if not (ST.taken and ST.taken[k]) then takenAdd(ST, k) end end end   -- batch 28: a list of keys
        local q = remoteField("Q")
        if q then
            local seq, id = q:match("^(%d+);(BP_AI_Human_C_%d+)$"); seq = tonumber(seq)
            if seq and not L.done[seq] then
                L.done[seq] = now
                local o = findPawnByName(id)
                if not o then log("S2CORE LOOT_REQ " .. id .. " seq=" .. seq .. ": pawn nao encontrado")
                elseif o:IsAlive() then log("S2CORE LOOT_REQ " .. id .. " seq=" .. seq .. ": ainda vivo, negado")
                elseif L.given[id] or (ST.taken and ST.taken[corpseKey(o)]) then log("S2CORE LOOT_REQ " .. id .. " seq=" .. seq .. ": ja entregue, negado")
                else
                    local med = StaticFindObject("/Script/Stalker2.Default__CppMediator")
                    local t0, items = os.clock(), {}
                    for _, s in ipairs(LOOT_SIDS) do
                        local ok, n = pcall(function() return med:GetItemsCountInInventory(o, s) end)
                        if ok and type(n) == "number" and n > 0 then items[#items + 1] = s .. "x" .. n end
                    end
                    L.given[id] = true; if not S2L.qiCorpse(ST, o, id) then takenAdd(ST, corpseKey(o)) end   -- b87qitem
                    L.replyQ = L.replyQ or {}; L.replyQ[#L.replyQ + 1] = {seq = seq, id = id, str = table.concat(items, "|")}      -- batch 28: lootOut sends them one after the other
                    log(string.format("S2CORE LOOT_REQ %s seq=%d: %d itens em %.3f s: %s (corpo marcado como entregue)", id, seq, #items, os.clock() - t0, table.concat(items, " ")))
                end
            end
        end
        if next(L.given) then               -- the corpse was given away: its own loot window is closed on the host
            local ia; pcall(function() ia = FindFirstOf("PlayerController"):GetInteractedActor() end)
            if valid(ia) then
                local nm = ia:GetFullName():match("BP_AI_Human_C_%d+")
                if nm and L.given[nm] and not (L.qkeep and L.qkeep[nm]) then   -- b87qitem: a quest corpse stays open for the host
                    local vis = false; pcall(function() local w = FindFirstOf("InventoryNew"); vis = w ~= nil and w:IsVisible() end)
                    if vis then pcall(function() FindFirstOf("PlayerController"):CloseLootMenu() end); log("S2CORE LOOT: janela de saque do corpo entregue " .. nm .. " fechada no host") end
                end
            end
        end
    else
        -- client: corpses list from the host, key G (or flag lootreq=<n>) asks for the nearest one within 4 m
        local near
        local lpos = lp:K2_GetActorLocation()
        for ent in (remoteField("C") or ""):gmatch("[^|]+") do
            local id, x, y, z = ent:match("^(BP_AI_Human_C_%d+)@([%-%d%.]+),([%-%d%.]+),([%-%d%.]+)$")
            if id then
                local d = math.sqrt((tonumber(x) - lpos.X)^2 + (tonumber(y) - lpos.Y)^2)
                if d < FL.lootr and (not near or d < near.d) then near = {id = id, d = d} end   -- batch 53
            end
        end
        L.near = near
        for k in (remoteField("X") or ""):gmatch("[^|]+") do if not (ST.taken and ST.taken[k]) then takenAdd(ST, k) end end
        local down = false
        pcall(function() down = FindFirstOf("PlayerController"):IsInputKeyDown({KeyName = FName("G")}) and true or false end)
        local latched = ST.lootLatch and (now - ST.lootLatch) < 1.0 and ST.lootLatch ~= L.latchUsed
        if latched then L.latchUsed = ST.lootLatch end
        local want = latched or (down and not L.keyDown) or (FL.lootreq ~= 0 and FL.lootreq ~= (L.reqSeen or 0))
        L.keyDown = down; L.reqSeen = FL.lootreq
        if want then
            if near then L.seq = L.seq + 1; L.req = {seq = L.seq, id = near.id, untilT = now + 3}; log(string.format("S2CORE LOOT pedido seq=%d %s a %.0f cm", L.seq, near.id, near.d))
            else log("S2CORE LOOT: nenhum corpo a menos de " .. (FL.lootr / 100) .. " m (lista do host: '" .. tostring(remoteField("C")) .. "')") end
        end
        local r = remoteField("R")
        if r then
            local seq, id, items = r:match("^(%d+);(BP_AI_Human_C_%d+);(.*)$"); seq = tonumber(seq)
            if seq and not L.applied[seq] then
                L.applied[seq] = now
                local mgr = FindFirstOf("CustomConsoleManagerRK"); local med = StaticFindObject("/Script/Stalker2.Default__CppMediator")
                local got = {}
                for ent in items:gmatch("[^|]+") do
                    local sid, n = ent:match("^([%w_]+)x(%d+)$"); n = tonumber(n)
                    if sid and n then
                        local b4 = 0; pcall(function() b4 = med:GetItemsCountInInventory(lp, sid) end)
                        local ok = pcall(function() mgr:XCreateItemInInventoryByID(sid, 0, n, 1.0) end)
                        local af = 0; pcall(function() af = med:GetItemsCountInInventory(lp, sid) end)
                        got[#got + 1] = string.format("%s +%d (%s->%s ok=%s)", sid, n, tostring(b4), tostring(af), tostring(ok))
                    end
                end
                log("S2CORE LOOT_GOT seq=" .. seq .. " de " .. id .. ": " .. table.concat(got, "; "))
            end
        end
    end
end
-- ===== batch 53: doors. The host reports the doors near the player whose leaf moved; the client sets its own door (same position) =====
local function doorKey(o)
    local q = o:K2_GetActorLocation()
    return string.format("%.0f,%.0f,%.0f", q.X / 10, q.Y / 10, q.Z / 10)
end
-- ===== b71kf: cross-PC key match (flag keyfix). Keys are "<prefix><x>,<y>,<z>" in 10 cm units; prefix "" (doors, #P, #Z) or "<class>@" (locks).
-- b76loc: NO new top-level local (the main chunk is at its 200-local limit): the helpers live in a do-block and are reached as M.kf.close/get/find.
do
local function keyParts(k)
    if type(k) ~= "string" then return nil end
    local p, x, y, z = k:match("^(.-)(%-?%d+),(%-?%d+),(%-?%d+)$")
    if not p then return nil end
    return p, tonumber(x), tonumber(y), tonumber(z)
end
local function keyClose(a, b)          -- the same object seen from both PCs?
    if a == b then return true end
    if FL.keyfix ~= 1 then return false end
    local pa, ax, ay, az = keyParts(a); local pb, bx, by, bz = keyParts(b)
    if not pa or not pb or pa ~= pb then return false end
    local t = FL.keytol or 1
    return math.abs(ax - bx) <= t and math.abs(ay - by) <= t and math.abs(az - bz) <= t
end
local function keyGet(map, k)          -- map[k]; with keyfix=1 else the value of a neighbour key (returns value, key found)
    if not map or k == nil then return nil end
    local v = map[k]
    if v ~= nil or FL.keyfix ~= 1 then return v, (v ~= nil and k or nil) end
    local p, x, y, z = keyParts(k); if not p then return nil end
    local t = FL.keytol or 1
    local function alts(n) local r = {} for d = -t, t do local m = n + d; r[#r + 1] = string.format("%d", m); if m == 0 then r[#r + 1] = "-0" end end return r end
    local X, Y, Z = alts(x), alts(y), alts(z)
    for _, a in ipairs(X) do for _, b in ipairs(Y) do for _, c in ipairs(Z) do
        local k2 = p .. a .. "," .. b .. "," .. c
        local v2 = map[k2]; if v2 ~= nil then return v2, k2 end
    end end end
    return nil
end
local function keyFind(objs, k)        -- the object whose doorKey is k (exact wins), else (keyfix=1) the closest one within keytol
    local best, bd, exact = nil, nil, nil
    local _, kx, ky, kz = keyParts(k)
    for _, o in ipairs(objs or {}) do pcall(function()
        if exact or not o:IsValid() then return end
        local ok2 = doorKey(o)
        if ok2 == k then exact = o; return end
        if FL.keyfix == 1 and kx and keyClose(ok2, k) then
            local _, ox, oy, oz = keyParts(ok2)
            local d = math.abs(ox - kx) + math.abs(oy - ky) + math.abs(oz - kz)
            if not bd or d < bd then best, bd = o, d end
        end
    end) end
    return exact or best
end
M.kf = {close = keyClose, get = keyGet, find = keyFind}
end
local function doorLeafYaw(o)
    local y = nil
    pcall(function() local c = o.FirstLeafMeshComponent; pcall(function() c = c:get() end); if c and c:IsValid() then y = c:K2_GetComponentRotation().Yaw end end)
    return y
end
-- ===== batch 53: post-tutorial kit for the client, once per campaign (marker bridge\kit_done.txt) =====
local function kitTick(ST)
    if FL.kit ~= 1 or api.ROLE ~= "client" then return end
    local K = ST.kit or {}; ST.kit = K
    local pw = S2.io and S2.io.pawn
    if not valid(pw) then pcall(function() pw = FindFirstOf("BP_Stalker2Character_C") end) end   -- no snapshot yet (no host): the local pawn
    if not valid(pw) then K.addr = nil; return end
    local now = os.clock()
    local addr = pw:GetAddress()
    if addr ~= K.addr then K.addr = addr; K.at = now; K.done = nil; return end
    if K.done or now - K.at < FL.kitdelay then return end
    local cid = ST.qs and ST.qs.apCid or nil
    if not cid then if now - K.at < FL.kitwait then return end; cid = "-" end
    local f = io.open(api.BR .. "kit_done.txt", "r")
    local mark = f and f:read("*l") or nil; if f then f:close() end
    if mark and (mark == cid or cid == "-") then K.done = true; return end   -- this campaign already got it (or unknown campaign and some marker exists)
    K.done = true
    f = io.open(api.BR .. "s2u_kit.tsv", "r")
    if not f then log("S2CORE kit: s2u_kit.tsv ausente; nada dado"); return end
    local rows = {}
    for ln in f:lines() do local sid, n = ln:match("^%s*([%w_]+)%s+(%d+)"); if sid then rows[#rows + 1] = {sid, tonumber(n)} end end
    f:close()
    local med = StaticFindObject("/Script/Stalker2.Default__CppMediator")
    local mgr = FindFirstOf("CustomConsoleManagerRK")
    local given, had = {}, 0
    for _, r in ipairs(rows) do
        local sid, n = r[1], r[2]
        local have = 0; pcall(function() local v = med:GetItemsCountInInventory(pw, sid); if type(v) == "number" then have = v end end)
        if have >= n then had = had + 1
        else
            local ok, e = pcall(function() mgr:XCreateItemInInventoryByID(sid, 0, n - have, 1.0) end)
            if ok then given[#given + 1] = sid .. "x" .. (n - have) else log("S2CORE kit: " .. sid .. " erro " .. tostring(e)) end
        end
    end
    local w = io.open(api.BR .. "kit_done.txt", "w"); if w then w:write(cid, "\n"); w:close() end
    log(string.format("S2CORE kit: campanha %s: %d itens dados (%s), %d ja tinha", cid, #given, table.concat(given, ","), had))
end
-- ===== batch 69: cutscene join (b68 cj68.lua; s2u_cine.lua). Whoever starts a scene, the other player is put beside the starter first.
-- Two hooks only: client replay of a scene node (cjBeforeStart) and host run of a client-fired cause that leads to a scene (cjOnRemoteCause).
-- No delay: K2_SetActorLocation is immediate, the node starts in the same tick (no ExecuteWithDelay game calls). Flag cjoin. =====
local CJ = {st = {}, g = nil}
local function cjG()
    if CJ.g ~= nil then return CJ.g end
    CJ.g = false
    local function ld(n)
        local f = io.open(api.BR .. n, "r"); if not f then log("S2CORE cjoin: falta " .. n); return nil end
        local src = f:read("*a"); f:close()
        local fn, e = load(src, n); if not fn then log("S2CORE cjoin: " .. n .. " erro " .. tostring(e)); return nil end
        return fn()
    end
    local okM, M = pcall(ld, "cj68.lua"); local okC, C = pcall(ld, "s2u_cine.lua")
    if not (okM and type(M) == "table" and okC and type(C) == "table") then return false end
    CJ.m = M
    CJ.g = {FL = FL, ROLE = api.ROLE, now = os.clock, log = log, CINE = C,
        pawnPos = function() local p; pcall(function() p = S2L.ioPawn():K2_GetActorLocation() end); return p end,
        remote = function() return S2L.remoteIn("cj68") end,
        teleport = function(x, y, z)   -- b108 CJ2: cj68 beside() passes (other x + cjoff, y, z + 50)
            if (FL.tpfloor or 0) == 1 then return S2L.tpfNow(S2.state, {x = x - (FL.cjoff or 150), y = y, z = z - 50}, "cjoin cena") end
            return pcall(function() S2L.ioPawn():K2_SetActorLocation({X = x, Y = y, Z = z}, false, {}, true) end)
        end,
        remoteField = function(t) return remoteField(t) end, localCine = function() return false end}
    log("S2CORE cjoin: tabelas carregadas")
    return CJ.g
end
-- ===== batch 55/56 (ported into the live core by batch 62, 2026-10-14: the client walking into a quest trigger did nothing): node relay. Client source (qnsrc: position table, or the client's own world via 's2q T') -> #QN origin c; host XEndQuestNodeBySID once, no echo =====
local function trigRows(T)
    if T.rows then return T.rows end
    T.rows = {}
    local f = io.open(api.BR .. "s2u_triggers.tsv", "r")
    if f then
        for ln in f:lines() do
            local c = {}
            for v in (ln .. "\t"):gmatch("([^\t]*)\t") do c[#c + 1] = v end
            local x, y, z = tonumber(c[3]), tonumber(c[4]), tonumber(c[5])
            if c[1] and x and y and z then
                T.rows[#T.rows + 1] = {sid = c[1], q = c[2], x = x, y = y, z = z, yaw = tonumber(c[6]) or 0,
                    sx = tonumber(c[7]) or 100, sy = tonumber(c[8]) or 100, sz = tonumber(c[9]) or 100, shape = c[10] or "Box"}
            end
        end
        f:close()
    end
    log("S2CORE trig: " .. #T.rows .. " gatilhos de missao em s2u_triggers.tsv")
    return T.rows
end
local function trigInside(r, q, m)
    local dx, dy, dz = q.X - r.x, q.Y - r.y, q.Z - r.z
    if r.shape ~= "Box" then return dx * dx + dy * dy < (r.sx + m) ^ 2 and math.abs(dz) < r.sz + m + 100 end
    local a = -math.rad(r.yaw); local c, s = math.cos(a), math.sin(a)
    local lx, ly = dx * c - dy * s, dx * s + dy * c
    return math.abs(lx) <= r.sx + m and math.abs(ly) <= r.sy + m and math.abs(dz) <= r.sz + m + 100
end
-- b98trig (flag trigself): one trigger node, one end. lead=1 makes the host scan its own entries (T.sent) and the host's world fires the
-- node by itself; a client #QN for the same sid inside trigre s is then a second end (seen live 2026-10-15 SQ01_Trigger_PGoesFromLocation).
function S2L.trSelf(T, sid, seq, now)                -- host, client-origin #QN: true = the host itself entered sid within trigre s
    if (FL.trigself or 0) ~= 1 or (FL.lead or 0) ~= 1 then return false end
    local at = T.sent[sid]; if not at or now - at > FL.trigre then return false end
    T.selfLog = T.selfLog or {}
    if T.selfLog[sid] ~= at then
        T.selfLog[sid] = at
        log(string.format("S2CORE qn: origem c %s seq=%s NAO disparado: o host ja entrou nesse gatilho ha %.1f s", sid, tostring(seq), now - at))
    end
    return true
end
function S2L.trLate(T, sid, now)                     -- host own entry: true = a client #QN already ended sid within trigre s (no lock)
    if (FL.trigself or 0) ~= 1 or (FL.lead or 0) ~= 1 or api.ROLE ~= "host" then return false end
    local at = T.fired and T.fired[sid]; if not at or now - at > FL.trigre then return false end
    log(string.format("S2CORE trig: o host entrou em %s que o cliente ja disparou ha %.1f s: o jogo do host dispara sozinho, sem trava", sid, now - at))
    return true
end
local function trigTick(ST)
    if FL.ctrig ~= 1 then return end
    local T = ST.trig or {inside = {}, out = {}, sent = {}, seen = {}, qseen = {}, qn = 0, nearAt = -100, tickAt = 0}; ST.trig = T
    local now = os.clock()
    if api.ROLE == "host" then                            -- host: fire what the client's own world reported (origin c)
        for ent in (remoteField("QN") or ""):gmatch("[^|]+") do
            local org, seq, sid = ent:match("^([hc]),(%d+),(.+)$")
            if org == "c" then                            -- client-authored: the client already ran it -> the host fires once and does NOT echo
                local key = sid .. ":" .. seq
                if not T.qseen[key] then
                    T.qseen[key] = now
                    if S2L.chTrigDeny(ST, sid, seq) then T.seen[sid] = now   -- b91choice (N3): the host's own entry fires it
                    elseif S2L.trSelf(T, sid, seq, now) then T.seen[sid] = now   -- b98trig: the host's own world already fired it
                    elseif now - (T.seen[sid] or -1000) > FL.trigre then
                        T.seen[sid] = now; T.fired = T.fired or {}; T.fired[sid] = now   -- b98trig
                        local aa = FindFirstOf("CustomConsoleManagerAA")
                        local ok, e = false, "CustomConsoleManagerAA ausente"
                        if FL.cjoin == 1 and S2L.mqSid(sid) then local G = cjG(); if G then pcall(CJ.m.cjOnRemoteCause, CJ.st, sid, G) end end   -- batch 69: host joins the client first; b109 mqpull
                        if valid(aa) then ok, e = pcall(function() aa:XEndQuestNodeBySID(sid) end) end
                        log(string.format("S2CORE qn: origem c %s seq=%s -> XEndQuestNodeBySID ok=%s %s", sid, seq, tostring(ok), ok and "" or tostring(e)))
                    end
                end
            elseif org == "h" then                        -- host-authored: ours, nothing to do here (the client mirrors it on its side)
                log(string.format("S2CORE qn: origem h %s seq=%s recebido de volta (ignorado no host)", sid, seq))
            end
        end
        if (FL.lead or 0) ~= 1 then return end   -- b92lead: the host also scans its own entries (story lock only)
    end
    if FL.qnsrc ~= 0 then return end   -- batch 56: the position table is a source only when qnsrc=0 (else the client's own world reports via 's2q T')
    if now - T.tickAt < 0.5 then return end
    T.tickAt = now
    local pw = S2.io and S2.io.pawn; if not valid(pw) then pcall(function() pw = FindFirstOf("BP_Stalker2Character_C") end) end
    if not valid(pw) then return end
    local q = pw:K2_GetActorLocation()
    if now - T.nearAt > 5 then
        T.nearAt = now; T.near = {}
        local r2 = FL.trignear * FL.trignear
        for _, r in ipairs(trigRows(T)) do if (r.x - q.X) ^ 2 + (r.y - q.Y) ^ 2 < r2 then T.near[#T.near + 1] = r end end
    end
    for _, r in ipairs(T.near or {}) do
        local inside = trigInside(r, q, FL.trigmargin)
        if inside and not T.inside[r.sid] then
            if now - (T.sent[r.sid] or -1000) > FL.trigre then
                T.qn = (T.qn or 0) + 1
                T.sent[r.sid] = now; if api.ROLE == "client" then T.out[#T.out + 1] = {sid = r.sid, at = now, seq = T.qn} end
                if not S2L.trLate(T, r.sid, now) then pcall(S2L.ldTrig, ST, r) end   -- b92lead; b98trig: not after the client fired it
                log(string.format("S2CORE trig: entrei no gatilho %s (%s) a %.0f m do centro", r.sid, r.q, math.sqrt((r.x - q.X) ^ 2 + (r.y - q.Y) ^ 2) / 100))
            end
        end
        T.inside[r.sid] = inside
    end
end
local function trigOut(ST)
    local T = ST.trig; if not T or api.ROLE ~= "client" then return "" end
    local now, keep = os.clock(), {}
    local hold = (FL.qnkeep or 1) == 1 and math.max(3, (FL.peersilent or 10) + 2) or 3   -- b127softlock B: was 3 s (lost in a 5-10 s relay gap)
    for _, e in ipairs(T.out) do if now - e.at < hold then keep[#keep + 1] = e end end
    T.out = keep
    if #keep == 0 then return "" end
    if FL.qrelay ~= 1 then return "" end              -- batch 62: the old #Z fallback is gone (#Z = barricades since b58)
    local q = {}
    for _, e in ipairs(keep) do q[#q + 1] = string.format("c,%d,%s", e.seq or 0, e.sid) end
    return "#QN:" .. table.concat(q, "|")
end
-- ===== b129qdiv A (flag qdiv, default 1; §149 audit #3): the CLIENT's own quest graph runs a trigger node the HOST never runs (a branch
-- trigger refused while the host leads = ldDeny, dropped for good; a #QN lost in a gap). qgap only sees host->client gaps.
-- Digest (state, rebuilt every qdivs s, rides in questOut): client "#QD:c,<journal>,<rnDone>;<sid>:<age s>|..." = its own trigger entries
-- (T.sent, last qdivkeep s, newest qdivn); host "#QD:h,<journal>,<rn>,<rsid>,<nrep>;<pending sid>|...". <journal> = "am.as.fi.fx" counts with qdivj=1
-- (CppMediator Get*Quests, unmeasured), else "-": compared and logged only.
-- HOST: a client step not covered (host fired it for the client, entered it itself, or replayed it within trigre s of the step) for qdivm s
-- is PENDING. While the host holds the story lock it waits (N14: the leader decides). Then: the host's quest log got a step of the same
-- quest after the client's step = the host decided meanwhile = tie, HOST WINS -> rollback order (rn+1); else the host replays the node with
-- the same call trigTick uses (XEndQuestNodeBySID; a failed call = cannot apply -> rollback order). Each step is decided once (done[sid]).
-- CLIENT: a new rn -> S2L.n7Follow (snapshot of its items, reload of its own newest paired save, host catch-up, items back), retried while
-- n7 is busy; the step leaves T.sent. While it has a step younger than qdivm+15 s, or one the host lists as pending, csave waits (the save
-- it may reload must not hold the step). lead=0: logged only (N3 keeps branch triggers host-side). Test SIDs S2QDIVTEST_R* / S2QDIVTEST_C*
-- (probe S1) take the replay / host-wins path as a DRY RUN: nothing is called. One line every qdivlog s: in-sync / diverged / repaired.
function S2L.qdSt(ST)
    ST.qd = ST.qd or {pend = {}, done = {}, rep = {}, hist = {}, hpend = {}, rn = 0, rsid = "-", rnDone = 0, nrep = 0, nroll = 0, nlast = 0, logAt = os.clock(), out = "", outAt = -1e9, buildAt = -1e9}
    return ST.qd
end
function S2L.qdJ()                                         -- "am.as.fi.fx" journal counts (qdivj=1), else "-"
    if (FL.qdivj or 0) ~= 1 then return "-" end
    local med = StaticFindObject("/Script/Stalker2.Default__CppMediator")
    if not valid(med) then return "-" end
    local r = {}
    for _, fn in ipairs({"GetActiveMainQuests", "GetActiveSecondaryQuests", "GetFinishedQuests", "GetFailedQuests"}) do
        local n
        pcall(function() local a = med[fn](med); local okN, v = pcall(function() return a:GetArrayNum() end); if okN and type(v) == "number" then n = v else n = #a end end)
        if type(n) ~= "number" then return "-" end
        r[#r + 1] = string.format("%d", n)
    end
    return table.concat(r, ".")
end
function S2L.qdQuest(ST, sid)                              -- quest of a trigger SID (s2u_triggers.tsv column 2), else its prefix
    local D = S2L.qdSt(ST)
    if not D.qmap then D.qmap = {}; for _, r in ipairs(trigRows(ST.trig or {}) or {}) do D.qmap[r.sid] = r.q end end
    return D.qmap[sid] or sid:match("^(.-)_OnTrigger") or sid:match("^([^_]+)") or sid
end
function S2L.qdN0(D, stepT, n)                             -- host: quest log size at time stepT (5 s samples; older than the samples = the oldest)
    local v = D.hist[1] and D.hist[1].n or n
    for _, h in ipairs(D.hist) do if h.at <= stepT then v = h.n else break end end
    return v
end
function S2L.qdConf(ST, q, n0)                             -- host: true (+ event) = its quest log got a step of quest q after index n0
    local L = ST.qs and ST.qs.log or {}
    local pre = q .. "_"
    for i = (n0 or #L) + 1, #L do
        local e = L[i] and L[i].e or ""
        local f = {}
        for w in e:gmatch("[^,]+") do f[#f + 1] = w end
        local sid = (f[1] == "J" or f[1] == "G") and f[5] or f[2]
        if (f[1] == "J" and f[2] == q) or (sid and sid:sub(1, #pre) == pre) then return true, e end
    end
    return false
end
function S2L.qdFix(ST, D, sid, p, now)                     -- host: decide one pending client step (once)
    local test = sid:find("^S2QDIVTEST_") ~= nil
    local conf, ev
    if test then conf = sid:find("^S2QDIVTEST_C") ~= nil; ev = "teste S1"
    else conf, ev = S2L.qdConf(ST, p.q, p.n0) end
    D.done[sid] = p.stepT; D.pend[sid] = nil
    if not conf then
        local ok, e = true, "teste S1: XEndQuestNodeBySID NAO chamado"
        if not test then
            local T = ST.trig; if T then T.seen = T.seen or {}; T.seen[sid] = now; T.fired = T.fired or {}; T.fired[sid] = now end
            if FL.cjoin == 1 and S2L.mqSid(sid) then local G = cjG(); if G then pcall(CJ.m.cjOnRemoteCause, CJ.st, sid, G) end end
            local aa = FindFirstOf("CustomConsoleManagerAA")
            ok, e = false, "CustomConsoleManagerAA ausente"
            if valid(aa) then ok, e = pcall(function() aa:XEndQuestNodeBySID(sid) end) end
        end
        if ok then
            D.rep[sid] = now; D.nrep = D.nrep + 1
            log(string.format("S2CORE qdiv: reparado: o host repetiu o passo do cliente %s (missao %s, %.0f s atras) -> XEndQuestNodeBySID ok %s", sid, p.q, now - p.stepT, test and e or ""))
            return
        end
        ev = "a repeticao falhou: " .. tostring(e)
    end
    if (FL.qdivroll or 1) ~= 1 then log(string.format("S2CORE qdiv: %s (missao %s): o host vence (%s) mas qdivroll=0: so registro", sid, p.q, tostring(ev))); return end
    D.rn = D.rn + 1; D.rsid = sid; D.nroll = D.nroll + 1
    log(string.format("S2CORE qdiv: o host vence (N14) em %s (missao %s; %s): o cliente volta ao estado do host (ordem #%d)", sid, p.q, tostring(ev), D.rn))
end
function S2L.qdHost(ST, D, now)
    local T, L = ST.trig or {}, ST.qs and ST.qs.log or {}
    if now - (D.histAt or -1e9) >= 5 then
        D.histAt = now; D.hist[#D.hist + 1] = {at = now, n = #L}
        while D.hist[2] and now - D.hist[1].at > (FL.qdivkeep or 600) + 60 do table.remove(D.hist, 1) end
    end
    local s = x1part("QD")
    if not s or s:sub(1, 2) ~= "c," then D.peerJ = nil; D.peer = false; return end   -- no client digest (old core / silent): nothing decided
    D.peer = true
    local head, list = s:match("^([^;]*);?(.*)$")
    D.peerJ = (head or ""):match("^c,([^,]*)")
    local lead, seen = S2L.ldMine(ST), {}
    for ent in (list or ""):gmatch("[^|]+") do
        local sid, age = ent:match("^(.+):(%d+)$"); age = tonumber(age)
        if sid and age then
            seen[sid] = true
            local stepT = now - age
            local cov = math.max(T.fired and T.fired[sid] or -1e9, T.sent and T.sent[sid] or -1e9, D.rep[sid] or -1e9)
            local dn = D.done[sid]
            if cov >= stepT - (FL.trigre or 30) or (dn and dn >= stepT - 15) then D.pend[sid] = nil
            elseif age >= (FL.qdivm or 20) then
                local p = D.pend[sid]
                if not p then
                    p = {first = now, stepT = stepT, n0 = S2L.qdN0(D, stepT, #L), q = S2L.qdQuest(ST, sid)}; D.pend[sid] = p
                    log(string.format("S2CORE qdiv: divergencia: o grafo do cliente rodou %s (missao %s) ha %d s e o host nao", sid, p.q, age))
                end
                if (FL.lead or 0) ~= 1 then p.why = "lead=0: so registro"
                elseif lead then p.why = "o host lidera agora: espera"
                else S2L.qdFix(ST, D, sid, p, now) end
            end
        end
    end
    for sid in pairs(D.pend) do if not seen[sid] then D.pend[sid] = nil end end
end
function S2L.qdRoll(ST, D, now)                            -- client: run the pending rollback order (n7 return), retried while n7 is busy
    local R = D.roll; if not R then return end
    local sid = R.sid
    if sid:find("^S2QDIVTEST_") then D.roll = nil; log("S2CORE qdiv: teste S1: o host venceu em " .. sid .. ": faria S2L.n7Follow (nada recarregado)"); return end
    if (FL.qdivroll or 1) ~= 1 or (FL.n7 or 0) ~= 1 or FL.csave ~= 1 or FL.qrollback ~= 1 then
        D.roll = nil; log("S2CORE qdiv: o host venceu em " .. sid .. " mas o cliente nao pode voltar (precisa qdivroll=1 n7=1 csave=1 qrollback=1): fica adiantado"); return
    end
    local N = S2L.n7St(ST)
    if N.stage ~= "idle" or (ST.qs and ST.qs.rbWait) then
        if now - R.at > 120 then D.roll = nil; log("S2CORE qdiv: retorno por " .. sid .. " desistido: n7 ocupado ha 120 s") end
        return
    end
    if not R.said then R.said = true; log(string.format("S2CORE qdiv: o host venceu em %s (ordem #%d): volto ao estado do host (save pareado + catch-up + meus itens)", sid, R.rn)) end
    S2L.n7Follow(ST, N, now)
    if N.stage ~= "idle" then D.roll = nil
    elseif now - R.at > 120 then D.roll = nil; log("S2CORE qdiv: retorno por " .. sid .. " desistido (120 s)") end
end
function S2L.qdClient(ST, D, now)
    local T = ST.trig
    local s = x1part("QD")
    if s and s:sub(1, 2) == "h," then
        D.peer = true
        local head, list = s:match("^([^;]*);?(.*)$")
        local pj, rn, rsid, hr = (head or ""):match("^h,([^,]*),(%d+),([^,]*),?(%d*)$")
        D.peerJ = pj; rn = tonumber(rn); D.hrep = tonumber(hr)
        D.hpend = {}; for sid in (list or ""):gmatch("[^|]+") do D.hpend[sid] = true end
        local sess = ST.peer and ST.peer.sessions or 0
        if rn and D.hsess ~= sess then D.hsess = sess; D.rnDone = rn         -- first digest of this host session: baseline, nothing run
        elseif rn and rn < D.rnDone then D.rnDone = rn                       -- the host's counter restarted
        elseif rn and rn > D.rnDone then
            D.rnDone = rn; D.nroll = D.nroll + 1
            if T and rsid and rsid ~= "-" then if T.sent then T.sent[rsid] = nil end; if T.inside then T.inside[rsid] = nil end end
            D.roll = {sid = rsid or "-", rn = rn, at = now}
        end
    else D.peer = false; D.peerJ = nil; D.hpend = {} end
    if D.roll then S2L.qdRoll(ST, D, now) end
    local hold, young = false, (FL.qdivm or 20) + 15
    if T and T.sent then
        for sid, at in pairs(T.sent) do if now - at < young or D.hpend[sid] then hold = true; break end end
    end
    if (hold or D.roll) and ST.qs then ST.qs.csNext = math.max(ST.qs.csNext or 0, now + 2) end   -- csave waits: the save it may reload must not hold the step
    D.hold = hold
end
function S2L.qdBuild(ST, D, now)                           -- the digest text (state)
    local j = S2L.qdJ()
    D.myJ = j
    if api.ROLE == "host" then
        local p = {}
        for sid in pairs(D.pend) do p[#p + 1] = sid end
        table.sort(p)
        D.out = string.format("#QD:h,%s,%d,%s,%d;%s", j, D.rn, D.rsid, D.nrep, table.concat(p, "|"))
    else
        local T, arr = ST.trig, {}
        for sid, at in pairs(T and T.sent or {}) do
            local age = now - at
            if age >= 0 and age <= (FL.qdivkeep or 600) then arr[#arr + 1] = {sid = sid, age = age} end
        end
        table.sort(arr, function(a, b) return a.age < b.age end)
        local t = {}
        for i = 1, math.min(#arr, FL.qdivn or 12) do t[#t + 1] = string.format("%s:%d", arr[i].sid, math.floor(arr[i].age)) end
        D.out = string.format("#QD:c,%s,%d;%s", j, D.rnDone, table.concat(t, "|"))
    end
    D.outAt = now
end
function S2L.qdLog(ST, D, now)                             -- one line every qdivlog s: in-sync / diverged / repaired
    if now - D.logAt < (FL.qdivlog or 30) then return end
    D.logAt = now
    local fixed = D.nrep + D.nroll - D.nlast; D.nlast = D.nrep + D.nroll
    local div = {}
    if api.ROLE == "host" then
        for sid, p in pairs(D.pend) do div[#div + 1] = string.format("%s [%s]", sid, p.why or "aguardando") end
    else
        for sid in pairs(D.hpend) do div[#div + 1] = sid .. " [o host ainda nao tem]" end
        if D.roll then div[#div + 1] = "retorno ao host pendente (" .. tostring(D.roll.sid) .. ")" end
        if (FL.qnohold or 0) ~= 1 then                    -- b163quest: host steps held here, or the host advanced and none reached this side
            local Q = ST.qs or {}
            local held, W = #(Q.rbQueue or {}) + #(Q.csQueue or {}), Q.rbWait or Q.csWait
            if held > 0 then div[#div + 1] = string.format("%d passos do host retidos (%s ha %.0f s)", held, Q.rbWait and "rbWait" or "csWait", W and (now - W.at) or 0) end
            local hn, rx = tonumber(((x1part("B") or ""):match("^%d+,(%d+)"))), Q.rx or 0
            if hn and D.hnLast and held == 0 and hn - D.hnLast >= 2 and rx == D.rxLast then div[#div + 1] = string.format("o host avancou %d passos e nenhum chegou aqui", hn - D.hnLast)
                if (FL.qdivfix or 0) == 1 and now >= (Q.gapAt or 0) and not Q.rbWait then Q.gapAt = now + 60; Q.csLoads = (Q.csLoads or 0) + 1; Q.cuDone = nil; log("S2CORE qdiv: b187 qdivfix: catch-up do host pedido de novo (#N " .. Q.csLoads .. ")") end
            end
            D.hnLast, D.rxLast = hn, rx
        end
    end
    table.sort(div)
    local jd = ""
    if D.myJ and D.peerJ and D.myJ ~= "-" and D.peerJ ~= "-" and D.myJ ~= D.peerJ then jd = string.format("; diario difere (aqui %s, outro %s)", D.myJ, D.peerJ) end
    local st = #div > 0 and "diverged" or (fixed > 0 and "repaired" or "in-sync")
    log(string.format("S2CORE qdiv %ds: %s%s (host repetiu %d, cliente voltou %d no total)%s%s", FL.qdivlog or 30, st,
        #div > 0 and (": " .. table.concat(div, ", ")) or "", D.nrep, D.nroll, jd, D.peer and "" or "; sem digest do outro lado"))
end
function S2L.qdTick(ST)                                    -- tick1000, both roles
    if (FL.qdiv or 0) ~= 1 then return end
    local D, now = S2L.qdSt(ST), os.clock()
    local P = ST.peer
    if not (P and P.state == "up") then return end
    if api.ROLE == "host" then S2L.qdHost(ST, D, now) else S2L.qdClient(ST, D, now) end
    if now - D.buildAt >= (FL.qdivs or 2) then D.buildAt = now; S2L.qdBuild(ST, D, now) end
    S2L.qdLog(ST, D, now)
end
function S2L.qdOut(ST)                                     -- questOut: the digest, while fresh
    if (FL.qdiv or 0) ~= 1 then return "" end
    local D = ST.qd
    if not D or os.clock() - D.outAt > 5 then return "" end
    return D.out
end
-- ===== end b129qdiv A
local function doorTick(ST)   -- b181 doorfix; batch 64: BIDIRECTIONAL (was host-scan / client-apply). D.applied[k] = last known state ("1"/"0") from either side
    if FL.doors ~= 1 then return end
    local D = ST.door or {seen = {}, out = {}, listAt = 0, applied = {}}; ST.door = D
    local lp = S2.io and S2.io.pawn; if not valid(lp) then return end
    local now = os.clock()
    local dfix = (FL.doorfix or 1) ~= 0                                          -- b181 doorfix (live switch, default on)
    if (FL.doorre or 0) == 1 then                                                -- b187 doorre: a client save load (no new session) re-sends the doors too
        if api.ROLE == "host" then local nl = x1part("N"); if nl and D.nl ~= nl then if D.nl then D.sess = -1 end; D.nl = nl end
        else local ad = lp:GetAddress(); if D.pw ~= ad then if D.pw then D.applied = {}; D.seen = {}; D.pend = nil; log("S2CORE door: b187 pawn novo (load): registro de portas zerado") end; D.pw = ad end end
    end
    if dfix and api.ROLE == "host" then                                          -- b181 doorfix: a NEW peer session (client reload / reconnect): re-queue the door states once
        local sn = ST.peer and ST.peer.sessions or 0
        if D.sess ~= sn then
            if D.sess ~= nil then
                local c = 0
                for k, e in pairs(D.seen) do if e.open or e.chg then D.out[k] = {open = e.open, at = now}; c = c + 1 end end
                if c > 0 then log(string.format("S2CORE door: sessao #%d do outro lado: %d estado(s) de porta reenviados", sn, c)) end
            end
            D.sess = sn
        end
    end
    local ents, have = {}, {}                                                    -- b181 doorfix: the #O entries of now + the doors still waiting to stream in
    for ent in (remoteField("O") or ""):gmatch("[^|]+") do
        local k, st = ent:match("^([%-%d,]+);([01])$")
        if k then ents[#ents + 1] = {k, st}; have[k] = true end
    end
    if dfix and D.pend then for k, p in pairs(D.pend) do if not have[k] then ents[#ents + 1] = {k, p.st} end end end
    for _, e2 in ipairs(ents) do
        local k, st = e2[1], e2[2]
        local pn = D.pend and D.pend[k]
        if D.applied[k] == st then if pn then D.pend[k] = nil end
        elseif not (pn and pn.st == st and now - pn.at < ((pn.n or 1) < 10 and 2 or 10)) then   -- b181: retry every 2 s (10 s after 10 tries)
            if not dfix then D.applied[k] = st end
            local hit = nil
            hit = M.kf.find(FindAllOf("DoorView"), k)          -- b71kf: exact, else (keyfix) the neighbour key
            if hit then
                D.applied[k] = st; if D.pend then D.pend[k] = nil end            -- b181 doorfix: applied only when the door was found here
                local lk = k; pcall(function() lk = doorKey(hit) end)
                if lk ~= k then D.applied[lk] = st; log("S2CORE keyfix: porta " .. k .. " = chave local " .. lk) end
                local y0 = doorLeafYaw(hit)
                local ok, e = pcall(function() if st == "1" then hit:ForceOpenDoor() else hit:ForceCloseDoor() end end)
                local s0 = D.seen[lk]                         -- record the applied state so our own scan does not echo it back (b71kf: local key)
                if s0 then s0.open = (st == "1"); s0.chg = true elseif y0 then D.seen[lk] = {y0 = y0, open = (st == "1"), chg = true} end
                log(string.format("S2CORE door: %s -> %s ok=%s %s", k, st == "1" and "ForceOpenDoor" or "ForceCloseDoor", tostring(ok), ok and "" or tostring(e)))
            elseif dfix then                                                      -- b181 doorfix: keep it pending, retry (throttled), give up after 200 tries
                D.pend = D.pend or {}
                local n = (pn and pn.st == st and pn.n or 0) + 1
                if n == 1 then log("S2CORE door: porta " .. k .. " nao encontrada neste PC (fica pendente ate carregar)") end
                if n > 200 then D.pend[k] = nil; D.applied[k] = st; log("S2CORE door: porta " .. k .. " nunca apareceu aqui: desisto")
                else D.pend[k] = {st = st, at = now, n = n} end
            else log("S2CORE door: porta " .. k .. " nao encontrada neste PC") end
        end
    end
    if now - D.listAt < FL.doorsec then return end
    D.listAt = now
    local lpos = lp:K2_GetActorLocation()
    local r2 = FL.doorr * FL.doorr
    for _, o in ipairs(FindAllOf("DoorView") or {}) do pcall(function()
        if not o:IsValid() then return end
        local q = o:K2_GetActorLocation()
        if (q.X - lpos.X) ^ 2 + (q.Y - lpos.Y) ^ 2 > r2 then return end
        local k = doorKey(o); local y = doorLeafYaw(o); if not y then return end
        local s = D.seen[k]
        if not s then D.seen[k] = {y0 = y, open = false}; return end
        local open = math.abs(api.angDiff(y, s.y0)) > 20
        if open ~= s.open then
            s.open = open; s.chg = true; D.out[k] = {open = open, at = now}; D.applied[k] = open and "1" or "0"; if D.pend then D.pend[k] = nil end   -- b181
            log(string.format("S2CORE door: %s %s (folha %.0f -> %.0f)", k, open and "ABERTA" or "FECHADA", s.y0, y))
        end
    end) end
end
local function doorOut(ST)
    local D = ST.door; if not D then return "" end   -- batch 64: both sides send
    local now, t = os.clock(), {}
    for k, e in pairs(D.out) do if now - e.at < FL.doorhold then t[#t + 1] = k .. ";" .. (e.open and "1" or "0") else D.out[k] = nil end end
    if #t == 0 then return "" end
    return "#O:" .. table.concat(t, "|")
end
-- ===== batch 58: destructible props (wooden barricades etc.). Measured 2026-10-14: breaking one REMOVES the actor (it is gone from
-- FindAllOf), there is no broken flag. So a side keeps the set of destructible keys it has seen within dobjr; a key that was in range before
-- and is gone now (while still inside the radius) really broke -> "#Z:<key>". BIDIRECTIONAL (user: dynamic events go both ways): either
-- player's break is reported and the other side removes its copy. Because K2_DestroyActor does NOT persist (World Partition re-streams the
-- actor back, seen 00:41/00:42), a permanent set B.broken is kept and the removal is RE-ENFORCED every scan while the actor re-appears. =====
local function dobjKey(o) return doorKey(o) end
local function dobjList() return {"BP_DestructibleObject_C", "BP_DestructibleObject_Small_C"} end
local function dobjFind(k, near)   -- batch 65: exact key, else (near=true) the nearest prop within dobjnear cm of the key (planks have physics: a nudge moves the key)
    local kx, ky, kz = k:match("^(%-?%d+),(%-?%d+),(%-?%d+)$")
    kx, ky, kz = tonumber(kx), tonumber(ky), tonumber(kz)
    local best, bd = nil, (FL.dobjnear or 120) ^ 2
    for _, cls in ipairs(dobjList()) do
        for _, o in ipairs(FindAllOf(cls) or {}) do
            local ok, m = pcall(function() return o:IsValid() and dobjKey(o) == k end)
            if ok and m then return o end
            if near and kx then pcall(function()
                if not o:IsValid() or o.bHidden == true then return end
                local q = o:K2_GetActorLocation()
                local d = (q.X - kx * 10) ^ 2 + (q.Y - ky * 10) ^ 2 + (q.Z - kz * 10) ^ 2
                if d < bd then best, bd = o, d end
            end) end
        end
    end
    return best
end
-- batch 60: apply the other player's break. The prop's break is data-driven (DestructibleObjectPrototypes.cfg, D_WoodenPlank_01..: phase 0
-- actions = PlayVisualEffect NS_Destruct_WoodenPlanks, PlaySound, Destroy); the native DestructibleObject:StartDestructActions() (no params)
-- is the C++ runner of that list. Mode 2 replays the same two assets by hand. Returns a short label for the log.
local DOBJ_NS = "/Game/_Stalker_2/VFX/Environment/Destruction/Wood_1/Niagara/NS_Destruct_WoodenPlanks.NS_Destruct_WoodenPlanks"
local DOBJ_SFX = "/Game/_STALKER2/Audio/WwiseAudio/Events/PhysicsInteraction/Destruct/SFX_PhysicsInteraction_Destruct_BigBox.SFX_PhysicsInteraction_Destruct_BigBox"
-- batch 61: the other player's break HIDES the copy (hidden + no collision) instead of K2_DestroyActor. Measured 2026-10-14 (user):
-- destroy -> World Partition streams the prop back -> the 1 s enforce destroys it again = visible flicker. A hidden actor stays loaded, so
-- nothing is re-streamed; the enforce only re-hides a fresh instance (a cell reload while far away).
local function dobjHide(o)
    local ok, e = pcall(function() o:SetActorHiddenInGame(true); o:SetActorEnableCollision(false) end)
    return "hide ok=" .. tostring(ok) .. (ok and "" or " " .. tostring(e))
end
local function dobjHidden(o) local ok, h = pcall(function() return o.bHidden end); return ok and h == true end
local function dobjBreak(o, B, k)
    local m = FL.dobjfx or 0
    if m == 1 then
        local ok, e = pcall(function() o:StartDestructActions() end)
        if ok then B.fxAt = B.fxAt or {}; B.fxAt[k] = os.clock(); return "StartDestructActions ok" end   -- left to the game; enforce only after 3 s
        log("S2CORE dobj: StartDestructActions erro " .. tostring(e) .. " -> K2_DestroyActor")
    elseif m == 2 then
        pcall(function()
            local q, r = o:K2_GetActorLocation(), o:K2_GetActorRotation()
            local ns = asset(DOBJ_NS)
            if ns then StaticFindObject("/Script/Niagara.Default__NiagaraFunctionLibrary"):SpawnSystemAtLocation(o, ns, q, r, {X = 1, Y = 1, Z = 1}, true, true, 0, true) end
            local ev = asset(DOBJ_SFX)
            if ev then StaticFindObject("/Script/AkAudio.Default__AkGameplayStatics"):PostEventAtLocation(ev, q, r, o) end
        end)
    end
    return dobjHide(o)
end
-- ===== b74world: GENERIC destructible mirror (flag dobjgen). Whole-game table s2u_destruct.lua (gen_destruct_table.py, from the
-- user's DestructibleObjectPrototypes: 435 prototypes, 391 meshes; kinds destroy/mesh/explode/physics/fx/movable). Differences from the
-- b58-b65 path above (which stays as it is under dobjgen=0):
--  * scans the NATIVE class DestructibleObject (all 10 BP subclasses; the old list missed BP_DestructibleObject_NoGravity_C);
--  * identity = the actor's full name on this PC; its key is the position where THIS PC first saw it (home key), so a pushed/shot
--    physics prop is no longer a "break" (the old key-only scan reported every move >= 10 cm as a break);
--  * movable props (256 prototypes, no break action) are never reported or touched (host-master physics: not synced);
--  * a break that LEAVES the actor (loot crates/ammo boxes/parcels open = SetNewMesh + SpawnItems, explosives = SetNewMesh + Explode,
--    lamps, metal shields) is seen as a phase/mesh step and sent as "<key>@<step>"; a removal is sent as "<key>" (same #Z channel);
--  * the receiver runs the object's own break (dobjfx=1: StartDestructActions, the call the stock Quest BP makes on its DestroyReceiver),
--    measures it 2 s later and falls back to the table replay (prototype VFX + DestructAudioEvent + hide / new mesh / physics);
--  * echo guard: the receiver's own change is quiet for dobjquiet s (default 6) and its home key is marked broken, so it is never sent back. =====
-- b77tp meshsafe: UE4SS 3.0.1 GetFName/GetClass/GetOuter on a NULL object wrapper is a native access violation (reading 0x18),
-- NOT a Lua error: pcall cannot catch it (6/6 crash dumps 19:14-19:30, UE4SS.dll+0x2ace27, Lua frame = DG.mesh). IsValid is null-safe.
function M.meshName(o, comp)
    if (FL.meshsafe or 0) ~= 1 then return o[comp].StaticMesh:GetFName():ToString() end   -- b76 behaviour
    if not (o and o:IsValid()) then return nil end
    local c = o[comp]; if not (c and c:IsValid()) then return nil end
    local sm = c.StaticMesh
    if not (sm and sm:IsValid()) then
        M.meshNull = (M.meshNull or 0) + 1
        if M.meshNull <= 10 or M.meshNull % 100 == 0 then log(string.format("S2CORE meshsafe: malha nula (%s.%s) n=%d %s", comp, "StaticMesh", M.meshNull, tostring(o:GetFullName()):match("[^%.:]+$") or "?")) end
        return nil
    end
    return sm:GetFName():ToString()
end
do   -- no new top-level local (the core is near Lua's 200-local limit): DG lives in this block and in M.dg
local DG = {T = nil, cdo = {}, fxAt = 0, fxN = 0}
M.dg = DG
function DG.tab()
    if DG.T ~= nil then return DG.T end
    DG.T = false
    local f = io.open(api.BR .. "s2u_destruct.lua", "r")
    if not f then log("S2CORE dobjgen: falta s2u_destruct.lua (sem tabela: todo destrutivel conta como 'destroy')"); return false end
    local s = f:read("*a"); f:close()
    local fn, e = load(s, "s2u_destruct.lua")
    if not fn then log("S2CORE dobjgen: s2u_destruct.lua erro " .. tostring(e)); return false end
    local ok, T = pcall(fn)
    if ok and type(T) == "table" then
        local n = 0; for _ in pairs(T) do n = n + 1 end
        DG.T = T; log("S2CORE dobjgen: tabela carregada, " .. n .. " malhas")
    end
    return DG.T
end
function DG.name(o) local n; pcall(function() n = o:GetFullName():gsub("^%S+ ", "") end); return n end
function DG.mesh(o) local m; pcall(function() m = M.meshName(o, "StaticMeshComponent") end); return m end
function DG.sig(o) local p = -1; pcall(function() p = o:GetCurrentPhaseIndex() end); return p, DG.mesh(o) or "?" end
function DG.info(B, o, name)      -- per actor, cached: k = kind, e = table entry, w = watched (the actor can stay after a break)
    local c = B.cls[name]; if c then return c end
    local ms = DG.mesh(o); local T = DG.tab(); local e = T and ms and T[ms] or nil
    c = {k = e and e.k or "?", e = e}
    if e and (e.nm ~= "" or e.k == "physics" or e.k == "explode" or (e.p or 1) > 1) then c.w = true end
    if ms or (FL.meshsafe or 0) ~= 1 then B.cls[name] = c end   -- b77tp: no mesh yet -> classify again next sweep
    return c
end
function DG.cdoOf(p)
    local o = DG.cdo[p]; if o and valid(o) then return o end
    o = StaticFindObject(p); DG.cdo[p] = o; return o
end
function DG.replay(o, c, ph, now)   -- the table's own assets: VFX + sound, then the persistent part of the break
    local e, r = c.e or {}, {}
    if now - DG.fxAt >= 1 then DG.fxAt = now; DG.fxN = 0 end
    if DG.fxN < (FL.dobjfxmax or 4) then
        DG.fxN = DG.fxN + 1
        pcall(function()
            local q, rot = o:K2_GetActorLocation(), o:K2_GetActorRotation()
            if e.fx and e.fx ~= "" then local ns = asset(e.fx); if ns then DG.cdoOf("/Script/Niagara.Default__NiagaraFunctionLibrary"):SpawnSystemAtLocation(o, ns, q, rot, {X = 1, Y = 1, Z = 1}, true, true, 0, true); r[#r + 1] = "vfx" end end
            if e.au and e.au ~= "" then local ev = asset(e.au); if ev then DG.cdoOf("/Script/AkAudio.Default__AkGameplayStatics"):PostEventAtLocation(ev, q, rot, o); r[#r + 1] = "som" end end
        end)
    end
    local k = c.k
    if ph == nil and (k == "destroy" or k == "fx" or k == "?") then r[#r + 1] = dobjHide(o)
    elseif e.nm and e.nm ~= "" then
        local ok = pcall(function() o.StaticMeshComponent:SetStaticMesh(asset(e.nm)) end); r[#r + 1] = "malha ok=" .. tostring(ok)
    elseif k == "physics" then
        local ok = pcall(function() o.StaticMeshComponent:SetSimulatePhysics(true) end); r[#r + 1] = "fisica ok=" .. tostring(ok)
    elseif ph == nil then r[#r + 1] = dobjHide(o) end
    return "replay " .. table.concat(r, " ")
end
function DG.apply(B, o, name, k, ph, now)
    local c = DG.info(B, o, name)
    B.quiet[name] = now + (FL.dobjquiet or 6)
    if ph == nil and B.home[name] then B.broken[B.home[name]] = true end   -- echo guard for a near (not exact) match
    local m = FL.dobjfx or 0
    if c.k == "explode" and (FL.dobjexp or 0) ~= 1 then m = 2 end           -- no second real explosion (its damage would reach the other player twice)
    if m == 1 then
        local p0, m0 = DG.sig(o)
        local ok, err = pcall(function() o:StartDestructActions() end)
        if ok then B.pend[name] = {at = now, p = p0, m = m0, k = k, ph = ph}; B.quiet[name] = now + (FL.dobjquiet or 6); return c.k .. " StartDestructActions ok (medido em 2 s)" end
        log("S2CORE dobjgen: StartDestructActions erro " .. tostring(err) .. " -> replay")
    elseif m == 0 and ph == nil then return c.k .. " " .. dobjHide(o) end   -- b61 behaviour for a removal
    return c.k .. " " .. DG.replay(o, c, ph, now)
end
function DG.find(B, k)            -- this PC's copy of the other side's key: same home key, else same current key, else nearest unhidden within dobjnear
    local kx, ky, kz = k:match("^(%-?%d+),(%-?%d+),(%-?%d+)$")
    kx, ky, kz = tonumber(kx), tonumber(ky), tonumber(kz)
    local best, bn, bd = nil, nil, (FL.dobjnear or 120) ^ 2
    for _, o in ipairs(FindAllOf("DestructibleObject") or {}) do
        local hit = nil
        pcall(function()
            if not o:IsValid() then return end
            local n = DG.name(o); if not n then return end
            local q = o:K2_GetActorLocation()
            local ck = string.format("%.0f,%.0f,%.0f", q.X / 10, q.Y / 10, q.Z / 10)
            if B.home[n] == k or (not B.home[n] and ck == k) then hit = n; return end
            if kx and o.bHidden ~= true then
                local d = (q.X - kx * 10) ^ 2 + (q.Y - ky * 10) ^ 2 + (q.Z - kz * 10) ^ 2
                if d < bd then best, bn, bd = o, n, d end
            end
        end)
        if hit then return o, hit end
    end
    return best, bn
end
function DG.alive(name, x, y)      -- b75dg: a broken actor can stay listed, valid, parked at 0,0,0 (measured 18:4x): alive = same name AND still within 5 m
    for _, o in ipairs(FindAllOf("DestructibleObject") or {}) do
        local same = false
        pcall(function()
            if o:IsValid() and DG.name(o) == name then
                if not x then same = true; return end
                local q = o:K2_GetActorLocation(); same = (q.X - x) ^ 2 + (q.Y - y) ^ 2 <= 500 ^ 2
            end
        end)
        if same then return true end
    end
    return false
end
function DG.report(B, k, now, why)
    if (FL.dobjgen or 0) == 2 then log("S2CORE dobjgen: (seco) quebrado " .. k .. " (" .. why .. ")"); return end
    B.out[k] = {at = now}; B.mine = B.mine or {}; B.mine[#B.mine + 1] = k
    log("S2CORE dobj: quebrado " .. k .. " (" .. why .. ")")
end
function DG.tick(ST)
    local B = ST.dobj or {}; ST.dobj = B
    local function reset() B.broken = {}; B.seen = {}; B.miss = {}; B.out = {}; B.cls = {}; B.home = {}; B.sigs = {}; B.quiet = {}; B.pend = {}; B.mine = {} end
    if not B.gen then reset(); B.gen = true; B.listAt = 0 end
    local lp = S2.io and S2.io.pawn; if not valid(lp) then return end
    local now = os.clock()
    local W = ST.wg                                             -- same world gate as the b60 path: a load / pawn change -> the save defines the world
    if W and W.key ~= B.wkey then
        if B.wkey then log("S2CORE dobjgen: jogador local trocado (load?) -> estado reiniciado") end
        B.wkey = W.key; reset()
    end
    if W and not W.open then B.seen = {}; B.miss = {} end
    local lvl = nil
    pcall(function() local w = FindFirstOf("World"); if w then lvl = w:GetFullName() end end)
    if lvl and lvl ~= B.level then B.level = lvl; reset(); log("S2CORE dobjgen: nivel " .. tostring(lvl):sub(1, 60) .. " -> estado reiniciado") end
    for ent in (remoteField("Z") or ""):gmatch("[^|]+") do    -- the other player's breaks (both directions); applied once each
        if not B.broken[ent] then
            B.broken[ent] = true
            local k, ph = ent:match("^([%-%d,]+)@(%d+)$"); ph = tonumber(ph); k = k or ent
            if (FL.dobjgen or 0) == 2 then log("S2CORE dobjgen: (seco) recebido " .. ent)
            else
                local o, n = DG.find(B, k)
                if o then log(string.format("S2CORE dobjgen: %s -> fx=%d %s", ent, FL.dobjfx or 0, DG.apply(B, o, n, k, ph, now)))
                else log("S2CORE dobjgen: " .. ent .. " nao encontrado neste PC") end
            end
        end
    end
    if now - (B.listAt or 0) < (FL.dobjsec or 1) then return end
    B.listAt = now
    local t0 = os.clock()
    local lpos = lp:K2_GetActorLocation()
    local r2 = (FL.dobjr or 2500) ^ 2
    local cur, nAll, nW = {}, 0, 0
    for _, o in ipairs(FindAllOf("DestructibleObject") or {}) do pcall(function()
        nAll = nAll + 1
        if not o:IsValid() then return end
        local name = DG.name(o); if not name then return end
        local c = B.cls[name]
        if c and c.k == "movable" then return end               -- pure physics props: never a break, never touched
        local q = o:K2_GetActorLocation()
        if (q.X - lpos.X) ^ 2 + (q.Y - lpos.Y) ^ 2 > r2 then return end
        c = c or DG.info(B, o, name)
        if c.k == "movable" then return end
        local k = B.home[name]
        if not k then k = string.format("%.0f,%.0f,%.0f", q.X / 10, q.Y / 10, q.Z / 10); B.home[name] = k end
        local v = {x = q.X, y = q.Y, k = k, o = o}
        if c.w then v.p, v.m = DG.sig(o); nW = nW + 1 end
        cur[name] = v
    end) end
    for name, p in pairs(B.pend) do                           -- dobjfx=1: did the stock break do anything? (live measurement + fallback)
        if now - p.at >= 2 then
            B.pend[name] = nil
            local v = cur[name]
            if not v then log("S2CORE dobjgen: SDA " .. p.k .. " efeito=sumiu (stock break ok)")
            else
                local ph, ms = DG.sig(v.o)
                if ph ~= p.p or ms ~= p.m or dobjHidden(v.o) then log(string.format("S2CORE dobjgen: SDA %s efeito=fase %s->%s malha %s->%s", p.k, p.p, ph, p.m, ms))
                else log("S2CORE dobjgen: SDA " .. p.k .. " SEM efeito -> " .. DG.replay(v.o, DG.info(B, v.o, name), p.ph, now)) end
                B.quiet[name] = now + 2
            end
        end
    end
    for name, v in pairs(cur) do
        if B.broken[v.k] and (FL.dobjgen or 0) == 1 and not B.pend[name] and now >= (B.quiet[name] or 0) and not dobjHidden(v.o) then   -- re-streamed copy of a broken prop
            local r = dobjHide(v.o)
            B.rehid = (B.rehid or 0) + 1
            if B.rehid <= 20 then log("S2CORE dobjgen: " .. v.k .. " reapareceu -> " .. r .. " (n=" .. B.rehid .. ")") end
        elseif v.p and not ((FL.meshsafe or 0) == 1 and v.m == "?") then   -- watched: a phase/mesh step that leaves the actor in place (b77tp: no mesh = no compare)
            local s = B.sigs[name]
            if not s or now < (B.quiet[name] or 0) then B.sigs[name] = {p = v.p, m = v.m, n = s and s.n or 0}
            elseif s.p ~= v.p or s.m ~= v.m then
                local step = (v.p > s.p) and v.p or ((s.n or 0) + 1)
                local ent = v.k .. "@" .. step
                B.sigs[name] = {p = v.p, m = v.m, n = step}
                if not B.broken[ent] then B.broken[ent] = true; DG.report(B, ent, now, string.format("fase %s->%s malha %s->%s", s.p, v.p, s.m, v.m)) end
            end
        end
    end
    for name, v in pairs(B.seen) do                           -- just went missing while in range -> a miss record
        if not cur[name] and not B.miss[name] then
            local dx, dy = v.x - lpos.X, v.y - lpos.Y
            if dx * dx + dy * dy <= r2 then B.miss[name] = {n = 0, x = v.x, y = v.y, k = v.k} end
        end
    end
    for name, e in pairs(B.miss) do
        if cur[name] then B.miss[name] = nil
        elseif (e.x - lpos.X) ^ 2 + (e.y - lpos.Y) ^ 2 > r2 then B.miss[name] = nil   -- walked away: unknown, forget it
        else
            e.n = e.n + 1
            if e.n >= (FL.dobjmiss or 3) then
                B.miss[name] = nil
                if not DG.alive(name, e.x, e.y) then
                    if B.broken[e.k] or now < (B.quiet[name] or 0) then B.broken[e.k] = true   -- ours to apply / already known: never echoed
                    else B.broken[e.k] = true; DG.report(B, e.k, now, "sumiu") end
                end
            end
        end
    end
    local seen = {}
    for name, v in pairs(cur) do seen[name] = {x = v.x, y = v.y, k = v.k} end   -- no UObject reference is kept between scans
    B.seen = seen
    B.st = B.st or {at = now, n = 0, ms = 0, all = 0, near = 0, w = 0}
    local S = B.st; S.n = S.n + 1; S.ms = S.ms + (os.clock() - t0) * 1000; S.all = nAll; local c = 0; for _ in pairs(cur) do c = c + 1 end; S.near = c; S.w = nW
    if (FL.dobjstat or 1) == 1 and now - S.at >= 60 then
        log(string.format("S2CORE dobjgen: varredura %d x, media %.2f ms, destrutiveis carregados=%d perto(nao moveis)=%d vigiados=%d", S.n, S.ms / S.n, S.all, S.near, S.w))
        B.st = {at = now, n = 0, ms = 0, all = 0, near = 0, w = 0}
    end
end
end   -- b74world block
local function dobjTick(ST)
    if FL.dobj ~= 1 then return end
    if (FL.dobjgen or 0) >= 1 then return M.dg.tick(ST) end      -- b74world generic path
    if ST.dobj and ST.dobj.gen then ST.dobj = nil end          -- leaving the generic path: start the b58-b65 path clean
    local B = ST.dobj or {}; ST.dobj = B
    B.seen = B.seen or {}; B.out = B.out or {}; B.broken = B.broken or {}; B.miss = B.miss or {}; B.listAt = B.listAt or 0
    local lp = S2.io and S2.io.pawn; if not valid(lp) then return end
    local now = os.clock()
    local W = ST.wg                                             -- batch 60: reuse the body world gate (bodyGate, batch 44/52): pawn gone/changed
    if W and W.key ~= B.wkey then                               -- = a load -> the save defines the world: forget everything
        if B.wkey then log("S2CORE dobj: jogador local trocado (load?) -> estado reiniciado") end
        B.wkey = W.key; B.broken = {}; B.seen = {}; B.miss = {}; B.out = {}
    end
    if W and not W.open then B.seen = {}; B.miss = {} end      -- while the world is not stable (load, own teleport): no miss can count
    local lvl = nil                                             -- level name: a new level / save resets the broken set
    pcall(function() local w = FindFirstOf("World"); if w then lvl = w:GetFullName() end end)
    if lvl and lvl ~= B.level then
        B.level = lvl; B.broken = {}; B.seen = {}; B.miss = {}; B.out = {}
        log("S2CORE dobj: nivel " .. tostring(lvl):sub(1, 60) .. " -> estado reiniciado")
    end
    for k in (remoteField("Z") or ""):gmatch("[^|]+") do      -- BIDIRECTIONAL: the other player's break -> remember it for good
        if not B.broken[k] then
            B.broken[k] = true; B.seen[k] = nil
            local hit = dobjFind(k, true)
            if hit then
                if FL.keyfix == 1 then local lk; pcall(function() lk = dobjKey(hit) end)
                    if lk and lk ~= k then B.broken[lk] = true; B.seen[lk] = nil; log("S2CORE keyfix: destruivel " .. k .. " = chave local " .. lk) end end
                log(string.format("S2CORE dobj: %s -> fx=%d %s", k, FL.dobjfx or 0, dobjBreak(hit, B, k)))
            else log("S2CORE dobj: destruivel " .. k .. " nao encontrado neste PC") end
        end
    end
    if now - B.listAt < (FL.dobjsec or 1) then return end
    B.listAt = now
    local lpos = lp:K2_GetActorLocation()
    local r2 = (FL.dobjr or 2500) ^ 2
    local cur = {}
    for _, cls in ipairs(dobjList()) do
        for _, o in ipairs(FindAllOf(cls) or {}) do pcall(function()
            if not o:IsValid() then return end
            local q = o:K2_GetActorLocation()
            if (q.X - lpos.X) ^ 2 + (q.Y - lpos.Y) ^ 2 > r2 then return end
            local k = dobjKey(o); cur[k] = {x = q.X, y = q.Y, z = q.Z, o = o}
        end) end
    end
    for k, v in pairs(cur) do                                 -- enforce: a known-broken prop re-appeared (World Partition re-stream) -> remove it again
        if B.broken[k] and now - ((B.fxAt or {})[k] or -1e9) >= 3 and not dobjHidden(v.o) then   -- batch 60: not while the stock break plays
            local r = dobjHide(v.o)                                                                 -- batch 61: hide, never destroy (destroy = flicker)
            B.rehid = (B.rehid or 0) + 1
            if B.rehid <= 20 then log("S2CORE dobj: " .. k .. " reapareceu -> " .. r .. " (n=" .. B.rehid .. ")") end
        end
    end
    -- batch 60 fix (two live bugs, found by the b60 stub test): the b58/59 code (1) indexed a nil B.miss[k] on the FIRST miss, so every
    -- scan raised and stopped before `B.seen = cur`; (2) counted misses only for keys still in B.seen, but a missing key leaves B.seen
    -- after one scan, so the count could never reach dobjmiss. Now a miss record keeps the position and is counted on its own.
    for k, v in pairs(B.seen) do                              -- just went missing while in range -> open a miss record (with its position)
        if not cur[k] and not B.broken[k] and type(B.miss[k]) ~= "table" then
            local dx, dy = v.x - lpos.X, v.y - lpos.Y
            if dx * dx + dy * dy <= r2 then B.miss[k] = {n = 0, x = v.x, y = v.y} end
        end
    end
    for k, e in pairs(B.miss) do                              -- a miss must repeat dobjmiss scans in range AND the actor must be findable nowhere -> a real break
        if cur[k] or B.broken[k] or type(e) ~= "table" or not e.x then B.miss[k] = nil
        elseif (e.x - lpos.X) ^ 2 + (e.y - lpos.Y) ^ 2 > r2 then B.miss[k] = nil      -- player walked away: unknown, forget it
        else
            e.n = e.n + 1
            if e.n >= (FL.dobjmiss or 3) and not dobjFind(k) then
                B.broken[k] = true; B.out[k] = {at = now}; B.miss[k] = nil
                B.mine = B.mine or {}; B.mine[#B.mine + 1] = k          -- batch 65: own breaks, re-sent in rotation (a load / late join on the other side gets them again)
                log("S2CORE dobj: quebrado " .. k)
            end
        end
    end
    B.seen = cur
end
local function dobjOut(ST)
    local B = ST.dobj; if not B then return "" end
    local now, t = os.clock(), {}
    for k, e in pairs(B.out) do if now - e.at < (FL.dobjhold or 6) then t[#t + 1] = k else B.out[k] = nil end end
    local M = B.mine                                      -- batch 65: plus a rotating window of every own break so far (dobjrot keys, advanced once per second)
    if M and #M > 0 then
        local n = math.min(#M, FL.dobjrot or 10)
        if now - (B.rotAt or 0) >= 1 then B.rotAt = now; B.rot = ((B.rot or 0) + n) % #M end
        for i = 0, n - 1 do t[#t + 1] = M[((B.rot or 0) + i) % #M + 1] end
    end
    if #t == 0 then return "" end
    return "#Z:" .. table.concat(t, "|")
end
-- ===== batch 59: interactable power/interaction state (levers, hatch valves, lamps...). Measured 2026-10-14 from iprobe: the bool that
-- flips on an interaction is bEnabled (a real bool on InteractableObject); pulling BP_Lever_Vertical set en=true (and bBroken=true on the lever),
-- and the lamps the lever powers went en=true. FindAllOf("InteractableObject") already returns the BP subclasses (Lever, lamps, valves), so the
-- host reports each nearby InteractableObject whose bEnabled changed -> "#P:<key>;<0|1>"; the client finds its copy by key and calls SetEnabled. =====
-- batch 68g: interaction prompts. A used one-shot lever keeps bEnabled but turns its click components inactive (measured: host lever
-- SingleClickLeverDown/Up active=false/false, client mirror false/true -> the client still showed the prompt). Mask = the actor's
-- InteractionComponent bIsInteractionActive bits, components sorted by name; sent with the state and applied on the other side.
local function ienComps(o)
    local t = {}
    if not valid(o) then return t end                  -- b112 (#5): GetFullName only after IsValid
    local full = o:GetFullName()
    for _, c in ipairs(FindAllOf("InteractionComponent") or {}) do pcall(function()
        if c:IsValid() then local ow = c:GetOwner(); if valid(ow) and ow:GetFullName() == full then t[#t + 1] = {n = c:GetFName():ToString(), c = c} end end
    end) end
    table.sort(t, function(a, b) return a.n < b.n end)
    return t
end
local function ienMask(o)
    local m = ""
    for _, e in ipairs(ienComps(o)) do local a = false; pcall(function() a = e.c.bIsInteractionActive == true end); m = m .. (a and "1" or "0") end
    return m
end
local function ienApplyMask(o, m, k)
    local cs = ienComps(o); local n = 0
    for i, e in ipairs(cs) do local b = m:sub(i, i); if b == "0" or b == "1" then
        local cur = nil; pcall(function() cur = e.c.bIsInteractionActive end)
        if cur ~= (b == "1") then if pcall(function() e.c:SetInteractionActive(b == "1") end) then n = n + 1 end end
    end end
    if n > 0 then log(string.format("S2CORE ien: %s prompts %s -> %s (%d mudados)", k, "?", m, n)) end
end
-- batch 69: b68 table-driven apply (offline Kismet decompile -> s2u_actions.lua; cj68.lua actApply), flag ientab
local function ienTab(I)
    if I.tab ~= nil then return I.tab end
    I.tab = false
    local function ld(n)
        local f = io.open(api.BR .. n, "r"); if not f then log("S2CORE ientab: falta " .. n); return nil end
        local src = f:read("*a"); f:close()
        local fn, e = load(src, n); if not fn then log("S2CORE ientab: " .. n .. " erro " .. tostring(e)); return nil end
        return fn()
    end
    local okA, A = pcall(ld, "s2u_actions.lua"); local okC, C = pcall(ld, "cj68.lua")
    if okA and type(A) == "table" and okC and type(C) == "table" then
        local n = 0; for _ in pairs(A) do n = n + 1 end
        I.tab = {ACT = A, M = C}; log("S2CORE ientab: tabela carregada, " .. n .. " classes")
    end
    return I.tab
end
-- ===== b71ia: interaction mirror (flag iamir). ia71.lua = pure logic (stub-tested); here only the game calls. =====
-- Detector (both PCs): PC:IsInteractionInProgress rising edge + latched GetInteractedActor (measured b70f: actor flickers to nil).
-- Receiver: find own copy (class + 10 cm key), teleport to the starter's stand point, face it, SetInteractionTarget(component),
-- Enhanced Input injection of IA_Interact until the stock interaction starts. Never reads hook params, never walks widgets,
-- keeps no UObject past the replay (A-Life pooling rule).
local IA = {st = nil, g = nil, m = nil, fresh = true}
local IA_PATH = "/Game/_Stalker_2/data/input/InputActions/Delayable/IA_Interact.IA_Interact"
local function iaPC() local pc = S2.io and S2.io.pawn; if valid(pc) then return pc end end
local function iaG(ST)
    if IA.g ~= nil then return IA.g end
    IA.g = false
    local function ld(n)
        local f = io.open(api.BR .. n, "r"); if not f then log("S2CORE iamir: falta " .. n); return nil end
        local s = f:read("*a"); f:close()
        local fn, e = load(s, n); if not fn then log("S2CORE iamir: " .. n .. " erro " .. tostring(e)); return nil end
        return fn()
    end
    local okM, M = pcall(ld, "ia71.lua"); local okT, T = pcall(ld, "s2u_interact.lua")
    if not (okM and type(M) == "table" and okT and type(T) == "table") then return false end
    IA.m = M
    local function sub() return FindFirstOf("EnhancedInputLocalPlayerSubsystem") end
    IA.g = {FL = FL, ROLE = api.ROLE, now = os.clock, log = log, T = T,
        pawnPos = function() local p; pcall(function() p = S2L.ioPawn():K2_GetActorLocation() end); return p end,
        pawnYaw = function() local y; pcall(function() y = S2L.ioPawn():K2_GetActorRotation().Yaw end); return y end,
        prog = function() local pc = iaPC(); if not pc then return nil end; local ok, v = pcall(function() return pc:IsInteractionInProgress() end); if ok then return v == true end end,
        actor = function() local pc = iaPC(); if not pc then return nil end; local a; pcall(function() local x = pc:GetInteractedActor(); if x and x:IsValid() then a = x end end); return a end,
        len = function() local pc = iaPC(); local v; pcall(function() v = pc:GetInteractLengthType() end); return v end,
        describe = function(a) local q = a:K2_GetActorLocation(); return a:GetClass():GetFName():ToString(), q.X, q.Y, q.Z end,
        find = function(cls, x, y, z, r)
            local best, bd = nil, r * r
            for _, o in ipairs(FindAllOf(cls) or {}) do pcall(function()
                if o:IsValid() then local q = o:K2_GetActorLocation(); local d = (q.X - x) ^ 2 + (q.Y - y) ^ 2 + (q.Z - z) ^ 2; if d <= bd then best, bd = o, d end end
            end) end
            return best
        end,
        busy = function()
            local pc = iaPC(); if not pc then return "sem pawn" end
            local b = nil
            pcall(function() if pc.bInCinematicSequence then b = "cinematica" end end)
            if ST.cv and ST.cv.fz then b = "congelado (#V)" end
            if ST.life and ST.life.down then b = "caido" end
            pcall(function() if not pc:IsAlive() then b = "morto" end end)
            return b
        end,
        teleport = function(x, y, z) return pcall(function() S2L.ioPawn():K2_SetActorLocation({X = x, Y = y, Z = z}, false, {}, true) end) end,
        face = function(o)
            if FL.iaface ~= 1 then return false end
            local pw = S2L.ioPawn(); if (FL.kzsafe or 0) == 1 and not valid(o) then return false end
            local p = pw:K2_GetActorLocation(); local q = o:K2_GetActorLocation()
            local dx, dy, dz = q.X - p.X, q.Y - p.Y, q.Z - (p.Z + 60)
            local yaw = math.deg(math.atan(dy, dx)); local pitch = math.deg(math.atan(dz, math.sqrt(dx * dx + dy * dy)))
            return pcall(function() api.UEHelpers.GetPlayerController():SetControlRotation({Pitch = pitch, Yaw = yaw, Roll = 0}) end)
        end,
        pickComp = function(o, e)
            local want = {}
            for n in ((e and e.comp) or ""):gmatch("([^,%.]+)%.[^,]+") do want[n] = true end
            local first, pref = nil, nil
            for _, x in ipairs(ienComps(o)) do
                local act = false; pcall(function() act = x.c.bIsInteractionActive == true end)
                if act then first = first or x.c; if want[x.n] then pref = pref or x.c end end
            end
            return pref or first
        end,
        setTarget = function(c) return pcall(function() S2L.ioPawn():SetInteractionTarget(c) end) end,
        press = function(on)
            local ia = asset(IA_PATH); local s = sub(); if not (ia and valid(s)) then return false end
            if FL.iainj == 2 then
                if not on then return true end
                return pcall(function()
                    local v = StaticFindObject("/Script/EnhancedInput.Default__EnhancedInputLibrary"):MakeInputActionValueOfType(1.0, 0.0, 0.0, 0)
                    s:InjectInputForAction(ia, v, {}, {})
                end)
            end
            if not on then return pcall(function() s:StopContinuousInputInjectionForAction(ia) end) end
            return pcall(function()
                local v = StaticFindObject("/Script/EnhancedInput.Default__EnhancedInputLibrary"):MakeInputActionValueOfType(1.0, 0.0, 0.0, 0)
                s:StartContinuousInputInjectionForAction(ia, v, {}, {})
            end)
        end,
        release = function()
            local pc, r = iaPC(), {}
            if not pc then return "sem pawn" end
            for _, fn in ipairs({"ResetInteractionTarget", "RestoreStatesAfterInteraction", "EnableInputAfterInteraction", "EnableInteractions"}) do
                r[#r + 1] = fn .. "=" .. tostring(pcall(function() pc[fn](pc) end))
            end
            return table.concat(r, " ")
        end,
        remoteField = function(tg) return remoteField(tg) end,
        ienMark = function(k) local I = ST.ien; if I then I.app = I.app or {}; I.app[k] = {w = "ia", at = os.clock()} end end}
    log("S2CORE iamir: modulo e tabela carregados")
    return IA.g
end
local function iaTick(ST)
    if FL.iamir ~= 1 and not (ST.iam and ST.iam.rep) then return end
    local G = iaG(ST); if not G then return end
    if IA.fresh then IA.fresh = false; pcall(IA.m.reset, ST, G, "modulo carregado"); pcall(G.press, false) end   -- a reload never leaves Interact held
    local ok, e = pcall(IA.m.detTick, ST, G); if not ok then log("S2CORE iamir det erro: " .. tostring(e)) end
    ok, e = pcall(IA.m.recvTick, ST, G); if not ok then log("S2CORE iamir recv erro: " .. tostring(e)); pcall(IA.m.reset, ST, G, "erro") end
end
local function iaOut(ST)
    if FL.iamir ~= 1 or not IA.m or not IA.g then return "" end
    return IA.m.out(ST, IA.g)
end
-- ===== b91choice (flag choice, default 0; user rule N3 "story choices is always host side, client cannot make story choices").
-- Whole-game table s2u_choice.lua (gen_choice.py from the Zone Kit quest graph): the quest BRANCH POINTS a player action outside a dialog
-- can fire (a listener whose firing cancels a parallel path, checks who killed, splits an If on an NPC's life, or changes sides/story
-- state). The host's quest graph is the only live story graph (the client's is muted by the L2 pak), so a client can only cause a
-- branch through the three channels that carry client actions INTO the host. Each one gets ONE gate on the host, keyed by the table:
--   kill  host shotResolve: a relayed client shot that hits a branch NPC (npc[guid], tier <= n3kill) is NOT applied (no damage, no
--         death, no hit/hostility event). Identity: n3id=1 CppMediator:GetActorByGuid(placeholder guid) for the table guids whose
--         placeholder is within n3r cm (nearest n3max), compared by address (no GUID call on a pawn); n3id=2 GetPlaceholderGuidByUObject
--         on the hit pawn. Cached per pawn address n3cache s.
--   trig  host trigTick: a client #QN trigger whose node is a branch (trig[sid], tier <= n3trig) is NOT fired; the host fires it by
--         walking in (its own graph).
--   use   host ienTick: a client #P state change of a branch usable (use[guid] == 1, the actor's Guid) is NOT applied; client side
--         (n3use=1): the prompts of such usables within n3ur cm are switched off (SetInteractionActive false), re-asserted every n3usesec s.
-- Items (get/lose-item listeners) stay with the N2 channel (b87 qitem): report only. Every function is inert while choice ~= 1.
function S2L.chTab()
    if S2L.chT ~= nil then return S2L.chT or nil end
    S2L.chT = false
    local ok, T = pcall(dofile, api.BR .. "s2u_choice.lua")
    if not ok or type(T) ~= "table" or type(T.npc) ~= "table" or type(T.use) ~= "table" or type(T.trig) ~= "table" then
        log("S2CORE choice: s2u_choice.lua ausente ou invalido: " .. tostring(T)); return nil
    end
    T.list = {}
    for g, v in pairs(T.npc) do
        if type(v) == "table" and (v[5] or 0) == 0 then T.list[#T.list + 1] = {g = g, x = v[1] or 0, y = v[2] or 0, tier = v[4] or 1} end
    end
    local nu = 0; for _, v in pairs(T.use) do if v == 1 then nu = nu + 1 end end
    T.nuse = nu
    local nt = 0; for _ in pairs(T.trig) do nt = nt + 1 end
    S2L.chT = T
    log(string.format("S2CORE choice: tabela %s: %d NPCs de escolha (%d resolviveis), %d usaveis, %d gatilhos", tostring(T.V), (function() local n = 0; for _ in pairs(T.npc) do n = n + 1 end return n end)(), #T.list, nu, nt))
    return T
end
function S2L.chSt(ST) ST.ch = ST.ch or {kc = {}, kn = 0, shots = 0, trig = 0, use = 0, blk = 0, ulog = {}, logAt = {}} return ST.ch end
function S2L.chLog(H, key, msg)                         -- at most one line per key per 30 s
    local now = os.clock()
    if now - (H.logAt[key] or -1e9) < 30 then return end
    H.logAt[key] = now; log(msg)
end
function S2L.chHex(g)                                   -- FGuid struct -> "%08X" x4 (the kit's 32-hex form), else nil
    if g == nil then return nil end
    local ok, h = pcall(function()
        local function u(v) v = math.tointeger(v) or 0; return v & 0xFFFFFFFF end
        return string.format("%08X%08X%08X%08X", u(g.A), u(g.B), u(g.C), u(g.D))
    end)
    if ok and h ~= "00000000000000000000000000000000" then return h end
    return nil
end
function S2L.chGuid(h)                                  -- "32 hex" -> {A,B,C,D} as signed int32 (the FGuid param form)
    local t = {}
    for i, k in ipairs({"A", "B", "C", "D"}) do
        local v = tonumber(h:sub(i * 8 - 7, i * 8), 16) or 0
        if v >= 0x80000000 then v = v - 0x100000000 end
        t[k] = v
    end
    return t
end
function S2L.chProt(ST, best)                           -- host: placeholder guid of a branch NPC if `best` is one, else false
    local T = S2L.chTab(); if not T then return false end
    local H = S2L.chSt(ST)
    local now, addr = os.clock(), best:GetAddress()
    local e = H.kc[addr]
    if e and now - e.at < (FL.n3cache or 10) then return e.g end
    local maxTier, g = FL.n3kill or 2, false
    if (FL.n3id or 1) == 2 then
        local lib = StaticFindObject("/Script/Stalker2.Default__QuestBindingFunctionLibrary")
        local hx = nil
        if valid(lib) then pcall(function() hx = S2L.chHex(lib:GetPlaceholderGuidByUObject(best)) end) end
        local v = hx and T.npc[hx]
        if v and (v[4] or 1) <= maxTier then g = hx end
    else
        if not valid(S2L.chCM) then S2L.chCM = StaticFindObject("/Script/Stalker2.Default__CppMediator") end
        local cm = S2L.chCM
        if valid(cm) then
            local q = best:K2_GetActorLocation()
            local r2, cand = (FL.n3r or 20000) ^ 2, {}
            for _, c in ipairs(T.list) do
                if c.tier <= maxTier then
                    local d2 = (c.x == 0 and c.y == 0) and 0 or (c.x - q.X) ^ 2 + (c.y - q.Y) ^ 2
                    if d2 <= r2 then cand[#cand + 1] = {c = c, d = d2} end
                end
            end
            table.sort(cand, function(a, b) return a.d < b.d end)
            for i = 1, math.min(#cand, FL.n3max or 60) do
                local o = nil
                pcall(function() o = cm:GetActorByGuid(S2L.chGuid(cand[i].c.g)) end)
                if valid(o) and o:GetAddress() == addr then g = cand[i].c.g; break end
            end
            H.kn = H.kn + 1
        end
    end
    H.kc[addr] = {at = now, g = g}
    for k, x in pairs(H.kc) do if now - x.at > 60 then H.kc[k] = nil end end
    return g
end
function S2L.chShotDeny(ST, best)                       -- host shotResolve: true = this client shot is not applied (N3)
    if ((FL.choice or 0) ~= 1 and (FL.lead or 0) ~= 1) or (FL.n3kill or 2) < 1 or api.ROLE ~= "host" then return false end
    local ok, g = pcall(S2L.chProt, ST, best)
    if not ok then S2L.chLog(S2L.chSt(ST), "kerr", "S2CORE choice: erro na identidade do NPC: " .. tostring(g)); return false end
    if not g then return false end
    if (FL.lead or 0) == 1 then return S2L.ldKill(ST, best, g) end   -- b92lead
    local H = S2L.chSt(ST); H.shots = H.shots + 1
    local nm = "?"; pcall(function() nm = best:GetFName():ToString() end)
    S2L.chLog(H, "k" .. g, string.format("S2CORE choice: tiro do cliente em %s (npc %s) NAO aplicado: so o host decide o destino deste NPC (N3) [%d]", nm, g, H.shots))
    return true
end
function S2L.chTrigDeny(ST, sid, seq)                   -- host trigTick: true = this client trigger is not fired on the host (N3)
    if ((FL.choice or 0) ~= 1 and (FL.lead or 0) ~= 1) or (FL.n3trig or 2) < 1 then return false end
    local T = S2L.chTab(); if not T then return false end
    local tier = T.trig[sid]
    if not tier or tier > (FL.n3trig or 2) then return false end
    if (FL.lead or 0) == 1 then return S2L.ldDeny(ST, "gatilho " .. sid .. " seq=" .. tostring(seq)) end   -- b92lead
    local H = S2L.chSt(ST); H.trig = H.trig + 1
    S2L.chLog(H, "t" .. sid, string.format("S2CORE choice: gatilho do cliente %s seq=%s NAO disparado no host: escolha de historia (N3, nivel %d); o host decide ao entrar [%d]", sid, tostring(seq), tier, H.trig))
    return true
end
function S2L.chUseDeny(ST, hit, k)                      -- host ienTick: true = the client's state change of this usable is not applied (N3)
    if ((FL.choice or 0) ~= 1 and (FL.lead or 0) ~= 1) or (FL.n3use or 1) ~= 1 or api.ROLE ~= "host" then return false end
    local T = S2L.chTab(); if not T then return false end
    local hx = nil; pcall(function() hx = S2L.chHex(hit.Guid) end)
    if not hx or T.use[hx] ~= 1 then return false end
    if (FL.lead or 0) == 1 then return S2L.ldDeny(ST, "uso " .. k) end   -- b92lead
    local H = S2L.chSt(ST); H.use = H.use + 1
    S2L.chLog(H, "u" .. k, string.format("S2CORE choice: uso do cliente em %s (guid %s) NAO aplicado no host: escolha de historia (N3) [%d]", k, hx, H.use))
    return true
end
function S2L.chUseTick(ST)                              -- client: branch usables near the player take no interaction here (N3)
    if (FL.lead or 0) == 1 then if (FL.n3use or 1) ~= 1 or not S2L.ldUseGate(ST) then return end   -- b92lead: both roles, while the peer leads
    elseif (FL.choice or 0) ~= 1 or (FL.n3use or 1) ~= 1 or api.ROLE == "host" then return end
    local H = S2L.chSt(ST)
    local now = os.clock()
    if now - (H.useAt or -1e9) < (FL.n3usesec or 2) then return end
    H.useAt = now
    local T = S2L.chTab(); if not T or (T.nuse or 0) == 0 then return end
    local lp = S2.io and S2.io.pawn; if not valid(lp) then return end
    local p = lp:K2_GetActorLocation()
    local r2 = (FL.n3ur or 2500) ^ 2
    for _, o in ipairs(FindAllOf("InteractableObject") or {}) do pcall(function()
        if not o:IsValid() then return end
        local q = o:K2_GetActorLocation()
        if (q.X - p.X) ^ 2 + (q.Y - p.Y) ^ 2 > r2 then return end
        local hx = S2L.chHex(o.Guid)
        if not hx or T.use[hx] ~= 1 then return end
        local n, kk = 0, doorKey(o)
        for i, e in ipairs(ienComps(o)) do
            local a = false; pcall(function() a = e.c.bIsInteractionActive == true end)
            if a and pcall(function() e.c:SetInteractionActive(false) end) then n = n + 1
                if (FL.lead or 0) == 1 then H.offk = H.offk or {}; H.offk[kk] = H.offk[kk] or {}; H.offk[kk][i] = true end   -- b92lead: given back on release
                pcall(S2L.lgNote, ST, "U", hx)   -- b129qdiv B: ledger by GUID
            end
        end
        if n > 0 then
            H.blk = H.blk + 1
            local k = doorKey(o)
            S2L.chLog(H, "b" .. k, string.format("S2CORE choice: %s (guid %s) bloqueado no cliente: %d prompts desligados, so o host usa (N3) [%d]", k, hx, n, H.blk))
        end
    end) end
end
-- ===== end b91choice
local function ienTick(ST)
    if FL.ien ~= 1 then return end
    local I = ST.ien or {}; ST.ien = I
    I.seen = I.seen or {}; I.out = I.out or {}; I.listAt = I.listAt or 0
    local lp = S2.io and S2.io.pawn; if not valid(lp) then return end
    local now = os.clock()
    for qk, q in pairs(I.pm or {}) do if now >= q.at then    -- b112 (#5): delayed prompt mask on the core tick (was a raw ExecuteWithDelay
        I.pm[qk] = nil                                         -- holding the actor 1.5 s); the actor is found again by key, valid() first
        local h = M.kf.find(FindAllOf("InteractableObject"), qk)
        if valid(h) then pcall(ienApplyMask, h, q.m, qk) end
    end end
    for ent in (remoteField("P") or ""):gmatch("[^|]+") do    -- BIDIRECTIONAL: apply the other player's interactable state
        local k, st, mk = ent:match("^([%-%d,]+);([01b]);?m?([01]*)$")
        if k and st == "b" then                                -- batch 65: the other side BROKE it (padlock...): one way, never un-break
            I.brk = I.brk or {}
            if not I.brk[k] then
                I.brk[k] = true
                local hit = nil
                hit = M.kf.find(FindAllOf("InteractableObject"), k)   -- b71kf
                if hit then
                    pcall(function() local lk = doorKey(hit); if lk ~= k then I.brk[lk] = true; I.bseen = I.bseen or {}; I.bseen[lk] = true end end)   -- b71kf: no echo
                    local ok, e = pcall(function() if hit.bBroken ~= true then hit:SetBroken(true) end end)
                    pcall(function() hit:StateChange() end)
                    local nowb = nil; pcall(function() nowb = hit.bBroken end)
                    log(string.format("S2CORE ien: %s -> SetBroken(true) ok=%s broken=%s %s", k, tostring(ok), tostring(nowb), ok and "" or tostring(e)))
                else I.brk[k] = nil; log("S2CORE ien: quebrado " .. k .. " nao encontrado neste PC") end
            end
        elseif k then
            local want = (st == "1")
            I.app = I.app or {}
            local ap = I.app[k]                                -- batch 67b echo guard: the same remote value is applied at most once per ienecho s
            if I.seen[k] ~= want and not (ap and ap.w == want and now - ap.at < (FL.ienecho or 10)) and not (FL.iamir == 1 and IA.m and IA.m.ienBusy(ST, k)) then
                local hit = nil
                hit = M.kf.find(FindAllOf("InteractableObject"), k)   -- b71kf
                if hit and S2L.chUseDeny(ST, hit, k) then I.app[k] = {w = want, at = now}; hit = false end   -- b91choice (N3)
                if hit then
                    local cur = hit.bEnabled
                    local lamp = false                          -- batch 67: lamps (BP_Light_Base_C children) draw their light only in LightOn/LightOff
                    if FL.ienlamp == 1 then pcall(function()   -- (measured: bEnabled=true with MainLight hidden on the client; LightOn() -> visible)
                        local c = hit:GetClass()
                        for _ = 1, 4 do if not c or not c:IsValid() then break end; if c:GetFName():ToString() == "BP_Light_Base_C" then lamp = true; break end; c = c:GetSuperStruct() end
                    end) end
                    if lamp then
                        local ok, e = pcall(function() if want then hit:LightOn() else hit:LightOff() end end)
                        pcall(function() if hit.bEnabled ~= want then hit:SetEnabled(want) end end)
                        local vis = nil; pcall(function() vis = hit.MainLight:IsVisible() end)
                        log(string.format("S2CORE ien: %s -> Light%s ok=%s vis=%s %s", k, want and "On" or "Off", tostring(ok), tostring(vis), ok and "" or tostring(e)))
                        cur = want
                    end
                    local T = (FL.ientab == 1) and ienTab(I) or nil
                    local cls = ""; pcall(function() cls = hit:GetClass():GetFName():ToString() end)
                    if T and T.ACT[cls] and type(cur) == "boolean" and cur ~= want then   -- batch 69: the object's own action handlers (b68 table)
                        local okT, r = pcall(T.M.actApply, hit, cls, want, {FL = FL, log = log, ACT = T.ACT})
                        if not okT then log("S2CORE ientab erro: " .. tostring(r)) end
                        cur = want
                    end
                    if type(cur) == "boolean" and cur ~= want then
                        local how, ok, e = "SetEnabled", nil, nil   -- batch 64: SetEnabled only flips the flag (user: no visible change); Toggle is the stock switch
                        local hatch = false
                        if FL.ienhatch == 1 and want then pcall(function() hatch = hit:GetClass():GetFName():ToString() == "BP_BunkerHatch_Valve_C" end) end
                        if hatch then   -- batch 66: hatch valve = its own click handler (bound event 4; measured: it runs SetEnabled like a real click)
                            how = "HatchClick"; ok, e = pcall(function() hit:BndEvt__BP_BunkerHatch_Valve_SingleClick_K2Node_ComponentBoundEvent_4_InteractSignature__DelegateSignature() end)
                        elseif (FL.ientoggle or 1) == 1 then
                            how = "Toggle"; ok, e = pcall(function() hit:Toggle() end)
                            local now2 = nil; pcall(function() now2 = hit.bEnabled end)
                            if now2 ~= want then how = "Toggle+SetEnabled"; ok, e = pcall(function() hit:SetEnabled(want) end) end
                        else ok, e = pcall(function() hit:SetEnabled(want) end) end
                        pcall(function() hit:StateChange() end)   -- batch 65: BP levers redraw/animate from bEnabled in StateChange() (measured: no params, ok=true)
                        if FL.ienanim == 1 then                  -- batch 67c: the handle/lid pose is the anim BP's DoorState (measured: lever off=1 both PCs, hatch open=0 both PCs); Toggle never sets it
                            for _, cn in ipairs({"SkeletalMesh", "Hatch", "Mesh"}) do
                                local okA = pcall(function() local ai = hit[cn]:GetAnimInstance(); if type(ai.DoorState) == "number" then ai.DoorState = want and 0 or 1; how = how .. "+DoorState" .. (want and 0 or 1) end end)
                                if okA and how:find("DoorState") then break end
                            end
                            if pcall(function() hit:PauseSkeletalComponent(false) end) then how = how .. "+Unpause" end   -- batch 67d: idle interactables keep their skeletal anim paused;
                            -- measured on the client hatch: PauseSkeletalComponent(false) -> lid bone jnt_door 0 -> 88 (= host's open pose), then the BP re-paused it by itself
                        end
                        log(string.format("S2CORE ien: %s -> %s(%s)+StateChange ok=%s %s", k, how, st, tostring(ok), ok and "" or tostring(e)))
                    end
                    I.seen[k] = want                          -- record so we do not echo it back
                    I.app[k] = {w = want, at = now}
                    pcall(function() local lk = doorKey(hit); if lk ~= k then I.seen[lk] = want; I.app[lk] = {w = want, at = now} end end)   -- b71kf: local key too
                    if FL.ienprompt == 1 and mk and mk ~= "" then I.pm = I.pm or {}; I.pm[k] = {m = mk, at = now + 1.5} end   -- after the BP's own handlers ran (b112: queued for ienTick, no UObject kept)
                elseif hit == nil then log("S2CORE ien: objeto " .. k .. " nao encontrado neste PC") end   -- b91choice: false = refused (N3)
            end
        end
    end
    if now - I.listAt < (FL.iensec or 1) then return end
    I.listAt = now
    local lpos = lp:K2_GetActorLocation()
    local r2 = (FL.ienr or 2500) ^ 2
    for _, o in ipairs(FindAllOf("InteractableObject") or {}) do pcall(function()
        if not o:IsValid() then return end
        local q = o:K2_GetActorLocation()
        if (q.X - lpos.X) ^ 2 + (q.Y - lpos.Y) ^ 2 > r2 then return end
        local en = o.bEnabled; if type(en) ~= "boolean" then return end
        local k = doorKey(o)
        local br = o.bBroken                                   -- batch 65: false -> true = broken here (padlock shot...) -> "key;b"
        I.bseen = I.bseen or {}; I.brk = I.brk or {}
        if br == true and I.bseen[k] == false and not I.brk[k] then
            I.brk[k] = true; I.bout = I.bout or {}; I.bout[k] = now
            log("S2CORE ien: " .. k .. " QUEBRADO")
        end
        if type(br) == "boolean" then I.bseen[k] = br end
        local s = I.seen[k]
        if s == nil then I.seen[k] = en; return end          -- first sight: record only
        local ap = I.app and I.app[k]                          -- batch 67b: a local change right after applying a remote one is its after-effect, never re-sent
        if s ~= en and not ap and FL.keyfix == 1 and I.app then ap = M.kf.get(I.app, k) end   -- b71kf (only on a change: cheap)
        if s ~= en and ap and now - ap.at < (FL.ienecho or 10) then I.seen[k] = en; return end
        if s ~= en then
            I.seen[k] = en; I.out[k] = {en = en, at = now}
            local cn = ""; pcall(function() cn = o:GetClass():GetFName():ToString() end)   -- 68g2: levers only (a hatch mask switched off the host's climb-down prompt)
            if FL.ienprompt == 1 and cn:find("^BP_Lever") then local okM, m = pcall(ienMask, o); if okM then I.out[k].m = m end end
            log(string.format("S2CORE ien: %s %s", k, en and "ON" or "OFF"))
        end
    end) end
end
local function ienOut(ST)
    local I = ST.ien; if not I then return "" end
    local now, t = os.clock(), {}
    for k, e in pairs(I.out) do if now - e.at < (FL.ienhold or 6) then t[#t + 1] = k .. ";" .. (e.en and "1" or "0") .. ((e.m and e.m ~= "") and (";m" .. e.m) or "") else I.out[k] = nil end end
    for k, at in pairs(I.bout or {}) do if now - at < (FL.ienhold or 6) then t[#t + 1] = k .. ";b" else I.bout[k] = nil end end
    if #t == 0 then return "" end
    return "#P:" .. table.concat(t, "|")
end
local function lootOut(ST)
    local L = ST.loot; if not L or FL.loot ~= 1 then return "" end
    local now, s = os.clock(), ""
    if api.ROLE == "host" then
        if ST.takenList and #ST.takenList > 0 then
            local n = #ST.takenList; L.xi = ((L.xi or 0) % n) + 1; if now - (L.xAt or 0) >= 0.3 then L.xAt = now; L.xi = (L.xi or 0) % n + 1 end
            local part = {}
            for i = 0, math.min(3, n - 1) do part[#part + 1] = ST.takenList[((L.xi + i - 1) % n) + 1] end
            s = s .. "#X:" .. table.concat(part, "|")
        end
        if L.corpses ~= "" then s = s .. "#C:" .. L.corpses end
        local Q = L.replyQ                                -- batch 28: one at a time, each for FL.resend s (the item list uses the | separator)
        if Q and Q[1] then
            local h = Q[1]; h.at = h.at or now
            if now >= h.at + FL.resend then table.remove(Q, 1); h = Q[1]; if h then h.at = now end end
            if h then s = s .. string.format("#R:%d;%s;%s", h.seq, h.id, h.str) end
        end
    else
        if L.req and now < L.req.untilT then s = s .. string.format("#Q:%d;%s", L.req.seq, L.req.id) end
        if ST.tOut then                                    -- batch 28: a list; the two senders still just set ST.tOut
            ST.tList = ST.tList or {}; ST.tList[#ST.tList + 1] = {id = ST.tOut.id, untilT = now + FL.resend}; ST.tOut = nil
        end
        if ST.tList and ST.tList[1] then
            local keep, ids = {}, {}
            for _, e in ipairs(ST.tList) do if now < e.untilT then keep[#keep + 1] = e; ids[#ids + 1] = e.id end end
            ST.tList = keep
            if #ids > 0 then s = s .. "#T:" .. table.concat(ids, "|") end
        end
    end
    return s
end

-- ===== core-20: ground items (flag ground=1). Measured 2026-10-11: a world item is a BP_StaticItemContainer_C actor (physics; lands at the
-- same spot on every run); Pawn:GetInteractedActor() returns it while the prompt shows; F picks it up and the actor stays valid but moves
-- to 0,0,0 (pool). Hiding the other PC's copy: SetActorHiddenInGame(true) + SetActorEnableCollision(false) (it then falls below the
-- ground and stays valid; no prompt). NEVER call CppMediator:GetItemsCountInInventory or GetGUID on these actors (both crash).
-- Message: "#G:<seq>;<x>,<y>,<z>;<class>;<mesh>|..." (cm, last 4 pickups, each repeated 4 s). Match on the other PC: same class and mesh,
-- nearest within 150 cm. Persistent on the host as taken key "item@<x>,<y>,<z>;<class>;<mesh>" (sent back to a restarted client by #X).
local function gInit(ST) ST.ground = ST.ground or {seq = 0, cand = {}, out = {}, seen = {}, pend = {}, sweepAt = 0} return ST.ground end
local function gHide(o)
    pcall(function() o:SetActorHiddenInGame(true) end); pcall(function() o:SetActorEnableCollision(false) end)
end
local function gFind(x, y, z, cls, mesh, tol)
    local best, bd
    for _, o in ipairs(FindAllOf(cls) or {}) do pcall(function()
        if o:IsValid() and not o.bHidden then
            local q = o:K2_GetActorLocation()
            local d = math.sqrt((q.X - x)^2 + (q.Y - y)^2 + (q.Z - z)^2)
            if d < tol and (not bd or d < bd) then
                local m = "?"; pcall(function() m = M.meshName(o, "MeshComponent") or "?" end)
                if m == mesh then best, bd = o, d end
            end
        end end) end
    return best, bd
end
local function groundTick(ST)
    if FL.ground ~= 1 then return end
    local G = gInit(ST); local now = os.clock()
    local pw = S2.io and S2.io.pawn; if not valid(pw) then return end
    -- (1) my pickups: remember the item under the prompt; it is picked up when it jumps to the origin (or becomes invalid)
    local a; pcall(function() a = pw:GetInteractedActor() end)
    if a and valid(a) then pcall(function()
        local full = a:GetFullName(); local cls = full:match("^(%S+)")
        if cls and cls:find("ItemContainer", 1, true) then
            local nm = a:GetFName():ToString(); local q = a:K2_GetActorLocation()
            if math.abs(q.X) + math.abs(q.Y) > 1000 then
                local c = G.cand[nm] or {}; G.cand[nm] = c
                c.o = a; c.cls = cls; c.x = q.X; c.y = q.Y; c.z = q.Z; c.t = now
                if not c.mesh then c.mesh = "?"; pcall(function() c.mesh = M.meshName(a, "MeshComponent") or "?" end) end
            end
        end end) end
    for nm, c in pairs(G.cand) do
        local gone, q = false, nil
        if not valid(c.o) then gone = true else pcall(function() q = c.o:K2_GetActorLocation() end); if q and math.abs(q.X) + math.abs(q.Y) < 1000 then gone = true end end
        if gone then
            G.cand[nm] = nil
            G.seq = G.seq + 1
            local e = string.format("%d;%.0f,%.0f,%.0f;%s;%s", G.seq, c.x, c.y, c.z, c.cls, c.mesh)
            table.insert(G.out, 1, {e = e, untilT = now + 4}); while #G.out > 4 do table.remove(G.out) end
            log("S2CORE GROUND_TAKE " .. e)
            if FL.taken == 1 and api.ROLE == "host" then takenAdd(ST, string.format("item@%.0f,%.0f,%.0f;%s;%s", c.x, c.y, c.z, c.cls, c.mesh)) end
        elseif now - c.t > 3 then G.cand[nm] = nil end
    end
    -- (2) the other player's pickups
    for ent in (x1part("G") or ""):gmatch("[^|]+") do
        if not G.seen[ent] then
            G.seen[ent] = now
            local seq, x, y, z, cls, mesh = ent:match("^(%d+);([%-%d]+),([%-%d]+),([%-%d]+);([%w_]+);([%w_%?]+)$")
            if seq then
                G.pend[#G.pend + 1] = {x = tonumber(x), y = tonumber(y), z = tonumber(z), cls = cls, mesh = mesh, untilT = now + 60, e = ent, nextT = 0}
                if FL.taken == 1 and api.ROLE == "host" then takenAdd(ST, string.format("item@%s,%s,%s;%s;%s", x, y, z, cls, mesh)) end
            end
        end
    end
    for k, t in pairs(G.seen) do if now - t > 60 then G.seen[k] = nil end end
    for i = #G.pend, 1, -1 do
        local p = G.pend[i]
        if now >= p.nextT then
            p.nextT = now + 2
            local o, d = gFind(p.x, p.y, p.z, p.cls, p.mesh, 150)
            if o then
                gHide(o); table.remove(G.pend, i)
                log(string.format("S2CORE GROUND_GOT %s: copia local %s a %.0f cm escondida", p.e, o:GetFName():ToString(), d))
            elseif now > p.untilT then
                table.remove(G.pend, i); log("S2CORE GROUND_GOT " .. p.e .. ": nenhuma copia local em 60 s (desisti)")
            end
        end
    end
    -- (3) persistence: every 5 s hide local copies of items in the taken list (keys item@...) that are within 30 cm
    if FL.taken == 1 and ST.takenList and now >= G.sweepAt then
        G.sweepAt = now + 5
        for _, k in ipairs(ST.takenList) do
            local x, y, z, cls, mesh = k:match("^item@([%-%d]+),([%-%d]+),([%-%d]+);([%w_]+);([%w_%?]+)$")
            if x then
                local o, d = gFind(tonumber(x), tonumber(y), tonumber(z), cls, mesh, 30)
                if o then gHide(o); log(string.format("S2CORE GROUND_SWEEP %s escondido (%.0f cm de %s)", o:GetFName():ToString(), d, k)) end
            end
        end
    end
end
local function groundOut(ST)
    local G = ST.ground; if not G or FL.ground ~= 1 then return "" end
    local now, r = os.clock(), {}
    for _, o in ipairs(G.out) do if now < o.untilT then r[#r + 1] = o.e end end
    return #r > 0 and ("#G:" .. table.concat(r, "|")) or ""
end


-- ===== QUEST SYNC (batch 7) =====
-- Host: the host-only pak zzzzzz_S2CoopQuestObserver_HostOnly_P.pak adds, next to each of the 4,449 SetJournal and 728 SetGlobalVariable quest
-- nodes, a ConsoleCommand node on the same launchers: "s2q J <quest> <stage or -> <Start|Finish|Fail|Cancel>" or "s2q G <variable> <Set|Add|
-- Subtract> <value>". The handler above passes each line here; every event gets a sequence number and rides in x1 as
-- "#J:<seq>,<J|G>,<a>,<b>,<c>|..." for 2 s (state of a short list, so a lost packet does not matter).
-- Client: applies each seq once (5 s window, emptied when the host relaunches = section 15): journal steps with the debug journal calls
-- (XDKStartQuest / XDKStartQuestStage / XDKFinishQuest / XDKFinishQuestStage; inference: they change only the journal, not the quest graph),
-- bool variables with XSetGlobalBool. Int variables and Add/Subtract cannot be applied (no setter in the dump): logged and counted.
-- Host log (batch 8): every event is also appended to <bridge>\quest_log.txt as "<game seconds>\t<event without seq>", kept across game
-- launches. If the host loads an older save (game clock jumps back), entries newer than the loaded clock are dropped (questRollback).
local function qlogPath() return api.BR .. "quest_log.txt" end
local function qlogLoad(Q)
    if Q.log then return end
    Q.log = {}
    local f = io.open(qlogPath(), "r")
    if not f then return end
    for ln in f:lines() do
        local t, e = ln:match("^([%d%.]+)\t(.+)$")
        if t then Q.log[#Q.log + 1] = {t = tonumber(t), e = e} end
    end
    f:close()
    log("S2CORE quest: log do host carregado, " .. #Q.log .. " eventos")
end
local function qlogRewrite(Q)
    local f = io.open(qlogPath() .. ".tmp", "w"); if not f then return end
    for _, x in ipairs(Q.log) do f:write(string.format("%.1f\t%s\n", x.t, x.e)) end
    f:close()
    if not os.rename(qlogPath() .. ".tmp", qlogPath()) then os.remove(qlogPath()); os.rename(qlogPath() .. ".tmp", qlogPath()) end
end
function M.questCmd(ST, full, params)
    local Q = ST.qs
    local t = {}
    for w in tostring(full or ""):gmatch("%S+") do t[#t + 1] = w end
    if t[1] == "s2q" then table.remove(t, 1) end          -- the handler may get the line with or without the command name
    pcall(S2L.mqSeen, ST, t)   -- b109dlgblock: last main-quest node of this side's graph (mqpull)
    if t[1] == "D" or t[1] == "L" then pcall(S2L.qpCmd, ST, t); return end   -- b99qpick: this side's own dialog pin / node run
    local kind = t[1]
    local e
    if (kind == "J" or kind == "G") and t[4] then e = table.concat({kind, t[2], t[3], t[4], t[5] or "-"}, ",")
    elseif (kind == "N" or kind == "W" or kind == "X") and t[2] then e = kind .. "," .. t[2] end
    if not e then log("S2CORE quest: linha s2q invalida: " .. tostring(full)); return end
    Q.got = Q.got + 1
    if FL.qsync ~= 1 then return end
    if api.ROLE == "host" and FL.newgame == 1 and kind == "G" and t[5] == "EQ101_P_SetGlobalVariable_CA_Disable_EQ101_False_NewStart" and M.campRotate then   -- batch 46
        local gt = tonumber(rawget(_G, "S2_gameSecs")) or 0
        if gt < FL.newgamesecs and os.clock() - (Q.ngAt or -1e9) > 120 then Q.ngAt = os.clock(); M.campRotate(ST, string.format("jogo novo (relogio %.0f s)", gt)) end
    end
    if api.ROLE == "host" and FL.qbootdedupe == 1 and kind == "J" then    -- batch 34: a world load re-runs the last reached journal node: not a new step (journal steps only: a counter can really repeat)
        local pw = S2.io and S2.io.pawn
        if not valid(pw) then pcall(function() pw = api.UEHelpers.GetPlayerController().Pawn end) end
        local addr = valid(pw) and pw:GetAddress() or nil
        if addr and addr ~= Q.dedAddr then Q.dedAddr = addr; Q.dedUntil = os.clock() + FL.qbootwin end
        if not addr or os.clock() < (Q.dedUntil or 0) then
            qlogLoad(Q)
            local dup = false
            for _, x in ipairs(Q.log) do if x.e == e then dup = true; break end end
            if not dup then for _, x in ipairs(Q.pend or {}) do if x.e == e then dup = true; break end end end
            if dup then Q.dedN = (Q.dedN or 0) + 1; log("S2CORE quest: host " .. e .. " repetido logo depois de carregar o mundo: ignorado (" .. Q.dedN .. ")"); return end
        end
    end
    Q.seq = Q.seq + 1
    local str = Q.seq .. "," .. e
    Q.list[#Q.list + 1] = {t = os.clock(), s = str}
    if api.ROLE == "host" then
        qlogLoad(Q)
        local gt = tonumber(rawget(_G, "S2_gameSecs")) or 0
        if gt <= 0 and FL.qbootdedupe == 1 then          -- batch 34: the clock is not read yet: written once it is (M.qlogFlush)
            Q.pend = Q.pend or {}; Q.pend[#Q.pend + 1] = {e = e}
        else
            Q.log[#Q.log + 1] = {t = gt, e = e, live = true}
            local f = io.open(qlogPath(), "a"); if f then f:write(string.format("%.1f\t%s\n", gt, e)); f:close() end
        end
        if kind == "J" and t[3] == "-" and t[4] == "Finish" then pcall(M.questFinished, ST, t[2]) end   -- core-22: reward to the client
    end
    log("S2CORE quest: host " .. str)
end
function M.qlogFlush(ST)                              -- batch 34: host: events that came before the game clock was read
    local Q = ST.qs
    if api.ROLE ~= "host" or not (Q and Q.pend and Q.pend[1]) then return end
    local gt = tonumber(rawget(_G, "S2_gameSecs"))
    if not gt or gt <= 0 then return end
    qlogLoad(Q)
    local f = io.open(qlogPath(), "a")
    for _, x in ipairs(Q.pend) do
        Q.log[#Q.log + 1] = {t = gt, e = x.e, live = true}
        if f then f:write(string.format("%.1f\t%s\n", gt, x.e)) end
    end
    if f then f:close() end
    log(string.format("S2CORE quest: %d eventos de antes do relogio gravados no log com %.1f s", #Q.pend, gt))
    Q.pend = nil
end
local function questRollback(ST)
    if api.ROLE ~= "host" or FL.qsync ~= 1 then return end
    local Q = ST.qs; qlogLoad(Q)
    local gt = tonumber(rawget(_G, "S2_gameSecs"))
    -- core-24d: the game clock reads 0 for the first seconds after a load (measured: "relogio 0 s < 1644 s" threw away 14 real events).
    -- A rollback is only believed when the clock is > 0, is advancing, and says "older" in 3 checks in a row (15 s).
    if not gt or gt <= 0 or (Q.rbLast and gt <= Q.rbLast) then Q.rbLast = gt; Q.rbHits = 0; return end
    Q.rbLast = gt
    if not rawget(_G, "S2_QLOG_BASE") then rawset(_G, "S2_QLOG_BASE", gt) end   -- clock at the first valid reading of this GAME session = the loaded save (kept across module reloads)
    local base = rawget(_G, "S2_QLOG_BASE")
    local newest = 0; for _, x in ipairs(Q.log) do if not x.live and x.t > newest then newest = x.t end end   -- not the last line: a load re-fires old nodes and appends them with the older clock
    if #Q.log == 0 or newest <= base + 60 then Q.rbHits = 0; Q.qlOk = true; return end   -- b165: Q.qlOk
    Q.rbHits = (Q.rbHits or 0) + 1
    if Q.rbHits < 3 then return end
    Q.rbHits = 0
    local keep = {}
    for _, x in ipairs(Q.log) do if x.live or x.t <= base then keep[#keep + 1] = x end end   -- cut at the LOADED clock; events of this session stay
    log(string.format("S2CORE quest: save mais antigo carregado (relogio %.0f s < %.0f s): %d eventos do log descartados", gt, newest, #Q.log - #keep))
    Q.log = keep; qlogRewrite(Q); Q.cu = nil
    if M.rbBump then M.rbBump(ST, "relogio do jogo voltou") end
end
local function questOut(ST)
    local Q, now, keep, parts = ST.qs, os.clock(), {}, {}
    for _, e in ipairs(Q.list) do if now - e.t < 2 then keep[#keep + 1] = e; parts[#parts + 1] = e.s end end
    Q.list = keep
    if #parts > 0 then Q.sent = Q.sent + 1; return "#J:" .. table.concat(parts, "|") .. S2L.qdOut(ST) end   -- b129qdiv: + #QD digest
    return S2L.qdOut(ST)
end
local QSKIP = nil                                     -- batch 11: node SIDs that stay live on the client (one per line)
local function qskip(sid)
    if not QSKIP then
        QSKIP = {}
        local n, f = 0, io.open(api.BR .. "s2c_skip.txt", "r")
        if f then for ln in f:lines() do local k = ln:match("^%s*(%S+)"); if k then QSKIP[k] = true; n = n + 1 end end; f:close() end
        log("S2CORE quest: " .. n .. " nos em s2c_skip.txt (ficam com o grafo local do cliente)")
    end
    return sid ~= nil and QSKIP[sid] == true
end
-- ===== b70dlg: DIALOG MIRROR (dm70.lua + s2u_dlg.lua from the bridge). Host = driver (reads its own answer screen, sends #DG picks),
-- client = follower (its answer Buttons disabled, clicks the host's pick by answer text). Flags dmir / dmirint, default 0. No hooks. =====
local DM = {g = nil, fresh = true}
local function dmG()
    if DM.g ~= nil then return DM.g end
    DM.g = false
    local function ld(n)
        local f = io.open(api.BR .. n, "r"); if not f then log("S2CORE dmir: falta " .. n); return nil end
        local src = f:read("*a"); f:close()
        local fn, e = load(src, n); if not fn then log("S2CORE dmir: " .. n .. " erro " .. tostring(e)); return nil end
        return fn()
    end
    local okM, Mo = pcall(ld, "dm70.lua"); local okT, T = pcall(ld, "s2u_dlg.lua")
    if not (okM and type(Mo) == "table" and okT and type(T) == "table" and type(T.set) == "table") then return false end
    DM.m = Mo
    DM.g = {FL = FL, now = os.clock, log = log, DT = T,
        FindAllOf = FindAllOf, FindFirstOf = FindFirstOf, StaticFindObject = StaticFindObject,
        remoteField = function(t) return remoteField(t) end,
        peerUp = function() local P = S2.state and S2.state.peer; return P ~= nil and P.state == "up" end,
        pawn = function() return S2.io and S2.io.pawn end,
        uidOf = function(a) local u; pcall(function() u = StaticFindObject("/Script/Stalker2.Default__CppMediator"):GetGUID(a) end); return u end}
    log("S2CORE dmir: tabelas carregadas (" .. tostring(T.stats and T.stats.setdialog) .. " nos SetDialog, " .. tostring(Mo.VERSION or "dm70-v1") .. ")")
    return DM.g
end
local function dmirTick(ST)
    if ST.vt and ST.vt.on then                              -- b78fix vendtrade: a vendor talk on this side; the mirror waits
        if ST.dm and DM.m and DM.g then DM.m.restore(ST.dm, DM.g, "vendtrade") end
        return
    end
    if FL.dmir ~= 1 and ST.dmf and ST.dmf.orig and next(ST.dmf.orig) and DM.m and DM.g then DM.m.restore(ST.dmf, DM.g, "dmir=0") end   -- b109dlgblock
    if FL.dmir ~= 1 and not (ST.dm and ST.dm.orig and next(ST.dm.orig)) then return end   -- off: only the release path runs
    local G = dmG(); if not G then return end
    ST.dm = ST.dm or {}
    if DM.fresh then DM.fresh = false; ST.dm.fresh = true end   -- module (re)loaded: the follower gives back what it disabled
    if S2L.ldDm(ST, G) then return end   -- b92lead: lead=1 picks the role by the story lock
    if api.ROLE == "host" then DM.m.driverTick(ST.dm, G) else DM.m.followerTick(ST.dm, G) end
end
local function dmirOut(ST)
    if FL.dmir ~= 1 or not ST.dm then return "" end
    if (FL.lead or 0) == 1 then if ST.dmRole ~= "d" then return "" end elseif api.ROLE ~= "host" then return "" end   -- b92lead
    local G = dmG(); if not G then return "" end
    return DM.m.driverOut(ST.dm, G)
end
local function dmirLet(sid, live)                     -- questApply: replay this dialog node on the client after all?
    if FL.dmir ~= 1 then return false end
    local G = dmG(); if not G then return false end
    local ok = DM.m.letNode(FL, G.DT, sid, live)
    if ok then log("S2CORE dmir: no de dialogo " .. tostring(sid) .. " repetido no cliente (respostas vem do host)") end
    return ok
end
-- ===== b92lead (flag lead, default 0; user rule N14 "first starter leads": "both client and host can proceed the quest, take quests,
-- and decide dialogs, but when 1 starts it, the others just follows, both cannot decide together"). ONE STORY LOCK over the bridge.
-- Event, sent only while held:  "#LK:<side h|c>,<seq>,<os.time()>,<why dlg|trig>,<x>,<y>,<z>".
--   taken by  a non-vendor dialog view opening on this side (why dlg, NPC position; released ldhold s after the view closes);
--             entering a BRANCH quest trigger (s2u_choice trig tier <= n3trig; why trig, ldtrig s; the host scans its own entries too);
--             host only: a client shot that hits a story NPC (s2u_choice npc tier <= n3kill) holds the HOST as follower for ldkill s.
--   tie       both sides hold it (each took it before the other's event arrived): the HOST keeps it, the client drops its own.
--   follower  a dialog opened while the peer holds a DIALOG lock at the same NPC (dmirnpcm cm) is mirrored (dm70 follower, answer
--             buttons disabled); any other story dialog is HELD: answers hidden (AnswerContainer collapsed, the call vendtrade measured)
--             and closed (UIDialogClose) after ldclose s; if the peer lets go first, the answers come back and this side takes the lock.
--             Vendors (s2u_vendor.lua rows within vendtr cm) never take the lock and are never held.
--   gates     the b91 functions (chShotDeny / chTrigDeny / chUseDeny / chUseTick): with lead=1 the role never blocks; the host refuses a
--             client trigger, shot or use only while the HOST holds the lock; usable prompts are off on either side while the PEER holds it.
--   items     the b87 guard: with lead=1 both players keep what they pick up and the other one gets a copy (S2L.ldQi), once per pickup.
function S2L.ldSt(ST) ST.ld = ST.ld or {seq = 0, vc = {}, logAt = {}} return ST.ld end
function S2L.ldLog(L, key, msg)                            -- at most one line per key per 10 s
    local now = os.clock()
    if now - (L.logAt[key] or -1e9) < 10 then return end
    L.logAt[key] = now; log(msg)
end
function S2L.ldRaw()                                       -- the peer's lock as it sent it, or nil
    local s = x1part("LK"); if not s then return nil end
    local sd, seq, ts, why, x, y, z = s:match("^([hc]),(%d+),(%d+),(%a+),([%-%d%.]+),([%-%d%.]+),([%-%d%.]+)")
    if not sd then return nil end
    return {side = sd, seq = tonumber(seq), ts = tonumber(ts), why = why, x = tonumber(x), y = tonumber(y), z = tonumber(z)}
end
function S2L.ldFree(ST, why)
    local L = S2L.ldSt(ST); local m = L.mine; if not m then return end
    L.mine = nil
    log(string.format("S2CORE lead: trava #%d (%s) solta: %s (%.1f s)", m.seq, m.why, why, os.clock() - m.at))
end
function S2L.ldMine(ST)                                    -- this side's lock (a timed lock past its time is released here), or nil
    local L = ST.ld; if (FL.lead or 0) ~= 1 or not L or not L.mine then return nil end
    if L.mine.untilT and os.clock() > L.mine.untilT then S2L.ldFree(ST, "tempo"); return nil end
    if (FL.ldwatch or 1) == 1 and S2L.ldStale(ST, L) then return nil end   -- b127softlock C
    return L.mine
end
-- ===== b127softlock C (flag ldwatch, default 1): a DIALOG lock has no end time; it ends only when ldDlg sees the view close. If the dialog
-- reader dm70 did not load (DM.g == false, e.g. after an S2.reload while the lock was held: ST survives, DM does not) nothing can end it and
-- the other player's story NPCs / usables stay blocked while the peer is up. Released here then, or after ldwatchs s in any case.
function S2L.ldStale(ST, L)                                -- true = my dialog lock was released by the watchdog
    local m = L.mine
    if not m or m.untilT or m.why ~= "dlg" then return false end
    local age, why = os.clock() - (m.at or os.clock()), nil
    if DM.g == false then why = "leitor de dialogo dm70 nao carregou (a trava nunca acabaria)"
    elseif age > (FL.ldwatchs or 600) then why = string.format("dialogo aberto ha mais de %d s (ldwatchs)", FL.ldwatchs or 600) end
    if not why then return false end
    S2L.ldFree(ST, "watchdog b127: " .. why .. "; o outro lado fica livre")
    return true
end
-- ===== end b127softlock C
function S2L.ldPeer(ST)                                    -- the lock the OTHER side holds after the tie rule, or nil
    if (FL.lead or 0) ~= 1 then return nil end
    local L = S2L.ldSt(ST)
    if L.pk and os.clock() < L.pk.untilT then return L.pk end         -- host: the client just shot a story NPC
    local P = S2L.ldRaw(); if not P then return nil end
    if api.ROLE == "host" and S2L.ldMine(ST) then return nil end      -- tie: the host wins
    return P
end
function S2L.ldTake(ST, why, x, y, z, dur)                 -- true = this side holds the lock now (new or kept)
    if (FL.lead or 0) ~= 1 then return false end
    local L, now = S2L.ldSt(ST), os.clock()
    local P = S2L.ldPeer(ST)
    if P then
        S2L.ldLog(L, "seg" .. why, string.format("S2CORE lead: %s aqui SEGURADO: o outro lado lidera (%s #%d)", why, tostring(P.why), P.seq or 0))
        return false
    end
    local m = S2L.ldMine(ST)
    if m then
        if not dur then m.untilT = nil; m.why = why; m.x, m.y, m.z = x or 0, y or 0, z or 0      -- a dialog holds it until the view closes
        elseif m.untilT then m.untilT = math.max(m.untilT, now + dur) end
        return true
    end
    L.seq = L.seq + 1
    L.mine = {seq = L.seq, ts = os.time(), why = why, x = x or 0, y = y or 0, z = z or 0, at = now, untilT = dur and (now + dur) or nil}
    log(string.format("S2CORE lead: trava #%d tomada (%s em %.0f,%.0f,%.0f): este lado lidera, o outro segue", L.seq, why, x or 0, y or 0, z or 0))
    return true
end
function S2L.ldOut(ST)
    if (FL.lead or 0) ~= 1 then return "" end
    local m = S2L.ldMine(ST); if not m then return "" end
    return string.format("#LK:%s,%d,%d,%s,%.0f,%.0f,%.0f", api.ROLE == "host" and "h" or "c", m.seq, m.ts, m.why, m.x, m.y, m.z)
end
function S2L.ldDeny(ST, what)                              -- host gates: true = refuse this client action (the HOST leads now)
    local m = S2L.ldMine(ST); if not m then return false end
    local L = S2L.ldSt(ST); L.deny = (L.deny or 0) + 1
    S2L.ldLog(L, "d" .. what, string.format("S2CORE lead: %s do cliente NAO aplicado: o host lidera (trava #%d %s) [%d]", what, m.seq, m.why, L.deny))
    return true
end
function S2L.ldKill(ST, best, g)                           -- host chShotDeny with lead=1: g = the story NPC's guid
    if S2L.ldDeny(ST, "tiro em NPC de historia " .. tostring(g)) then return true end
    local L, now = S2L.ldSt(ST), os.clock()
    local q = {X = 0, Y = 0, Z = 0}; pcall(function() q = best:K2_GetActorLocation() end)
    if not (L.pk and now < L.pk.untilT) then log(string.format("S2CORE lead: tiro do cliente em NPC de historia %s aplicado: o cliente lidera %s s (o host segue)", tostring(g), tostring(FL.ldkill or 3))) end
    L.pk = {why = "kill", seq = 0, x = q.X, y = q.Y, z = q.Z, untilT = now + (FL.ldkill or 3)}
    return false
end
function S2L.ldTrig(ST, r)                                 -- trigTick (both roles with lead=1): this side walked into trigger row r
    if (FL.lead or 0) ~= 1 then return end
    local T = S2L.chTab(); local tier = T and T.trig[r.sid]
    if not tier or tier > (FL.n3trig or 2) then return end
    if not S2L.ldTake(ST, "trig", r.x, r.y, r.z, FL.ldtrig or 3) and api.ROLE == "host" then
        log("S2CORE lead: o host entrou no gatilho de historia " .. r.sid .. " com o cliente liderando (o jogo do host dispara sozinho: nao bloqueavel)")
    end
end
function S2L.ldUseGate(ST)                                 -- chUseTick with lead=1: true = the peer leads (prompts off); else give back ours
    if S2L.ldPeer(ST) then return true end
    local H = S2L.chSt(ST)
    if not H.offk or not next(H.offk) then return false end
    local p; pcall(function() p = S2.io.pawn:K2_GetActorLocation() end)
    if not p then return false end
    local r2, n, left = ((FL.n3ur or 2500) * 4) ^ 2, 0, 0
    for _, o in ipairs(FindAllOf("InteractableObject") or {}) do pcall(function()
        if not o:IsValid() then return end
        local q = o:K2_GetActorLocation()
        if (q.X - p.X) ^ 2 + (q.Y - p.Y) ^ 2 > r2 then return end
        local k = doorKey(o); local idx = H.offk[k]; if not idx then return end
        H.offk[k] = nil
        for i, e in ipairs(ienComps(o)) do if idx[i] and pcall(function() e.c:SetInteractionActive(true) end) then n = n + 1 end end
    end) end
    for _ in pairs(H.offk) do left = left + 1 end
    if n > 0 then log(string.format("S2CORE lead: o outro lado soltou a trava: %d prompts devolvidos (%d objetos longe ficam para depois)", n, left)) end
    return false
end
-- ===== b109dlgblock (flags ldblk / mqpull, default 0; user 2026-10-15 "Block, no mirror", N15: block up front, do not sync after).
-- ldblk=1 (lead=1): while the OTHER side holds the story lock (S2L.ldPeer), every living human NPC (BP_AI_Human_C) within ldblkr cm of
-- this player, except vendors (s2u_vendor.lua rows within vendtr cm) and our own bodies, takes no interaction here:
-- NPCComponent:SetInteractionActive(false) (the call tgtnoint uses), re-asserted each second (tick1000). Only NPCs whose interaction
-- was ON are touched; they are kept by ADDRESS (no UObject refs). Lock over / ldblk 0 / lead 0: each one found again gets
-- SetInteractionActive(true); one that died meanwhile gets it back at once (its body stays lootable). The follower never opens a story
-- dialog: no mirror (dmir=0); quest state comes by qsync (host leads) / qpick #QP (client leads). ldDlg's held path (answers hidden,
-- closed after ldclose s) stays the fallback for a dialog that starts in the same instant.
-- mqpull=1: the pull to the host's dialog/cinematic (cinedlg freeze, cjoin CJ1, cine follow, cj68 joins) only for MAIN QUEST ones.
function S2L.isMain(sid)                                   -- (C_)E<nn>_MQ*, E<nn>_Boss*, E<nn>_Flashback* = main quest; anything else false
    if type(sid) ~= "string" then return false end
    if sid:find("^Arch_Bossfight") or sid:find("^Arch_E%d+_") or sid:find("^E%d+_Duga") then return true end   -- b179prep: main-story scenes without an E<nn>_MQ prefix
    local q = sid:match("^C_(E%d+_%a+)") or sid:match("^(E%d+_%a+)")
    local k = q and q:match("_(%a+)$")
    return k == "MQ" or (k ~= nil and (k:find("^Boss") ~= nil or k:find("^Flashback") ~= nil))
end
function S2L.mqSeen(ST, t)                                 -- M.questCmd (both roles): stamp a main-quest node this side's graph ran
    local sid = t[1] == "G" and t[5] or t[2]               -- J: quest SID; G: node SID; N/W/X and qpick D/L: node SID
    if S2L.isMain(sid) then ST.mq = {at = os.clock(), sid = sid} end
end
function S2L.mqFlag(ST, V)                                 -- host cineOut: 1 = this dialog/cinematic is main quest (sticky per episode)
    if not V.on then V.mq0, V.mqOn = nil, nil; return 0 end
    local now = os.clock()
    V.mq0 = V.mq0 or now
    if not V.mqOn and (V.sleep or (ST.mq and ST.mq.at >= V.mq0 - (FL.mqwin or 15))) then
        V.mqOn = true
        log(string.format("S2CORE mq: %s do host e de MISSAO PRINCIPAL (%s)", V.kind == "d" and "dialogo" or "cinematica",
            V.sleep and "dormindo" or string.format("%s ha %.1f s", tostring(ST.mq.sid), now - ST.mq.at)))
    end
    return V.mqOn and 1 or 0
end
function S2L.mqOk(V)                                       -- client: may the host's current episode pull / freeze / follow me?
    return (FL.mqpull or 0) ~= 1 or V.mEp == true
end
function S2L.mqSid(sid)                                    -- cj68 joins: may this scene / cause node pull a player?
    return (FL.mqpull or 0) ~= 1 or S2L.isMain(sid)
end
function S2L.ldBlkFree(ST, B, why)                         -- every NPC switched off by ldBlkTick gets its interaction back (found by address)
    local n, gone = 0, 0
    for _, o in ipairs(FindAllOf("BP_AI_Human_C") or {}) do pcall(function()
        if not o:IsValid() then return end
        local a = o:GetAddress()
        if not B.off[a] then return end
        B.off[a] = nil
        local c = o.NPCComponent
        if valid(c) and c.bIsInteractionActive ~= true then c:SetInteractionActive(true) end
        n = n + 1
    end) end
    for _ in pairs(B.off) do gone = gone + 1 end
    B.off = {}
    log(string.format("S2CORE lead: NPCs de historia liberados (%s): %d com interacao de volta, %d sumiram", why, n, gone))
end
function S2L.ldBlkTick(ST)                                 -- tick1000 (both roles): story NPCs take no interaction here while the PEER leads
    local B = ST.ldb or {off = {}}; ST.ldb = B
    local P = ((FL.lead or 0) == 1 and (FL.ldblk or 0) == 1) and S2L.ldPeer(ST) or nil
    if not P then
        if next(B.off) then S2L.ldBlkFree(ST, B, (FL.ldblk or 0) ~= 1 and "ldblk=0" or ((FL.lead or 0) ~= 1 and "lead=0" or "o outro lado soltou a trava")) end
        return
    end
    local lp = S2L.ioPawn(); if not valid(lp) then return end
    local p = lp:K2_GetActorLocation()
    local r2, n, back = (FL.ldblkr or 3000) ^ 2, 0, 0
    for _, o in ipairs(FindAllOf("BP_AI_Human_C") or {}) do pcall(function()
        if not o:IsValid() then return end
        local a = o:GetAddress()
        local alive = o:IsAlive() == true
        local c = o.NPCComponent
        if not valid(c) then return end
        if B.off[a] then                                   -- ours: keep it off while alive; a body gets it back (lootable)
            if not alive then c:SetInteractionActive(true); B.off[a] = nil; back = back + 1
            elseif c.bIsInteractionActive == true then c:SetInteractionActive(false) end
            return
        end
        if not alive or o == lp or M.isOwnBody(ST, o) then return end
        local q = o:K2_GetActorLocation()
        if (q.X - p.X) ^ 2 + (q.Y - p.Y) ^ 2 > r2 then return end
        if S2L.vendorAt(q, FL.vendtr or 1500) then return end           -- vendors keep working (never synced)
        if c.bIsInteractionActive ~= true then return end              -- already off (game / tgtnoint): never ours to give back
        c:SetInteractionActive(false)
        B.off[a] = true; n = n + 1
        pcall(S2L.lgNote, ST, "N", o)   -- b129qdiv B: ledger by GUID
    end) end
    if n > 0 then
        local tot = 0; for _ in pairs(B.off) do tot = tot + 1 end
        log(string.format("S2CORE lead: %d NPCs de historia sem interacao aqui (total %d): o outro lado lidera (%s #%d)", n, tot, tostring(P.why), P.seq or 0))
    end
    if back > 0 then log(string.format("S2CORE lead: %d NPCs bloqueados morreram: interacao de volta (corpo saqueavel)", back)) end
end
M.isMain = function(_, sid) return S2L.isMain(sid) end   -- b109dlgblock: for the probes (S2.mod.isMain)
-- ===== end b109dlgblock
-- ===== b129qdiv B (flag ldsave, default 1 = report; 2 = give back; 0 = off; §149 audit #7): can our blocks land in a save?
-- What we switch off: ldblk NPCComponent:SetInteractionActive(false) (BP_AI_Human_C), chUseTick InteractionComponent:SetInteractionActive(false)
-- on story usables, slBeds BedMeshComponent:SetCollisionEnabled(0). All three are runtime UActorComponent state set by a native setter; the
-- game's PERSISTENT NPC restriction is a different thing (quest node DisableNPCInteraction, 321 in the kit, RestrictDialogInteractions flags on
-- the quest's NPC data). Inference: the save never holds ours (the actors are rebuilt from A-Life / map data on a load). Unmeasured, so:
-- every id we switch off goes to <bridge>\s2_blk_ledger.txt (NPC = CppMediator:GetGUID int, survives pooling; usable = Guid hex; no addresses;
-- 7 days, 400 newest). ldsavew s and +25 s after every new local pawn (a load or a game start) one pass counts living NPCs / usable
-- components / beds that are OFF and not ours now, and how many of them are in the ledger (= what a save could have kept). ldsave=2 also
-- switches the ledger ones back on (ldblk / chUseTick re-block and re-own them within 1-2 s if the other side still leads).
function S2L.lgSt(ST)
    if ST.lgd then return ST.lgd end
    local G = {N = {}, U = {}}
    local f = io.open(api.BR .. "s2_blk_ledger.txt", "r")
    if f then for ln in f:lines() do local k, id, ts = ln:match("^([NU])\t(%-?%w+)\t(%d+)"); if k then G[k][id] = tonumber(ts) end end; f:close() end
    ST.lgd = G
    return G
end
function S2L.lgSave(G)
    local t, now = {}, os.time()
    for _, k in ipairs({"N", "U"}) do
        for id, ts in pairs(G[k]) do if now - ts > (FL.ldsavekeep or 604800) then G[k][id] = nil else t[#t + 1] = {k, id, ts} end end
    end
    table.sort(t, function(a, b) if a[3] ~= b[3] then return a[3] > b[3] end return a[2] < b[2] end)
    for i = 401, #t do G[t[i][1]][t[i][2]] = nil end
    local p = api.BR .. "s2_blk_ledger.txt"
    local f = io.open(p .. ".tmp", "w"); if not f then return end
    for i = 1, math.min(#t, 400) do f:write(t[i][1] .. "\t" .. t[i][2] .. "\t" .. t[i][3] .. "\n") end
    f:close()
    if not os.rename(p .. ".tmp", p) then os.remove(p); os.rename(p .. ".tmp", p) end
end
function S2L.lgId(o)                                       -- NPC pawn -> CppMediator:GetGUID as text, or nil
    local id
    pcall(function() local u = StaticFindObject("/Script/Stalker2.Default__CppMediator"):GetGUID(o); if type(u) == "number" then id = tostring(math.tointeger(u) or u) end end)
    if id == "0" then return nil end
    return id
end
function S2L.lgNote(ST, k, x)                              -- ldBlkTick ("N", the NPC) / chUseTick ("U", guid hex): we switched it off
    if (FL.ldsave or 1) < 1 then return end
    local G = S2L.lgSt(ST)
    local id = k == "N" and S2L.lgId(x) or x
    if not id then return end
    local had, now = G[k][id], os.time()
    G[k][id] = now
    if had and now - had < 600 then return end            -- the file is rewritten at most once per 10 min per id
    S2L.lgSave(G)
end
function S2L.lgScan(ST, G, fix, tag)                       -- one pass: what is off and not ours; fix = give the ledger ones back
    local B, S = ST.ldb or {off = {}}, ST.sl or {}
    local skip = {}
    for _, P in ipairs({ST.tgt or {}, ST.pup or {}}) do if valid(P.pawn) then skip[P.pawn:GetAddress()] = true end end
    local n = {npc = 0, npcOff = 0, npcLed = 0, npcFix = 0, useOff = 0, useLed = 0, useFix = 0, beds = 0, bedOff = 0}
    for _, o in ipairs(FindAllOf("BP_AI_Human_C") or {}) do pcall(function()
        if not o:IsValid() then return end
        local a = o:GetAddress()
        if skip[a] or B.off[a] or o:IsAlive() ~= true or M.isOwnBody(ST, o) then return end
        local c = o.NPCComponent; if not valid(c) then return end
        n.npc = n.npc + 1
        if c.bIsInteractionActive == true then return end
        n.npcOff = n.npcOff + 1
        if not next(G.N) then return end
        local id = S2L.lgId(o)
        if id and G.N[id] then
            n.npcLed = n.npcLed + 1
            if fix and pcall(function() c:SetInteractionActive(true) end) then n.npcFix = n.npcFix + 1; G.N[id] = nil end
        end
    end) end
    for _, c in ipairs(FindAllOf("InteractionComponent") or {}) do pcall(function()
        if not c:IsValid() or c.bIsInteractionActive == true then return end
        local ow = c:GetOwner(); if not valid(ow) then return end
        local hx; pcall(function() hx = S2L.chHex(ow.Guid) end)
        if not hx then return end                          -- NPC components (no Guid) are counted above
        n.useOff = n.useOff + 1
        if G.U[hx] then
            n.useLed = n.useLed + 1
            if fix and pcall(function() c:SetInteractionActive(true) end) then n.useFix = n.useFix + 1; G.U[hx] = 0 end
        end
    end) end
    for k, v in pairs(G.U) do if v == 0 then G.U[k] = nil end end
    if not S.bedOff then
        for _, b in ipairs(FindAllOf("BedView") or {}) do pcall(function()
            if not b:IsValid() then return end
            local m = b.BedMeshComponent; if not valid(m) then return end
            n.beds = n.beds + 1
            if m:GetCollisionEnabled() == 0 then n.bedOff = n.bedOff + 1 end
        end) end
    end
    if n.npcFix + n.useFix > 0 then S2L.lgSave(G) end
    log(string.format("S2CORE ldsave: %s: NPCs vivos %d, sem interacao e nao nossos %d (no registro %d%s); usaveis sem interacao %d (no registro %d%s); camas %s; registro N=%d U=%d",
        tostring(tag), n.npc, n.npcOff, n.npcLed, fix and (", devolvidos " .. n.npcFix) or "", n.useOff, n.useLed, fix and (", devolvidos " .. n.useFix) or "",
        S.bedOff and "bloqueadas agora (nao contadas)" or string.format("%d, sem colisao %d", n.beds, n.bedOff),
        (function() local a = 0; for _ in pairs(G.N) do a = a + 1 end; return a end)(), (function() local a = 0; for _ in pairs(G.U) do a = a + 1 end; return a end)()))
    return n
end
function S2L.lgTick(ST)                                    -- tick1000, both roles: a pass ldsavew s and +25 s after every new local pawn
    if (FL.ldsave or 1) < 1 then return end
    local pw = S2L.ioPawn(); if not valid(pw) then return end
    local G, now = S2L.lgSt(ST), os.clock()
    local a = pw:GetAddress()
    if a ~= G.addr then G.addr = a; G.loads = (G.loads or 0) + 1; G.passes = {now + (FL.ldsavew or 8), now + (FL.ldsavew or 8) + 25} end
    if not (G.passes and G.passes[1]) or now < G.passes[1] then return end
    table.remove(G.passes, 1)
    S2L.lgScan(ST, G, (FL.ldsave or 1) >= 2, string.format("pawn #%d +%s s", G.loads, G.passes[1] and tostring(FL.ldsavew or 8) or tostring((FL.ldsavew or 8) + 25)))
end
-- ===== end b129qdiv B
function S2L.ldHide(L, vw, on, why)                        -- held dialog: AnswerContainer collapsed (1) / given back
    if on then
        local cur; pcall(function() cur = vw.AnswerContainer:GetVisibility() end)
        if L.av == nil and cur ~= nil then L.av = cur; log("S2CORE lead: respostas escondidas (" .. why .. ")") end
        if cur ~= nil and cur ~= 1 then pcall(function() vw.AnswerContainer:SetVisibility(1) end) end
    elseif L.av ~= nil then
        local ok = pcall(function() vw.AnswerContainer:SetVisibility(L.av) end)
        log("S2CORE lead: respostas devolvidas (" .. why .. ") ok=" .. tostring(ok)); L.av = nil
    end
end
function S2L.ldDlg(ST, L, now)                             -- this side's dialog view: lead / follow / held / vendor
    local G = dmG(); if not G then return end
    local vw = DM.m.view(L.vc, G)
    local d = L.dlg
    if not vw then
        if not d then return end
        if d.mode == "lead" and L.mine and L.mine.why == "dlg" then
            d.closeAt = d.closeAt or now
            if now - d.closeAt < (FL.ldhold or 1) then return end      -- a view that comes back within ldhold is the same talk
            S2L.ldFree(ST, "dialogo fechou")
        end
        if d.mode ~= "lead" then log("S2CORE lead: dialogo fechado (" .. tostring(d.mode) .. ")") end
        L.dlg = nil
        return
    end
    if not d then
        d = {t0 = now}; L.dlg = d
        if L.av ~= nil then S2L.ldHide(L, vw, false, "dialogo novo") end   -- hidden earlier and closed before it was given back
    end
    d.closeAt = nil
    if d.mode == nil then
        local q
        pcall(function() local a = S2L.ioPawn():GetInteractedActor(); if valid(a) then local p = a:K2_GetActorLocation(); q = {X = p.X, Y = p.Y, Z = p.Z} end end)
        if not q and now - d.t0 < 2 then return end                    -- no NPC read yet: try for 2 s
        d.q = q or d.q
        if ((FL.vendtrade or 0) ~= 0 or (FL.ldblk or 0) == 1) and q and S2L.vendorAt(q, FL.vendtr or 1500) then d.mode = "vend"; log("S2CORE lead: dialogo com vendedor: livre, sem trava (vendtrade/ldblk)"); return end   -- b95vend (user N14): vendtrade=0 -> a vendor dialog is a story dialog under the lock
        local P = S2L.ldPeer(ST)
        if P then
            local same = P.why == "dlg" and (not d.q or (P.x == 0 and P.y == 0) or (d.q.X - P.x) ^ 2 + (d.q.Y - P.y) ^ 2 + (d.q.Z - P.z) ^ 2 <= (FL.dmirnpcm or 500) ^ 2)
            if FL.dmir ~= 1 then same = false end   -- b109dlgblock: no mirror -> the old hide/close fallback
            d.mode = same and "follow" or "held"; d.heldAt = now
            log(string.format("S2CORE lead: dialogo aberto com o outro lado liderando (%s #%d): %s", tostring(P.why), P.seq or 0, same and "SEGUE (respostas do lider)" or "SEGURADO (respostas escondidas)"))
        else
            local p = d.q or {X = 0, Y = 0, Z = 0}
            if S2L.ldTake(ST, "dlg", p.X, p.Y, p.Z) then d.mode = "lead" end
        end
    end
    if d.mode == "held" then
        if not S2L.ldPeer(ST) then S2L.ldHide(L, vw, false, "o outro lado soltou a trava"); d.mode = nil; return end
        S2L.ldHide(L, vw, true, "o outro lado lidera")
        if now - d.heldAt >= (FL.ldclose or 4) and now - (d.cl or -99) >= 0.5 then
            d.cl = now; d.tries = (d.tries or 0) + 1
            local ok = pcall(function() vw:UIDialogClose() end)
            if d.tries == 1 then log("S2CORE lead: dialogo segurado fechado (UIDialogClose) ok=" .. tostring(ok)) end
        end
    end
end
function S2L.ldTick(ST)                                    -- tick100, right before dmirTick
    if (FL.lead or 0) ~= 1 then
        local L = ST.ld
        if L and (L.mine or L.dlg or L.av ~= nil) then
            L.mine = nil; L.dlg = nil; L.pk = nil
            local G = dmG(); local vw = G and DM.m.view(L.vc, G)
            if vw then S2L.ldHide(L, vw, false, "lead=0") end
            log("S2CORE lead: desligado (lead=0)")
        end
        return
    end
    local L, now = S2L.ldSt(ST), os.clock()
    local m, P = S2L.ldMine(ST), S2L.ldRaw()
    if m and P then
        if api.ROLE ~= "host" then
            log(string.format("S2CORE lead: empate: os dois tomaram a trava (cliente #%d %s, host #%d %s): o HOST vence, o cliente segue", m.seq, m.why, P.seq, P.why))
            L.mine = nil
            if L.dlg and L.dlg.mode == "lead" then L.dlg.mode = nil end     -- decided again: follow (same NPC) or held
        elseif L.tieSeq ~= P.seq then
            L.tieSeq = P.seq
            log(string.format("S2CORE lead: empate com a trava #%d do cliente (%s): o host vence (trava #%d %s)", P.seq, P.why, m.seq, m.why))
        end
    end
    S2L.ldDlg(ST, L, now)
end
function S2L.ldRole(ST)                                    -- dm70 role this tick: "f" follower, "d" driver, nil = none
    local L = S2L.ldSt(ST); local d = L.dlg; local md = d and d.mode
    if md == "follow" then return "f" end
    if md == "held" or (d and md == nil and S2L.ldPeer(ST)) then return nil end   -- deciding under the peer's lock: neither
    if md == "vend" or md == "lead" then return "d" end
    return S2L.ldPeer(ST) and "f" or "d"                  -- no view here: track the leader's picks while it leads
end
function S2L.ldDm(ST, G)                                   -- dmirTick: true = lead mode ran dm70 this tick (driver/follower by the lock)
    if (FL.lead or 0) ~= 1 then
        if ST.dmLead then ST.dmLead = nil; if ST.dmf then DM.m.restore(ST.dmf, G, "lead=0") end; ST.dmf = nil; ST.dm = {fresh = true} end
        return false
    end
    if not ST.dmLead then ST.dmLead = true; DM.m.restore(ST.dm, G, "lead=1"); ST.dm = {}; ST.dmf = {fresh = true} end
    if ST.dm.fresh then ST.dm.fresh = nil; ST.dmf.fresh = true end
    local r = S2L.ldRole(ST)
    if r ~= "f" and ST.dmf.orig and next(ST.dmf.orig) then DM.m.restore(ST.dmf, G, "este lado nao segue") end
    if r == "f" then DM.m.followerTick(ST.dmf, G) elseif r == "d" then DM.m.driverTick(ST.dm, G) end
    ST.dmRole = r
    return true
end
function S2L.ldQi(ST, T, Q, now)                           -- qiTick with lead=1 (both roles): keep it + a copy to the other player
    if (FL.qimirror or 0) == 1 then return S2L.qmTick(ST, T, Q, now) end   -- b97n7 part B: absolute counts both ways
    for ent in (x1part("QI") or ""):gmatch("[^|]+") do     -- the other player's pickups
        if not Q.seen[ent] then
            Q.seen[ent] = now
            local seq, sid, n = ent:match("^(%d+),([^,]+),(%d+)$")
            if seq and T.S[sid] then
                n = tonumber(n)
                local r = S2L.qiGive(sid, n, "copia do outro jogador", true)
                local c = 0; pcall(function() c = S2L.qiCount(S2.io.pawn, sid) end)
                Q.sent[sid] = {at = now, n = math.max(c, math.min(n, T.S[sid] or 1))}   -- echo guard: a copy is not a pickup here
                log("S2CORE lead: o outro jogador pegou " .. sid .. " x" .. n .. " (seq " .. seq .. ") -> copia: " .. r)
            elseif seq then log("S2CORE lead: relay de item ignorado (fora da tabela): " .. ent) end
        end
    end
    for k, t in pairs(Q.seen) do if now - t > 60 then Q.seen[k] = nil end end
    if now < (Q.at or 0) then return end
    Q.at = now + (FL.qisec or 0.5)
    local pw = S2.io and S2.io.pawn; if not valid(pw) then return end
    do local okG, rG = pcall(S2L.qgDead, ST, Q, pw, now, true); if okG and rG then return end end   -- b135fixes 2 (qdead)
    local nL = #T.list; if nL == 0 then return end
    for _ = 1, math.min(FL.qiper or 80, nL) do
        Q.i = Q.i % nL + 1
        local sid = T.list[Q.i]
        local n = S2L.qiCount(pw, sid)
        local s = Q.sent[sid]
        if not Q.based then                                -- first full sweep = what this player already had: not a pickup
            if n > 0 then Q.sent[sid] = {at = now, n = n} end
            Q.bn = (Q.bn or 0) + 1
            if Q.bn >= nL then Q.based = true; log("S2CORE lead: inventario base lido (" .. nL .. " itens de missao); a partir de agora cada item pego vai em copia ao outro") end
        elseif n > (s and s.n or 0) then                   -- a pickup (any source): keep it, send it once
            Q.sent[sid] = {at = now, n = n}; Q.seq = Q.seq + 1
            Q.out[#Q.out + 1] = {e = Q.seq .. "," .. sid .. "," .. n}
            log(string.format("S2CORE lead: item de missao pego aqui: %s x%d (fica com este jogador); copia enviada ao outro (seq %d)", sid, n, Q.seq))
        elseif s and n < s.n then s.n = n end               -- used or handed in: never re-sent; a later pickup counts again
    end
end
M.ldTake = function(ST, why, x, y, z, dur) return S2L.ldTake(ST, why, x, y, z, dur) end   -- for the live probes (S2.mod.ldTake)
M.ldFree = function(ST, why) return S2L.ldFree(ST, why) end
-- ===== end b92lead
-- ===== b99qpick (flag qpick, default 0; user rule N14 "either player may take quests ... quest state lands on the host's save of
-- record"). Live 2026-10-15 10:41: the client LED Mityay's dialog alone and picked "take the quest"; its own graph ran the pin (SQ02 in
-- the client journal), the host never heard of it. Whole family = every dialog pin a client-led dialog fires.
-- Pak zzzzzz_S2CoopQuestPick_P.pak (BOTH PCs, gen_qpick.py) adds ConsoleCommand siblings only:
--   "s2q D <SetDialog SID> <k>"  on each of the 3,553 pins (1,895 dialogs) that launch something: this side's OWN graph fired pin k;
--   "s2q L <node SID>"           next to each of the 16,198 nodes the host observer paks report (J/G/N/W): this side's graph ran it.
-- CLIENT: a pin fired while the client LEADS a dialog (b92 lock why=dlg, or within qplead s after it) -> "#QP:<seq>,<sid>,<k>" (3 s).
--         Vendors never take the lock, a followed/held dialog is not led: nothing is sent for them.
--         L: every node its own graph ran is counted; the host's later #J/catch-up report of that node is SKIPPED once per local run
--         (questApply), so the client never runs a step twice (own run + _S2C replay). The client's own run is the one kept.
-- HOST:   a client pin waits qpwait s (and while the host has a dialog open, up to qpmax s). If the host's own graph fired the same pin
--         (it followed the dialog) it is consumed, nothing runs; another pin of the same dialog = divergence, nothing runs (logged).
--         Else the host runs what the pin launches (s2u_qpick.lua): XExcludeQuestNodeBySID for '!' nodes, then XStartQuestNodeBySID
--         for the rest. The host's graph then runs the quest; its observer reports reach the client and are skipped there (L).
function S2L.qpSt(ST)
    ST.qp = ST.qp or {seq = 0, out = {}, seen = {}, own = {}, ownS = {}, loc = {}, q = {}, leadUntil = -1e9, pruneAt = 0,
        n = {sent = 0, ran = 0, dup = 0, div = 0, skip = 0, nolead = 0}}
    return ST.qp
end
function S2L.qpTab()                                       -- host: s2u_qpick.lua from the bridge (pin -> launched nodes)
    if S2L.qpT ~= nil then return S2L.qpT or nil end
    S2L.qpT = false
    local f = io.open(api.BR .. "s2u_qpick.lua", "r"); if not f then log("S2CORE qpick: falta s2u_qpick.lua na ponte"); return nil end
    local src = f:read("*a"); f:close()
    local fn, e = load(src, "s2u_qpick.lua")
    local ok, T = false, e
    if fn then ok, T = pcall(fn) end
    if not (ok and type(T) == "table" and type(T.pin) == "table") then log("S2CORE qpick: s2u_qpick.lua erro " .. tostring(T)); return nil end
    S2L.qpT = T
    log(string.format("S2CORE qpick: tabela carregada (%s dialogos, %s pinos)", tostring(T.stats and T.stats.dialogs), tostring(T.stats and T.stats.pins)))
    return T
end
function S2L.qpCmd(ST, t)                                  -- questCmd: "s2q D <sid> <k>" / "s2q L <sid>" from this side's own graph
    if (FL.qpick or 0) ~= 1 then return end
    local P, now = S2L.qpSt(ST), os.clock()
    if t[1] == "L" and t[2] then
        if api.ROLE ~= "client" then return end
        local e = P.loc[t[2]] or {n = 0}; e.n = e.n + 1; e.at = now; P.loc[t[2]] = e
        log(string.format("S2CORE qpick: no %s rodou no grafo do cliente (x%d): o relato do host desse no sera pulado", t[2], e.n))
        return
    end
    local k = tonumber(t[3])
    if t[1] ~= "D" or not t[2] or not k then return end
    local key = t[2] .. "#" .. k
    if api.ROLE == "host" then
        local o = P.own[key] or {n = 0}; o.n = o.n + 1; o.at = now; P.own[key] = o; P.ownS[t[2]] = now
        log(string.format("S2CORE qpick: pino %s #%d rodou no grafo do host", t[2], k))
        return
    end
    local m = S2L.ldMine(ST)
    if not ((m and m.why == "dlg") or now <= P.leadUntil) then
        P.n.nolead = P.n.nolead + 1
        log(string.format("S2CORE qpick: pino %s #%d rodou no cliente sem o cliente liderar o dialogo: nao enviado (o host roda o seu)", t[2], k))
        return
    end
    P.seq = P.seq + 1; P.n.sent = P.n.sent + 1
    P.out[#P.out + 1] = {e = P.seq .. "," .. t[2] .. "," .. k, at = now}
    log(string.format("S2CORE qpick: pino %s #%d escolhido aqui (o cliente lidera): enviado ao host (seq %d)", t[2], k, P.seq))
end
function S2L.qpSkip(sid)                                   -- client questApply: true = its own graph already ran sid (consumed once)
    if (FL.qpick or 0) ~= 1 or api.ROLE ~= "client" or not sid then return false end
    local P = S2.state and S2.state.qp; if not P then return false end
    local e = P.loc[sid]; if not e or e.n <= 0 then return false end
    if os.clock() - e.at > (FL.qplkeep or 900) then P.loc[sid] = nil; return false end
    e.n = e.n - 1; P.n.skip = P.n.skip + 1
    return true
end
function S2L.qpOut(ST)                                     -- client: "#QP:<seq>,<sid>,<k>|..." for 3 s each
    if (FL.qpick or 0) ~= 1 or api.ROLE ~= "client" or not ST.qp then return "" end
    local P, now, keep, r = ST.qp, os.clock(), {}, {}
    for _, o in ipairs(P.out) do if now - o.at < 3 then keep[#keep + 1] = o; r[#r + 1] = o.e end end
    P.out = keep
    return #r > 0 and ("#QP:" .. table.concat(r, "|")) or ""
end
function S2L.qpRun(P, it, now)                             -- host: run what pin it.k of it.sid launches; true = done (drop it)
    local T = S2L.qpTab(); local row = T and T.pin[it.sid] and T.pin[it.sid][it.k]
    if not row then log(string.format("S2CORE qpick: origem c %s pino %d seq=%s fora da tabela: nada feito", it.sid, it.k, it.seq)); return true end
    local aa = FindFirstOf("CustomConsoleManagerAA")
    if not valid(aa) then if now - it.at < (FL.qpmax or 120) then return false end; log("S2CORE qpick: CustomConsoleManagerAA ausente: pino " .. it.key .. " perdido"); return true end
    local ns, nx, bad, names = 0, 0, 0, {}
    for tok in row:gmatch("!(%S+)") do
        if pcall(function() aa:XExcludeQuestNodeBySID(tok) end) then nx = nx + 1 else bad = bad + 1 end
    end
    for tok in (" " .. row):gmatch(" ([^!%s]%S*)") do
        if pcall(function() aa:XStartQuestNodeBySID(tok) end) then ns = ns + 1; names[#names + 1] = tok else bad = bad + 1 end
    end
    P.n.ran = P.n.ran + 1
    log(string.format("S2CORE qpick: origem c %s pino %d seq=%s -> host XStartQuestNodeBySID %d [%s], XExclude %d, erros %d", it.sid, it.k, it.seq, ns, table.concat(names, " "), nx, bad))
    return true
end
function S2L.qpTick(ST)                                    -- tick100, both roles
    if (FL.qpick or 0) ~= 1 then return end
    local P, now = S2L.qpSt(ST), os.clock()
    if api.ROLE == "client" then
        local m = S2L.ldMine(ST)
        if m and m.why == "dlg" then P.leadUntil = now + (FL.qplead or 5) end
    else
        for ent in (remoteField("QP") or ""):gmatch("[^|]+") do
            local seq, sid, k = ent:match("^(%d+),([^,]+),(%d+)$")
            if seq and not P.seen[seq .. ":" .. sid .. "#" .. k] then
                P.seen[seq .. ":" .. sid .. "#" .. k] = now
                P.q[#P.q + 1] = {sid = sid, k = tonumber(k), key = sid .. "#" .. k, seq = seq, at = now}
                log(string.format("S2CORE qpick: origem c %s pino %s seq=%s recebido: espera %s s pelo grafo do host", sid, k, seq, tostring(FL.qpwait or 8)))
            end
        end
        local keep = {}
        for _, it in ipairs(P.q) do
            local o, done = P.own[it.key], false
            if o and o.n > 0 and now - o.at <= (FL.qpkeep or 600) then
                o.n = o.n - 1; P.n.dup = P.n.dup + 1; done = true
                log(string.format("S2CORE qpick: origem c %s pino %d seq=%s: o grafo do host ja rodou esse pino (dialogo seguido): nada repetido", it.sid, it.k, it.seq))
            elseif P.ownS[it.sid] and now - P.ownS[it.sid] <= (FL.qpwait or 8) + (now - it.at) then
                P.n.div = P.n.div + 1; done = true
                log(string.format("S2CORE qpick: DIVERGENCIA origem c %s pino %d seq=%s: o host rodou outro pino desse dialogo: nada repetido", it.sid, it.k, it.seq))
            elseif now - it.at >= (FL.qpwait or 8) and not (ST.ld and ST.ld.dlg and now - it.at < (FL.qpmax or 120)) then
                done = S2L.qpRun(P, it, now)
            end
            if not done then keep[#keep + 1] = it end
        end
        P.q = keep
    end
    if now - P.pruneAt >= 5 then
        P.pruneAt = now
        for k, at in pairs(P.seen) do if now - at > 60 then P.seen[k] = nil end end
        for k, o in pairs(P.own) do if now - o.at > (FL.qpkeep or 600) then P.own[k] = nil end end
        for k, at in pairs(P.ownS) do if now - at > (FL.qpkeep or 600) then P.ownS[k] = nil end end
        for k, e in pairs(P.loc) do if e.n <= 0 or now - e.at > (FL.qplkeep or 900) then P.loc[k] = nil end end
    end
end
-- ===== end b99qpick
local function questApply(kind, a, b, c, sid, live)
    if S2L.qiSkip(sid) then return nil, "item de missao fica so no host (qitem)" end   -- b87qitem
    if (FL.natnpc or 0) == 1 and (FL.b164off or 0) == 0 and sid and rawget(_G, "s2n_qr_npc") and rawget(_G, "s2n_nat_listed_addr") then local g = s2n_qr_npc(sid); if g then local hit = false; pcall(function() local a = S2L.qiMed():GetActorByGuid(S2L.chGuid(g)); hit = a ~= nil and a:IsValid() and s2n_nat_listed_addr(a:GetAddress()) == true end); if hit then S2L.qrN = (S2L.qrN or 0) + 1; return nil, "no de NPC que o natnpc dirige: pulado (b164, total " .. S2L.qrN .. ")" end end end   -- b164 (audit 185 #2)
    if S2L.qpSkip(sid) then return nil, "ja rodou no grafo do cliente (qpick): pulado" end   -- b99qpick
    if qskip(sid) and not (live and FL.qskipcatch == 1) then return nil, "no vivo no cliente (s2c_skip)" end
    if FL.qnodlg == 1 and sid and sid:find("Dialog", 1, true) and not dmirLet(sid, live) then return nil, "dialogo fica so no host (qnodlg)" end   -- b70d; b70dlg: dmir lets SetDialog nodes through
    if kind == "W" and FL.qworld ~= 1 then return nil, "evento de mundo ignorado (qworld=0)" end
    if kind == "X" and FL.qextra ~= 1 then return nil, "evento extra ignorado (qextra=0)" end
    if FL.qnode == 1 and sid and sid ~= "-" then          -- client pak route: the exact stock node, run in isolation
        local aa = FindFirstOf("CustomConsoleManagerAA"); if not valid(aa) then return false, "CustomConsoleManagerAA ausente" end
        if FL.cjoin == 1 and S2L.mqSid(sid) then local G = cjG(); if G then pcall(CJ.m.cjBeforeStart, CJ.st, sid, G) end end   -- batch 69: client joins the host first; b109 mqpull
        aa:XStartQuestNodeBySID(sid .. "_S2C")
        return true, "no " .. sid .. "_S2C"
    end
    if kind == "J" then
        local dk = FindFirstOf("CustomConsoleManagerDK"); if not valid(dk) then return false, "CustomConsoleManagerDK ausente" end
        if c == "Start" then
            if b == "-" then dk:XDKStartQuest(a) else dk:XDKStartQuestStage(a, b) end
            return true
        elseif c == "Finish" or c == "Fail" or c == "Cancel" then
            if b == "-" then dk:XDKFinishQuest(a, c == "Finish") else dk:XDKFinishQuestStage(a, b, c == "Finish") end
            return true
        end
        return false, "acao desconhecida " .. tostring(c)
    end
    if kind == "G" and b == "Set" and (c == "true" or c == "false" or c == "True" or c == "False") then
        local rk = FindFirstOf("CustomConsoleManagerRK"); if not valid(rk) then return false, "CustomConsoleManagerRK ausente" end
        rk:XSetGlobalBool(a, c == "true" or c == "True")
        return true
    end
    return nil, "nao aplicavel sem o pak do cliente (qnode=1)"
end
local function parseEv(e)                             -- "J,a,b,c,sid" / "G,a,b,c,sid" / "N,sid" / old "J,a,b,c"
    local f = {}
    for w in e:gmatch("[^,]+") do f[#f + 1] = w end
    if f[1] == "N" or f[1] == "W" or f[1] == "X" then return f[1], nil, nil, nil, f[2] end
    return f[1], f[2], f[3], f[4], f[5]
end
-- Catch-up (batch 8): for qcatchup seconds after a client session starts, the host offers its whole log as module texts (api.lookPut,
-- 1,200-byte chunks, each re-put every 1.5 s) and names them in x1 as "#S:<log size>,<hash>.<hash>...". The client fetches every chunk
-- (main.lua asks in "need"), then replays all entries in order, once per host log size. Live #J events keep running meanwhile.
local function catchupOut(ST)
    if api.ROLE ~= "host" or FL.qsync ~= 1 or not api.lookPut then return "" end
    local Q, P, now = ST.qs, ST.peer or {}, os.clock()
    qlogLoad(Q)
    if not S2L.qlSettled(Q) then return "" end   -- b165: no offer before the log matches the loaded save
    if P.sessions and P.sessions ~= Q.cuSession and P.state == "up" and not P.loopback then
        Q.cuSession = P.sessions; Q.cuUntil = now + FL.qcatchup; Q.cu = nil; Q.cuN = #Q.log   -- b11: size frozen here, empty or not
        Q.cuLoads = x1part("N")
    end
    local nl = x1part("N")                               -- batch 24: the client loaded a save in the middle of the session
    if nl and Q.cuLoads ~= nil and nl ~= Q.cuLoads and P.state == "up" then
        Q.cuLoads = nl; Q.cuUntil = now + FL.qcatchup; Q.cu = nil; Q.cuN = #Q.log
        log("S2CORE quest: o cliente carregou um save (#" .. nl .. "); catch-up oferecido de novo")
    elseif nl and Q.cuLoads == nil then Q.cuLoads = nl end
    if not Q.cuUntil or now > Q.cuUntil or (Q.cuN or 0) == 0 or #Q.log == 0 then return "" end
    if not Q.cu then                                   -- frozen at session start: later events reach the client as live #J
        local chunks, cur = {}, {}
        local size = 0
        for i, x in ipairs(Q.log) do
            if i > Q.cuN then break end
            if size + #x.e + 1 > 1200 and #cur > 0 then chunks[#chunks + 1] = table.concat(cur, ";"); cur = {}; size = 0 end
            cur[#cur + 1] = x.e; size = size + #x.e + 1
        end
        if #cur > 0 then chunks[#chunks + 1] = table.concat(cur, ";") end
        Q.cu = {n = math.min(Q.cuN, #Q.log), chunks = chunks, hashes = {}, rr = 0}
        for i, c in ipairs(chunks) do Q.cu.hashes[i] = api.lookPut("QL" .. i .. "~" .. c) end
        log(string.format("S2CORE quest: catch-up oferecido, %d eventos em %d pedacos", Q.cu.n, #chunks))
    end
    local C = Q.cu
    for _ = 1, math.min(#C.chunks, math.max(2, math.ceil(#C.chunks / 15))) do   -- main.lua re-offers a text on "need" only if it was
                                                       -- put in the last 2 s and keeps one hash cache: refresh every chunk within 1.5 s
        C.rr = C.rr % #C.chunks + 1
        api.lookPut("QL" .. C.rr .. "~" .. C.chunks[C.rr])
    end
    return "#S:" .. C.n .. "," .. table.concat(C.hashes, ".")
end
-- ===== batch 9: item take-back. Messages in x1 (both directions):
--   #U:<seq>,P,<item>,<n>|<seq>,C,<placeholder SID>,<item>,<n or A>|...   "remove <n> <item> from YOUR player / from YOUR copy of that container"
-- The receiving side starts the stand-alone ItemRemove nodes of the S2U paks (gen_s2u.py): S2U_P_<item>_<2^k> (bits of n, up to 1023 per
-- pass) and S2U_C_<SID>_<item>_1 (n times, one per tick) or _A (all). Never an inventory call on a container actor or UID (two crashes).
local function itInit(ST) ST.it = ST.it or {seq = 0, list = {}, seen = {}, run = {}, sent = 0, applied = 0} return ST.it end
local function itContLoad(I)
    if I.cont then return I.cont end
    I.cont = {}
    local f = io.open(api.BR .. "s2u_cont.tsv", "r")
    if f then
        for ln in f:lines() do
            local sid, x, y, z = ln:match("^(%x+)\t[^\t]*\t[^\t]*\t([%-%d%.]+)\t([%-%d%.]+)\t([%-%d%.]+)")
            if sid then I.cont[#I.cont + 1] = {sid = sid, x = tonumber(x), y = tonumber(y), z = tonumber(z)} end
        end
        f:close()
    end
    log("S2CORE itake: " .. #I.cont .. " recipientes em s2u_cont.tsv")
    return I.cont
end
-- Placeholder SID of a container from its lock id ("<class>@<x>,<y>,<z>" in 10 cm units, see lockTick) or from a position in cm.
-- Pure table lookup: nothing is called on the actor.
function M.contSid(ST, idOrX, y, z)
    local I = itInit(ST)
    local x = idOrX
    if type(idOrX) == "string" then
        local a, b, c = idOrX:match("@([%-%d%.]+),([%-%d%.]+),([%-%d%.]+)$")
        if not a then return nil end
        x, y, z = tonumber(a) * 10, tonumber(b) * 10, tonumber(c) * 10
    end
    local best, bd = nil, (FL.icontr or 300) ^ 2
    for _, c in ipairs(itContLoad(I)) do
        local d = (c.x - x) ^ 2 + (c.y - y) ^ 2 + (c.z - z) ^ 2
        if d <= bd then best, bd = c.sid, d end
    end
    return best
end
local function itQueue(I, kind, a, b, c)               -- local removal: turn it into node SIDs
    if kind == "P" then
        local n = math.floor(tonumber(b) or 0)
        while n > 0 do
            local part = math.min(n, 1023); n = n - part
            for k = 9, 0, -1 do if part >= 2 ^ k then part = part - 2 ^ k; I.run[#I.run + 1] = string.format("S2U_P_%s_%d", a, 2 ^ k) end end
        end
        return true
    elseif kind == "C" then
        if c == "A" then
            if FL.ionly1 == 1 then log("S2CORE itake: pedido 'A' recusado (paks so com nos de 1 unidade; mande a quantidade)"); return false end
            I.run[#I.run + 1] = string.format("S2U_C_%s_%s_A", a, b); return true
        end
        local n = math.floor(tonumber(c) or 0)
        if n < 1 then return false end
        if n > (FL.icmax or 1000) then log(string.format("S2CORE itake: %s x%d cortado em %d", tostring(b), n, FL.icmax or 1000)); n = FL.icmax or 1000 end
        for _ = 1, n do I.run[#I.run + 1] = string.format("S2U_C_%s_%s_1", a, b) end
        return true
    elseif kind == "A" then                              -- batch 22 (artsync=2): the client picked an artifact up at place <a>
        return M.artPlaceTaken ~= nil and M.artPlaceTaken(S2.state, tonumber(a), "cliente") or false
    elseif kind == "T" then                              -- batch 21: the other player bought n of a slot item from this trader: remember it
        return M.trNote ~= nil and M.trNote(S2.state, a, b, c) or false
    end
    return false
end
-- API (game worker / luacmd): S2.mod.itemRemove(S2.state, "P", item, n)            -> remove from MY player
--                              S2.mod.itemRemove(S2.state, "C", sid, item, n|"A")   -> remove from MY copy of a container
--                              S2.mod.itemSend(S2.state, "P", item, n) / ("C", sid, item, n|"A") -> ask the OTHER side to do it
function M.itemRemove(ST, kind, a, b, c)
    local I = itInit(ST)
    local ok = itQueue(I, kind, a, b, c)
    log(string.format("S2CORE itake: local %s %s %s %s -> %s (%d nos na fila)", tostring(kind), tostring(a), tostring(b), tostring(c), tostring(ok), #I.run))
    return ok
end
function M.itemSend(ST, kind, a, b, c)
    local I = itInit(ST)
    if FL.itake ~= 1 then return false end
    local tok = function(v) return v ~= nil and tostring(v):match("^[%w_%-%.]+$") ~= nil end
    if not (tok(a) and tok(b) and (kind == "P" or ((kind == "C" or kind == "T" or kind == "A") and tok(c)))) then log("S2CORE itake: pedido invalido"); return false end
    I.seq = I.seq + 1
    local s = I.seq .. "," .. kind .. "," .. a .. "," .. b .. (kind ~= "P" and ("," .. c) or "")
    I.list[#I.list + 1] = {t = os.clock(), s = s}
    log("S2CORE itake: envio " .. s)
    return true
end
local function itOut(ST)
    local I = itInit(ST)
    local now, keep, parts = os.clock(), {}, {}
    for _, e in ipairs(I.list) do if now - e.t < FL.resend then keep[#keep + 1] = e; parts[#parts + 1] = e.s end end
    I.list = keep
    if #parts > 0 then I.sent = I.sent + 1; return "#U:" .. table.concat(parts, "|") end
    return ""
end
local function itIn(ST)
    if FL.itake ~= 1 then return end
    local I, now = itInit(ST), os.clock()
    local P = ST.peer or {}
    if P.loopback and FL.itest ~= 1 then return end        -- our own snapshot coming back: not for us
    if now - (I.seen.pruneAt or -10) >= 1 then
        I.seen.pruneAt = now
        for k, at in pairs(I.seen) do if type(k) == "number" and now - at > 5 then I.seen[k] = nil end end
    end
    for ev in (x1part("U") or ""):gmatch("[^|]+") do
        local f = {}
        for w in ev:gmatch("[^,]+") do f[#f + 1] = w end
        local seq = tonumber(f[1])
        if seq and not I.seen[seq] then
            I.seen[seq] = now
            local ok = itQueue(I, f[2], f[3], f[4], f[5])
            if ok then I.applied = I.applied + 1 end
            log(string.format("S2CORE itake: recebido seq=%d %s -> %s (%d nos na fila)", seq, ev, tostring(ok), #I.run))
        elseif seq then I.seen[seq] = now end
    end
end
local function itRun(ST)
    local I = itInit(ST)
    if #I.run == 0 then return end
    local aa = FindFirstOf("CustomConsoleManagerAA"); if not valid(aa) then return end
    local used = {}
    local n = 0
    while #I.run > 0 and n < (FL.inodes or 4) do
        local sid = I.run[1]
        if used[sid] and FL.isame ~= 1 then break end        -- isame=0: one start of a node per tick
        table.remove(I.run, 1); used[sid] = true; n = n + 1
        local ok, e = pcall(function() aa:XStartQuestNodeBySID(sid) end)
        if not ok then log("S2CORE itake: XStartQuestNodeBySID " .. sid .. " erro " .. tostring(e)) end
    end
    if #I.run == 0 then log("S2CORE itake: fila vazia") end
end
-- batch 22: how many times each event text was applied in the current client world
local CSFMT = "#S2CA 1"                                -- batch 25: first line of client_applied.txt
local function qApplied(Q)
    local pw = S2.io and S2.io.pawn
    if not valid(pw) then pcall(function() pw = api.UEHelpers.GetPlayerController().Pawn end) end
    local addr = valid(pw) and pw:GetAddress() or nil
    if addr and addr ~= Q.apAddr then
        if Q.apAddr ~= nil and Q.ap and next(Q.ap) then log("S2CORE quest: pawn novo do cliente: contagem de eventos aplicados zerada (mundo voltou ao save base)") end
        Q.apAddr = addr; Q.ap = {}
        Q.csDirty = nil; Q.csSnap = nil; Q.csWait = nil; Q.csQueue = nil; Q.csAhead = nil; Q.csRefused = nil; Q.apFresh = nil; Q.apCid = nil; Q.apW = nil   -- b167: apW = campaign of the loaded world
        Q.csLoads = (Q.csLoads or 0) + 1; Q.cuDone = nil      -- batch 24: a load: ask the host for the catch-up again
        Q.cuStale = x1part("S"); Q.cuStaleAt = os.clock()     -- batch 26: the offer on the wire now was made before this load
        if FL.csave == 1 and api.ROLE == "client" then      -- batch 23: the loaded save already holds what the last mod-made save had
            local n, f = 0, io.open(api.BR .. "client_applied.txt", "r")
            if f then
                local head = f:read("*l") or ""
                Q.apCid = head:match("^" .. CSFMT .. " (%w+)$")                       -- batch 32: the campaign this record belongs to (nil: written before batch 32)
                if head ~= CSFMT and not Q.apCid then log("S2CORE csave: client_applied.txt sem o carimbo '" .. CSFMT .. "' (arquivo antigo ou de outro mod): ignorado")
                else for ln in f:lines() do local c, e = ln:match("^(%d+)\t(.+)$"); if c then Q.ap[e] = tonumber(c); n = n + tonumber(c) else Q.apW = Q.apW or ln:match("^W\t(%w+)$") end end end   -- b167 W
                f:close()
            end
            Q.bootRec = {cid = Q.apCid, n = n, addr = addr, wc = Q.apW}   -- b167 wc   -- b165: what the loaded save holds (n7Boot)
            Q.apFresh = n > 0 or nil                         -- batch 25: read from the file and not yet compared with a host log
            log("S2CORE csave: save do cliente carregado; " .. n .. " eventos do host ja estao nele (client_applied.txt)")
        end
    end
    Q.ap = Q.ap or {}
    return Q.ap
end
local function catchupIn(ST)
    if FL.qsync ~= 1 or not (api.ROLE == "client" or FL.qtest == 1) or not api.lookGet then return end
    local Q = ST.qs
    local sg = x1part("S"); if not sg then return end
    if Q.cuStale and sg == Q.cuStale and os.clock() < (Q.cuStaleAt or 0) + 3 then return end   -- batch 26: wait for the host's answer to our #N
    local n, hs = sg:match("^(%d+),(.+)$"); if not n then return end
    local key = (ST.peer and ST.peer.id or "?") .. "/" .. n
    if Q.cuDone == key then return end
    if Q.rbWait and not M.rbReady(ST) then return end   -- batch 16: reloading after a host rollback: replay only on the new pawn
    if Q.csWait then return end                         -- batch 25: a save is being confirmed: apply nothing until it is settled
    local texts, missing = {}, 0
    for h in hs:gmatch("[^%.]+") do
        local t = api.lookGet(h)
        if t then texts[#texts + 1] = t else missing = missing + 1 end
    end
    if missing > 0 then return end
    table.sort(texts, function(x, y) return tonumber(x:match("^QL(%d+)~")) < tonumber(y:match("^QL(%d+)~")) end)
    local okN, skip, had = 0, 0, 0
    local ap, seen = qApplied(Q), {}
    local rolled, stale = false, false
    if FL.csave == 1 then                                -- batch 23: counts of another campaign (more events than the host has)?
        local have, nlog = 0, 0
        for _, c in pairs(ap) do have = have + c end
        for _, t in ipairs(texts) do for _ in t:gsub("^QL%d+~", ""):gmatch("[^;]+") do nlog = nlog + 1 end end
        local cid = (x1part("B") or ""):match("^%d+,%d+,(%w+)")                       -- batch 32: the host's campaign id
        local bn = tonumber(((x1part("B") or ""):match("^%d+,(%d+)")))
        if bn and nlog < bn then                         -- batch 33: an offer made before the host's last steps: apply what is missing, decide nothing
            stale = true
            log(string.format("S2CORE quest: catch-up antigo (%d eventos, o host ja tem %d): so o que falta, sem decidir rollback", nlog, bn))
        elseif cid and Q.apCid and cid ~= Q.apCid and have > 0 then
            log(string.format("S2CORE csave: client_applied.txt e da campanha %s e o host esta na %s: registro descartado; tudo sera aplicado", Q.apCid, cid))
            for k in pairs(ap) do ap[k] = nil end; Q.csAhead = nil   -- batch 46: the old campaign's 'ahead' list goes too
        elseif have > nlog and (Q.csRb or (cid and cid == Q.apCid)) then rolled = true   -- the host went back (seen live, or same campaign with a shorter log): we are ahead
        elseif have > nlog then
            log(string.format("S2CORE csave: client_applied.txt tem %d eventos e o log do host so %d: e de outra campanha; tudo sera aplicado", have, nlog))
            for k in pairs(ap) do ap[k] = nil end; Q.csAhead = nil   -- batch 46: the old campaign's 'ahead' list goes too
        end
        if cid then Q.apCid = cid end                    -- from now on the record follows this host's campaign
    end
    for _, t in ipairs(texts) do
        for e in t:gsub("^QL%d+~", ""):gmatch("[^;]+") do
            seen[e] = (seen[e] or 0) + 1
            if FL.qonce == 1 and seen[e] <= (ap[e] or 0) then had = had + 1          -- batch 22: this world already has it
            else
                local ok, r = pcall(questApply, parseEv(e))
                if ok and r then okN = okN + 1 else skip = skip + 1 end
                ap[e] = math.max(ap[e] or 0, seen[e]); Q.csDirty = true; Q.csAt = os.clock()
            end
        end
    end
    if rolled then
        local na = 0
        for k, c in pairs(ap) do
            local d = c - (seen[k] or 0)
            if d > 0 then Q.csAhead = Q.csAhead or {}; Q.csAhead[k] = d; ap[k] = seen[k]; na = na + d end
        end
        Q.csDirty = true
        log("S2CORE csave: o host voltou no tempo e o cliente esta " .. na .. " eventos adiante; nada reaplicado, esses eventos serao pulados quando o host refizer")
    end
    if not stale then Q.csRb = nil; Q.apFresh = nil end   -- batch 33: an old offer decides nothing, so it clears nothing
    Q.cuDone = key; Q.rx = (Q.rx or 0) + okN + skip + had   -- b163quest: qdiv counter
    log(string.format("S2CORE quest: catch-up aplicado: %d eventos aplicados, %d sem rota%s (log do host com %s eventos)", okN, skip, had > 0 and (", " .. had .. " ja aplicados antes") or "", n))
    if Q.rbWait then M.rbDone(ST, "catch-up refeito") end
end
-- ===== batch 16 (2): host rollback -> client reload (#B) =====
local function rbPawn()
    local pw = S2.io and S2.io.pawn
    if not valid(pw) then pcall(function() pw = api.UEHelpers.GetPlayerController().Pawn end) end
    return pw
end
local function campId(Q)                              -- batch 32: host: one id per quest log, made once
    if Q.cid then return Q.cid end
    local f = io.open(api.BR .. "quest_log.id", "r")
    local v = f and f:read("*l"); if f then f:close() end
    if not (v and v:match("^%w+$")) then
        v = string.format("c%x%04x", os.time(), math.random(0, 65535))
        f = io.open(api.BR .. "quest_log.id", "w"); if f then f:write(v .. "\n"); f:close() end
        log("S2CORE quest: id da campanha criado: " .. v .. " (quest_log.id)")
    end
    Q.cid = v
    return v
end
local function rbGen(Q)
    if not Q.gen then
        local f = io.open(api.BR .. "quest_log.gen", "r")
        Q.gen = f and tonumber(f:read("*l")) or 0
        if f then f:close() end
    end
    return Q.gen
end
function M.rbBump(ST, why)                             -- host: its quest log was just cut
    if FL.qrollback ~= 1 or api.ROLE ~= "host" then return end
    local Q = ST.qs
    Q.gen = rbGen(Q) + 1
    local f = io.open(api.BR .. "quest_log.gen", "w"); if f then f:write(tostring(Q.gen) .. "\n"); f:close() end
    Q.cu = nil; Q.cuN = #Q.log; Q.cuUntil = os.clock() + FL.qrbwindow
    log(string.format("S2CORE rollback: host voltou no tempo (%s): geracao %d, log com %d eventos, catch-up oferecido por %d s", tostring(why), Q.gen, #Q.log, FL.qrbwindow))
end
-- ===== batch 46: a new co-op campaign record on the host (new game seen, or the host's flag campaign=<word> changed)
function M.campRotate(ST, why)
    if api.ROLE ~= "host" then return end
    local Q = ST.qs; qlogLoad(Q)
    local old, n = campId(Q), #Q.log
    for _, p in ipairs({qlogPath(), takenFile(), api.BR .. "art_taken.txt", mineFile()}) do   -- batch 50: taken_mine.txt too
        local f = io.open(p, "r")
        if f then f:close(); os.remove(p .. "." .. old); os.rename(p, p .. "." .. old) end
    end
    os.remove(api.BR .. "quest_log.id")
    Q.cid = nil; Q.log = {}; Q.pend = nil; Q.cu = nil
    local new = campId(Q)
    pcall(takenLoad, ST); ST.ap2 = nil; ST.takenMine = nil
    log(string.format("S2CORE campanha: nova campanha cooperativa (%s): %s -> %s; %d eventos e taken/art_taken antigos guardados como *.%s", tostring(why), old, new, n, old))
    if M.rbBump then M.rbBump(ST, "campanha nova") end
end
function M.campFlag(ST)
    local want = tostring(FL.campaign or "")
    if want == "" or api.ROLE ~= "host" then return end
    local Q = ST.qs
    if Q.campTag == nil then local f = io.open(api.BR .. "quest_log.camp", "r"); Q.campTag = f and (f:read("*l") or "") or ""; if f then f:close() end end
    if want == Q.campTag then return end
    M.campRotate(ST, "flag campaign=" .. want)
    Q.campTag = want
    local f = io.open(api.BR .. "quest_log.camp", "w"); if f then f:write(want .. "\n"); f:close() end
end
local function rbOut(ST)
    if FL.qrollback ~= 1 or api.ROLE ~= "host" or FL.qsync ~= 1 then return "" end
    local Q = ST.qs; qlogLoad(Q)
    if not S2L.qlSettled(Q) then return "" end   -- b165: #B (gen, size, kh) only once the log matches the loaded save
    return string.format("#B:%d,%d,%s", rbGen(Q), #Q.log, campId(Q)) .. S2L.n7B(ST)   -- b97n7: ",hl,hs,kh,tag" with n7=1
end
function M.rbReady(ST)                                 -- client: reloaded and settled?
    local W = ST.qs.rbWait; if not W then return true end
    local now, pw = os.clock(), rbPawn()
    if valid(pw) and pw:GetAddress() ~= W.addr then W.readyAt = W.readyAt or (now + FL.qrbdelay) end
    return W.readyAt ~= nil and now >= W.readyAt
end
function M.rbDone(ST, why)                             -- client: replay finished (or given up): apply the queued live events
    local Q = ST.qs; if not Q.rbWait then return end
    local n, okN = #(Q.rbQueue or {}), 0
    local ap = qApplied(Q)
    for _, rest in ipairs(Q.rbQueue or {}) do
        local ok, r = pcall(questApply, parseEv(rest))
        if ok and r then okN = okN + 1 end
        ap[rest] = (ap[rest] or 0) + 1; Q.csDirty = true; Q.csAt = os.clock()
    end
    log(string.format("S2CORE rollback: cliente pronto (%s) depois de %.0f s; %d eventos ao vivo guardados, %d aplicados", tostring(why), os.clock() - Q.rbWait.at, n, okN))
    Q.rbWait = nil; Q.rbQueue = nil
end
local function rbTick(ST)
    if FL.qsync ~= 1 then return end
    local Q, now = ST.qs, os.clock()
    if api.ROLE == "host" then
        pcall(M.campFlag, ST)                                     -- batch 46
        if FL.qrollback ~= 1 then return end
        local pw = rbPawn(); if not valid(pw) then return end
        local addr = pw:GetAddress()
        if Q.rbAddr == nil then Q.rbAddr = addr
        elseif addr ~= Q.rbAddr then Q.rbAddr = addr; Q.rbCheckAt = now + 12 end      -- a load: wait for main.lua to read the new game clock
        if Q.rbCheckAt and now >= Q.rbCheckAt then
            Q.rbCheckAt = nil; qlogLoad(Q)
            local gt = tonumber(rawget(_G, "S2_gameSecs"))
            local newest = 0
            for _, x in ipairs(Q.log) do if x.t > newest then newest = x.t end end      -- not the last line: a load re-fires old nodes and appends them with the older clock (core-24d)
            if gt and gt > 0 and newest > gt + 1 then
                local keep = {}
                for _, x in ipairs(Q.log) do if x.t <= gt then keep[#keep + 1] = x end end
                log(string.format("S2CORE quest: pawn novo do host e relogio %.0f s atras do log (%.0f s): %d eventos descartados", gt, newest, #Q.log - #keep))
                Q.log = keep; qlogRewrite(Q); M.rbBump(ST, "pawn novo do host")
            end
        end
        return
    end
    if FL.qrollback ~= 1 then if Q.rbWait then M.rbDone(ST, "qrollback desligado") end return end
    local g, n = (x1part("B") or ""):match("^(%d+),(%d+)")
    g, n = tonumber(g), tonumber(n)
    if g then
        if Q.rbSeen == nil then Q.rbSeen = g                 -- first value in this game process: our journal starts from the base save anyway
        elseif g ~= Q.rbSeen then
            Q.rbSeen = g
            if FL.qrbmode ~= 1 or FL.csave == 1 then           -- batch 23: a client that keeps its own save is not reloaded
                if FL.csave == 1 then Q.csRb = true; Q.cuDone = nil end   -- batch 25: the next catch-up is a rollback, not another campaign; batch 32: and it must run
                log(string.format("S2CORE rollback: o host voltou no tempo (geracao %d, %d eventos); qrbmode=0: o cliente NAO recarrega e fica com o diario adiantado", g, n))
            elseif not Q.rbWait then
                local pw = rbPawn()
                if M.cpFlush then pcall(M.cpFlush, ST, "antes de recarregar") end          -- persistence: the reload must give this inventory back
                Q.cuDone = nil; Q.seen = {}
                Q.rbWait = {at = now, addr = valid(pw) and pw:GetAddress() or nil, n = n}; Q.rbQueue = {}
                local ok, e = pcall(function() FindFirstOf("CustomConsoleManagerMP"):XLoadLastSave() end)
                log(string.format("S2CORE rollback: o host voltou no tempo (geracao %d, %d eventos) -> XLoadLastSave ok=%s %s", g, n, tostring(ok), ok and "" or tostring(e)))
                if not ok then M.rbDone(ST, "XLoadLastSave falhou") end
            end
        end
    end
    local bcid = (x1part("B") or ""):match("^%d+,%d+,(%w+)")      -- batch 46: the host started another co-op campaign record: drop ours at once
    if bcid and FL.csave == 1 and Q.apCid and bcid ~= Q.apCid then
        local ap, had = qApplied(Q), 0
        for k in pairs(ap) do had = had + 1; ap[k] = nil end
        log(string.format("S2CORE csave: campanha do host mudou (%s -> %s): registro descartado (%d eventos), adiantados tambem; o proximo catch-up e aplicado inteiro", Q.apCid, bcid, had))
        Q.apCid = bcid; Q.csAhead = nil; Q.csRb = nil; Q.cuDone = nil; Q.csDirty = true; Q.csAt = os.clock()
    end
    local W = Q.rbWait
    if W then
        if n ~= nil then W.n = n end
        if W.n == 0 and M.rbReady(ST) then M.rbDone(ST, "log do host vazio")
        elseif (FL.qnohold or 0) ~= 1 and W.addr and now - W.at > (FL.qrbnl or 30) and (function() local p = rbPawn(); return valid(p) and p:GetAddress() == W.addr end)() then
            M.rbDone(ST, "nenhum save carregado em " .. tostring(FL.qrbnl or 30) .. " s (mesmo pawn)")   -- b163quest
        elseif now - W.at > FL.qrbmax then M.rbDone(ST, "tempo esgotado") end
    end
end
-- ===== batch 16 (3): a sleeping client is only told about it (its clock follows the host's) =====
local function slTick(ST)
    if api.ROLE == "host" then return end
    local pw = rbPawn(); if not valid(pw) then return end
    local slp = false
    pcall(function() slp = pw:IsSleeping() == true end)
    if slp and not ST.cslp then log("S2CORE sono: o cliente dormiu; isso so pula o relogio LOCAL, que volta ao do host (synctime)") end
    ST.cslp = slp
end
-- ===== batch 23: the client keeps its own save; the mod saves after applying host events and remembers what that save holds =====
local function csTick(ST)
    if FL.csave ~= 1 or api.ROLE ~= "client" then return end
    local Q, now = ST.qs, os.clock()
    local pw = S2.io and S2.io.pawn
    if not valid(pw) then pcall(function() pw = api.UEHelpers.GetPlayerController().Pawn end) end
    if not valid(pw) then return end
    local addr = pw:GetAddress()
    local hp; pcall(function() hp = pw:GetHP() end)
    if type(hp) ~= "number" then hp = nil end
    if hp and Q.csHp and hp < Q.csHp - 0.5 then Q.csHurtAt = now end      -- batch 25: losing HP = in a fight
    Q.csHp = hp
    local frozen = (ST.cv and ST.cv.fz ~= nil) or (ST.nd and ST.nd.dead ~= nil) or (ST.life and ST.life.down ~= nil)
        or (ST.em and ST.em.noSave)                     -- batch 33: no save while an emission runs here or just ended
    qApplied(Q)                                          -- batch 24: notice a load at once (load counter, record of the loaded save)
    if addr ~= Q.csAddr then                             -- a load: a save made while we held the player frozen must not leave it stuck
        Q.csAddr = addr; Q.csCommitAt = nil; Q.csNext = now + 20
        Q.csPeerWas = nil; Q.csLeave = nil                  -- batch 32: the silence of a loading screen is not "the other side left"
        if not frozen then pcall(function() if pw.CharacterMovement.MovementMode == 0 then pw.CharacterMovement:SetMovementMode(1, 0); log("S2CORE csave: movimento devolvido ao pawn carregado") end end) end
    end
    -- batch 24: the save counts only when the game's save index file changed
    local function sig()
        do local _, st = S2L.svInfo(); if st then return st end end   -- b165: the folder this game writes (C++)
        if Q.csPath == nil then
            Q.csPath = false
            local f = io.open(api.BR .. "savepath.txt", "r")
            local p = f and f:read("*l"); if f then f:close() end
            if not p or p == "" then local la = os.getenv("LOCALAPPDATA"); p = la and (la .. "\\Stalker2\\Saved\\STEAM\\SaveGames\\CampaignsSave.sav") or nil end
            if p then Q.csPath = p end
        end
        if not Q.csPath then return nil end
        local f = io.open(Q.csPath, "rb"); if not f then return nil end
        local d = f:read("*a"); f:close()
        local sum = 0
        for i = 1, #d, 7 do sum = (sum * 31 + d:byte(i)) % 2147483647 end
        return #d .. ":" .. sum
    end
    local function record(why)
        local f = io.open(api.BR .. "client_applied.txt.tmp", "w")
        if f then
            local n = 0
            Q.apCid = Q.apCid or (x1part("B") or ""):match("^%d+,%d+,(%w+)")      -- batch 32: the campaign of the host we are playing with
            f:write(CSFMT .. (Q.apCid and (" " .. Q.apCid) or "") .. "\n")
            if Q.apW then f:write("W\t" .. Q.apW .. "\n") end   -- b167: the world this save holds
            for e, c in pairs(Q.csSnap or {}) do f:write(c .. "\t" .. e .. "\n"); n = n + c end
            f:close()
            os.remove(api.BR .. "client_applied.txt"); os.rename(api.BR .. "client_applied.txt.tmp", api.BR .. "client_applied.txt")
            log("S2CORE csave: jogo do cliente salvo (" .. why .. "); " .. n .. " eventos do host registrados como contidos nele")
            Q.csBase = n                                 -- batch 63: steps are counted from what this save holds
            Q.n7SavedAt = os.clock()                     -- b97n7: time of my newest confirmed save (rewards after it are 'kept')
            do local ok, e = pcall(S2L.svRec, ST, Q, S2L.cs1Rec(ST, Q, n)); if not ok then log("S2CORE n7sv: erro " .. tostring(e)) end end   -- b101n7save
        end
        Q.csSnap = nil; Q.csWait = nil; Q.csRefused = nil
    end
    if FL.qgap == 1 and FL.qonce == 1 and now >= (Q.gapChk or 0) then       -- batch 27: did a live step get lost in a connection gap?
        Q.gapChk = now + 5
        local hn = tonumber(((x1part("B") or ""):match("^%d+,(%d+)")))
        local have = #(Q.csQueue or {}) + #(Q.rbQueue or {})
        for _, c in pairs(qApplied(Q)) do have = have + c end
        if hn and hn > have and not x1part("S") and not Q.rbWait then
            Q.gapN = (Q.gapN or 0) + 1
            if Q.gapN >= 2 and now >= (Q.gapAt or 0) then
                Q.gapAt = now + 60; Q.gapN = 0
                Q.csLoads = (Q.csLoads or 0) + 1; Q.cuDone = nil
                log(string.format("S2CORE quest: o host tem %d eventos e este mundo %d: catch-up pedido de novo (#N %d)", hn, have, Q.csLoads))
            end
        else Q.gapN = 0 end
    end
    if not Q.csWait and Q.csQueue then                   -- batch 25: steps held while the save was being confirmed
        local ap, okN = qApplied(Q), 0
        for _, rest in ipairs(Q.csQueue) do
            ap[rest] = (ap[rest] or 0) + 1
            if Q.csAhead and (Q.csAhead[rest] or 0) > 0 then        -- the host redoes a step we kept from before its rollback
                Q.csAhead[rest] = Q.csAhead[rest] - 1
                log("S2CORE quest: cliente " .. rest .. " -> pulado: ja estava aplicado antes de o host voltar no tempo")
            else
                local ok, r = pcall(questApply, parseEv(rest))
                if ok and r then okN = okN + 1; Q.applied = Q.applied + 1 else Q.skipped = Q.skipped + 1 end
            end
        end
        log("S2CORE csave: " .. #Q.csQueue .. " eventos do host guardados durante o save; " .. okN .. " aplicados agora")
        Q.csQueue = nil; Q.csDirty = true; Q.csAt = now
    end
    if Q.csWait then                                     -- a save was asked for: did the index file change?
        local W = Q.csWait
        if now >= W.at + FL.csdelay then
            local s1 = sig()
            if W.sig == nil and s1 == nil then           -- no index file to look at: cannot verify
                if not Q.csBlind then Q.csBlind = true; log("S2CORE csave: nao consigo ler o indice de saves (" .. tostring(Q.csPath) .. "); registro sem verificacao") end
                record("sem verificacao")
            elseif s1 ~= W.sig then                      -- batch 25: a world load rewrites the index too: a new pawn in these seconds cancels the wait
                W.okAt = W.okAt or now
                if now >= W.okAt + FL.csconfirm then record("arquivo de saves mudou") end
            elseif now >= W.at + FL.cswait then          -- refused (measured: while enemies attack)
                Q.csRefused = (Q.csRefused or 0) + 1
                if Q.csRefused <= 3 or Q.csRefused % 10 == 0 then log(string.format("S2CORE csave: o jogo RECUSOU salvar (tentativa %d; em combate?); nada registrado, nova tentativa em %d s ou mais, depois de %d s sem perder vida", Q.csRefused, FL.csretry, FL.cscalm)) end
                Q.csWait = nil; Q.csSnap = nil; Q.csDirty = true; Q.csNext = now + FL.csretry
            end
        end
        return
    end
    local st = ST.peer and ST.peer.state
    local leaving = st == "silent" and Q.csPeerWas == "up"
    Q.csPeerWas = st
    if leaving then Q.csLeave = true end                 -- the leave save is owed until one succeeds
    local timer = now >= (Q.csLast or now) + FL.cstimer
    Q.csLast = Q.csLast or now
    local calm = not Q.csRefused or hp == nil or now >= (Q.csHurtAt or -1e9) + FL.cscalm
    local settled = now >= (Q.csAt or -1e9) + FL.cssettle and (hp == nil or hp > 0)
    local tot = 0                                        -- batch 63: host steps applied since the last save (save every cssteps steps)
    for _, c in pairs(qApplied(Q)) do tot = tot + c end
    if Q.csBase == nil or tot < Q.csBase then Q.csBase = tot end
    local stepsDue = Q.csDirty and (tot - Q.csBase) >= (FL.cssteps or 5)
    local go = (FL.csold or 0) ~= 1 and S2L.cs1Due(ST, Q, frozen, now)   -- b165: ONE rule, the host saved; the old triggers and gates need csold=1
    if go or (FL.csold or 0) == 1 and (stepsDue or Q.csLeave or timer or S2L.n7Due(ST, Q, tot)) and not frozen and not Q.rbWait and calm and settled and (leaving or now >= (Q.csNext or 0)) and S2L.n7SaveOk(ST, Q, tot) and S2L.svSaveOk(ST, Q) then   -- b97n7, b101n7save
        local s0 = sig()
        local ok, e = pcall(function() FindFirstOf("CustomConsoleManagerRK"):XTriggerForcedAutoSave() end)
        if not Q.csRefused then log("S2CORE csave: XTriggerForcedAutoSave ok=" .. tostring(ok) .. (Q.csLeave and " (o outro lado saiu)" or "") .. " " .. (ok and "" or tostring(e))) end
        Q.csNext = now + ((FL.csold or 0) == 1 and FL.cssecs or 0); Q.csLast = now   -- b165: no wait between pairs
        if ok then
            Q.csDirty = nil; Q.csLeave = nil; Q.csWait = {at = now, sig = s0, tag = Q.n7Pair}; Q.csSnap = {}
            for k, c in pairs(qApplied(Q)) do Q.csSnap[k] = c end
        end
    end
end
local function csOut(ST)                               -- batch 24: "I loaded a save" counter, so the host offers the catch-up again
    if FL.csave ~= 1 or api.ROLE ~= "client" then return "" end
    return "#N:" .. tostring(ST.qs.csLoads or 0)
end
local function questIn(ST)
    if FL.qsync ~= 1 or not (api.ROLE == "client" or FL.qtest == 1) then return end
    local Q, now = ST.qs, os.clock()
    if now - (Q.seen.pruneAt or -10) >= 1 then
        Q.seen.pruneAt = now
        for k, at in pairs(Q.seen) do if type(k) == "number" and now - at > 5 then Q.seen[k] = nil end end
    end
    for ev in (x1part("J") or ""):gmatch("[^|]+") do
        local seq, rest = ev:match("^(%d+),(.+)$")
        seq = tonumber(seq)
        local kind, a, b, c, sid = parseEv(rest or "")
        if seq then
            local seenAt = Q.seen[seq]; Q.seen[seq] = now; if not seenAt then Q.rx = (Q.rx or 0) + 1 end   -- b163quest: qdiv counter
            if not seenAt and Q.rbWait then              -- batch 16: reloading: keep the live event for after the replay
                Q.rbQueue = Q.rbQueue or {}; Q.rbQueue[#Q.rbQueue + 1] = rest
                if #Q.rbQueue == 1 or #Q.rbQueue % 25 == 0 then log(string.format("S2CORE quest: cliente seq=%d %s -> retido (rbWait ha %.0f s; %d na fila)", seq, rest, now - Q.rbWait.at, #Q.rbQueue)) end   -- b163quest
            elseif not seenAt and Q.csWait then          -- batch 25: a save is being confirmed: hold the step (csTick applies it)
                Q.csQueue = Q.csQueue or {}; Q.csQueue[#Q.csQueue + 1] = rest
                if #Q.csQueue == 1 or #Q.csQueue % 25 == 0 then log(string.format("S2CORE quest: cliente seq=%d %s -> retido (csWait; %d na fila)", seq, rest, #Q.csQueue)) end   -- b163quest
            elseif not seenAt and Q.csAhead and (Q.csAhead[rest] or 0) > 0 then      -- batch 25: the host redoes a step we kept from before its rollback
                Q.csAhead[rest] = Q.csAhead[rest] - 1
                local ap = qApplied(Q); ap[rest] = (ap[rest] or 0) + 1; Q.csDirty = true
                log("S2CORE quest: cliente seq=" .. seq .. " " .. rest .. " -> pulado: ja estava aplicado antes de o host voltar no tempo")
            elseif not seenAt then
                local ap = qApplied(Q); ap[rest] = (ap[rest] or 0) + 1; Q.csDirty = true; Q.csAt = os.clock()      -- batch 22: a later catch-up must not apply it again
                local ok, r, why = pcall(questApply, kind, a, b, c, sid, true)   -- b70e: live event
                if ok and r then Q.applied = Q.applied + 1 else Q.skipped = Q.skipped + 1 end
                log(string.format("S2CORE quest: cliente seq=%d %s %s %s %s %s -> %s %s", seq, kind, tostring(a), tostring(b), tostring(c), tostring(sid), ok and tostring(r) or "erro", ok and (why or "") or tostring(r)))
            end
        end
    end
end
-- ===== core-22: inventory snapshot of the LOCAL player (636 item SIDs, about 2 ms; only ever called on the player pawn)
local function invSnap(pw)
    local med = StaticFindObject("/Script/Stalker2.Default__CppMediator")
    local t, m = {}, 0
    for _, s in ipairs(LOOT_SIDS) do
        local ok, n = pcall(function() return med:GetItemsCountInInventory(pw, s) end)
        if ok and type(n) == "number" and n > 0 then t[s] = n end
    end
    pcall(function() m = pw:GetMoney() end)
    return t, m
end
local function invDelta(a, b)                     -- items that went UP from a to b, as "sidxN,sidxN"
    local r = {}
    for s, n in pairs(b) do local d = n - (a[s] or 0); if d > 0 then r[#r + 1] = s .. "x" .. d end end
    table.sort(r)
    return table.concat(r, ",")
end

-- ===== b97n7 (flag n7, default 0; user rule N7): HOST DEATH / HOST LOADS AN OLDER SAVE -> THE WORLD FOLLOWS THE HOST, THE CLIENT
-- KEEPS ONLY ITS OWN ITEMS. One mechanism on top of batch 16 (#B rollback generation) and batch 23 (csave):
-- HOST: #B gets ",<hl>,<hs>,<kh>,<tag>": hl = loads of the host in this process (+1 once per new host pawn, right after rbTick's 12 s
--   rollback check, so a log cut and its hl arrive in the same snapshot); hs = host saves (its save index changed and the same pawn stayed
--   n7conf s); kh = host quest log size at its newest save or load; tag = hash of the host's save index path.
-- CLIENT: (1) save gate: a csave save only while its applied host steps <= kh, so its newest save is never ahead of the host's newest
--   save; a 'paired' save is asked for each time hs goes up. (2) hl up AND the generation changed (the host lost steps): snapshot of its own
--   inventory + money (LOOT_SIDS; quest items left to the qitem/lead rules), XLoadLastSave (its newest = paired save) through the batch-16
--   rbWait path, the existing catch-up replays the host log, then the snapshot is restored exactly (adds XCreateItemInInventoryByID,
--   removals S2U_P nodes, money ModifyMoney, one trim pass, verify). (3) a quest reward (#W) the client kept from after its newest save is
--   not taken twice when the host redoes that quest. Refuses to reload when both games use the same save folder (D13) unless n7shared=1.
-- Logs: 'S2CORE n7: ...'. Needs qrollback=1 qsync=1 on both, csave=1 + itake=1 on the client.
function S2L.n7St(ST) ST.n7 = ST.n7 or {stage = "idle", hl = 0, hs = 0, rw = {}, keep = {}} return ST.n7 end
function S2L.n7Path()
    do local d, _, pl = S2L.svInfo(); if d then return d .. (pl ~= "" and pl or "STEAM") .. "\\SaveGames\\CampaignsSave.sav" end end   -- b165                                   -- the save index file this game writes (csave's rule: bridge savepath.txt, else default)
    local f = io.open(api.BR .. "savepath.txt", "r")
    local p = f and f:read("*l"); if f then f:close() end
    if not p or p == "" then local la = os.getenv("LOCALAPPDATA"); p = la and (la .. "\\Stalker2\\Saved\\STEAM\\SaveGames\\CampaignsSave.sav") or nil end
    return p
end
function S2L.n7Tag(p)                                   -- short tag of a save index path (case and slash insensitive); "0" = unknown
    if not p then return "0" end
    local s, h = p:lower():gsub("/", "\\"), 5381
    for i = 1, #s do h = (h * 33 + s:byte(i)) % 2147483647 end
    return string.format("%x", h)
end
function S2L.n7Sig(p)
    do local _, st = S2L.svInfo(); if st then return st end end   -- b165: STEAM and NULL index stamps (C++)                                   -- signature of the save index file (same sampling as csave)
    if not p then return nil end
    local f = io.open(p, "rb"); if not f then return nil end
    local d = f:read("*a"); f:close()
    local sum = 0
    for i = 1, #d, 7 do sum = (sum * 31 + d:byte(i)) % 2147483647 end
    return #d .. ":" .. sum
end
function S2L.n7QT()                                     -- quest-item table when the qitem/lead rules are on (those items are not ours to restore)
    if FL.qitem ~= 1 and (FL.lead or 0) ~= 1 then return nil end
    local T = S2L.qiTab and S2L.qiTab(); return T or nil
end
function S2L.n7B(ST)                                    -- host: extra #B fields (rbOut)
    if (FL.n7 or 0) ~= 1 or api.ROLE ~= "host" then return "" end
    local N = S2L.n7St(ST)
    N.tag = N.tag or S2L.n7Tag(S2L.n7Path())
    return string.format(",%d,%d,%d,%s", N.hl, N.hs, N.kh or -1, N.tag)
end
function S2L.n7Host(ST, N, now)
    local Q = ST.qs
    if (FL.sxjoin or 0) == 1 then local f = rawget(_G, "s2n_sx_host"); if type(f) == "function" then pcall(f, api.BR, N.sxLd and -1 or (N.kh or -1)) end end   -- b167
    local pw = rbPawn(); if not valid(pw) then return end
    local addr = pw:GetAddress()
    N.path = N.path or S2L.n7Path() or false
    if N.addr == nil then N.addr = addr; N.pend = now; N.first = true      -- first pawn of this process (or of n7=1): kh once it settles
    elseif addr ~= N.addr then N.addr = addr; N.pend = now; N.first = nil; N.sv = nil end
    if N.pend and now >= N.pend + 11 and not Q.rbCheckAt and S2L.qlSettled(Q) then   -- b165: and questRollback's cut             -- rbTick's rollback check (12 s after the new pawn) is done
        N.pend = nil; qlogLoad(Q); N.kh = #Q.log; N.sxLd = true   -- b167: loaded: the newest file is not this world until a save
        N.sig = S2L.n7Sig(N.path or nil)                                     -- a load rewrites the index: new baseline, not a save
        if N.first then N.first = nil; log(string.format("S2CORE n7: host: pawn no mundo; passos no save carregado kh=%d", N.kh))
        else N.hl = N.hl + 1; log(string.format("S2CORE n7: host carregou um save (#%d): geracao %s, kh=%d", N.hl, tostring(Q.gen), N.kh)) end
        return
    end
    if now >= (N.pollAt or 0) then
        N.pollAt = now + (FL.n7poll or 2)
        local s = S2L.n7Sig(N.path or nil)
        if N.sig == nil then N.sig = s elseif s ~= N.sig then N.sig = s; N.sv = {at = now, addr = addr} end
    end
    if N.sv and now >= N.sv.at + (FL.n7conf or 5) then
        local sv = N.sv; N.sv = nil
        if sv.addr == addr and not N.pend then
            qlogLoad(Q); N.hs = N.hs + 1; N.kh = #Q.log; N.sxLd = nil   -- b167
            log(string.format("S2CORE n7: host salvou (#%d): kh=%d", N.hs, N.kh))
        end
    end
end
function S2L.n7EqTab()                                  -- b103n7ammo: slot-capable SIDs (s2u_equip.tsv, gen_equip.py) when n7eq=1, else nil
    if (FL.n7eq or 0) ~= 1 then return nil end
    local C = S2L.n7EqC
    if not C then
        C = {}; S2L.n7EqC = C
        local ok, e = pcall(S2L.cpTables, C)
        if not ok or not C.eqList or #C.eqList == 0 then
            C.none = true
            log("S2CORE n7: n7eq=1 mas s2u_equip.tsv ausente ou vazio: itens de slot julgados como antes" .. (ok and "" or (" (" .. tostring(e) .. ")")))
        end
    end
    if C.none then return nil end
    return C.eqTab
end
function S2L.n7EqLog(eq, why)                           -- b103n7ammo: one line with the slot-capable differences that were not touched
    local r = {}
    for _, x in ipairs(eq) do r[#r + 1] = x[1] .. (x[2] > 0 and "+" or "") .. x[2] .. (x[3] and ("(" .. x[3] .. ")") or "") end
    log(string.format("S2CORE n7: itens de slot %s (n7eq=1; contagem 0 = vestido ou ausente): [%s]", why, table.concat(r, " ")))
end
function S2L.n7Diff(want, cur, T)                       -- {sid, n} lists to add and to remove so that cur == want (quest items skipped)
    local give, take, eq = {}, {}, {}
    local E, mag = S2L.n7EqTab(), {}                    -- b103n7ammo: a worn item reads 0: its difference goes to eq, never created/removed
    if E then                                           -- the counted rounds in the magazine of such a gun move with it (up to MaxAmmo each)
        for _, s in ipairs(LOOT_SIDS) do
            local e = E[s]
            if e and e.ammo and e.max and not (T and T.S[s]) then
                local d = (want[s] or 0) - (cur[s] or 0)
                if d ~= 0 then mag[e.ammo] = (mag[e.ammo] or 0) + d * e.max end
            end
        end
    end
    for _, s in ipairs(LOOT_SIDS) do
        if not (T and T.S[s]) then
            local d = (want[s] or 0) - (cur[s] or 0)
            if d ~= 0 and E and E[s] then eq[#eq + 1] = {s, d}
            else
                local m = mag[s] or 0
                if (m > 0 and d > 0) or (m < 0 and d < 0) then
                    local k = d > 0 and math.min(d, m) or math.max(d, m); d = d - k; eq[#eq + 1] = {s, k, "pente"}
                end
                if d > 0 then give[#give + 1] = {s, d} elseif d < 0 then take[#take + 1] = {s, -d} end
            end
        end
    end
    return give, take, eq
end
function S2L.n7Follow(ST, N, now)                      -- client: snapshot, then reload its own newest (paired) save
    local Q = ST.qs
    do local ok, w = pcall(S2L.svPre, ST, N, now); if not ok then log("S2CORE n7sv: erro " .. tostring(w)) elseif w then return end end   -- b101n7save
    local pw = rbPawn(); if not valid(pw) then log("S2CORE n7: sem pawn: nao sigo o host"); return end
    local items, money = invSnap(pw)
    local T, nq = S2L.n7QT(), 0
    if T then for s in pairs(items) do if T.S[s] then items[s] = nil; nq = nq + 1 end end end
    N.snap = {items = items, money = money or 0, addr = pw:GetAddress(), at = now}
    N.keep = {}
    for q, t in pairs(N.rw) do if t > (Q.n7SavedAt or -1e9) then N.keep[q] = true end end
    local ni = 0; for _, n in pairs(items) do ni = ni + n end
    Q.cuDone = nil; if (FL.qnohold or 0) == 1 then Q.seen = {} end; Q.csRb = true; Q.n7Pair = nil   -- b163quest
    Q.rbWait = {at = now, addr = N.snap.addr, n = -1}; Q.rbQueue = {}
    local ok, e = false, "CustomConsoleManagerMP ausente"
    local mp = FindFirstOf("CustomConsoleManagerMP")
    if valid(mp) then ok, e = pcall(function() mp:XLoadLastSave() end) end
    log(string.format("S2CORE n7: host voltou no tempo -> retrato do cliente: %d itens, dinheiro %d%s; XLoadLastSave ok=%s %s",
        ni, N.snap.money, nq > 0 and string.format(" (%d itens de missao ficam com as regras qitem/lead)", nq) or "", tostring(ok), ok and "" or tostring(e)))
    if not ok then M.rbDone(ST, "n7: XLoadLastSave falhou"); N.snap = nil; return end
    N.stage = "load"; N.qiReset = nil
end
function S2L.n7Stage(ST, N, now)                       -- client: after the reload and the catch-up, give back exactly the snapshot
    if N.stage == "sx" then do local okX, e = pcall(S2L.sxTick, ST, N, now); if not okX then log("S2CORE sxjoin: erro " .. tostring(e)); S2L.sxAbort(ST, N, "erro") end end return end   -- b167
    local S = N.snap; if not S then N.stage = "idle"; return end
    local Q, pw = ST.qs, rbPawn()
    if N.stage == "load" then
        if now - S.at > (FL.qrbmax or 240) + 30 then log("S2CORE n7: o recarregamento nao terminou: nada restaurado"); N.stage = "idle"; N.snap = nil; return end
        if valid(pw) and pw:GetAddress() ~= S.addr and not N.qiReset then   -- the loaded save's pawn: its quest items are a baseline, not pickups
            N.qiReset = true                                     -- (n7Tick runs before qiTick in the same tick)
            if ST.qi then ST.qi.based = nil; ST.qi.bn = 0; ST.qi.sent = {}; ST.qi.pend = {}; ST.qi.hold = {}; ST.qi.re = {} end
        end
        if Q.rbWait or not valid(pw) then return end
        if pw:GetAddress() == S.addr then log("S2CORE n7: o pawn nao mudou (nenhum save carregado): nada restaurado"); N.stage = "idle"; N.snap = nil; return end
        if S.skip then log("S2CORE n7boot: save pareado carregado; os itens do save da outra campanha NAO sao restaurados"); N.stage = "idle"; N.snap = nil; N.qiReset = nil; return end   -- b165
        N.qiReset = nil; N.stage = "settle"; N.t = now + (FL.n7wait or 5)
        return
    end
    if not valid(pw) then return end
    local busy = #((ST.it and ST.it.run) or {}) > 0
    if N.stage == "settle" or N.stage == "trim" then
        if now < N.t or busy then return end
        local cur, money = invSnap(pw)
        local give, take, eq = S2L.n7Diff(S.items, cur, S.qt or S2L.n7QT())   -- b167: S.qt = quest items of the join snapshot (both keep the host's)
        if #eq > 0 and N.stage == "settle" then S2L.n7EqLog(eq, "nao restaurados (podem estar num slot)") end   -- b103n7ammo
        for _, t in ipairs(take) do pcall(M.itemRemove, ST, "P", t[1], t[2]) end
        local dm = 0
        if N.stage == "settle" then
            dm = math.floor(S.money - (money or 0) + 0.5)
            if dm ~= 0 then pcall(function() pw:ModifyMoney(dm) end) end
            N.res = {give = 0, take = 0, money = dm}
        end
        local g, t = {}, {}
        for _, x in ipairs(give) do g[#g + 1] = x[1] .. "+" .. x[2]; N.res.give = N.res.give + x[2] end
        for _, x in ipairs(take) do t[#t + 1] = x[1] .. "-" .. x[2]; N.res.take = N.res.take + x[2] end
        log(string.format("S2CORE n7: %s: +[%s] -[%s]%s", N.stage == "settle" and "restaurando o retrato" or "ajuste depois das criacoes",
            table.concat(g, " "), table.concat(t, " "), N.stage == "settle" and string.format(" dinheiro %+d", dm) or ""))
        N.give = give; N.next = N.stage == "settle" and "trim" or "verify"; N.stage = "give"
        return
    end
    if N.stage == "give" then
        local mgr = FindFirstOf("CustomConsoleManagerRK")
        for _ = 1, (FL.n7per or 4) do
            local x = table.remove(N.give, 1); if not x then break end
            local ok, e = false, "CustomConsoleManagerRK ausente"
            if valid(mgr) then ok, e = pcall(function() mgr:XCreateItemInInventoryByID(x[1], 0, x[2], 1.0) end) end
            if not ok then log("S2CORE n7: XCreateItemInInventoryByID " .. x[1] .. " erro " .. tostring(e)) end
        end
        if #N.give == 0 then N.stage = N.next; N.t = now + (N.next == "trim" and 1.5 or 3) end
        return
    end
    if N.stage == "verify" then
        if now < N.t or busy then return end
        local cur, money = invSnap(pw)
        local give, take, eq = S2L.n7Diff(S.items, cur, S.qt or S2L.n7QT())   -- b103n7ammo: eq = slot items, not counted as differences; b167 S.qt
        local bad = {}
        for _, x in ipairs(give) do bad[#bad + 1] = x[1] .. " falta " .. x[2] end
        for _, x in ipairs(take) do bad[#bad + 1] = x[1] .. " sobra " .. x[2] end
        log(string.format("S2CORE n7: RESTAURADO: +%d -%d itens, dinheiro %d (retrato %d), %d tipos diferentes do retrato%s",
            N.res.give, N.res.take, money or 0, S.money, #bad, #bad > 0 and (" " .. table.concat(bad, ", ", 1, math.min(6, #bad))) or ""))
        if #eq > 0 then S2L.n7EqLog(eq, "fora da conta do retrato") end   -- b103n7ammo
        N.stage = "idle"; N.snap = nil; N.done = (N.done or 0) + 1
        if S.sxId then local f = rawget(_G, "s2n_sx_mark"); if type(f) == "function" then pcall(f, api.BR, S.sxId, "applied") end end   -- b167: never restored twice
        Q.n7Pair = "force"                                 -- the restored inventory goes into the client's own save soon
    end
end
function S2L.n7Client(ST, N, now)
    local Q = ST.qs
    local b = x1part("B") or ""
    local g, cid, hl, hs, kh, tag = b:match("^(%d+),%d+,(%w+),(%d+),(%d+),(%-?%d+),(%w+)")
    if not g then
        if b ~= "" and not N.warned then N.warned = true; log("S2CORE n7: o host nao manda os campos n7 em #B (n7=0 ou core antigo no host): nada a seguir") end
        if N.stage ~= "idle" then S2L.n7Stage(ST, N, now) end
        return
    end
    g, hl, hs, kh = tonumber(g), tonumber(hl), tonumber(hs), tonumber(kh)
    local sess = ST.peer and ST.peer.sessions or 0
    if N.sess ~= sess then                                 -- first fields of this peer session (also a host relaunch): baseline only
        N.sess = sess; N.g, N.hl, N.hs = g, hl, hs; N.kh = kh >= 0 and kh or nil; N.cid = cid   -- b163quest
        N.shared = tag == S2L.n7Tag(S2L.n7Path())
        log(string.format("S2CORE n7: host na sessao %d: cargas=%d saves=%d kh=%s geracao=%d pasta_de_saves=%s", sess, hl, hs, tostring(N.kh), g,
            N.shared and "A MESMA do host (D13)" or "propria"))
        do local okB, eB = pcall(S2L.n7Boot, ST, N, now, cid, kh); if not okB then log("S2CORE n7boot: erro " .. tostring(eB)) end end   -- b165
        return
    end
    if N.cid ~= cid and (FL.qnohold or 0) ~= 1 then      -- b163quest: the host started another co-op campaign (new game): its generation bump lost no step
        if N.cid then log(string.format("S2CORE n7: host comecou outra campanha (%s -> %s, geracao %d): jogo novo, nao e volta no tempo; o cliente NAO recarrega", tostring(N.cid), cid, g)) end
        N.cid = cid; N.g = g
    end
    if kh >= 0 then N.kh = kh end
    do local okB, eB = pcall(S2L.n7Boot, ST, N, now, cid, kh); if not okB then log("S2CORE n7boot: erro " .. tostring(eB)) end end   -- b165
    local ok = not N.shared or (FL.n7shared or 0) == 1
    if hs > N.hs then
        N.hs = hs
        if (FL.csold or 0) ~= 1 then Q.n7Pair = {hs = hs, kh = kh, cid = cid, at = now}; log(string.format("S2CORE csave1: host salvou (#%d, kh=%d, campanha %s): o cliente salva agora%s", hs, kh, cid, ok and "" or " (NAO: mesma pasta de saves, D13)"))   -- b165
        elseif ok then Q.n7Pair = Q.n7Pair or true; log(string.format("S2CORE n7: host salvou (#%d, kh=%d): save pareado do cliente pedido", hs, kh)) end
    end
    if hl > N.hl then
        local cut = g ~= N.g
        N.hl = hl; N.g = g
        if not cut then log(string.format("S2CORE n7: host carregou um save (#%d) sem perder passos (geracao %d): nada a seguir", hl, g))
        elseif N.stage ~= "idle" then log(string.format("S2CORE n7: host carregou de novo (#%d) durante o retorno: o catch-up em curso segue o host", hl))
        elseif not ok then log(string.format("S2CORE n7: host voltou no tempo (carga #%d, geracao %d) mas a pasta de saves e a MESMA dos dois jogos (D13): o cliente NAO recarrega (n7shared=0)", hl, g))
        elseif FL.csave ~= 1 or FL.qrollback ~= 1 then log("S2CORE n7: host voltou no tempo mas o cliente precisa de csave=1 e qrollback=1: nada feito")
        else S2L.n7Follow(ST, N, now) end
    elseif hl < N.hl then N.hl = hl end
    if N.stage ~= "idle" then S2L.n7Stage(ST, N, now) end
end
function S2L.n7Tick(ST)                                 -- tick100, right after csTick
    if (FL.n7 or 0) ~= 1 or FL.qsync ~= 1 then return end
    local N, now = S2L.n7St(ST), os.clock()
    if api.ROLE == "host" then return S2L.n7Host(ST, N, now) end
    do local ok, e = pcall(S2L.svTick, ST, N, now); if not ok then log("S2CORE n7sv: erro " .. tostring(e)) end end   -- b101n7save
    return S2L.n7Client(ST, N, now)
end
function S2L.n7Due(ST, Q, tot)                          -- csTick: a paired save is due (the host saved, or the restore finished)
    if (FL.n7 or 0) ~= 1 or not Q.n7Pair then return false end
    if Q.n7Pair == "force" or tot > (Q.csBase or 0) then return true end
    Q.n7Pair = nil                                       -- nothing new since my newest save: no pair needed
    return false
end
function S2L.n7SaveOk(ST, Q, tot)                       -- csTick: last condition of a save (true = save as before)
    if (FL.n7 or 0) ~= 1 then return true end
    local N = S2L.n7St(ST)
    if N.stage ~= "idle" then return false end
    if N.kh == nil or (N.shared and (FL.n7shared or 0) ~= 1) then return true end
    if tot > N.kh then
        local now = os.clock()
        if now >= (N.gateLog or 0) then N.gateLog = now + 60; log(string.format("S2CORE n7: save do cliente adiado: %d passos do host aplicados e o save mais novo do host so tem %d", tot, N.kh)) end
        return false
    end
    Q.n7Pair = nil
    return true
end
function S2L.n7Rw(ST, q)                                -- rewardTick (client): true = skip this #W (already kept from before the host went back)
    if (FL.n7 or 0) ~= 1 then return false end
    local N = S2L.n7St(ST)
    if N.keep[q] then
        N.keep[q] = nil
        log("S2CORE n7: recompensa da missao " .. tostring(q) .. " ja ficou com o cliente (de antes de o host voltar no tempo): NAO recebida de novo")
        return true
    end
    N.rw[q] = os.clock()
    return false
end
-- ===== b97n7 part B (flag qimirror, default 0; needs lead=1): QUEST ITEM COUNTS MIRROR BOTH WAYS. Replaces the lead=1 'copy on a count
-- increase' (S2L.ldQi) with an ABSOLUTE count: a quest-item count (s2u_qitems table only) that changed on this side and stayed the same for
-- qmset s is sent as "#QI:<seq>,<sid>,<count>" (pickup, hand-in, consumed, placed by a quest node: all the same); the other side sets its
-- own count to that number (adds with XCreateItemInInventoryByID; removes with the S2QI_<sid> node = all, then re-adds the rest) and
-- records it (no echo). A drop + re-take inside qmset s sends nothing. The client's ItemAdd replays of quest items are skipped (qiSkip),
-- because the host's quest log has ItemAdd nodes but never ItemRemove nodes (that pair duplicated the tutorial Scanner).
-- Removal on the host needs the S2QI pak there too (qmrmhost=1). Logs 'S2CORE qimirror: ...'.
function S2L.qmApply(ST, T, Q, sid, t, seq, now)       -- the other player's committed count -> mine
    local pw = S2.io and S2.io.pawn; if not valid(pw) then return end
    local have = S2L.qiCount(pw, sid)
    Q.pend[sid] = nil; Q.re[sid] = nil
    Q.sent[sid] = {at = now, n = t}; Q.hold[sid] = now + 2 * (FL.qmset or 3)   -- no echo: my count moving to t is not a change of mine
    local r
    if t == have then r = "ja igual"
    elseif t > have then
        local ok, e = pcall(function() FindFirstOf("CustomConsoleManagerRK"):XCreateItemInInventoryByID(sid, 0, t - have, 1.0) end)
        r = string.format("+%d ok=%s%s", t - have, tostring(ok), ok and "" or (" " .. tostring(e)))
    elseif T.norm[sid] or (api.ROLE == "host" and (FL.qmrmhost or 0) ~= 1) then
        Q.sent[sid].n = have; Q.hold[sid] = nil                    -- cannot remove here: keep mine, never send it back
        r = T.norm[sid] and "sem no de remocao (item fora do kit): fica" or "host sem o pak S2QI (qmrmhost=0): fica"
    else
        local aa = FindFirstOf("CustomConsoleManagerAA")
        local ok, e = false, "CustomConsoleManagerAA ausente"
        if valid(aa) then ok, e = pcall(function() aa:XStartQuestNodeBySID("S2QI_" .. sid) end) end
        if ok and t > 0 then Q.re[sid] = {n = t, at = now + (FL.qmre or 1)} end   -- the node removes all: the rest comes back a moment later
        r = string.format("S2QI_%s ok=%s%s%s", sid, tostring(ok), ok and "" or (" " .. tostring(e)), (ok and t > 0) and string.format(", depois +%d", t) or "")
    end
    log(string.format("S2CORE qimirror: o outro jogador tem %s x%d (seq %s); eu tinha %d -> %s", sid, t, tostring(seq), have, r))
end
-- ===== b127softlock A (flag qmload, default 1): a NEW LOCAL PAWN (death reload, own save load, respawn) is a new quest-item baseline,
-- never a count change. Before: qmTick compared the loaded save's counts with the old pawn's and mirrored the drop (client death -> its
-- older csave without the item -> host S2QI_<sid> removal with qmrmhost=1 = the save of record lost a quest item it cannot get again).
-- Now: on a new pawn address nothing is sent; after qmloadw s (inventory filled) the base is read again; the CLIENT tops each item back up
-- to the count both sides had agreed (never down: a spare is harmless, a missing quest item is a soft-lock); the HOST only re-reads (N7: the
-- world follows the host). An n7 return in progress (the host went back in time) gets no top-up: n7 owns that reload.
function S2L.qmLoad(ST, Q, pw, now)                    -- qmTick: true = skip this tick (waiting after a new pawn)
    local a = pw:GetAddress()
    if Q.pAddr == nil then Q.pAddr = a; return false end
    if a ~= Q.pAddr then
        Q.pAddr = a
        local agr, n, N = {}, 0, ST.n7
        if api.ROLE ~= "host" and not (N and N.stage and N.stage ~= "idle") then
            for sid, s in pairs(Q.sent or {}) do if type(s) == "table" and type(s.n) == "number" and s.n > 0 then agr[sid] = s.n; n = n + 1 end end
        end
        Q.agr = agr; Q.based = nil; Q.bn = 0; Q.sent = {}; Q.pend = {}; Q.hold = {}; Q.re = {}
        Q.ldWait = now + (FL.qmloadw or 8); Q.ldN = (Q.ldN or 0) + 1
        log(string.format("S2CORE qmload: pawn novo #%d (save carregado / morte / respawn): contagens de itens de missao NAO enviadas; base relida em %.0f s%s",
            Q.ldN, FL.qmloadw or 8, n > 0 and string.format("; %d itens voltam ao combinado se faltarem", n) or ""))
    end
    if Q.ldWait then
        if now < Q.ldWait then return true end
        Q.ldWait = nil
    end
    return false
end
function S2L.qmTop(Q, sid, n, now)                     -- base sweep after a new pawn (client): back up to the agreed count, never down
    local want = Q.agr[sid]; Q.agr[sid] = nil
    if not want or n >= want then return n end
    local ok, e = pcall(function() FindFirstOf("CustomConsoleManagerRK"):XCreateItemInInventoryByID(sid, 0, want - n, 1.0) end)
    Q.hold[sid] = now + 2 * (FL.qmset or 3)
    log(string.format("S2CORE qmload: %s %d -> %d (o combinado antes do load) ok=%s%s; nada enviado", sid, n, want, tostring(ok), ok and "" or (" " .. tostring(e))))
    return ok and want or n
end
-- ===== end b127softlock A
function S2L.qmTick(ST, T, Q, now)                     -- S2L.ldQi with qimirror=1 (both roles)
    Q.pend = Q.pend or {}; Q.hold = Q.hold or {}; Q.re = Q.re or {}
    local set = FL.qmset or 3
    for ent in (x1part("QI") or ""):gmatch("[^|]+") do
        if not Q.seen[ent] then
            Q.seen[ent] = now
            local seq, sid, n = ent:match("^(%d+),([^,]+),(%d+)$")
            if seq and T.S[sid] then S2L.qmApply(ST, T, Q, sid, tonumber(n), seq, now)
            elseif seq then log("S2CORE qimirror: relay ignorado (fora da tabela): " .. ent) end
        end
    end
    for k, t in pairs(Q.seen) do if now - t > 60 then Q.seen[k] = nil end end
    for sid, x in pairs(Q.re) do
        if now >= x.at then
            Q.re[sid] = nil
            local ok = pcall(function() FindFirstOf("CustomConsoleManagerRK"):XCreateItemInInventoryByID(sid, 0, x.n, 1.0) end)
            log(string.format("S2CORE qimirror: %s devolvido x%d depois da remocao ok=%s", sid, x.n, tostring(ok)))
        end
    end
    if now < (Q.at or 0) then return end
    Q.at = now + (FL.qisec or 0.5)
    local pw = S2.io and S2.io.pawn; if not valid(pw) then return end
    if (FL.qmload or 1) == 1 then local okL, rL = pcall(S2L.qmLoad, ST, Q, pw, now); if okL and rL then return end end   -- b127softlock A
    do local okG, rG = pcall(S2L.qgDead, ST, Q, pw, now, true); if okG and rG then return end end   -- b135fixes 2 (qdead)
    local nL = #T.list; if nL == 0 then return end
    local look, order = {}, {}
    local function add(sid) if not look[sid] then look[sid] = true; order[#order + 1] = sid end end
    for _ = 1, math.min(FL.qiper or 80, nL) do
        Q.i = Q.i % nL + 1
        local sid = T.list[Q.i]
        if not Q.based then
            local n = S2L.qiCount(pw, sid)
            if Q.agr and Q.agr[sid] then n = S2L.qmTop(Q, sid, n, now) end   -- b127softlock A
            if n > 0 then Q.sent[sid] = {at = now, n = n} end
            Q.bn = (Q.bn or 0) + 1
            if Q.bn >= nL then Q.based = true; log("S2CORE qimirror: inventario base lido (" .. nL .. " itens de missao); a partir de agora a contagem de cada um e espelhada") end
        else add(sid) end
    end
    if not Q.based then return end
    for sid, s in pairs(Q.sent) do if s.n > 0 then add(sid) end end     -- held items and pending changes are counted every qisec
    for sid in pairs(Q.pend) do add(sid) end
    for _, sid in ipairs(order) do
        local n = S2L.qiCount(pw, sid)
        local s = Q.sent[sid]
        local base = s and s.n or 0
        if (Q.hold[sid] or 0) > now or Q.re[sid] then          -- a received count is being applied
        elseif n ~= base then
            local p = Q.pend[sid]
            if not p or p.n ~= n then Q.pend[sid] = {n = n, at = now}
            elseif now - p.at >= set then
                Q.pend[sid] = nil; Q.sent[sid] = {at = now, n = n}; Q.seq = Q.seq + 1
                Q.out[#Q.out + 1] = {e = Q.seq .. "," .. sid .. "," .. n}
                log(string.format("S2CORE qimirror: %s %d -> %d aqui (estavel %.0f s); contagem enviada ao outro (seq %d)", sid, base, n, set, Q.seq))
            end
        elseif Q.pend[sid] then
            Q.pend[sid] = nil
            log(string.format("S2CORE qimirror: %s voltou a %d em menos de %.0f s: nada enviado", sid, n, set))
        end
    end
end
-- ===== end b97n7
-- ===== b132qhold (flag qdrop, default 0; user 2026-10-15 "instead of the message, just make the quest items drop to hold D instead
-- of press D"; replaces b130qdrop's message + two-press window). Needs the pak zzzzzz_S2CoopQuestDrop_P (BOTH players) + the bridge
-- table s2u_qdrop.lua (318 items). Client AND host (a fallback for stuck/dead quest items).
--   TAP of D on a quest item: the stock refusal, nothing of ours runs past the first check.
--   HOLD D qdrophold ms in the open inventory with the mouse on an item (InventoryNew.HoveredItem, the same slot widget all the hold)
--       while the player holds a table item -> S2QD0_<sid> (droppable) for every held table item, then the game's own drop of the
--       hovered item (qdropfn 1 = InventoryNew:UIInventoryDropItem, 2 = OnDropItemHoldTriggered, 0 = no call); a held count going
--       down -> S2QD1_<sid> at once (quest items again). If nothing drops 0.6 s after the call: fallback window, the items stay
--       droppable until qdropwin s after D is released (the next tap drops). Re-lock also on: inventory closed, qdropwin + 10 s after
--       the unlock, a new pawn (load/death). A new hold needs D released first.
-- D = ONE RegisterKeyBind per game session (global S2_QDROP_KEY, registered lazily with qdrop=1; it only stamps the time and does not
-- eat the key); the hold is read in tick100 with IsInputKeyDown (only after a press). Hook params never read, no widget tree walked.
-- Sync = qimirror unchanged (client drop: the host keeps its copy with qmrmhost=0; host drop: the client's copy goes). No message.
function S2L.qdrTab()
    if S2L.QDRT ~= nil then return S2L.QDRT end
    S2L.QDRT = false
    local ok, T = pcall(dofile, api.BR .. "s2u_qdrop.lua")
    if not ok or type(T) ~= "table" or type(T.D) ~= "table" then log("S2CORE qdrop: s2u_qdrop.lua ausente ou invalido: " .. tostring(T)); return false end
    T.list = {}; for s in pairs(T.D) do T.list[#T.list + 1] = s end; table.sort(T.list)
    S2L.QDRT = T
    log(string.format("S2CORE qdrop: %d itens de missao podem ser largados segurando D (pak S2QD)", #T.list))
    return T
end
function S2L.qdrKey()                                  -- D held now (PlayerInput state of the local PlayerController)
    local d = false
    pcall(function() d = FindFirstOf("PlayerController"):IsInputKeyDown({KeyName = FName("D")}) and true or false end)
    return d
end
function S2L.qdrInv()                                  -- the open inventory widget, else nil (re-found every call: no ref kept)
    local w = FindFirstOf("InventoryNew")
    if not valid(w) then return nil end
    local vis = false; pcall(function() vis = w:IsVisible() and true or false end)
    return vis and w or nil
end
function S2L.qdrHov(w)                                 -- address of the hovered slot widget (a property of InventoryNew), else nil
    local a; pcall(function() local h = w.HoveredItem; if valid(h) then a = h:GetAddress() end end)
    return a
end
function S2L.qdrSet(Q, on)                             -- S2QD0 (droppable) / S2QD1 (quest item again) for every held table item
    local aa = FindFirstOf("CustomConsoleManagerAA")
    local n, okN = 0, 0
    for sid in pairs(Q.held or {}) do
        n = n + 1
        if valid(aa) and pcall(function() aa:XStartQuestNodeBySID((on and "S2QD0_" or "S2QD1_") .. sid) end) then okN = okN + 1 end
    end
    return okN, n
end
function S2L.qdrEnd(Q, why)
    if Q.st == "drop" or Q.st == "win" then
        local okN, n = S2L.qdrSet(Q, false)
        log(string.format("S2CORE qdrop: %s: %d/%d itens voltaram a ser item de missao (S2QD1)", why, okN, n))
    else log("S2CORE qdrop: " .. why) end
    Q.st = nil; Q.held = nil; Q.needUp = S2L.qdrKey()   -- D still held: a new cycle only after a release
end
function S2L.qdrRun(ST, Q)
    local T = S2L.qdrTab(); if not T then return end
    if not rawget(_G, "S2_QDROP_KEY") then
        local okB, eB = pcall(function() (rawget(_G, "S2_RegisterKeyBindRaw") or RegisterKeyBind)(Key.D, function() local st = S2 and S2.state; if st then st.qdrKeyAt = os.clock() end end) end)
        rawset(_G, "S2_QDROP_KEY", okB and "ok" or ("erro " .. tostring(eB)))
        log("S2CORE qdrop: RegisterKeyBind D -> " .. rawget(_G, "S2_QDROP_KEY"))
    end
    local now = os.clock()
    local pw = S2.io and S2.io.pawn
    local addr = valid(pw) and pw:GetAddress() or nil
    if Q.st and addr ~= Q.addr then
        if not addr then return end                    -- loading: re-lock once the new pawn exists
        S2L.qdrEnd(Q, "pawn novo: load/morte/respawn"); return
    end
    if not addr then return end
    local kAt = ST.qdrKeyAt
    if not Q.st then
        if Q.needUp then if S2L.qdrKey() then Q.used = kAt; return end Q.needUp = false end
        if not kAt or kAt == Q.used then return end
        Q.used = kAt
        if now - kAt > 0.5 then return end            -- an old press (e.g. made while the module reloaded)
        local w = S2L.qdrInv(); if not w then return end
        local hv = S2L.qdrHov(w); if not hv then return end
        local held, nh = {}, 0
        for _, sid in ipairs(T.list) do local n = S2L.qiCount(pw, sid); if n > 0 then held[sid] = n; nh = nh + 1 end end
        if nh == 0 then return end                     -- no quest item held: a normal drop, the game's own
        Q.st = "hold"; Q.t0 = kAt; Q.hv = hv; Q.held = held; Q.addr = addr; Q.n = Q.n + 1
        pcall(S2L.qfArm, ST, "qdrop")   -- b135fixes 3 (qfall): baseline before the drop
        return
    end
    Q.used = kAt                                       -- presses during a cycle (the fallback tap) never start a new one
    local w = S2L.qdrInv()
    if Q.st == "hold" then
        if not S2L.qdrKey() then S2L.qdrEnd(Q, string.format("D solto antes de %d ms: toque = recusa normal do jogo", FL.qdrophold or 1000)); return end
        if not w or S2L.qdrHov(w) ~= Q.hv then S2L.qdrEnd(Q, "inventario fechado ou outro item sob o cursor: nada liberado"); return end
        if (now - Q.t0) * 1000 < (FL.qdrophold or 1000) then return end
        local okN, n = S2L.qdrSet(Q, true)
        Q.st = "drop"; Q.callAt = now + 0.1; Q.chkEnd = nil; Q.capT = now + (FL.qdropwin or 4) + 10; Q.via = "?"
        log(string.format("S2CORE qdrop: D segurado #%d: %d/%d itens de missao liberados (S2QD0)", Q.n, okN, n))
        return
    end
    for sid, n0 in pairs(Q.held) do
        local n = S2L.qiCount(pw, sid)
        if n < n0 then
            Q.drops = (Q.drops or 0) + 1
            S2L.qdrEnd(Q, string.format("%s largado segurando D (%d -> %d) #%d via %s", sid, n0, n, Q.drops, Q.via)); return
        end
    end
    if not w then S2L.qdrEnd(Q, "inventario fechado, nada largado"); return end
    if now >= Q.capT then S2L.qdrEnd(Q, "limite de tempo, nada largado"); return end
    if Q.st == "drop" then
        if Q.callAt and now >= Q.callAt then
            Q.callAt = nil; Q.chkEnd = now + 0.6
            local fn = ({"UIInventoryDropItem", "OnDropItemHoldTriggered"})[FL.qdropfn or 1]
            if not fn then Q.chkEnd = now; return end
            if S2L.qdrHov(w) ~= Q.hv then log("S2CORE qdrop: outro item sob o cursor: sem chamada de largar"); Q.chkEnd = now; return end
            local ok, e = pcall(function() w[fn](w) end)
            Q.via = fn; Q.calls = (Q.calls or 0) + 1
            if not ok then log("S2CORE qdrop: " .. fn .. " erro: " .. tostring(e)) end
        elseif Q.chkEnd and now >= Q.chkEnd then
            Q.st = "win"; Q.relAt = nil; Q.via = "toque na janela"; Q.fb = (Q.fb or 0) + 1
            log(string.format("S2CORE qdrop: a chamada nao largou: itens liberados ate %d s depois de soltar D (o proximo toque larga)", FL.qdropwin or 4))
        end
        return
    end
    if S2L.qdrKey() then Q.relAt = nil else Q.relAt = Q.relAt or now end
    if Q.relAt and now - Q.relAt >= (FL.qdropwin or 4) then S2L.qdrEnd(Q, "janela acabou, nada largado") end
end
function S2L.qdrTick(ST)                               -- tick100
    if (FL.qdrop or 0) ~= 1 then return end
    local Q = ST.qdh
    if not Q then
        Q = {n = 0}; ST.qdh = Q
        local O = ST.qdr; ST.qdr = nil                 -- a b130 window left open by a reload: re-lock it (its widget only if the same pawn)
        if O and O.open and O.sw then
            local pw = S2.io and S2.io.pawn
            if O.w and valid(pw) and pw:GetAddress() == O.addr then pcall(function() if O.w:IsValid() then O.w:RemoveFromParent() end end) end
            pcall(S2L.qdrSet, {held = O.sw}, false); log("S2CORE qdrop: janela do b130 fechada no reload (S2QD1)")
        end
    end
    local ok, e = pcall(S2L.qdrRun, ST, Q)
    if not ok then Q.err = (Q.err or 0) + 1; if Q.err <= 5 then log("S2CORE qdrop erro: " .. tostring(e)) end end
end
-- ===== end b132qhold
-- ===== b101n7save (flag n7sv, default 0; client only; needs n7=1, csave=1, qrollback=1): AFTER ANY HOST LOAD THE CLIENT LOADS ITS SAVE
-- PAIRED WITH THE HOST'S LOADED SAVE (not simply its newest one). Measured offline 2026-10-15 (client save folder, read-only copies):
--   * every client save is csave's own XTriggerForcedAutoSave (timer cstimer=300 s -> the 10:49/10:54/10:59 saves, steps, pair, and the
--     'o outro lado saiu' save, which also fires when the HOST is on its load screen, 11:17:24); the ClientAutoSaveOnly pak sets
--     AutoSaveIntervalTime=0 and AllowedSaveTypes=Auto only, so the game makes no save of its own; n7's gate (applied host steps <= kh)
--     held: those saves were 'paired' with the host's NEWEST save (kh=132), the host then loaded an OLDER one (10:03, kh=113);
--   * the index CampaignsSave.sav is Oodle-compressed (Lua cannot read it); the client's Auto saves are a ring of 10 Data\<GUID>.sav files
--     (the oldest GUID is rewritten); every data file ends with its own CRC32 (crc32(file) = 2144DF1C for all) and holds neither its
--     GUID nor a copy in the index (no size/CRC of it there); no reflected load-by-name exists (only XLoadLastSave, XCallQuickLoad).
-- DESIGN (one mechanism, every host load):
--   (1) ledger: when csave confirms a save (record), the one Data file whose size/tail changed is that save -> row {guid, host steps it
--       holds (= the client_applied.txt count), tail signature, campaign}; client_applied.txt is copied to n7ap_<guid>.txt.
--   (2) host load with kh=K (n7Follow): the intact row with the most steps <= K (newest on ties). If it is not the newest save, its bytes are
--       copied over the newest save's file (that file is first copied to n7sv_backup.sav) and client_applied.txt becomes the pick's copy;
--       then the old path runs: XLoadLastSave loads it, the catch-up replays the host steps after it, n7 gives back the client's items.
--       Rows with more than K steps (the branch the host left) are dropped. Any doubt (no row <= K, the newest save unknown or changed
--       outside csave, a copy that does not verify) = the old path (XLoadLastSave of the newest). A save still being confirmed delays the
--       follow up to 15 s.
--   (3) save gate (part a): no client save while a follow waits, nor while the client's own graph ran a qpick node the host has not reported
--       yet (n7svloc s), so a row's step count really describes its quest state.
-- Bridge files: n7sv_pares.txt (ledger + signatures), n7sv_ring.txt (Data file names; one 'dir' through io.popen when missing or when a
-- save made a new file, n7svdir=1), n7ap_<guid>.txt, n7sv_backup.sav/.txt (the newest save before the last copy). Logs 'S2CORE n7sv: ...'.
function S2L.svSt(ST)
    if not ST.n7sv then ST.n7sv = {rows = {}, seq = 0, dirty = false}; S2L.svLoad(ST.n7sv) end
    return ST.n7sv
end
function S2L.svDir()                                    -- the client's Data folder, next to the save index (csave's savepath.txt rule)
    local p = S2L.n7Path(); if not p then return nil end
    local d = p:match("^(.*)[\\/][^\\/]+$"); if not d then return nil end
    return d .. "\\Data\\"
end
function S2L.svSig(path)                                -- size + last 16 bytes (the file's own CRC32 sits at its end)
    local f = io.open(path, "rb"); if not f then return nil end
    local size = f:seek("end") or 0
    local tail = ""
    if size >= 16 then f:seek("set", size - 16); tail = f:read(16) or "" end
    f:close()
    local h = 0
    for i = 1, #tail do h = (h * 31 + tail:byte(i)) % 2147483647 end
    return size .. ":" .. h
end
function S2L.svWrite(path, data)                        -- write through a temp file; true = the file now holds exactly data
    local f = io.open(path .. ".n7tmp", "wb"); if not f then return false end
    f:write(data); f:close()
    os.remove(path)
    if not os.rename(path .. ".n7tmp", path) then return false end
    local g = io.open(path, "rb"); if not g then return false end
    local n = g:seek("end"); g:close()
    return n == #data
end
function S2L.svRead(path, bin)
    local f = io.open(path, bin and "rb" or "r"); if not f then return nil end
    local d = f:read("a"); f:close(); return d
end
function S2L.svLoad(V)                                  -- ledger from the bridge (kept across relaunches)
    local d = S2L.svRead(api.BR .. "n7sv_pares.txt"); if not d or d:sub(1, 7) ~= "#N7SV 1" then return end
    for ln in d:gmatch("[^\r\n]+") do
        local t = {}
        for x in (ln .. "\t"):gmatch("([^\t]*)\t") do t[#t + 1] = x end
        if t[1] == "R" and #t >= 6 and tonumber(t[2]) and tonumber(t[4]) then
            V.rows[#V.rows + 1] = {seq = tonumber(t[2]), guid = t[3], tot = tonumber(t[4]), sig = t[5], cid = t[6], wc = (t[7] and t[7] ~= "" and t[7] ~= "-") and t[7] or nil}   -- b167 wc
            V.seq = math.max(V.seq, tonumber(t[2]))
        elseif t[1] == "B" and #t >= 3 then V.base = V.base or {}; V.base[t[2]] = t[3]
        elseif t[1] == "D" then V.dirty = t[2] == "1" end
    end
end
function S2L.svSave(V)
    local o = {"#N7SV 1"}
    for _, r in ipairs(V.rows) do o[#o + 1] = string.format("R\t%d\t%s\t%d\t%s\t%s\t%s", r.seq, r.guid, r.tot, r.sig, r.cid or "-", r.wc or "-") end
    for g, s in pairs(V.base or {}) do o[#o + 1] = "B\t" .. g .. "\t" .. s end
    o[#o + 1] = "D\t" .. (V.dirty and "1" or "0")
    S2L.svWrite(api.BR .. "n7sv_pares.txt", table.concat(o, "\n") .. "\n")
end
function S2L.svDrop(V, pred)                            -- remove the rows pred(row) is true for; returns how many
    local keep, n = {}, 0
    for _, r in ipairs(V.rows) do if pred(r) then n = n + 1 else keep[#keep + 1] = r end end
    V.rows = keep
    return n
end
function S2L.svNames(V, force)                          -- names (GUIDs) of the client's save files
    do local f = (FL.csold or 0) ~= 1 and rawget(_G, "s2n_save_list")   -- b165: C++ list of Data\*.sav, no io.popen
        if type(f) == "function" then local ok, l = pcall(f); if ok and type(l) == "string" then local t = {}; for g in l:gmatch("%x+") do if #g == 32 then t[#t + 1] = g:upper() end end; V.names = t; return t end end end
    if V.names and not force then return V.names end
    if not force and not V.names then
        local d, t = S2L.svRead(api.BR .. "n7sv_ring.txt"), {}
        for g in (d or ""):gmatch("%x+") do if #g == 32 then t[#t + 1] = g:upper() end end
        if #t > 0 then V.names = t; return t end
    end
    local dir = S2L.svDir()
    if (FL.n7svdir or 1) ~= 1 or not io.popen or not dir then V.names = V.names or {}; return V.names end   -- only at start and when a save made a new file
    local t, p = {}, io.popen('dir /b /a-d "' .. dir:gsub("/", "\\") .. '*.sav" 2>nul')
    if p then
        for ln in p:lines() do local g = ln:match("^(%x+)%.[sS][aA][vV]%s*$"); if g and #g == 32 then t[#t + 1] = g:upper() end end
        p:close()
    end
    V.names = t
    if #t > 0 then S2L.svWrite(api.BR .. "n7sv_ring.txt", table.concat(t, "\n") .. "\n") end
    log(string.format("S2CORE n7sv: %d saves do cliente listados (dir %s) -> n7sv_ring.txt", #t, dir))
    return t
end
function S2L.svSigs(V)                                  -- signature of every listed save file ("-" = missing)
    local dir, t = S2L.svDir(), {}
    if not dir then return t end
    for _, g in ipairs(S2L.svNames(V)) do t[g] = S2L.svSig(dir .. g .. ".sav") or "-" end
    return t
end
function S2L.svRec(ST, Q, n)                            -- csave record(): which file holds the save just confirmed, with n host steps
    if (FL.n7sv or 0) ~= 1 or api.ROLE ~= "client" then return end
    local V = S2L.svSt(ST)
    local base, cur, ch = V.base or {}, S2L.svSigs(V), {}
    for g, s in pairs(cur) do if s ~= "-" and s ~= base[g] then ch[#ch + 1] = g end end
    if #ch == 0 then                                     -- a new file (the ring is still growing): list again
        local old = {}; for g in pairs(cur) do old[g] = true end
        S2L.svNames(V, true); cur = S2L.svSigs(V)
        for g, s in pairs(cur) do if not old[g] and s ~= "-" then ch[#ch + 1] = g end end
    end
    for _, g in ipairs(ch) do S2L.svDrop(V, function(r) return r.guid == g end) end
    if #ch == 1 then
        local g = ch[1]
        V.seq = V.seq + 1
        V.rows[#V.rows + 1] = {seq = V.seq, guid = g, tot = n, sig = cur[g], cid = Q.apCid or "-", clk = os.clock(), wc = Q.apW}
        S2L.svWrite(api.BR .. "n7ap_" .. g .. ".txt", S2L.svRead(api.BR .. "client_applied.txt") or "")
        V.dirty = false
        local pos = ""
        pcall(function() local pw = rbPawn(); if valid(pw) then local l = pw:K2_GetActorLocation(); pos = string.format(" em %.0f,%.0f,%.0f", l.X, l.Y, l.Z) end end)
        log(string.format("S2CORE n7sv: save do cliente %s guarda %d passos do host (par #%d, %d guardados)%s", g:sub(1, 8), n, V.seq, #V.rows, pos))
    else
        V.dirty = true
        log(string.format("S2CORE n7sv: save do cliente nao identificado (%d arquivos mudaram): o mais novo fica desconhecido ate o proximo save", #ch))
    end
    V.base = cur
    S2L.svSave(V)
end
function S2L.svTick(ST, N, now)                         -- n7Tick (client): signatures every 10 s; a follow that waited for a save
    if (FL.n7sv or 0) ~= 1 or api.ROLE ~= "client" or FL.csave ~= 1 then return end
    local Q, V = ST.qs, S2L.svSt(ST)
    if N.svPend then
        if Q.csWait and now < N.svPend + 15 then return end
        N.svPend = nil; N.svGo = true
        log("S2CORE n7sv: o save em confirmacao acabou (ou 15 s): sigo o host agora")
        S2L.n7Follow(ST, N, now)
        N.svGo = nil
        return
    end
    if Q.csWait or N.stage ~= "idle" or now < (V.tickAt or -1e9) then return end
    V.tickAt = now + 10
    local cur, ch = S2L.svSigs(V), {}
    if V.base then for g, s in pairs(cur) do if V.base[g] ~= nil and s ~= V.base[g] then ch[#ch + 1] = g end end end
    for _, g in ipairs(ch) do S2L.svDrop(V, function(r) return r.guid == g end) end
    if #ch > 0 then
        V.dirty = true
        log(string.format("S2CORE n7sv: %d save(s) do cliente mudaram fora do csave (%s): pares descartados; o mais novo fica desconhecido ate o proximo save do csave", #ch, ch[1]:sub(1, 8)))
    end
    local first = V.base == nil
    V.base = cur
    if first or #ch > 0 then S2L.svSave(V) end
end
function S2L.svSaveOk(ST, Q)                            -- csTick, last save condition (part a): true = save as before
    if (FL.n7sv or 0) ~= 1 or api.ROLE ~= "client" then return true end
    local N, now = ST.n7, os.clock()
    if N and N.svPend then return false end
    local P = ST.qp
    if P and P.loc and (FL.qpick or 0) == 1 then
        for sid, e in pairs(P.loc) do
            if (e.n or 0) > 0 and now - (e.at or -1e9) < (FL.n7svloc or 30) then
                local V = S2L.svSt(ST)
                if now >= (V.gateLog or -1e9) then V.gateLog = now + 60; log("S2CORE n7sv: save do cliente adiado: o grafo do cliente rodou " .. sid .. " e o host ainda nao relatou") end
                return false
            end
        end
    end
    return true
end
function S2L.svPair(ST, K, cid)                         -- host went back to a save with K steps: make the client's newest save the paired one
    local V, dir = S2L.svSt(ST), S2L.svDir()
    if not dir or type(K) ~= "number" or K < 0 then log("S2CORE n7sv: sem kh do host ou sem pasta de saves: XLoadLastSave normal"); return nil end
    local newest
    for _, r in ipairs(V.rows) do if not newest or r.seq > newest.seq then newest = r end end
    local pick, nok = nil, 0
    for _, r in ipairs(V.rows) do
        if r.tot <= K and (r.cid == "-" or not cid or r.cid == cid) and ((FL.cswc or 1) ~= 1 or (r.wc ~= nil and (not cid or r.wc == cid))) and S2L.svSig(dir .. r.guid .. ".sav") == r.sig then
            nok = nok + 1
            if not pick or r.tot > pick.tot or (r.tot == pick.tot and r.seq > pick.seq) then pick = r end
        end
    end
    local gone = S2L.svDrop(V, function(r) return r.tot > K end)     -- the branch the host left
    if V.dirty or not newest or S2L.svSig(dir .. newest.guid .. ".sav") ~= newest.sig then
        log(string.format("S2CORE n7sv: host voltou ao save com %d passos, mas o save mais novo do cliente e desconhecido (%s): XLoadLastSave normal (antigo)",
            K, V.dirty and "mudou fora do csave" or "sem registro"))
        S2L.svSave(V); return nil
    end
    if not pick then
        log(string.format("S2CORE n7sv: host voltou ao save com %d passos e nenhum save do cliente tem <= %d (o mais novo tem %d): XLoadLastSave normal (antigo)", K, K, newest.tot))
        S2L.svSave(V); return nil
    end
    if pick.guid == newest.guid then
        log(string.format("S2CORE n7sv: host voltou ao save com %d passos: o save mais novo do cliente %s (%d passos) ja e o pareado", K, pick.guid:sub(1, 8), pick.tot))
        S2L.svSave(V); return pick
    end
    local ap, data = S2L.svRead(api.BR .. "n7ap_" .. pick.guid .. ".txt"), S2L.svRead(dir .. pick.guid .. ".sav", true)
    local old = S2L.svRead(dir .. newest.guid .. ".sav", true)
    if not (ap and data and old and ap ~= "") then
        log("S2CORE n7sv: copia do save pareado " .. pick.guid:sub(1, 8) .. " ou do seu registro ilegivel: XLoadLastSave normal (antigo)")
        S2L.svSave(V); return nil
    end
    S2L.svWrite(api.BR .. "n7sv_backup.sav", old)
    S2L.svWrite(api.BR .. "n7sv_backup.txt", string.format("%s.sav, %d passos do host, copiado por cima em %s pelo save %s (%d passos)\n", newest.guid, newest.tot, os.date(), pick.guid, pick.tot))
    if not S2L.svWrite(dir .. newest.guid .. ".sav", data) or S2L.svSig(dir .. newest.guid .. ".sav") ~= pick.sig then
        S2L.svWrite(dir .. newest.guid .. ".sav", old)
        log("S2CORE n7sv: a copia por cima de " .. newest.guid:sub(1, 8) .. " nao conferiu: original devolvido; XLoadLastSave normal (antigo)")
        S2L.svSave(V); return nil
    end
    S2L.svWrite(api.BR .. "n7ap_" .. newest.guid .. ".txt", ap)
    S2L.svWrite(api.BR .. "client_applied.txt", ap)
    local was = newest.tot
    newest.tot, newest.sig, newest.cid, newest.clk = pick.tot, pick.sig, pick.cid, pick.clk
    V.base = V.base or {}; V.base[newest.guid] = pick.sig
    S2L.svSave(V)
    log(string.format("S2CORE n7sv: host voltou ao save com %d passos -> save pareado do cliente %s (%d passos, par #%d) copiado por cima do mais novo %s (%d passos; copia em n7sv_backup.sav; %d pares do ramo abandonado largados); XLoadLastSave carrega o pareado",
        K, pick.guid:sub(1, 8), pick.tot, pick.seq, newest.guid:sub(1, 8), was, gone))
    return pick
end
function S2L.svPre(ST, N, now)                          -- top of n7Follow (client): true = wait (a save is being confirmed)
    if (FL.n7sv or 0) ~= 1 or api.ROLE ~= "client" then return false end
    local Q = ST.qs
    if N.svGo then N.svGo = nil
    elseif Q.csWait then
        if not N.svPend then N.svPend = now; log("S2CORE n7sv: host voltou no tempo com um save do cliente em confirmacao: espero ate 15 s para saber qual arquivo e o mais novo") end
        return true
    end
    N.svPend = nil
    local r = S2L.svPair(ST, N.kh, (x1part("B") or ""):match("^%d+,%d+,(%w+)"))
    if r then Q.n7SavedAt = r.clk or -1e9 end           -- rewards after the paired save are the ones the client keeps
    return false
end
function M.n7svApi() return S2L end                     -- probes: S2.mod.n7svApi()
-- ===== end b101n7save
-- ===== b165 (flag csold, default 0 = ON; 1 = old behaviour): ONE CLIENT SAVE RULE + the save folder this game really writes + host log settle
-- User 2026-10-16: "save client whenever the host saves". Measured 2026-10-16 (UE4SS_0950/_1041 logs, probe p165, process command lines):
--   (a) both games ran without -saveddirsuffix and without Steam, so BOTH wrote Saved\NULL\SaveGames, while the host watched Saved\STEAM
--       (default) and the client Saved_client\STEAM (B\savepath.txt): the host never saw its own saves (hs=0; kh stayed 0 after the 09:34
--       new campaign), so the n7 gate deferred every client save forever ('adiado ... so tem 0', 373 -> 440), and every client save that did
--       run was judged 'RECUSOU' (csRefused=12): client_applied.txt stayed at the old campaign c6ad0b6ff3ef3 (125 events);
--   (b) the 10:39 relaunch: the client loaded a save of the OLD campaign (the only record it had) and the catch-up went into that world;
--   (c) the host offered its catch-up (183) and n7 kh=183 BEFORE questRollback cut its log to the loaded save (152): the client 31 ahead.
-- FIX: (1) S2L.svInfo: C++ s2n_save_info gives this process's real folder (-saveddirsuffix) and a stamp of BOTH platform indexes (STEAM and
--   NULL); n7Host, csave's confirm, n7Path (svDir, D13 tag) and the n7sv file list use it (no C++ function = the old paths).
--   (2) the client saves only when the host saved: Q.n7Pair = {hs, kh, cid} (a newer host save replaces it), right away, retried every
--   csretry s after a refusal until it sticks; no timer/steps/leave saves, no n7/n7sv gates; never on a save folder shared with the host
--   (D13). The n7sv row of that save holds the host's kh, so a host load of that save finds it. (3) host: #B and the catch-up wait until its
--   quest log matches the loaded save (questRollback / rbTick checks done; qlsettle s at most). (4) client: once per loaded save, a save of
--   another campaign, or ahead of the host's, is replaced by its paired save of this campaign when the ledger has one (n7Follow; the other
--   campaign's items are not restored); otherwise the catch-up runs and the log says what the world is.
function S2L.svInfo()                                   -- dir, stamp, plat, newest of the save folder this game writes; nil = C++ absent or csold=1
    if (FL.csold or 0) == 1 then return nil end
    local f = rawget(_G, "s2n_save_info"); if type(f) ~= "function" then return nil end
    local ok, dir, stamp, plat, newest = pcall(f)
    if not ok or type(dir) ~= "string" or dir == "" then return nil end
    return dir, stamp, plat or "", newest or ""
end
function S2L.qlSettled(Q)                               -- host: true once the quest log matches the loaded save (questRollback / rbTick checks done)
    if (FL.csold or 0) == 1 or api.ROLE ~= "host" then return true end
    if Q.rbCheckAt or (FL.qrollback == 1 and not Q.rbAddr) then return false end
    if Q.qlOk then return true end
    Q.qlT0 = Q.qlT0 or os.clock()
    if os.clock() - Q.qlT0 >= (FL.qlsettle or 60) then
        Q.qlOk = true
        log(string.format("S2CORE quest: o relogio do jogo nao confirmou o log do host em %d s: #B e catch-up liberados assim mesmo", FL.qlsettle or 60))
    end
    return Q.qlOk == true
end
function S2L.cs1Due(ST, Q, frozen, now)                 -- csTick (client): the ONE save rule: the host saved (Q.n7Pair), save now; retried after a refusal
    local T = Q.n7Pair
    if type(T) ~= "table" or frozen or Q.rbWait or now < (Q.csNext or 0) then return false end
    if (FL.cswc or 1) == 1 and T.cid and Q.apW ~= T.cid then   -- b167: a save of another world tagged with the host campaign = a false pair
        Q.n7Pair = nil
        log(string.format("S2CORE csave1: host salvou (#%d) mas o mundo carregado do cliente e da campanha %s e o host esta na %s: o cliente NAO salva (seria um par falso; entrada sxjoin=1 ou um save pareado de verdade)", T.hs or -1, tostring(Q.apW or "desconhecida"), tostring(T.cid)))
        return false
    end
    local N = ST.n7
    if N and N.stage ~= "idle" then return false end
    if N and N.shared and (FL.n7shared or 0) ~= 1 then
        Q.n7Pair = nil
        if not Q.cs1Warn then Q.cs1Warn = true; log("S2CORE csave1: o cliente e o host gravam na MESMA pasta de saves (D13): o cliente NAO salva (lance o cliente com -saveddirsuffix=client e com o Steam aberto)") end
        return false
    end
    return true
end
function S2L.cs1Rec(ST, Q, n)                           -- csave record(): which host save this client save pairs with; returns the step count of its n7sv row
    local T = Q.csWait and Q.csWait.tag
    if (FL.csold or 0) == 1 or type(T) ~= "table" then return n end
    if Q.n7Pair == T then Q.n7Pair = nil end
    log(string.format("S2CORE csave1: save do cliente = par do save #%d do host (kh=%d, campanha %s); o cliente tinha %d passos aplicados%s%s", T.hs, T.kh, tostring(T.cid), n,
        n ~= T.kh and " (diferente do host: o catch-up da proxima carga acerta)" or "",
        type(Q.n7Pair) == "table" and string.format("; o host ja salvou de novo (#%d): outro save a seguir", Q.n7Pair.hs) or ""))
    return T.kh
end
function S2L.svFind(ST, K, cid)                         -- the intact ledger row of campaign cid with the most steps <= K that svPair can copy (not the newest)
    local V, dir = S2L.svSt(ST), S2L.svDir()
    if not dir or V.dirty then return nil end
    local newest, pick
    for _, r in ipairs(V.rows) do if not newest or r.seq > newest.seq then newest = r end end
    if not newest or S2L.svSig(dir .. newest.guid .. ".sav") ~= newest.sig then return nil end
    for _, r in ipairs(V.rows) do
        if r.tot <= K and r.cid == cid and ((FL.cswc or 1) ~= 1 or r.wc == cid) and S2L.svSig(dir .. r.guid .. ".sav") == r.sig and (not pick or r.tot > pick.tot or (r.tot == pick.tot and r.seq > pick.seq)) then pick = r end
    end
    if pick and pick.guid == newest.guid then return nil end
    return pick
end
function S2L.n7Boot(ST, N, now, cid, kh)                -- n7Client: once per loaded client save: is it the pair of the host's save?
    if (FL.csold or 0) == 1 or FL.csave ~= 1 then return end
    local Q = ST.qs; local R = Q.bootRec
    if not R or R.done or type(kh) ~= "number" or kh < 0 or not cid or N.stage ~= "idle" or Q.rbWait then return end
    local pw = rbPawn(); if not (valid(pw) and pw:GetAddress() == R.addr) then return end
    R.done = true
    if N.bootGo then N.bootGo = nil; log(string.format("S2CORE n7boot: save pareado carregado (%d passos do host no registro, host %d)", R.n, kh)); return end
    do local okX, tk = pcall(S2L.sxBoot, ST, N, now, cid, kh, R); if not okX then log("S2CORE sxjoin: erro " .. tostring(tk)) elseif tk then return end end   -- b167
    local other = (R.cid ~= nil and R.cid ~= cid) or (R.cid == nil and R.n > 0) or ((FL.cswc or 1) == 1 and R.wc ~= cid)   -- b167: world campaign unknown/other = not the host's
    if not other and R.n <= kh then
        log(string.format("S2CORE n7boot: o save carregado do cliente casa com o do host (campanha %s, %d passos, host %d)%s", cid, R.n, kh, R.n < kh and ": o catch-up aplica o resto" or ""))
        return
    end
    local why = other and string.format("e da campanha %s e o host esta na %s", tostring(R.cid), cid) or string.format("tem %d passos do host e o save do host so %d (adiantado)", R.n, kh)
    local rest = other and "catch-up limpo: registro descartado, o log do host inteiro e aplicado (o mundo do cliente pode ter restos da outra campanha)"
        or "o cliente fica adiantado: os passos a mais sao pulados quando o host os refizer"
    if N.shared and (FL.n7shared or 0) ~= 1 then
        log("S2CORE n7boot: o save carregado do cliente " .. why .. "; a pasta de saves e a MESMA do host (D13), nao ha save pareado para carregar; " .. rest)
        return
    end
    local pick = (FL.n7sv or 0) == 1 and FL.qrollback == 1 and S2L.svFind(ST, kh, cid)
    if not pick then
        log("S2CORE n7boot: o save carregado do cliente " .. why .. " e nenhum outro save do cliente da campanha " .. cid .. " tem <= " .. kh .. " passos" .. ((FL.n7sv or 0) == 1 and "" or " (n7sv=0)") .. ": " .. rest)
        return
    end
    log(string.format("S2CORE n7boot: o save carregado do cliente %s -> carrego o save pareado %s (%d passos do host, campanha %s)", why, pick.guid:sub(1, 8), pick.tot, cid))
    N.bootGo = true
    S2L.n7Follow(ST, N, now)
    if N.snap and other then N.snap.skip = true end      -- the other campaign's inventory is not given back (n7Stage)
    if N.stage == "idle" then N.bootGo = nil end          -- the follow did not start (XLoadLastSave failed / waits for a save)
end
-- ===== end b165
-- ===== b167 (flag sxjoin, default 0): JOIN = THE CLIENT LOADS A COPY OF THE HOST'S SAVE AND KEEPS ITS OWN ITEMS
-- User rule: "the client keeps his items every time, but it needs to sync to the host when it loads into his world".
-- n7Boot (once per loaded client save): the save is another campaign's, ahead of the host's, or has no record, and the ledger has no paired
-- save (svFind; that path stays: it loads the pair and keeps the pair's items) -> N.stage "sx":
--   snap    C++ s2n_sx_snap: counts of LOOT_SIDS on the player pawn (quest items skipped), money, hand item -> bridge sx_snap.txt "taken"
--   want    s2n_sx_want(id) -> relay b167 asks the host relay; host C++ s2n_sx_host packs its NEWEST Data save + its applied record (kh steps
--           of quest_log.txt, campaign); refused while the host loaded and has not saved since (it logs "save once")
--   save    the package arrived (relay: chunks, resume, CRC32) -> XTriggerForcedAutoSave = a NEW client entry (sxsave s, sxtries tries)
--   install s2n_sx_install: that newest entry backed up (sx_backup_<guid>.sav), the host bytes written over it, client_applied.txt =
--           the host's record; ledger row (kh, campaign) so later boots find this pair; snapshot "installed"; XLoadLastSave
--   then n7Stage restores the snapshot exactly (load / settle / trim / give / verify; quest items of the host save stay) -> "applied".
-- Quit mid-join: "installed" + a matching boot -> restore only (never a new snapshot of the host's items). Transfer/save failure: the
-- client stays in its own world (snapshot "dropped", the old catch-up). Logs 'S2CORE sxjoin: ...' and '[S2CoopNative] S2SX: ...'.
function S2L.sxF(n) local f = rawget(_G, n); return type(f) == "function" and f or nil end
function S2L.sxBoot(ST, N, now, cid, kh, R)              -- n7Boot: true = the join took over this boot
    if (FL.sxjoin or 0) ~= 1 or api.ROLE ~= "client" or not S2L.sxF("s2n_sx_snap") then return false end
    local other = (R.cid ~= nil and R.cid ~= cid) or (R.cid == nil and R.n > 0) or ((FL.cswc or 1) == 1 and R.wc ~= cid)
    local mism = other or R.n > kh or R.cid == nil
    local st, id = S2L.sxF("s2n_sx_get")(api.BR)
    if st == "installed" and not mism then return S2L.sxRestore(ST, N, now, id, false) end   -- quit after the install: give back now
    if not mism then return false end
    if N.shared and (FL.n7shared or 0) ~= 1 then log("S2CORE sxjoin: mesma pasta de saves do host (D13): sem entrada"); return false end
    if (FL.n7sv or 0) == 1 and FL.qrollback == 1 and S2L.svFind(ST, kh, cid) then return false end   -- a paired save exists: n7Boot loads it
    N.sx = {at = now, cid = cid, kh = kh, try = 0, s = "snap"}; N.stage = "sx"
    log(string.format("S2CORE sxjoin: o save carregado do cliente nao e o mundo do host (campanha %s, %d passos; host %s, %d) e nao ha save pareado: pego o save do host", tostring(R.cid), R.n, cid, kh))
    return true
end
function S2L.sxAbort(ST, N, why)
    local X = N.sx or {}
    pcall(S2L.sxF("s2n_sx_want") or function() end, api.BR, "")
    if X.id and not X.inst then pcall(S2L.sxF("s2n_sx_mark") or function() end, api.BR, X.id, "dropped") end
    log("S2CORE sxjoin: entrada cancelada (" .. tostring(why) .. "): o cliente fica no proprio mundo; o catch-up segue como antes")
    N.sx = nil; N.stage = "idle"
end
function S2L.sxRestore(ST, N, now, id, load)              -- snapshot file -> N.snap for n7Stage; load = XLoadLastSave the installed save first
    local Q, pw = ST.qs, rbPawn()
    local st, sid, money, items, qs = S2L.sxF("s2n_sx_get")(api.BR)
    if not valid(pw) or sid ~= id then log("S2CORE sxjoin: retrato " .. tostring(id) .. " ilegivel ou sem pawn: nada restaurado"); return false end
    local t, q = {}, {S = {}}
    for k, n in tostring(items):gmatch("([%w_%-%.]+)=(%d+)") do t[k] = tonumber(n) end
    for k in tostring(qs):gmatch("[%w_%-%.]+") do q.S[k] = true end
    N.snap = {items = t, money = money or 0, addr = pw:GetAddress(), at = now, qt = q, sxId = id}
    if not load then
        N.stage = "settle"; N.t = now + (FL.n7wait or 5)
        log("S2CORE sxjoin: o save do host ja esta carregado e o retrato " .. id .. " nao foi devolvido: devolvo agora")
        return true
    end
    Q.cuDone = nil; Q.n7Pair = nil; Q.rbWait = {at = now, addr = N.snap.addr, n = -1}; Q.rbQueue = {}
    N.bootGo = true
    local ok, e = false, "CustomConsoleManagerMP ausente"
    local mp = FindFirstOf("CustomConsoleManagerMP")
    if valid(mp) then ok, e = pcall(function() mp:XLoadLastSave() end) end
    log("S2CORE sxjoin: XLoadLastSave do save do host instalado ok=" .. tostring(ok) .. (ok and "" or (" " .. tostring(e))))
    if not ok then M.rbDone(ST, "sxjoin: XLoadLastSave falhou"); N.bootGo = nil; N.snap = nil; return false end
    N.stage = "load"; N.qiReset = nil
    return true
end
function S2L.sxTick(ST, N, now)                          -- n7Stage while N.stage == "sx"
    local X = N.sx; if not X then N.stage = "idle"; return end
    if X.s == "snap" then
        local pw = rbPawn(); if not valid(pw) then return end
        S2L.sxSids = S2L.sxSids or table.concat(LOOT_SIDS, ",")
        local id, n, money, hand, reused = S2L.sxF("s2n_sx_snap")(api.BR, pw:GetAddress(), S2L.sxSids)
        if not id then return S2L.sxAbort(ST, N, "retrato: " .. tostring(n)) end
        X.id = id; X.inst = reused or nil
        log(string.format("S2CORE sxjoin: retrato %s%s: %d itens, dinheiro %d, na mao %s", id, reused and " (o mesmo da entrada interrompida)" or "", n or 0, money or 0, tostring(hand)))
        S2L.sxF("s2n_sx_want")(api.BR, id); X.s = "want"; X.t = now
        return
    end
    if X.s == "want" then
        local st, d = S2L.sxF("s2n_sx_in")(api.BR, X.id)
        if st == "ready" then log("S2CORE sxjoin: save do host recebido (" .. tostring(d) .. ")"); X.s = "save"; X.t = nil; return end
        if now >= (X.logAt or 0) then X.logAt = now + 10; log(string.format("S2CORE sxjoin: esperando o save do host (%s %s, %.0f s)", tostring(st), tostring(d), now - (X.t or now))) end
        if now - X.t > (FL.sxwait or 180) then return S2L.sxAbort(ST, N, "o save do host nao chegou em " .. (FL.sxwait or 180) .. " s") end
        return
    end
    if X.s == "save" then                                 -- a NEW client entry for the host's world (the client's other saves stay)
        local _, stamp = S2L.svInfo()
        if not X.t then
            X.try = X.try + 1
            if X.try > (FL.sxtries or 3) then return S2L.sxAbort(ST, N, "o jogo recusou o save automatico " .. (FL.sxtries or 3) .. " vezes") end
            X.s0 = stamp; X.t = now
            local ok, e = pcall(function() FindFirstOf("CustomConsoleManagerRK"):XTriggerForcedAutoSave() end)
            log("S2CORE sxjoin: XTriggerForcedAutoSave (entrada nova para o mundo do host, tentativa " .. X.try .. ") ok=" .. tostring(ok) .. (ok and "" or (" " .. tostring(e))))
            return
        end
        if stamp and stamp ~= X.s0 then X.s = "wait"; X.t = now; return end
        if now - X.t > (FL.sxsave or 25) then X.t = nil end
        return
    end
    if X.s == "wait" then if now - X.t >= 3 then X.s = "install" end return end   -- the save file is complete
    if X.s == "install" then
        local ok, g, kh, cid = S2L.sxF("s2n_sx_install")(api.BR, X.id)
        if not ok then return S2L.sxAbort(ST, N, "instalacao: " .. tostring(g)) end
        X.inst = true
        pcall(function()                                  -- ledger row: this entry is the pair of the host's save (kh steps, campaign)
            local V, dir = S2L.svSt(ST), S2L.svDir()
            S2L.svDrop(V, function(r) return r.guid == g end)
            V.seq = V.seq + 1
            V.rows[#V.rows + 1] = {seq = V.seq, guid = g, tot = kh, sig = dir and S2L.svSig(dir .. g .. ".sav") or "-", cid = cid ~= "" and cid or "-", clk = os.clock(), wc = cid ~= "" and cid or nil}
            V.base = S2L.svSigs(V); V.dirty = false; S2L.svSave(V)
        end)
        S2L.sxF("s2n_sx_mark")(api.BR, X.id, "installed"); S2L.sxF("s2n_sx_want")(api.BR, "")
        log(string.format("S2CORE sxjoin: save do host instalado no save novo %s (kh=%d, campanha %s); carrego", tostring(g):sub(1, 8), kh or -1, tostring(cid)))
        local id = X.id; N.sx = nil
        if not S2L.sxRestore(ST, N, now, id, true) then N.stage = "idle"; pcall(S2L.sxF("s2n_sx_undo"), api.BR, g) end
    end
end
-- ===== end b167
-- ===== core-22 (a): quest rewards to the client (flag qreward=1). xrRazom model (SendQuestReward): the host runs the quest; when the host's
-- journal says a quest finished (s2q J <quest> - Finish), the host compares its money and items with a snapshot from >= 6 s before the
-- finish (rewards are often handed out in the dialog just before the journal node) and sends the gain: "#W:<seq>;<quest>;<money>;<sidxN,...>".
-- The client adds it to itself: Obj:ModifyMoney (measured: +250/-250 exact on the client) and XCreateItemInInventoryByID(sid, 0, n, 1.0).
local function rwInit(ST) ST.rw = ST.rw or {seq = 0, snaps = {}, pend = {}, out = {}, seen = {}, snapAt = 0} return ST.rw end
function M.questFinished(ST, quest)               -- called from questCmd on the host
    local R = rwInit(ST); R.pend[#R.pend + 1] = {q = quest, at = os.clock()}
end
local function rewardTick(ST)
    if FL.qreward ~= 1 then return end
    local R = rwInit(ST); local now = os.clock()
    local pw = S2.io and S2.io.pawn; if not valid(pw) then return end
    if api.ROLE == "host" then
        if now >= R.snapAt then
            R.snapAt = now + 3
            local t, m = invSnap(pw)
            table.insert(R.snaps, {t = now, items = t, money = m}); while #R.snaps > 8 do table.remove(R.snaps, 1) end
        end
        for i = #R.pend, 1, -1 do
            local p = R.pend[i]
            if now - p.at >= 3 then
                table.remove(R.pend, i)
                local base
                for _, s in ipairs(R.snaps) do if s.t <= p.at - 6 then base = s end end
                base = base or R.snaps[1]
                if base then
                    local t, m = invSnap(pw)
                    local items = invDelta(base.items, t); local dm = math.floor((m - base.money) + 0.5)
                    if dm < 0 then dm = 0 end
                    if dm == 0 and items == "" then
                        log("S2CORE reward: missao " .. p.q .. " terminou sem ganho no host; nada enviado")
                    else
                        R.seq = R.seq + 1
                        local e = string.format("%d;%s;%d;%s", R.seq, p.q, dm, items)
                        R.out[#R.out + 1] = {e = e, untilT = now + FL.resend}
                        log(string.format("S2CORE reward: missao %s terminou; ganho do host desde %.0f s antes: dinheiro +%d, itens %s -> enviado ao cliente", p.q, p.at - base.t, dm, items == "" and "-" or items))
                    end
                end
            end
        end
    end
    if api.ROLE ~= "host" or FL.rwtest == 1 then          -- rwtest=1: loopback test, the host applies its own reward message
        for ent in (x1part("W") or ""):gmatch("[^|]+") do
            if not R.seen[ent] then
                R.seen[ent] = now
                local seq, q, dm, items = ent:match("^(%d+);([^;]+);(%d+);(.*)$")
                if seq and S2L.n7Rw(ST, q) then seq = nil end   -- b97n7
                if seq then
                    local m0, m1 = 0, 0; pcall(function() m0 = pw:GetMoney() end)
                    if tonumber(dm) > 0 then pcall(function() pw:ModifyMoney(tonumber(dm)) end) end
                    pcall(function() m1 = pw:GetMoney() end)
                    local mgr = FindFirstOf("CustomConsoleManagerRK"); local got = {}
                    for sid, n in items:gmatch("([%w_]+)x(%d+)") do
                        local ok = pcall(function() mgr:XCreateItemInInventoryByID(sid, 0, tonumber(n), 1.0) end)
                        got[#got + 1] = sid .. "+" .. n .. (ok and "" or "(erro)")
                    end
                    log(string.format("S2CORE reward: recompensa da missao %s recebida: dinheiro %.0f -> %.0f, itens %s", q, m0, m1, #got > 0 and table.concat(got, " ") or "-"))
                end
            end
        end
        for k, t in pairs(R.seen) do if now - t > 30 then R.seen[k] = nil end end
    end
end
local function rewardOut(ST)
    local R = ST.rw; if not R or FL.qreward ~= 1 or api.ROLE ~= "host" then return "" end
    local now, r = os.clock(), {}
    for _, o in ipairs(R.out) do if now < o.untilT then r[#r + 1] = o.e end end
    return #r > 0 and ("#W:" .. table.concat(r, "|")) or ""
end

-- ===== core-23: cutscene / dialog / teleport signal watcher (flag cinelog=1; read-only, logs only when a signal changes).
-- Measured: PC.bInCinematicSequence is true during a sequence; an instant quest teleport shows only as a position jump.
local function cineWatch(ST)
    if FL.cinelog ~= 1 then return end
    local C = ST.cine or {}; ST.cine = C
    local pw = S2.io and S2.io.pawn
    local pc = C.pc; if not valid(pc) then pc = FindFirstOf("PC"); C.pc = pc end
    if not valid(pc) then return end
    local s = {}
    pcall(function() s[#s + 1] = "cin=" .. tostring(pc.bInCinematicSequence) end)
    pcall(function() s[#s + 1] = "tp=" .. tostring(pc.bIsProcessingTeleportation) end)
    pcall(function() s[#s + 1] = "staticDlg=" .. tostring(pc:IsInStaticDialog()) end)
    pcall(function() s[#s + 1] = "noMove=" .. tostring(pc:IsMoveInputIgnored()) end)
    pcall(function() s[#s + 1] = "noLook=" .. tostring(pc:IsLookInputIgnored()) end)
    if valid(pw) then pcall(function() s[#s + 1] = "isCin=" .. tostring(pw:IsInCinematic()) end) end
    local now = os.clock()
    if now - (C.dlgAt or 0) >= 0.5 then                       -- widgets: twice a second
        C.dlgAt = now
        local d = C.dlg; if not valid(d) then d = FindFirstOf("DialogueViewFolder"); C.dlg = d end
        C.dlgVis = "nil"; if valid(d) then pcall(function() C.dlgVis = tostring(d:IsVisible()) end) end
    end
    s[#s + 1] = "dlgWidget=" .. tostring(C.dlgVis)
    local str = table.concat(s, " ")
    local x, y, z = 0, 0, 0
    if valid(pw) then pcall(function() local q = pw:K2_GetActorLocation(); x, y, z = q.X, q.Y, q.Z end) end
    local jump = C.x and math.sqrt((x - C.x)^2 + (y - C.y)^2) or 0
    if jump > 3000 then log(string.format("S2CORE cine: SALTO de posicao %.0f m em um tique (%.0f,%.0f -> %.0f,%.0f) %s", jump / 100, C.x, C.y, x, y, str)) end
    C.x, C.y = x, y
    if str ~= C.last then C.last = str; log(string.format("S2CORE cine: %s pos=%.0f,%.0f,%.0f", str, x, y, z)) end
end

-- ===== core-22 (b): partial container take (flag ptake=1) for placed containers that have a placeholder SID in s2u_cont.tsv.
-- While the loot window is open the player's own inventory is the measure: snapshot at open, snapshot at close, the items that went UP
-- were taken from the container (the loot-window slots are pooled widgets and keep stale counts, so they are not used). The take is
-- written as a taken key "ctake@<SID>;<id>;<sidxN,...>" (host: taken.txt; client: sent with #T). Each PC applies every key it has
-- not applied yet by removing those items from ITS copy (batch 9 ItemRemove nodes, S2.mod.itemRemove "C"). The first-opener
-- "whole container taken" rule is skipped for these containers. Host keys loaded from taken.txt at start count as applied (its save has them).
local function ptInit(ST) ST.pt = ST.pt or {applied = {}, checkAt = 0} return ST.pt end
function M.partialSid(ST, id)
    if FL.ptake ~= 1 or not id then return nil end
    local ok, s = pcall(M.contSid, ST, id); return ok and s or nil
end
local function ptakeTick(ST)
    if FL.ptake ~= 1 then return end
    local P = ptInit(ST); local now = os.clock()
    if now < P.checkAt then return end
    P.checkAt = now + 1
    if not ST.takenList then return end
    if not P.init then
        P.init = true
        if api.ROLE == "host" then for _, k in ipairs(ST.takenList) do if k:sub(1, 6) == "ctake@" then P.applied[k] = true end end end
    end
    for _, k in ipairs(ST.takenList) do
        if k:sub(1, 6) == "ctake@" and not P.applied[k] and not M.LS.hold(ST, k) then   -- b90loot
            P.applied[k] = true
            local sid, items = k:match("^ctake@(%x+);[^;]+;(.*)$")
            if sid then
                for it, n in items:gmatch("([%w_]+)x(%d+)") do pcall(M.itemRemove, ST, "C", sid, it, tonumber(n)) end
                log("S2CORE ptake: aplicado na minha copia: " .. k)
            end
        end
    end
end

-- ===== b90loot (flag lootshare, default 0; user rule N1 "both can loot, the items are shared on screen, the first that opens generates them"):
-- ONE SHARED ITEM LIST PER BODY, for the bodies BOTH PCs really have: the DeadBody placeholders of s2u_cont.tsv. They are corpse PAWNS, so
-- counting (CppMediator), adding by the corpse UID (XCreateItemInInventoryByID) and the S2U_C_<SID>_<item>_1 remove nodes (ItemTakeBack
-- C1_lean pak) are all allowed. Nothing here is ever called on an item-container actor (crash rule).
-- (1) The first player to open a body reads its copy (636 LOOT_SIDS counts on the corpse pawn, ~2 ms): that is the shared list.
--     Host: taken key "lsc@<SID>;<list>" at once. Client: request "lsl@<SID>;c;<list>" over #T; the host keeps the FIRST list per body
--     (lsc) and ignores later ones. lsc keys persist in the host's taken.txt and reach the client by the #X rotation.
-- (2) The other PC makes ITS copy equal to lsc minus every take key of that body (ctake@, the existing ptake keys of both players): it reads
--     its copy, adds what is missing (corpse UID, never UID 0 = the player) and queues the removal of the rest (S2U_C nodes, itRun).
--     Ahead of time when the body is within lsr cm; at its open otherwise (the window is closed once: open it again).
-- (3) Takes after that are the existing ptake keys; ptakeTick holds a body's keys until (2) has run (the count-based fix includes them).
-- (4) lsnog=1: the host's G list (#C) leaves these bodies out (the client opens its own copy with F instead of getting a copy of all).
-- Quest items (s2u_qitems) are never added or removed here (N2 / b87). Item containers are NOT covered (no safe read): OFFLINE_REPORT 115.
M.LS = {}
function M.LS.st(ST) ST.ls = ST.ls or {canon = {}, done = {}, mine = {}, ix = 0, nog = {}, at = 0, err = 0} return ST.ls end
function M.LS.rows()
    if M.LS.R then return M.LS.R end
    local R, n = {}, 0
    M.LS.R = R
    local f = io.open(api.BR .. "s2u_cont.tsv", "r")
    if f then
        for ln in f:lines() do
            local sid, x, y, z = ln:match("^(%x+)\tDeadBody\t[^\t]*\t([%-%d%.]+)\t([%-%d%.]+)\t([%-%d%.]+)")
            if sid then R[sid] = {x = tonumber(x), y = tonumber(y), z = tonumber(z)}; n = n + 1 end
        end
        f:close()
    end
    log("S2CORE lootshare: " .. n .. " corpos (DeadBody) em s2u_cont.tsv")
    return R
end
function M.LS.body(ST, id)                             -- placeholder SID of a body lock id "BP_AI_Human_C@x,y,z" (10 cm units), else nil
    if type(id) ~= "string" or not id:find("^BP_AI_Human_C@") then return nil end
    local ok, sid = pcall(M.contSid, ST, id)
    if ok and sid and M.LS.rows()[sid] then return sid end
    return nil
end
function M.LS.enc(t)
    local r = {}
    for s, n in pairs(t) do if n > 0 then r[#r + 1] = s .. "x" .. n end end
    table.sort(r)
    return #r > 0 and table.concat(r, ",") or "-"
end
function M.LS.dec(s)
    local t = {}
    for sid, n in (s or ""):gmatch("([%w_]+)x(%d+)") do t[sid] = (t[sid] or 0) + tonumber(n) end
    return t
end
function M.LS.count(o)                                 -- corpse PAWN only (never a container actor)
    local med, t = StaticFindObject("/Script/Stalker2.Default__CppMediator"), {}
    for _, s in ipairs(LOOT_SIDS) do
        local ok, n = pcall(function() return med:GetItemsCountInInventory(o, s) end)
        if ok and type(n) == "number" and n > 0 then t[s] = n end
    end
    return t
end
function M.LS.scan(ST)                                 -- new taken keys -> the shared lists (lsc); host: adopts a client's first list (lsl)
    local S = M.LS.st(ST)
    local TL = ST.takenList; if not TL then return S end
    if S.ix > #TL then S.ix = 0 end
    while S.ix < #TL do
        S.ix = S.ix + 1
        local k = TL[S.ix]
        local sid, lst = k:match("^lsc@(%x+);(.*)$")
        if sid then
            if not S.canon[sid] then
                S.canon[sid] = lst
                if S.mine[sid] == lst then S.done[sid] = true
                else
                    S.done[sid] = nil
                    if S.mine[sid] then log("S2CORE lootshare: corpo " .. sid .. ": a lista comum veio do outro jogador e difere da minha copia: " .. lst) end
                end
                log("S2CORE lootshare: lista comum do corpo " .. sid .. ": " .. lst .. (S.done[sid] and " (minha copia ja e igual)" or " (minha copia sera igualada)"))
            end
        elseif api.ROLE == "host" then
            local s2, l2 = k:match("^lsl@(%x+);c;(.*)$")
            if s2 and not S.canon[s2] then
                log("S2CORE lootshare: o cliente abriu primeiro o corpo " .. s2 .. ": a lista dele vale para os dois")
                takenAdd(ST, "lsc@" .. s2 .. ";" .. l2)     -- read by this same loop a moment later
            elseif s2 then log("S2CORE lootshare: pedido do cliente para o corpo " .. s2 .. " ignorado (ja tem lista comum)") end
        end
    end
    return S
end
function M.LS.target(ST, sid)                          -- the shared list minus every take key of that body (both players)
    local S = M.LS.st(ST)
    local t, keys = M.LS.dec(S.canon[sid]), {}
    for _, k in ipairs(ST.takenList or {}) do
        local s2, items = k:match("^ctake@(%x+);[^;]+;(.*)$")
        if s2 == sid then
            keys[#keys + 1] = k
            for it, n in items:gmatch("([%w_]+)x(%d+)") do t[it] = (t[it] or 0) - tonumber(n) end
        end
    end
    return t, keys
end
function M.LS.fix(ST, sid, o)                          -- make MY copy of the body equal to the shared list (count-based, so it can run again)
    local S = M.LS.st(ST)
    if not valid(o) then return false end
    if #itInit(ST).run > 0 then return false end       -- removals still queued: wait, the count would be stale
    local cur = M.LS.count(o)
    local tgt, keys = M.LS.target(ST, sid)
    local QT = S2L.qiTab(); local qs = (QT and QT.S) or {}
    local uid
    pcall(function() uid = StaticFindObject("/Script/Stalker2.Default__CppMediator"):GetGUID(o) end)   -- a corpse PAWN: allowed
    local rk = FindFirstOf("CustomConsoleManagerRK")
    local add, rm, seen = {}, {}, {}
    for _, tb in ipairs({tgt, cur}) do
        for it in pairs(tb) do
            if not seen[it] then
                seen[it] = true
                local d = math.max(0, tgt[it] or 0) - (cur[it] or 0)
                if qs[it] then
                elseif d > 0 then
                    if type(uid) == "number" and uid ~= 0 and valid(rk) then
                        local ok = pcall(function() rk:XCreateItemInInventoryByID(it, uid, d, 1.0) end)
                        add[#add + 1] = it .. "x" .. d .. (ok and "" or "(erro)")
                    else add[#add + 1] = it .. "x" .. d .. "(sem uid, nao posto)" end
                elseif d < 0 then
                    pcall(M.itemRemove, ST, "C", sid, it, -d); rm[#rm + 1] = it .. "x" .. (-d)
                end
            end
        end
    end
    local P = ptInit(ST)
    for _, k in ipairs(keys) do P.applied[k] = true end
    S.done[sid] = true
    table.sort(add); table.sort(rm)
    log(string.format("S2CORE lootshare: corpo %s igualado a lista comum (%d tiradas contadas): +[%s] -[%s] uid=%s", sid, #keys, table.concat(add, " "), table.concat(rm, " "), tostring(uid)))
    return true
end
function M.LS.hold(ST, k)                              -- ptakeTick: keep a body's take key until fix() has run (fix counts it)
    if FL.lootshare ~= 1 then return false end
    local sid = k:match("^ctake@(%x+);"); if not sid then return false end
    local S = M.LS.st(ST)
    if FL.lscont == 1 and M.LS.crows()[sid] then return not (S.cb and S.cb[sid]) end   -- b93cont
    return S.canon[sid] ~= nil and not S.done[sid]
end
function M.LS.same(ST, sid, o)                         -- my copy already equals the shared list (quest items ignored)
    local cur = M.LS.count(o); local tgt = M.LS.target(ST, sid)
    local QT = S2L.qiTab(); local qs = (QT and QT.S) or {}
    for _, tb in ipairs({tgt, cur}) do for it in pairs(tb) do
        if not qs[it] and math.max(0, tgt[it] or 0) ~= (cur[it] or 0) then return false end
    end end
    return true
end
function M.LS.open(ST, id, pw)                         -- lockTick LOCK_GRANT
    if FL.lootshare ~= 1 then return end
    local sid = M.LS.body(ST, id); if not sid then return M.LS.copen(ST, id, pw) end   -- b93cont (lscont)
    local S = M.LS.scan(ST)
    local o
    pcall(function() local a = pw:GetInteractedActor(); if a and a:IsValid() then o = a end end)
    if S.canon[sid] or S.mine[sid] then
        if not S.canon[sid] then return end                    -- my own list is waiting for the host's answer
        if S.done[sid] and (not o or M.LS.same(ST, sid, o)) then return end   -- equal already (re-checked: the game can roll extra loot at the first open)
        S.rc = S.rc or {}
        if S.done[sid] then if S.rc[sid] then return end; S.rc[sid] = true end   -- close at most ONCE per body after it was made equal (an item with no removal node must never lock the body)
        pcall(function() pw:CloseLootMenu() end)
        local ok, r = pcall(M.LS.fix, ST, sid, o)
        log("S2CORE lootshare: corpo " .. sid .. " ainda diferente da lista comum: janela fechada; " .. ((ok and r) and "igualado agora, abra de novo" or ("abra de novo em alguns segundos " .. tostring(r))))
        return
    end
    if not o then return end
    local L = M.LS.enc(M.LS.count(o))
    S.mine[sid] = L; S.done[sid] = true
    if api.ROLE == "host" then takenAdd(ST, "lsc@" .. sid .. ";" .. L)
    else ST.tList = ST.tList or {}; ST.tList[#ST.tList + 1] = {id = "lsl@" .. sid .. ";c;" .. L, untilT = os.clock() + (FL.resend or 6)} end
    log("S2CORE lootshare: primeiro a abrir o corpo " .. sid .. " (" .. api.ROLE .. "): lista gerada aqui: " .. L)
end
function M.LS.noG(ST, o)                               -- host #C list: with lsnog=1 the bodies the client has a copy of are left out
    if (FL.cloot or 0) == 1 then return true end   -- b138loot: loot bodies replace the G path (no duplicate)
    if FL.lsnog ~= 1 then return false end
    local q = o:K2_GetActorLocation()
    local S = M.LS.st(ST)
    local key = string.format("%.0f,%.0f,%.0f", q.X / 100, q.Y / 100, q.Z / 100)
    if S.nog[key] == nil then
        local ok, sid = pcall(M.contSid, ST, q.X, q.Y, q.Z)
        S.nog[key] = (ok and sid and M.LS.rows()[sid]) and true or false
        if S.nog[key] then log("S2CORE lootshare: corpo " .. tostring(sid) .. " fora da lista do G (lsnog): o cliente abre a propria copia") end
    end
    return S.nog[key]
end
function M.LS.tick(ST)                                 -- once per lsper s: pending bodies within lsr cm are made equal ahead of time
    if FL.lootshare ~= 1 then return end
    local S = M.LS.st(ST); local now = os.clock()
    if S.crd then local okR, eR = pcall(M.LS.cread, ST); if not okR then S.crd = nil; log("S2CORE lscont erro: " .. tostring(eR)) end end   -- b93cont
    if now < S.at then return end
    S.at = now + (FL.lsper or 1)
    local ok, e = pcall(function()
        M.LS.scan(ST)
        if FL.lscont == 1 then M.LS.csync(ST) end   -- b93cont
        local pw = S2.io and S2.io.pawn; if not valid(pw) then return end
        local R, q = M.LS.rows(), pw:K2_GetActorLocation()
        local busy = ST.lock and ST.lock.mine
        local bsid = busy and M.LS.body(ST, busy) or nil
        local want, r2 = {}, (FL.lsr or 3000) ^ 2
        for sid in pairs(S.canon) do
            local r = R[sid]
            if not S.done[sid] and r and sid ~= bsid and (r.x - q.X) ^ 2 + (r.y - q.Y) ^ 2 <= r2 then want[#want + 1] = sid end
        end
        if #want == 0 or #itInit(ST).run > 0 then return end
        local rc = (FL.icontr or 300) ^ 2
        for _, o in ipairs(FindAllOf("BP_AI_Human_C") or {}) do
            if #want == 0 then break end
            pcall(function()
                if not o:IsValid() or o:IsAlive() then return end
                local p = o:K2_GetActorLocation()
                for i, sid in ipairs(want) do
                    local r = R[sid]
                    if (r.x - p.X) ^ 2 + (r.y - p.Y) ^ 2 + (r.z - p.Z) ^ 2 <= rc then table.remove(want, i); M.LS.fix(ST, sid, o); return end
                end
            end)
        end
    end)
    if not ok then S.err = S.err + 1; if S.err <= 5 then log("S2CORE lootshare erro: " .. tostring(e)) end end
end
-- ===== b93cont (flag lscont, default 0; needs lootshare=1; N1 stage 2, OFFLINE_REPORT 118): ONE SHARED LIST PER ITEM CONTAINER.
-- Measured 2026-10-15: the same bag rolls different items on each PC (both roll at their own first open). A container actor can be neither
-- read nor filled by Lua (GetItemsCountInInventory / GetGUID / XCreateItemInInventoryByID on it crashed), so:
-- READ = the stock take-all of the open loot window (InventoryNew:InventoryWidgetTakeAllItems, lsctake s after the open): what went UP in
--        MY inventory lscwait s later (invSnap, player pawn only) is my copy's content D. The window is closed and D leaves my inventory
--        again (S2U_P nodes, Player pak). ptake does not count the take-all (its open snapshot is replaced).
-- LIST = the first reader's D, with the b90 keys: host taken key lsc@<SID>;<list> (the host keeps the first per SID), client lsl@ over #T.
-- FILL = my copy, empty after the take-all, gets the shared list minus every take key of that SID (S2U_K_<SID>_<item>_1 ItemAdd nodes,
--        K1 pak). A client that read first fills its own D at once and, if the host's list differs (both read at once), applies the
--        difference (K adds / S2U_C removals). Each PC reads a table container at its first open in a session (a re-read empties and
--        refills the same list, so it is safe after a load); takes after that are the existing ptake keys, held until my read (the fill
--        counts them). Table s2u_contk.tsv (gen_contk.py: 1129 ItemContainer placeholders, quest-item containers excluded).
function M.LS.crows()
    if M.LS.CK then return M.LS.CK end
    local rt = FL.crowsretry or 30                                  -- b181: an empty first read is no longer cached for the session
    if rt > 0 and os.clock() < (M.LS.CKat or 0) then return {} end
    local R, n = {}, 0
    if rt <= 0 then M.LS.CK = R end
    local f = io.open(api.BR .. "s2u_contk.tsv", "r")
    if f then
        for ln in f:lines() do
            local sid, x, y, z, its = ln:match("^(%x+)\t([%-%d%.]+)\t([%-%d%.]+)\t([%-%d%.]+)\t[^\t]*\t(%S+)")
            if sid then
                local set = {}; for it in its:gmatch("[^,]+") do set[it] = true end
                R[sid] = {x = tonumber(x), y = tonumber(y), z = tonumber(z), set = set}; n = n + 1
            end
        end
        f:close()
    end
    if rt > 0 then if n > 0 then M.LS.CK = R else M.LS.CKat = os.clock() + rt end end   -- b181
    log("S2CORE lscont: " .. n .. " recipientes em s2u_contk.tsv" .. (n == 0 and (" (arquivo ausente ou vazio: tento de novo em " .. rt .. " s)") or ""))
    return R
end
function M.LS.cid(id)                                  -- table container SID of a lock id (nearest row within lscr cm), else nil
    if type(id) ~= "string" or id:find("^BP_AI_Human_C") or id:find("PlayerStash", 1, true) then return nil end
    local a, b, c = id:match("@([%-%d%.]+),([%-%d%.]+),([%-%d%.]+)$"); if not a then return nil end
    local x, y, z = tonumber(a) * 10, tonumber(b) * 10, tonumber(c) * 10
    local best, bd = nil, (FL.lscr or 150) ^ 2
    for sid, r in pairs(M.LS.crows()) do
        local d = (r.x - x) ^ 2 + (r.y - y) ^ 2 + (r.z - z) ^ 2
        if d <= bd then best, bd = sid, d end
    end
    return best
end
function M.LS.copen(ST, id, pw)                        -- LOCK_GRANT of a non-body (M.LS.open): my first open of a table container -> read it
    if FL.lscont ~= 1 then return end
    local sid = M.LS.cid(id); if not sid then return end
    local S = M.LS.st(ST); S.cb = S.cb or {}
    if S.cb[sid] or S.crd then return end              -- read already this session (takes = ptake keys) / one read at a time
    local K = ST.lock or {}
    if K.psid ~= sid then log("S2CORE lscont: recipiente " .. sid .. " sem ptake (psid=" .. tostring(K.psid) .. "): fica com a rolagem desta PC"); return end
    S.crd = {sid = sid, id = id, ta = os.clock() + (FL.lsctake or 0.3)}
end
function M.LS.cread(ST)                                -- every tick while a read is pending: take-all, then the read lscwait s later
    local S = M.LS.st(ST); local R = S.crd
    if not R then return end
    local K, now = ST.lock or {}, os.clock()
    local pw = S2.io and S2.io.pawn
    if not R.at then
        if K.mine ~= R.id or not valid(pw) then S.crd = nil; log("S2CORE lscont: janela do recipiente " .. R.sid .. " fechada antes da leitura (abra de novo)"); return end
        if now < R.ta or #itInit(ST).run > 0 then return end   -- queued nodes would move my inventory during the read: wait
        R.s0, R.m0 = invSnap(pw)
        local w = K.widget; if not valid(w) then w = FindFirstOf("InventoryNew") end
        local ok, e = pcall(function() w:InventoryWidgetTakeAllItems() end)
        if not ok then S.crd = nil; log("S2CORE lscont: pegar tudo falhou (" .. tostring(e) .. "): recipiente " .. R.sid .. " fica com a rolagem desta PC"); return end
        if K.psid == R.sid then K.psnap = setmetatable({}, {__index = function() return math.huge end}) end   -- the take-all is not a take
        R.at = now + (FL.lscwait or 0.6)
        log("S2CORE lscont: recipiente " .. R.sid .. " aberto pela 1a vez nesta sessao: pegar tudo (leitura)")
        return
    end
    if now < R.at then return end
    S.crd = nil
    if not valid(pw) then return end
    local s1, m1 = invSnap(pw)
    local D, QT = {}, S2L.qiTab(); local qs = (QT and QT.S) or {}
    for s, n in pairs(s1) do local d = n - (R.s0[s] or 0); if d > 0 and not qs[s] then D[s] = d end end
    if K.psid == R.sid and K.mine == R.id then K.psnap = s1 end
    if K.mine == R.id then pcall(function() pw:CloseLootMenu() end) end
    local L, dm = M.LS.enc(D), (m1 or 0) - (R.m0 or 0)
    if L == "-" then log("S2CORE lscont: recipiente " .. R.sid .. ": nada lido (vazio ou pegar tudo sem efeito; dinheiro " .. dm .. "): sem lista"); return end
    for s, n in pairs(D) do pcall(M.itemRemove, ST, "P", s, n) end      -- the roll leaves my inventory again
    M.LS.scan(ST)
    if not S.canon[R.sid] and not S.mine[R.sid] then
        S.mine[R.sid] = L
        if api.ROLE == "host" then takenAdd(ST, "lsc@" .. R.sid .. ";" .. L); M.LS.scan(ST)
        else ST.tList = ST.tList or {}; ST.tList[#ST.tList + 1] = {id = "lsl@" .. R.sid .. ";c;" .. L, untilT = os.clock() + (FL.resend or 6)} end
        log("S2CORE lscont: primeiro a abrir o recipiente " .. R.sid .. " (" .. api.ROLE .. "): lista gerada aqui: " .. L)
    end
    log("S2CORE lscont: recipiente " .. R.sid .. " lido: " .. L .. (dm ~= 0 and (" (dinheiro " .. dm .. " fica comigo)") or "") .. "; janela fechada, abra de novo em alguns segundos")
    M.LS.cfill(ST, R.sid, S.canon[R.sid] or L, nil)
end
function M.LS.cfill(ST, sid, newL, oldL)               -- my copy: from empty (oldL nil: list minus every take key of sid) or from list oldL to newL
    local S = M.LS.st(ST); S.cb = S.cb or {}
    local row = M.LS.crows()[sid]; if not row then return false end
    local QT = S2L.qiTab(); local qs = (QT and QT.S) or {}
    local t, keys = M.LS.dec(newL), {}
    if oldL then for it, n in pairs(M.LS.dec(oldL)) do t[it] = (t[it] or 0) - n end
    else
        for _, k in ipairs(ST.takenList or {}) do
            local s2, items = k:match("^ctake@(%x+);[^;]+;(.*)$")
            if s2 == sid then keys[#keys + 1] = k; for it, n in items:gmatch("([%w_]+)x(%d+)") do t[it] = (t[it] or 0) - tonumber(n) end end
        end
    end
    local I, add, rm = itInit(ST), {}, {}
    for it, d in pairs(t) do
        if qs[it] then
        elseif d > 0 then
            if row.set[it] then
                for _ = 1, math.min(d, FL.icmax or 1000) do I.run[#I.run + 1] = string.format("S2U_K_%s_%s_1", sid, it) end
                add[#add + 1] = it .. "x" .. d
            else add[#add + 1] = it .. "x" .. d .. "(sem no K)" end
        elseif d < 0 and oldL then pcall(M.itemRemove, ST, "C", sid, it, -d); rm[#rm + 1] = it .. "x" .. (-d) end
    end
    local P = ptInit(ST)
    for _, k in ipairs(keys) do P.applied[k] = true end
    S.cb[sid] = newL
    table.sort(add); table.sort(rm)
    log(string.format("S2CORE lscont: recipiente %s %s (%d tiradas contadas): +[%s] -[%s]", sid, oldL and "corrigido para a lista do host" or "enchido com a lista comum", #keys, table.concat(add, " "), table.concat(rm, " ")))
    return true
end
function M.LS.csync(ST)                                -- once per lsper s: a copy I filled with my own list gets the host's list when it differs
    local S = M.LS.st(ST)
    for sid, b in pairs(S.cb or {}) do
        if S.canon[sid] and S.canon[sid] ~= b then M.LS.cfill(ST, sid, S.canon[sid], b) end
    end
end
-- ===== end b93cont
-- ===== end b90loot
-- ===== batch 13 (1): client persistence =====
local function cpOn() return FL.cpersist == 1 and FL.csave ~= 1 and (api.ROLE == "client" or FL.cptest == 1) end   -- batch 23: with csave the game's own save keeps the inventory
function S2L.cpPawn()
    local pw = S2.io and S2.io.pawn
    if not valid(pw) then pcall(function() pw = api.UEHelpers.GetPlayerController().Pawn end) end
    return pw
end
function S2L.cpFile() return api.BR .. "client_state.txt" end
function S2L.cpTables(C)                             -- slot items and weapon ammo (gen_equip.py), and the new-game kit that is worn
    if C.eqTab then return end
    C.eqTab, C.eqList, C.base = {}, {}, {}
    local f = io.open(api.BR .. "s2u_equip.tsv", "r")
    if f then
        for ln in f:lines() do
            local sid, kind, ammo, mx = ln:match("^(%S+)\t(%S*)\t(%S*)\t(%S*)")
            if sid then C.eqTab[sid] = {kind = kind, ammo = ammo ~= "" and ammo or nil, max = tonumber(mx)}; C.eqList[#C.eqList + 1] = sid end
        end
        f:close()
    end
    f = io.open(api.BR .. "s2u_basekit.txt", "r")
    if f then for ln in f:lines() do local sid = ln:match("^%s*([%w_]+)"); if sid then C.base[sid] = true end end f:close() end
    log(string.format("S2CORE persist: %d itens de slot em s2u_equip.tsv, kit base vestido: %d", #C.eqList, (function() local n = 0; for _ in pairs(C.base) do n = n + 1 end return n end)()))
end
function S2L.cpPairs(t)                              -- "sidxN,sidxN" of a count table
    local r = {}
    for sid, n in pairs(t or {}) do if n > 0 then r[#r + 1] = sid .. "x" .. n end end
    table.sort(r)
    return table.concat(r, ",")
end
function S2L.cpHand(pw)                              -- SID of the weapon in the main hand and the rounds in it (nil when unknown)
    local sid, n
    pcall(function()
        local d = StaticFindObject("/Script/Stalker2.Default__CppMediator"):GetPlayerItemInHandsData()
        local v = d.MainHandItemPrototypeID
        if type(v) ~= "string" then v = v:ToString() end
        if v ~= "" and v ~= "None" then sid = v end
    end)
    if sid then pcall(function() local pc = FindFirstOf("PC"); local c = pc:GetAmmoCountInWeapon(); if type(c) == "number" then n = c end end) end
    return sid, n
end
-- equip ledger: once a second, only when this pawn's restore is finished and the player is not exchanging items with something
function S2L.cpEquipTick(ST, C, pw, now)
    if FL.cpequip ~= 1 or now < (C.eqAt or 0) then return end
    C.eqAt = now + 1
    S2L.cpTables(C)
    C.eq = C.eq or {}; C.loaded = C.loaded or {}
    local busy = (ST.lock and ST.lock.mine ~= nil) or (ST.tr and ST.tr.open ~= nil) or (ST.ss and ST.ss.open ~= nil) or #((ST.it and ST.it.run) or {}) > 0
    if not busy then pcall(function() local a = pw:GetInteractedActor(); if a and a:IsValid() then busy = true end end) end
    local med = StaticFindObject("/Script/Stalker2.Default__CppMediator")
    local cur = {}
    for _, sid in ipairs(C.eqList) do
        local ok, n = pcall(function() return med:GetItemsCountInInventory(pw, sid) end)
        cur[sid] = (ok and type(n) == "number") and n or 0
    end
    if C.eqPrev and not busy and not C.eqBusy then
        for _, sid in ipairs(C.eqList) do
            local d = cur[sid] - (C.eqPrev[sid] or 0)
            if d < 0 then C.eq[sid] = (C.eq[sid] or 0) - d                                    -- left the inventory with no exchange: into a slot
            elseif d > 0 and (C.eq[sid] or 0) > 0 then C.eq[sid] = math.max(0, C.eq[sid] - d) end   -- came back out of a slot
        end
    end
    C.eqPrev = cur; C.eqBusy = busy                    -- the tick right after an exchange is not judged either
    local hs, hn = S2L.cpHand(pw)
    if hs and hn and C.eqTab[hs] and C.eqTab[hs].max then C.loaded[hs] = math.min(hn, C.eqTab[hs].max) end
    if hs and C.eqTab[hs] and cur[hs] == 0 and (C.eq[hs] or 0) == 0 then      -- batch 21: in my hand, not counted, not in the ledger: it is a slot item
        C.eq[hs] = 1; log("S2CORE persist: " .. hs .. " esta na mao e nao e contado -> entra no livro de equipados")
    end
end
function M.cpRead()
    local f = io.open(S2L.cpFile(), "r"); if not f then return nil end
    local S = {items = {}}
    for ln in f:lines() do
        local k, v = ln:match("^(%w+)=(.*)$")
        if k == "items" or k == "equip" or k == "loaded" then
            S[k] = S[k] or {}
            for sid, n in v:gmatch("([%w_]+)x(%d+)") do S[k][sid] = tonumber(n) end
        elseif k then S[k] = v end
    end
    f:close()
    if S.v ~= "1" or S.done ~= "1" then return nil end        -- another format or a half-written file: ignored
    S.money = tonumber(S.money) or 0; S.hp = tonumber(S.hp) or 0
    return S
end
function S2L.cpWrite(ST, pw, why)
    local C = ST.cp
    local items, money = invSnap(pw)
    local hp, q = 0, nil
    pcall(function() hp = pw:GetHP() end); pcall(function() q = pw:K2_GetActorLocation() end)
    if type(hp) ~= "number" or hp <= 0 then return false end   -- never the state of a dead player
    local t = {}
    for sid, n in pairs(items) do t[#t + 1] = sid .. "x" .. n end
    table.sort(t)
    local f = io.open(S2L.cpFile() .. ".tmp", "w"); if not f then return false end
    f:write(string.format("v=1\nt=%d\nmoney=%d\nhp=%.0f\npos=%.0f,%.0f,%.0f\nitems=%s\nequip=%s\nloaded=%s\ndone=1\n", os.time(), money or 0, hp, q and q.X or 0, q and q.Y or 0, q and q.Z or 0, table.concat(t, ","),
        FL.cpequip == 1 and S2L.cpPairs(C.eq) or "", FL.cpequip == 1 and S2L.cpPairs(C.loaded) or ""))
    f:close()
    os.remove(S2L.cpFile()); os.rename(S2L.cpFile() .. ".tmp", S2L.cpFile())
    C.saves = (C.saves or 0) + 1; C.saveAt = os.clock() + FL.cpsecs
    if C.saves <= 3 or why ~= "tempo" then log(string.format("S2CORE persist: gravado (%s): %d tipos de item, dinheiro %d, hp %.0f", why, #t, money or 0, hp)) end
    return true
end
function M.cpFlush(ST, why)                            -- save now, if this pawn's restore is finished
    local C = ST.cp
    if not (C and cpOn() and C.stage == "ready") or (ST.nd and ST.nd.dead) then return false end
    local pw = S2L.cpPawn(); if not valid(pw) then return false end
    return S2L.cpWrite(ST, pw, why)
end
local function cpTick(ST)
    if not cpOn() then return end
    ST.cp = ST.cp or {}
    local C, now = ST.cp, os.clock()
    local pw = S2L.cpPawn(); if not valid(pw) then return end
    local addr = pw:GetAddress()
    if addr ~= C.addr then                             -- new local pawn: new-game load, or the load after a death
        C.addr = addr; C.stage = "wait"; C.t = now + FL.cpdelay; C.give = nil
        log("S2CORE persist: pawn novo; restauro em " .. tostring(FL.cpdelay) .. " s")
        return
    end
    if C.stage == "wait" then
        if now < C.t then return end
        local S = M.cpRead()
        if not S then S2L.cpTables(C); C.eq = {}; C.eqPrev = nil; for sid in pairs(C.base) do C.eq[sid] = 1 end end   -- batch 19: first session: the worn new-game kit starts in the ledger
        if not S then C.stage = "ready"; C.saveAt = now + 2; log("S2CORE persist: sem client_state.txt valido; o estado atual passa a ser o gravado"); return end
        local cur, money = invSnap(pw)
        local give, nGive, nTake = {}, 0, 0
        -- batch 19: what was in a slot comes back into the INVENTORY; one unit of a worn new-game kit item is already on the pawn
        S2L.cpTables(C)
        local want, eq = {}, {}
        for sid, n in pairs(S.items) do want[sid] = n end
        if FL.cpequip == 1 then
            for sid, n in pairs(S.equip or {}) do want[sid] = (want[sid] or 0) + n end
            for sid in pairs(C.base) do                      -- batch 21: the new pawn ALWAYS wears its base kit (a slot item reads count 0, so the file may not know it)
                eq[sid] = 1
                if (want[sid] or 0) >= 1 then want[sid] = want[sid] - 1 end
            end
        end
        S.items = want; C.eq = eq; C.eqPrev = nil; C.loaded = {}
        for sid, n in pairs(S.loaded or {}) do C.loaded[sid] = n end
        C.ammoFix = {}
        for _, sid in ipairs(LOOT_SIDS) do
            local d = (S.items[sid] or 0) - (cur[sid] or 0)
            if d > 0 then give[#give + 1] = {sid, d}; nGive = nGive + d
            elseif d < 0 then nTake = nTake - d; pcall(M.itemRemove, ST, "P", sid, -d) end      -- batch 9 player nodes (needs the Player pak)
        end
        local dm = S.money - (money or 0)
        if dm ~= 0 then pcall(function() pw:ModifyMoney(dm) end) end
        if S.hp > 0 then pcall(function() pw:ForceSetHP(S.hp) end) end
        C.give = give; C.stage = "apply"; C.plan = {give = nGive, take = nTake, money = dm, want = S}
        log(string.format("S2CORE persist: restaurando: +%d itens (%d tipos), -%d itens do kit, dinheiro %+d, hp %.0f, posicao gravada %s (a coleira leva ao host)", nGive, #give, nTake, dm, S.hp, tostring(S.pos)))
        return
    end
    if C.stage == "apply" then
        local mgr = FindFirstOf("CustomConsoleManagerRK")
        for _ = 1, FL.cpper do
            local g = table.remove(C.give, 1); if not g then break end
            local ok, e = pcall(function() mgr:XCreateItemInInventoryByID(g[1], 0, g[2], 1.0) end)
            if not ok then log("S2CORE persist: XCreateItemInInventoryByID " .. g[1] .. " erro " .. tostring(e)) end
        end
        if #C.give == 0 and #((ST.it and ST.it.run) or {}) == 0 then
            C.trimmed = nil                                 -- batch 21: the surplus is trimmed in the next stage, once the creations are counted
            C.stage = "verify0"; C.t = now + 1
        end
        return
    end
    if C.stage == "verify0" then                               -- wait for the ammo take-back nodes
        if now >= C.t and #((ST.it and ST.it.run) or {}) == 0 then
            if FL.cpammo == 1 and not C.trimmed then          -- batch 21: a created weapon brings counted rounds (measured +2 A012D per GunTOZ_SG)
                C.trimmed = true
                local cur2, fixed = invSnap(pw), {}
                for _, sid in ipairs(LOOT_SIDS) do
                    local ex = (cur2[sid] or 0) - (C.plan.want.items[sid] or 0)
                    if ex > 0 then pcall(M.itemRemove, ST, "P", sid, ex); fixed[#fixed + 1] = sid .. " -" .. ex end
                end
                if #fixed > 0 then log("S2CORE persist: excedente depois das criacoes tirado: " .. table.concat(fixed, ", ")); C.t = now + 1; return end
            end
            C.stage = "verify"; C.t = now + 3
        end
        return
    end
    if C.stage == "verify" then
        if now < C.t then return end
        local cur, money = invSnap(pw)
        local bad, list = 0, {}
        for _, sid in ipairs(LOOT_SIDS) do
            local a, b = cur[sid] or 0, C.plan.want.items[sid] or 0
            if a ~= b then bad = bad + 1; if #list < 6 then list[#list + 1] = string.format("%s %d/%d", sid, a, b) end end
        end
        log(string.format("S2CORE persist: RESTAURADO: +%d -%d itens, dinheiro %d (gravado %d), %d tipos diferentes do gravado %s",
            C.plan.give, C.plan.take, money or 0, C.plan.want.money, bad, table.concat(list, ", ")))
        C.stage = "ready"; C.saveAt = now + FL.cpsecs; C.plan = nil
        return
    end
    S2L.cpEquipTick(ST, C, pw, now)
    -- ready: periodic save, and one when the other side goes silent (session end)
    local st = ST.peer and ST.peer.state
    if st == "silent" and C.peerWas == "up" then M.cpFlush(ST, "outro lado em silencio") end
    C.peerWas = st
    if now >= (C.saveAt or 0) then M.cpFlush(ST, "tempo"); C.saveAt = now + FL.cpsecs end
end

-- ===== batch 13 (2): client death without a save load =====
local function ndOn() return FL.nodeath == 1 and (api.ROLE == "client" or FL.ndtest == 1) end
function M.ndRelease(ST, why)                          -- undo everything nodeath holds on the pawn
    local N = ST.nd; if not N then return end
    local pw = S2L.cpPawn()
    if N.leashSave then FL.leashm = N.leashSave; N.leashSave = nil end
    S2L.lrDrop(ST, "nodeath", why)                     -- b115leash
    if valid(pw) then
        if N.dead and N.dead.move then pcall(function() pw.CharacterMovement:SetMovementMode(1, 0) end) end
        if N.dead then pcall(function() if pw:GetHP() < FL.ndresphp then pw:ForceSetHP(FL.ndresphp) end end) end   -- never leave it at 0 HP and killable
        if N.armed then pcall(function() FindFirstOf("CustomConsoleManagerBuild"):XSetEntityUnkillableByUID(N.uid, false) end) end
    end
    if N.dead or N.armed then log("S2CORE nodeath: desligado (" .. tostring(why) .. ")") end
    N.dead = nil; N.armed = nil; N.addr = nil
end
local function ndTick(ST)
    ST.nd = ST.nd or {deaths = 0}
    local N, D, now = ST.nd, ST.din, os.clock()
    if not ndOn() then if N.armed or N.dead or N.leashSave then M.ndRelease(ST, "nodeath desligado") end return end
    if N.leashSave and now > (N.leashUntil or 0) then FL.leashm = N.leashSave; N.leashSave = nil; log("S2CORE nodeath: coleira de volta a " .. tostring(FL.leashm) .. " m") end
    local pw = S2L.cpPawn(); if not valid(pw) then return end
    local addr = pw:GetAddress()
    if addr ~= N.addr then
        N.addr = addr; N.armed = nil; N.dead = nil; N.armAt = now + 5                              -- new pawn: arm again
        N.newAt = now; N.emStopped = nil
        N.graceUntil = now + FL.ndgrace; N.graceHp = nil                                           -- batch 20: nothing kills a pawn in its first seconds (Unkillable is not armed yet)
    end
    if N.newAt and now - N.newAt < 20 and not N.emStopped then                                     -- batch 20: the save was made inside an emission and the host has none now
        local em, host = false, (x1part("E") or ""):match("^(%d)")
        pcall(function() em = StaticFindObject("/Script/Stalker2.Default__CppMediator"):IsEmissionActive() == true end)
        if em and host == "0" then
            N.emStopped = true
            local ok = pcall(function() FindFirstOf("CustomConsoleManagerSM"):XStopEmission() end)
            log("S2CORE nodeath: pawn novo dentro de uma emissao do save e o host sem emissao -> XStopEmission ok=" .. tostring(ok))
        end
    end
    if now >= (N.armAt or 0) then                      -- core-24c: re-asserted every 5 s (measured: set 5 s after a load it did not hold; set again later it did)
        N.armAt = now + 5
        local ok, e = pcall(function()
            N.uid = StaticFindObject("/Script/Stalker2.Default__CppMediator"):GetGUID(pw)
            FindFirstOf("CustomConsoleManagerBuild"):XSetEntityUnkillableByUID(N.uid, true)
        end)
        if ok then N.armed = true end
        if not N.armLogged or not ok then N.armLogged = true; log("S2CORE nodeath: Unkillable no jogador local uid=" .. tostring(N.uid) .. " ok=" .. tostring(ok) .. " " .. (ok and "" or tostring(e))) end
    end
    local hp, alive = nil, true
    pcall(function() hp = pw:GetHP() end); pcall(function() alive = pw:IsAlive() and true or false end)
    if type(hp) ~= "number" then return end
    if N.dead then
        if not alive then N.dead = nil; log("S2CORE nodeath: o jogo matou o jogador mesmo assim; segue a tela do jogo"); return end
        if hp ~= 1 then pcall(function() pw:ForceSetHP(1) end) end                       -- held at 1 HP, hurt by nothing
        pcall(function() local b = pw:GetBleeding(); if type(b) == "number" and b > 0 then pw:ForceSetBleeding(0.0) end end)
        D.protectUntil = math.max(D.protectUntil or 0, now + 1); D.downUntil = math.max(D.downUntil or 0, now + 1)   -- the host sees "w" and stops hitting the body
        if now - N.dead.at < FL.ndwait then return end
        local em = false                                       -- batch 19: an emission in the open would kill again at once: stay held until it is over
        pcall(function() em = StaticFindObject("/Script/Stalker2.Default__CppMediator"):IsEmissionActive() == true end)
        if em then
            if not N.dead.emLog then N.dead.emLog = true; log("S2CORE nodeath: emissao ativa; o cliente fica caido e protegido ate ela acabar") end
            return
        end
        -- respawn
        local loss = 0
        if FL.ndmoney > 0 then pcall(function() loss = math.floor(pw:GetMoney() * FL.ndmoney / 100); if loss > 0 then pw:ModifyMoney(-loss) end end) end
        pcall(function() pw:ForceSetHP(FL.ndresphp) end)
        if N.dead.move and not (ST.cv and ST.cv.fz) then pcall(function() pw.CharacterMovement:SetMovementMode(1, 0) end) end
        D.downUntil = 0; D.protectUntil = now + math.max(FL.dmgspawn, FL.ndgrace)
        N.graceUntil = now + FL.ndgrace; N.graceHp = FL.ndresphp      -- batch 19: nothing kills during the grace
        local far = "sem host"
        local r = S2L.remoteIn("ndleash")
        if r and now - (r.recv or 0) < 2 then
            local q = pw:K2_GetActorLocation()
            local d = math.sqrt((r.x - q.X) ^ 2 + (r.y - q.Y) ^ 2)
            far = string.format("host a %.0f m", d / 100)
            if (FL.leashown or 0) == 1 then                     -- b115leash: a 6 s pull request (refused while another feature pulls)
                if d > FL.ndnear then far = far .. (S2L.lrPull(ST, "nodeath", FL.ndleash, 6, "respawn") and " -> teleporte pela coleira" or " -> sem puxao (coleira ocupada)") end
            elseif d > FL.ndnear and not N.leashSave and not (ST.cv and ST.cv.leashSave) then
                N.leashSave = FL.leashm; FL.leashm = FL.ndleash; N.leashUntil = now + 6; far = far .. " -> teleporte pela coleira"
            end
        end
        log(string.format("S2CORE nodeath: RESPAWN #%d depois de %.1f s: hp %d, dinheiro -%d, %s", N.deaths, now - N.dead.at, FL.ndresphp, loss, far))
        N.dead = nil
        return
    end
    if N.graceUntil then                                       -- batch 19: grace after a respawn: HP held, no new "death"
        if now < N.graceUntil then
            if not N.graceHp then N.graceHp = hp > 0 and hp or FL.ndresphp end       -- batch 20: grace of a new pawn: hold the HP it arrived with
            if hp < N.graceHp then pcall(function() pw:ForceSetHP(N.graceHp) end) end
            D.protectUntil = math.max(D.protectUntil or 0, now + 1)
            return
        end
        N.graceUntil = nil
    end
    if (alive or N.armed) and hp <= FL.ndhp then      -- core-24c: with Unkillable a lethal hit reads HP 0 and IsAlive false within 100 ms, yet ForceSetHP brings the pawn back (measured)
        N.deaths = N.deaths + 1
        N.dead = {at = now}
        N.dead.move = pcall(function() pw.CharacterMovement:DisableMovement() end)
        pcall(function() pw:ForceSetHP(1) end)
        log(string.format("S2CORE nodeath: MORTE #%d (hp %.1f, Unkillable=%s); respawn em %d s", N.deaths, hp, tostring(N.armed == true), FL.ndwait))
    end
end

-- ===== batch 14: emission sync (#E) =====
function S2L.emMed() return StaticFindObject("/Script/Stalker2.Default__CppMediator") end
function S2L.emActive()
    local on = nil
    pcall(function() on = S2L.emMed():IsEmissionActive() == true end)
    return on
end
function M.emRelease(ST, why)                          -- give the client its own schedule back
    local E = ST.em; if not (E and E.paused) then return end
    local ok = pcall(function() FindFirstOf("CustomConsoleManagerSM"):XResumeScheduledEmission() end)
    E.paused = nil; E.addr = nil
    log("S2CORE emission: agenda propria de volta (" .. tostring(why) .. ") ok=" .. tostring(ok))
end
function S2L.emOut(ST)
    local E = ST.em
    if FL.emsync ~= 1 or api.ROLE ~= "host" or not E or E.on == nil then return "" end
    return string.format("#E:%d,%d", E.on and 1 or 0, E.n or 0)
end
function S2L.emReset(why)                          -- batch 35: the game has no countdown for Lua; pause again and ask for its state around it
    local sm = FindFirstOf("CustomConsoleManagerSM")
    local a = tostring(pcall(function() sm:XGetEmissionState() end))
    local p = tostring(pcall(function() sm:XPauseScheduledEmission() end))
    local b = tostring(pcall(function() sm:XGetEmissionState() end))
    log("S2CORE emission: agenda propria pausada de novo (" .. why .. "): XGetEmissionState antes ok=" .. a .. ", XPauseScheduledEmission ok=" .. p .. ", XGetEmissionState depois ok=" .. b .. " (a saida do estado vai para o console do jogo)")
    return p == "true"
end
local function emTick(ST)
    ST.em = ST.em or {n = 0}
    local E, now = ST.em, os.clock()
    if FL.emsync ~= 1 then if E.paused then M.emRelease(ST, "emsync desligado") end return end
    if now < (E.at or 0) then return end
    E.at = now + 1
    local host = api.ROLE == "host"
    if host then
        local on = S2L.emActive()
        if on ~= nil and on ~= E.on then
            if on then E.n = E.n + 1 end
            if E.on ~= nil or on then log("S2CORE emission: host " .. (on and ("EMISSAO #" .. E.n .. " comecou") or "emissao terminou")) end
            E.on = on
        end
        if FL.emtest ~= 1 then return end
    end
    -- client side
    local pw = S2.io and S2.io.pawn
    if not valid(pw) then pcall(function() pw = api.UEHelpers.GetPlayerController().Pawn end) end   -- no snapshot of the other side yet (solo)
    if not valid(pw) then return end
    local addr = pw:GetAddress()
    if addr ~= E.addr then                              -- new pawn (load): the game's schedule runs again, pause it again
        E.addr = addr
        E.stopAt = 0; E.stops = 0; E.offAt = now - FL.emgrace    -- batch 20/32: a save loaded inside an emission: stop it at once if the host has none
        E.newAt = now; E.gs0 = tonumber(rawget(_G, "S2_gameSecs")); E.ran = nil   -- batch 39: the load case
        if FL.emreset == 1 then E.paused = S2L.emReset("pawn novo") else
        E.paused = pcall(function() FindFirstOf("CustomConsoleManagerSM"):XPauseScheduledEmission() end) end
        log("S2CORE emission: agenda propria de emissoes em pausa ok=" .. tostring(E.paused))
    end
    local c, n
    if host then c, n = E.on and "1" or "0", E.n else c, n = (x1part("E") or ""):match("^(%d),(%d+)") end
    do                                                   -- batch 33: protection, decided before anything else (also with no host)
        local m = S2L.emActive()
        if m then E.calmAt = now + FL.emprotect end      -- IsEmissionActive goes false long before the lethal wave (measured 46-80 s)
        E.noSave = (m == true) or now < (E.calmAt or 0) or c == "1"   -- no client save that could bring an emission back on load (the host's lead-in counts)
        if c == "1" then E.hostOnAt = now end            -- batch 39: skew tolerance
        if m then E.mineOn = E.mineOn or now else E.mineOn = nil end
        if E.newAt and not E.ran then                    -- the world of a new pawn starts running when the game clock moves
            local gs = tonumber(rawget(_G, "S2_gameSecs"))
            if gs ~= nil and E.gs0 ~= nil and gs ~= E.gs0 then E.ran = true else E.newAt = now end
            if gs ~= nil and E.gs0 == nil then E.gs0 = gs end
        end
        local loadCase = m and E.newAt ~= nil and now - E.newAt <= 10
        local alone = m and c ~= "1" and E.mineOn ~= nil and now - math.max(E.mineOn, E.hostOnAt or -1e9) > FL.emskew
        if (loadCase or alone) and not E.armed and not host then
            E.armed = true
            log(string.format("S2CORE emission: protecao armada (%s)", loadCase and "save carregado dentro de emissao" or ("minha emissao ha mais de " .. FL.emskew .. " s sem a do host")))
        end
        if E.armed and not m and now >= (E.calmAt or 0) then E.armed = nil end
        local want = E.armed and c ~= "1" and not host   -- the host has no emission (or no host): its wave must not kill the client
        if want then
            if not E.god or now >= (E.godAt or 0) then
                E.godAt = now + 5
                local ok = pcall(function()
                    local uid = StaticFindObject("/Script/Stalker2.Default__CppMediator"):GetGUID(pw)
                    FindFirstOf("CustomConsoleManagerBuild"):XSetEntityUnkillableByUID(uid, true); E.god = uid
                end)
                if not E.godLogged then E.godLogged = true; log(string.format("S2CORE emission: minha emissao sem a do host -> cliente PROTEGIDO (Unkillable ok=%s, sem saves) ate %d s depois do fim", tostring(ok), FL.emprotect)) end
            end
            local hp; pcall(function() hp = pw:GetHP() end)
            if type(hp) == "number" and hp > 0 then
                if not E.hp or hp > E.hp then E.hp = hp end
                if hp < E.hp then pcall(function() pw:ForceSetHP(E.hp) end) end
            end
        elseif E.god or E.hp then
            local keep = ST.cv and ST.cv.fz ~= nil          -- the cinematic freeze holds its own protection: leave the flag to it
            if E.god ~= nil and not keep then pcall(function() FindFirstOf("CustomConsoleManagerBuild"):XSetEntityUnkillableByUID(E.god, false) end) end
            E.god = nil; E.hp = nil; E.godLogged = nil
            log("S2CORE emission: protecao do cliente terminada")
        end
    end
    if not c then return end                             -- the host does not send (older module, emsync off there, silent)
    local ron = c == "1"
    local mine = S2L.emActive()
    if ron and not E.ron and FL.emreset == 1 and api.ROLE == "client" then S2L.emReset("emissao do host #" .. tostring(n)) end
    if ron and not E.ron then E.hk = (E.hk or 0) + 1; E.hostSince = now; E.seenK = nil end   -- batch 40: one key per host emission (rising edge)
    if ron and mine then E.seenK = E.hk end
    if ron and E.hostSince and now - E.hostSince >= FL.emwait and E.chkK ~= E.hk and mine ~= nil then
        E.chkK = E.hk
        local wx; pcall(function() wx = FindFirstOf("WeatherController").CurrentWeatherData.TargetWeather end)
        log(string.format("S2CORE emission: host em emissao ha %s s; a minha %s (alvo do clima %s)", tostring(FL.emwait),
            mine and "ativa" or (E.seenK == E.hk and "inativa agora, ativa antes" or "inativa"), tostring(wx)))
        if FL.emstart == 2 and not mine and E.seenK ~= E.hk and api.ROLE == "client" then   -- the weather did not start it: ONE start, never a retry
            local ok, e = pcall(function() FindFirstOf("CustomConsoleManagerSM"):XStartEmission() end)
            log("S2CORE emission: a minha nao comecou com o clima -> XStartEmission (uma vez nesta emissao do host) ok=" .. tostring(ok) .. " " .. (ok and "" or tostring(e)))
        end
    end
    if ron ~= (E.ron or false) then log("S2CORE emission: host " .. (ron and "em emissao" or "sem emissao") .. "; a minha " .. (mine and "ativa" or "inativa")) end
    if mine ~= nil then                                  -- batch 32: no start by the mod, one stop per emission
        if ron then
            E.offAt = nil; E.stops = 0
            if FL.emstart == 1 and not mine and E.startedN ~= n then      -- fallback: ONE call per host emission number, never a retry
                E.startedN = n
                local ok, e = pcall(function() FindFirstOf("CustomConsoleManagerSM"):XStartEmission() end)
                log("S2CORE emission: host em emissao #" .. tostring(n) .. " -> XStartEmission (uma vez) ok=" .. tostring(ok) .. " " .. (ok and "" or tostring(e)))
            end
        elseif mine then                                 -- the host has none and mine runs (a loaded save, the game's own weather, the tail of the host's)
            E.offAt = E.offAt or now
            if now - E.offAt >= FL.emgrace and now >= (E.stopAt or 0) and (E.stops or 0) < 3 then
                E.stops = (E.stops or 0) + 1; E.stopAt = now + FL.emstopgap
                local rwx = tonumber(((S2.io and S2.io.head or ""):match('"wx"%s*:%s*(%-?%d+)')))
                local okW = "-"
                if rwx and rwx >= 0 and rwx <= 9 and rwx ~= 7 then      -- emission weather alone restarts an emission: put the host's weather first
                    okW = tostring(pcall(function() FindFirstOf("CustomConsoleManagerDR"):XSwitchToWeather(rwx) end))
                end
                local ok = pcall(function() FindFirstOf("CustomConsoleManagerSM"):XStopEmission() end)
                log(string.format("S2CORE emission: o host nao esta em emissao e a minha esta ativa ha %.0f s -> clima do host %s ok=%s, XStopEmission ok=%s (parada %d de 3)", now - E.offAt, tostring(rwx), okW, tostring(ok), E.stops))
            end
        else E.offAt = nil; E.stops = 0 end
    end
    E.ron = ron
end

local B73 = {}                                         -- batch 73 functions (defined before M.snapExtra)
-- ===== batch 17 (a): one stash shared by both players (#Y, #YL) =====
local function ssOps(led, ops)                         -- "+sidxN.-sidxN" applied to a count table
    for sg, sid, n in ops:gmatch("([%+%-])([%w_]+)x(%d+)") do
        local v = (led[sid] or 0) + (sg == "+" and tonumber(n) or -tonumber(n))
        led[sid] = v > 0 and v or nil
    end
end
local function ssText(led)
    local t = {}
    for sid, n in pairs(led) do t[#t + 1] = sid .. "x" .. n end
    table.sort(t)
    return table.concat(t, ",")
end
local function ssRewrite(S)
    local f = io.open(api.BR .. "stash_ledger.txt", "w"); if not f then return end
    for _, h in ipairs(S.hist) do f:write(string.format("%.1f\t%s\n", h.t, h.op)) end
    f:close()
end
local function ssInit(ST)
    if ST.ss then return ST.ss end
    ST.ss = {led = {}, seq = 0, out = {}, seen = {}, hist = {}}
    local S = ST.ss
    if api.ROLE == "host" then
        local f = io.open(api.BR .. "stash_ledger.txt", "r")
        if f then
            for ln in f:lines() do
                local t, op = ln:match("^([%d%.]+)\t(.+)$")
                if t then S.hist[#S.hist + 1] = {t = tonumber(t), op = op}; ssOps(S.led, op) end
            end
            f:close()
        end
        log("S2CORE stash: livro do host carregado, " .. #S.hist .. " operacoes")
    end
    return S
end
local function ssRecord(S, op)                         -- the host's file is the record
    if api.ROLE ~= "host" then return end
    local gt = tonumber(rawget(_G, "S2_gameSecs")) or 0
    S.hist[#S.hist + 1] = {t = gt, op = op}
    local f = io.open(api.BR .. "stash_ledger.txt", "a"); if f then f:write(string.format("%.1f\t%s\n", gt, op)); f:close() end
    S.yl = nil
end
local function ssQueue(ST, prefix, sid, n)             -- bits of n as node starts (S2U_SA_ / S2U_SR_)
    local I = itInit(ST)
    n = math.floor(n)
    while n > 0 do
        local part = math.min(n, 1023); n = n - part
        for k = 9, 0, -1 do if part >= 2 ^ k then part = part - 2 ^ k; I.run[#I.run + 1] = string.format("%s%s_%d", prefix, sid, 2 ^ k) end end
    end
end
function M.stashNote(ST, ops)                          -- by hand (luacmd): record "+sidxN.-sidxN" in the ledger, e.g. for items put in with a node
    local S = ssInit(ST)
    ssOps(S.led, ops); ssRecord(S, ops)
    S.seq = S.seq + 1; S.out[#S.out + 1] = {t = os.clock(), s = S.seq .. "," .. ops}
    log("S2CORE stash: anotado a mao: " .. tostring(ops))
    return ssText(S.led)
end
local function ssOut(ST)
    if FL.sstash ~= 1 then return "" end
    local S, now, keep, parts = ssInit(ST), os.clock(), {}, {}
    for _, e in ipairs(S.out) do if now - e.t < 2 then keep[#keep + 1] = e; parts[#parts + 1] = e.s end end
    S.out = keep
    local r = #parts > 0 and ("#Y:" .. table.concat(parts, "|")) or ""
    if api.ROLE == "host" and api.lookPut then          -- the whole ledger, as look texts (1,000-byte pieces)
        if not S.yl then
            local txt, chunks = ssText(S.led), {}
            while #txt > 1000 do
                local cut = txt:sub(1, 1000):match("^.*(),") or 1000
                chunks[#chunks + 1] = txt:sub(1, cut - 1); txt = txt:sub(cut + 1)
            end
            chunks[#chunks + 1] = txt
            S.yl = {chunks = chunks, hashes = {}, rr = 0}
            for i, c in ipairs(chunks) do S.yl.hashes[i] = api.lookPut("YL" .. i .. "~" .. c) end
        end
        local Y = S.yl
        Y.rr = Y.rr % #Y.chunks + 1; api.lookPut("YL" .. Y.rr .. "~" .. Y.chunks[Y.rr])     -- keep one piece "in use" per tick
        r = r .. "#YL:" .. #Y.chunks .. "," .. table.concat(Y.hashes, ".")
    end
    return r
end
local function ssTick(ST)
    if FL.sstash ~= 1 then return end
    local S, now = ssInit(ST), os.clock()
    local pw = S2.io and S2.io.pawn
    if not valid(pw) then pcall(function() pw = api.UEHelpers.GetPlayerController().Pawn end) end
    if not valid(pw) then return end
    local addr = pw:GetAddress()
    if addr ~= S.addr then S.addr = addr; S.addrAt = now; S.addrN = (S.addrN or 0) + 1; S.open = nil end
    -- host: an older save was loaded -> cut the ledger back to the game clock (same two signs as the quest log)
    if api.ROLE == "host" and #S.hist > 0 and now >= (S.rbAt or 0) then
        S.rbAt = now + 5
        local gt = tonumber(rawget(_G, "S2_gameSecs"))
        local last = S.hist[#S.hist].t
        local newPawn = S.addrN > 1 and S.rbAddr ~= addr and now - S.addrAt >= 12
        if gt and (last > gt + 60 or (newPawn and last > gt + 1)) then
            local keep = {}; S.led = {}
            for _, h in ipairs(S.hist) do if h.t <= gt then keep[#keep + 1] = h; ssOps(S.led, h.op) end end
            log(string.format("S2CORE stash: save mais antigo no host: %d operacoes do livro descartadas", #S.hist - #keep))
            S.hist = keep; ssRewrite(S); S.yl = nil
        end
        if now - S.addrAt >= 12 then S.rbAddr = addr end
    end
    -- my own use of the box: inventory before / after, as in ptake
    local mine = ST.lock and ST.lock.mine
    local isBox = type(mine) == "string" and mine:find("PlayerStash", 1, true) ~= nil
    if isBox and not S.open then
        S.open = {id = mine, snap = invSnap(pw)}
        pcall(function()                                   -- batch 21: the box's Guid property (measured readable; a property read, not an inventory call)
            local g = pw:GetInteractedActor().Guid
            S.open.guid = string.format("%08X%08X%08X%08X", g.A & 0xFFFFFFFF, g.B & 0xFFFFFFFF, g.C & 0xFFFFFFFF, g.D & 0xFFFFFFFF)
        end)
        log("S2CORE stash: caixa aberta; inventario anotado")
    elseif S.open and mine ~= S.open.id then
        local after, ops, ghost = invSnap(pw), {}, 0
        for _, sid in ipairs(LOOT_SIDS) do
            local d = (after[sid] or 0) - (S.open.snap[sid] or 0)
            if d ~= 0 and FL.ssbox == 1 then B73.boxAdd(S, sid, -d) end   -- batch 73: taken out (d > 0) or put in (d < 0) of MY box
            if d > 0 then                                -- taken out of the box
                local have = S.led[sid] or 0
                if d > have then
                    if api.ROLE ~= "host" or FL.ssstrict == 1 then pcall(M.itemRemove, ST, "P", sid, d - have); ghost = ghost + (d - have) end   -- a ghost: the other player already took it
                    d = have
                end
                if d > 0 then ops[#ops + 1] = "-" .. sid .. "x" .. d end
            elseif d < 0 then ops[#ops + 1] = "+" .. sid .. "x" .. (-d) end
        end
        local opened = S.open; S.open = nil
        if #ops > 0 then
            S.open = opened
            local op = table.concat(ops, ".")
            ssOps(S.led, op); ssRecord(S, op)
            S.seq = S.seq + 1; S.out[#S.out + 1] = {t = now, s = S.seq .. "," .. op .. (S.open.guid and (".@" .. S.open.guid) or "")}
            S.open = nil
        end
        log(string.format("S2CORE stash: caixa fechada: %s%s", #ops > 0 and table.concat(ops, " ") or "nada mudou", ghost > 0 and (" | " .. ghost .. " itens fantasma tirados de mim") or ""))
    end
    -- the other player's use of the box
    if now - (S.seen.pruneAt or -10) >= 1 then
        S.seen.pruneAt = now
        for k, at in pairs(S.seen) do if type(k) == "number" and now - at > 5 then S.seen[k] = nil end end
    end
    local echo = ST.peer and ST.peer.loopback                -- batch 21: my own snapshot coming back is not "the other player"
    for ev in ((not echo and x1part("Y")) or ""):gmatch("[^|]+") do
        local seq, op = ev:match("^(%d+),(.+)$")
        seq = tonumber(seq)
        if seq then
            local was = S.seen[seq]; S.seen[seq] = now
            if not was then
                ssOps(S.led, op); ssRecord(S, (op:gsub("%.?@%x+", "")))      -- the ledger file keeps the operations, not the box Guid
                for sg, sid, n in op:gmatch("([%+%-])([%w_]+)x(%d+)") do
                    if sg == "+" then ssQueue(ST, "S2U_SA_", sid, tonumber(n)); if FL.ssbox == 1 then B73.boxAdd(S, sid, tonumber(n)) end
                    elseif FL.ssremove == 1 and op:match("@(%x+)") then                     -- batch 21: count-1 remove nodes of THAT box
                        local I, g = itInit(ST), op:match("@(%x+)")
                        for _ = 1, math.min(tonumber(n), FL.icmax or 1000) do I.run[#I.run + 1] = string.format("S2U_SR_%s_%s_1", g, sid) end
                    end
                end
                log("S2CORE stash: o outro jogador mexeu na caixa: " .. op .. (FL.ssremove == 1 and "" or " (retiradas so no livro)"))
            end
        end
    end
    -- client: ledger of the host, and the fill of my (new-game, empty) box copy once per pawn
    if api.ROLE ~= "host" and api.lookGet then
        local sg = x1part("YL")
        if sg and not S.open then
            if sg ~= S.ylSeen then
                local parts, miss = {}, 0
                for h in (sg:match("^%d+,(.+)$") or ""):gmatch("[^%.]+") do
                    local t = api.lookGet(h)
                    if t then parts[#parts + 1] = t else miss = miss + 1 end
                end
                if miss == 0 and #parts > 0 then
                    table.sort(parts, function(a, b) return tonumber(a:match("^YL(%d+)~")) < tonumber(b:match("^YL(%d+)~")) end)
                    local led = {}
                    for _, t in ipairs(parts) do for sid, n in t:gsub("^YL%d+~", ""):gmatch("([%w_]+)x(%d+)") do led[sid] = tonumber(n) end end
                    S.led = led; S.ylSeen = sg; S.ylOk = true
                end
            end
            if S.ylOk and S.fillAddr ~= addr and now - S.addrAt >= FL.ssdelay then
                S.fillAddr = addr
                local n, kinds = 0, 0
                local bx = (FL.ssbox == 1 and FL.csave == 1) and B73.boxLoad(S) or nil   -- batch 73: only ledger - believed box
                for sid, c in pairs(S.led) do
                    local d = bx and (c - (bx[sid] or 0)) or c
                    if d > 0 then ssQueue(ST, "S2U_SA_", sid, d); n = n + d; kinds = kinds + 1; if bx then B73.boxAdd(S, sid, d) end end
                end
                log(string.format("S2CORE stash: copia da caixa preenchida pelo livro do host: %d itens de %d tipos", n, kinds))
            end
        end
    end
end

-- ===== batch 22: artsync=2 = each PC keeps its OWN artifact spawns; a place where one player picked an artifact up gives the other
-- player nothing until the host's clock passes the spawner cooldown. Places = the artifact spawner placeholders of the kit
-- (<bridge>/s2u_artspawn.tsv: SID, prototype, x, y, z; same on both PCs). The host owns the list (<bridge>/art_taken.txt:
-- "<place number>\t<until game seconds>") and sends it as "#AL:<n>.<n>...". Every PC hides the Artifact actors inside a listed place
-- (hidden + collision off: measured to remove the prompt) and shows them again when the place leaves the list.
local function apInit(ST)
    if ST.ap2 then return ST.ap2 end
    ST.ap2 = {places = {}, taken = {}, track = {}, hid = {}, list = ""}
    local P = ST.ap2
    local f = io.open(api.BR .. "s2u_artspawn.tsv", "r")
    if f then
        for ln in f:lines() do
            local sid, x, y, z = ln:match("^(%x+)\t[^\t]*\t([%-%d%.]+)\t([%-%d%.]+)\t([%-%d%.]+)")
            if sid then P.places[#P.places + 1] = {sid = sid, x = tonumber(x), y = tonumber(y), z = tonumber(z)} end
        end
        f:close()
    end
    if api.ROLE == "host" then
        f = io.open(api.BR .. "art_taken.txt", "r")
        if f then for ln in f:lines() do local i, u = ln:match("^(%d+)\t([%-%d%.]+)"); if i then P.taken[tonumber(i)] = tonumber(u) end end f:close() end
    end
    log("S2CORE art2: " .. #P.places .. " lugares de artefato em s2u_artspawn.tsv")
    return P
end
local function apClock() return tonumber(rawget(_G, "S2_gameSecs")) or os.time() end
local function apSave(P)
    local f = io.open(api.BR .. "art_taken.txt", "w"); if not f then return end
    for i, u in pairs(P.taken) do f:write(string.format("%d\t%.0f\n", i, u)) end
    f:close()
end
local function apPlace(P, x, y, z)
    local best, bd = nil, FL.artrad ^ 2
    for i, p in ipairs(P.places) do
        local d = (p.x - x) ^ 2 + (p.y - y) ^ 2
        if d <= bd then best, bd = i, d end
    end
    return best
end
function M.artPlaceTaken(ST, i, who)                   -- host: place i was just emptied by a player
    if FL.artsync ~= 2 or api.ROLE ~= "host" or not i then return false end
    local P = apInit(ST)
    if not P.places[i] then return false end
    P.taken[i] = apClock() + FL.artcool; apSave(P)
    log(string.format("S2CORE art2: lugar %d (%s) esvaziado pelo %s; fechado para o outro jogador por %d s de jogo", i, P.places[i].sid:sub(1, 8), tostring(who), FL.artcool))
    return true
end
local function apOut(ST)
    local P = apInit(ST)
    if api.ROLE ~= "host" then return "" end
    local t = {}
    for i in pairs(P.taken) do t[#t + 1] = i end
    table.sort(t)
    return "#AL:" .. table.concat(t, ".")
end
-- ===== batch 47: a hidden artifact must not be sensed either. Round 37: the client's detector beeped at a closed place; its hidden copy
-- still had AkComponent, NiagaraComponent and PhysicsInteractionComponent active (ArtifactInteractableComponent was inactive).
local ART_QUIET = {"/Script/AkAudio.AkComponent", "/Script/Niagara.NiagaraComponent", "/Script/Stalker2.PhysicsInteractionComponent",
                   "/Script/Stalker2.ArtifactInteractableComponent"}   -- batch 50: the interaction component too (P-det step 3: the white dot)
-- batch 47 + 50 + 51: on = make the artifact at a closed place silent and inert where it lies (batch 51: never parked);
-- off = put it back on its spot and restore each component as it was. The actor itself is hidden/shown by apTick.
local function apSilence(P, id, o, on)
    if FL.artmute ~= 1 and not (P.away and P.away[id]) then return end
    P.away = P.away or {}; P.was = P.was or {}
    local n, moved = 0, ""
    if on then
        local was = {}
        for _, cp in ipairs(ART_QUIET) do
            if FL.artmute == 1 then
                local ok, list = pcall(api.compList, o, cp)
                was[cp] = {}
                for i, c in ipairs(ok and list or {}) do
                    local act = true; pcall(function() act = c:IsActive() and true or false end)
                    was[cp][i] = act
                    if pcall(function()
                        if cp:find("AkComponent", 1, true) then c:Stop() end
                        c:Deactivate(); c:SetActive(false, false); c:SetComponentTickEnabled(false)
                    end) then n = n + 1 end
                end
            end
        end
        P.was[id] = was
        if FL.artmute == 1 then pcall(function() o:SetActorTickEnabled(false) end) end   -- batch 51: inert in place, never parked (r42)
    else
        local p = P.away[id]
        if p then
            P.away[id] = nil
            pcall(function() o:K2_SetActorLocation({X = p.X, Y = p.Y, Z = p.Z}, false, {}, true) end)
            if p.rot then pcall(function() o:K2_SetActorRotation(p.rot, true) end) end
            if p.phys then pcall(function() o.MeshComponent:SetSimulatePhysics(true) end) end
            moved = "; de volta ao seu lugar"
        end
        pcall(function() o:SetActorTickEnabled(true) end)
        local was = P.was[id] or {}
        P.was[id] = nil
        for _, cp in ipairs(ART_QUIET) do
            local ok, list = pcall(api.compList, o, cp)
            for i, c in ipairs(ok and list or {}) do
                local act = (was[cp] == nil) and (not cp:find("ArtifactInteractable", 1, true)) or (was[cp] ~= nil and was[cp][i] == true)
                if pcall(function() c:SetComponentTickEnabled(true); if act then c:SetActive(true, false) end end) then n = n + 1 end
            end
        end
    end
    log(string.format("S2CORE art2: artefato %s %s (%d componentes%s)", id, on and "silenciado" or "reativado", n, moved))
end
local function apTick(ST)
    local P, now = apInit(ST), os.clock()
    if now < (P.at or 0) then return end
    P.at = now + 1
    local pw = S2.io and S2.io.pawn
    if not valid(pw) then pcall(function() pw = api.UEHelpers.GetPlayerController().Pawn end) end
    if not valid(pw) then return end
    local host = api.ROLE == "host"
    if host then                                        -- places whose cooldown is over are open again
        local gs, ch = apClock(), false
        for i, u in pairs(P.taken) do if gs >= u or u - gs > FL.artcool + 60 then P.taken[i] = nil; ch = true; log("S2CORE art2: lugar " .. i .. " aberto de novo") end end   -- also when the clock went back (older save)
        if ch then apSave(P) end
    else
        local sg = x1part("AL")
        if sg then                                      -- the host's list is the truth
            local t = {}
            for i in sg:gmatch("%d+") do t[tonumber(i)] = true end
            P.taken = t
        end
    end
    local live = {}
    local pp; pcall(function() pp = pw:K2_GetActorLocation() end)      -- batch 33: where I am, and did I just jump (teleport, load)?
    local paddr = pw:GetAddress()
    if pp then
        if paddr ~= P.paddr or (P.pp and ((pp.X - P.pp.X) ^ 2 + (pp.Y - P.pp.Y) ^ 2) > 5000 ^ 2) then P.quiet = now + FL.artquiet end
        P.pp = {X = pp.X, Y = pp.Y, Z = pp.Z}; P.paddr = paddr
    end
    for _, o in ipairs(FindAllOf("Artifact") or {}) do pcall(function()
        if not o:IsValid() then return end
        local id = o:GetFName():ToString()
        live[id] = true
        local q = o:K2_GetActorLocation()
        if P.away and P.away[id] then q = {X = P.away[id].X, Y = P.away[id].Y, Z = P.away[id].Z} end   -- batch 50: judged at its own spot
        local T = P.track[id]
        if math.abs(q.X) + math.abs(q.Y) < 1000 then      -- at the origin: picked up (measured), if it had a place before
            if T and T.place and not T.done then
                T.done = true
                local why = (now < (P.quiet or 0) and "teleporte ou save carregado ha pouco") or ((T.pd or 1e9) > FL.artreach and string.format("eu estava a %.0f m dele", (T.pd or 0) / 100)) or nil
                if why then log("S2CORE art2: artefato do lugar " .. T.place .. " sumiu, mas nao conta como pego (" .. why .. ")") end
                if not why and not P.hid[id] then        -- an artifact I could see and took
                    if host then M.artPlaceTaken(ST, T.place, "host") else pcall(M.itemSend, ST, "A", tostring(T.place), "x", "1") end
                    log("S2CORE art2: peguei um artefato no lugar " .. T.place)
                end
            end
            return
        end
        if not T then T = {}; P.track[id] = T end
        T.place = apPlace(P, q.X, q.Y, q.Z)
        T.pd = pp and math.sqrt((q.X - pp.X) ^ 2 + (q.Y - pp.Y) ^ 2 + (q.Z - pp.Z) ^ 2) or nil      -- batch 33: my distance at this sighting
        local closed = T.place ~= nil and P.taken[T.place] ~= nil
        if closed and not P.hid[id] then P.hid[id] = o; gHide(o); apSilence(P, id, o, true)   -- batch 47
        elseif not closed and P.hid[id] then
            P.hid[id] = nil
            apSilence(P, id, o, false)                     -- batch 47: components on, back to its own spot
            pcall(function() o:SetActorHiddenInGame(false) end); pcall(function() o:SetActorEnableCollision(true) end)
        end
    end) end
    for id in pairs(P.track) do if not live[id] then P.track[id] = nil; P.hid[id] = nil; if P.away then P.away[id] = nil end; if P.was then P.was[id] = nil end end end
end
-- ===== batch 18: artifact mirror (#A list, #AG gone, #AT client take) =====
local function artInit(ST)
    if ST.art then return ST.art end
    ST.art = {cls = nil, track = {}, gone = {}, mir = {}, out = {}, seen = {}, seq = 0, list = ""}
    return ST.art
end
local function artTable(A)                             -- class -> item SID (offline table)
    if A.cls then return A.cls end
    A.cls = {}
    local n, f = 0, io.open(api.BR .. "s2u_art.tsv", "r")
    if f then
        for ln in f:lines() do local c, sid = ln:match("^(%S+)\t(%S+)"); if c then A.cls[c] = sid; n = n + 1 end end
        f:close()
    end
    log("S2CORE art: " .. n .. " classes de artefato em s2u_art.tsv")
    return A.cls
end
local function artCount(pw, sid)
    local n = 0
    pcall(function() n = StaticFindObject("/Script/Stalker2.Default__CppMediator"):GetItemsCountInInventory(pw, sid) end)
    return type(n) == "number" and n or 0
end
local function artOut(ST)
    if FL.artsync == 2 then return apOut(ST) end
    if FL.artsync ~= 1 then return "" end
    local A, now = artInit(ST), os.clock()
    if api.ROLE == "host" then
        local g = {}
        for id, at in pairs(A.gone) do if now - at < 15 then g[#g + 1] = id end end
        table.sort(g)
        return (A.list ~= "" and ("#A:" .. A.list) or "") .. (#g > 0 and ("#AG:" .. table.concat(g, "|")) or "")
    end
    local keep, parts = {}, {}
    for _, e in ipairs(A.out) do if now - e.t < 3 then keep[#keep + 1] = e; parts[#parts + 1] = e.s end end
    A.out = keep
    return #parts > 0 and ("#AT:" .. table.concat(parts, "|")) or ""
end
local function artTick(ST)
    if FL.artsync == 2 then return apTick(ST) end
    if FL.artsync ~= 1 then return end
    local A, now = artInit(ST), os.clock()
    if now < (A.at or 0) then return end
    A.at = now + (api.ROLE == "host" and 1 or 0.5)
    local pw = S2.io and S2.io.pawn; if not valid(pw) then return end
    local lp = pw:K2_GetActorLocation()
    local tab = artTable(A)
    if api.ROLE == "host" then
        local r = api.getRemote and api.getRemote()
        local live, cand = {}, {}
        for _, o in ipairs(FindAllOf("Artifact") or {}) do pcall(function()
            if not o:IsValid() then return end
            local id = o:GetFName():ToString()
            live[id] = o
            local T = A.track[id]
            if not T then
                local cls = o:GetFullName():match("^(%S+)") or "?"
                local sid = tab[cls]
                if not sid then                                  -- batch 21: measured class name BP_<SID>_C
                    local guess = cls:match("^BP_(.+)_C$")
                    if guess then for _, x in ipairs(LOOT_SIDS) do if x == guess then sid = guess; break end end end
                end
                if not sid then return end
                T = {sid = sid, cnt = artCount(pw, sid)}; A.track[id] = T
            end
            local q = o:K2_GetActorLocation()
            if math.abs(q.X) + math.abs(q.Y) < 1000 then                -- at the origin
                if T.x and not A.gone[id] then                            -- batch 21: it had a place and jumped to 0,0,0 = picked up (measured)
                    A.gone[id] = now; log("S2CORE art: " .. id .. " (" .. T.sid .. ") foi pego no host (foi para a origem)")
                end
                return
            end
            T.x, T.y, T.z = q.X, q.Y, q.Z
            if A.gone[id] then return end
            if r and now - (r.recv or 0) < 3 then
                local d = math.sqrt((q.X - r.x) ^ 2 + (q.Y - r.y) ^ 2)
                if d <= FL.artradius then cand[#cand + 1] = {id = id, d = d, T = T} end
            end
        end) end
        for id, T in pairs(A.track) do                    -- my own pickups, and artifacts that stopped existing
            if not A.gone[id] then
                local c = artCount(pw, T.sid)
                local near = T.x and math.sqrt((T.x - lp.X) ^ 2 + (T.y - lp.Y) ^ 2 + (T.z - lp.Z) ^ 2) <= FL.artnear
                if not live[id] then A.gone[id] = now; log("S2CORE art: " .. id .. " (" .. T.sid .. ") deixou de existir no host")
                elseif near and c > T.cnt then A.gone[id] = now; log("S2CORE art: o host pegou " .. id .. " (" .. T.sid .. ")") end
                T.cnt = c
            elseif not live[id] and now - A.gone[id] > 60 then A.track[id] = nil; A.gone[id] = nil end
        end
        table.sort(cand, function(a, b) return a.d < b.d end)
        local parts = {}
        for i = 1, math.min(#cand, FL.artmax) do
            local c = cand[i]
            parts[#parts + 1] = string.format("%s;%s;%.0f,%.0f,%.0f", c.id, c.T.sid, c.T.x, c.T.y, c.T.z)
        end
        A.list = table.concat(parts, "|")
        -- the client took one
        for ev in (x1part("AT") or ""):gmatch("[^|]+") do
            local seq, id = ev:match("^(%d+);(.+)$")
            seq = tonumber(seq)
            if seq and not A.seen[seq] then
                A.seen[seq] = now
                local o = live[id]
                if o and not A.gone[id] then gHide(o) end
                A.gone[id] = A.gone[id] or now
                log("S2CORE art: o cliente pegou " .. tostring(id) .. " -> escondido no host ok=" .. tostring(o ~= nil))
            elseif seq then A.seen[seq] = now end
        end
        for k, at in pairs(A.seen) do if now - at > 10 then A.seen[k] = nil end end
        return
    end
    -- ---- client
    local addr = pw:GetAddress()
    if addr ~= A.addr then A.addr = addr; A.mir = {} end      -- a load: the proxies of the old world are gone
    local listed = {}
    for ent in (x1part("A") or ""):gmatch("[^|]+") do
        local id, sid, x, y, z = ent:match("^([^;]+);([%w_]+);([%-%d%.]+),([%-%d%.]+),([%-%d%.]+)$")
        if id then
            listed[id] = true
            if not A.mir[id] and M.inWorld(nil, tonumber(x), tonumber(y), tonumber(z)) then   -- b81kz
                local ok, e = pcall(function() FindFirstOf("CustomConsoleManagerBuild"):XSpawnItemInCoordinatesBySID(sid, tonumber(x), tonumber(y), tonumber(z)) end)
                A.mir[id] = {sid = sid, x = tonumber(x), y = tonumber(y), z = tonumber(z), cnt = artCount(pw, sid), at = now}
                A.mir[id].find = now + 0.4                       -- batch 21: look for the actor the call created (class BP_<SID>_C at that spot)
                log(string.format("S2CORE art: espelho de %s (%s) criado em %.0f,%.0f,%.0f ok=%s %s", id, sid, x, y, z, tostring(ok), ok and "" or tostring(e)))
            end
            A.mir[id].seenAt = now
        end
    end
    local gone = {}
    for id in (x1part("AG") or ""):gmatch("[^|]+") do gone[id] = true end
    for id, m in pairs(A.mir) do
        if m.find and now >= m.find and not valid(m.obj) then  -- batch 21: remember my proxy actor
            m.find = (now - m.at < 5) and (now + 0.5) or nil
            local best, bd = nil, 300 ^ 2
            for _, o in ipairs(FindAllOf("Artifact") or {}) do pcall(function()
                if o:IsValid() and (o:GetFullName():match("^(%S+)") or "") == "BP_" .. m.sid .. "_C" then
                    local q = o:K2_GetActorLocation()
                    local d = (q.X - m.x) ^ 2 + (q.Y - m.y) ^ 2 + (q.Z - m.z) ^ 2
                    if d <= bd then best, bd = o, d end
                end
            end) end
            if best then m.obj = best; m.find = nil end
        end
        if valid(m.obj) and not m.done then                  -- batch 21: my proxy jumped to the origin = I picked it up (measured)
            local q; pcall(function() q = m.obj:K2_GetActorLocation() end)
            if q and math.abs(q.X) + math.abs(q.Y) < 1000 then
                if m.gone then
                    pcall(M.itemRemove, ST, "P", m.sid, 1); m.done = "ghost"
                    log("S2CORE art: " .. id .. " ja era do host; " .. m.sid .. " tirado de mim")
                else
                    A.seq = A.seq + 1; A.out[#A.out + 1] = {t = now, s = A.seq .. ";" .. id}; m.done = "mine"
                    log("S2CORE art: peguei " .. id .. " (" .. m.sid .. ")")
                end
            end
        end
        if gone[id] and not m.gone and valid(m.obj) and not m.done then   -- batch 21: hide exactly my proxy (hidden + collision off removes the prompt: measured)
            m.gone = true; gHide(m.obj); m.done = "hidden"
            log("S2CORE art: " .. id .. " foi pego no host; meu espelho foi escondido")
        end
        if gone[id] and not m.gone then                      -- taken on the host side: try to remove my proxy
            m.gone = true
            if not m.done then
                local hid = 0
                for _, cname in ipairs({"Artifact", tostring(FL.artclass or "")}) do
                    if cname ~= "" then
                        for _, o in ipairs(FindAllOf(cname) or {}) do pcall(function()
                            local q = o:K2_GetActorLocation()
                            if o:IsValid() and (q.X - m.x) ^ 2 + (q.Y - m.y) ^ 2 + (q.Z - m.z) ^ 2 <= 300 ^ 2 then gHide(o); hid = hid + 1 end
                        end) end
                    end
                end
                log("S2CORE art: " .. id .. " foi pego no host; espelho escondido: " .. hid)
            end
        end
        if not m.done then
            local c = artCount(pw, m.sid)
            local near = (m.x - lp.X) ^ 2 + (m.y - lp.Y) ^ 2 + (m.z - lp.Z) ^ 2 <= FL.artnear ^ 2
            if near and c > m.cnt then
                if m.gone then                                -- the host had it first: mine is a ghost
                    pcall(M.itemRemove, ST, "P", m.sid, c - m.cnt); m.done = "ghost"
                    log("S2CORE art: " .. id .. " ja era do host; " .. m.sid .. " tirado de mim")
                else
                    A.seq = A.seq + 1; A.out[#A.out + 1] = {t = now, s = A.seq .. ";" .. id}; m.done = "mine"
                    log("S2CORE art: peguei " .. id .. " (" .. m.sid .. ")")
                end
            end
            m.cnt = c
        end
        if not listed[id] and now - (m.seenAt or m.at) > 120 and (m.done or m.gone) then A.mir[id] = nil end
    end
end

-- ===== batch 17 (c): trader stock: what one player buys leaves the other PC's copy of that trader (flag tsync) =====
-- The trader is named by its placeholder SID, found from its position in <bridge>/s2u_trade.tsv (offline table, no call on the NPC
-- beyond its location). A buy = while I interact with that NPC my money went DOWN and item counts went UP (the ptake method).
local function trLoad(T)
    if T.tab then return T.tab end
    T.tab = {}
    local f = io.open(api.BR .. "s2u_trade.tsv", "r")
    if f then
        for ln in f:lines() do
            local sid, x, y, z = ln:match("^(%x+)\t[^\t]*\t[^\t]*\t([%-%d%.]+)\t([%-%d%.]+)\t([%-%d%.]+)")
            if sid then T.tab[#T.tab + 1] = {sid = sid, x = tonumber(x), y = tonumber(y), z = tonumber(z)} end
        end
        f:close()
    end
    log("S2CORE trade: " .. #T.tab .. " comerciantes em s2u_trade.tsv")
    return T.tab
end
function M.traderSid(ST, x, y, z)
    ST.tr = ST.tr or {}
    local best, bd = nil, (FL.trradius or 800) ^ 2
    for _, c in ipairs(trLoad(ST.tr)) do
        local d = (c.x - x) ^ 2 + (c.y - y) ^ 2 + (c.z - z) ^ 2
        if d <= bd then best, bd = c.sid, d end
    end
    return best
end
function S2L.trSlot(T, sid)                         -- is it a slot item (weapon, armor, artifact, detector, ...)? table of gen_equip.py
    if not T.slot then
        T.slot = {}
        local f = io.open(api.BR .. "s2u_equip.tsv", "r")
        if f then for ln in f:lines() do local k = ln:match("^(%S+)\t"); if k then T.slot[k] = true end end f:close() end
    end
    return T.slot[sid] == true
end
function S2L.trClock() return tonumber(rawget(_G, "S2_gameSecs")) or os.clock() end
function M.trNote(ST, trader, item, n)                 -- a buy of the OTHER player
    ST.tr = ST.tr or {}
    local T = ST.tr; T.other = T.other or {}
    local k = tostring(trader) .. "/" .. tostring(item)
    local e = T.other[k]
    if e and S2L.trClock() - e.at > FL.trwindow then e = nil end
    T.other[k] = {n = (e and e.n or 0) + (tonumber(n) or 0), at = S2L.trClock()}
    log(string.format("S2CORE trade: o outro jogador comprou %s x%s de %s", tostring(item), tostring(n), tostring(trader)))
    return true
end
local function trTick(ST)
    if FL.tsync ~= 1 then return end
    ST.tr = ST.tr or {}
    local T, now = ST.tr, os.clock()
    T.other = T.other or {}
    local pw = S2.io and S2.io.pawn
    if not valid(pw) then pcall(function() pw = api.UEHelpers.GetPlayerController().Pawn end) end
    if not valid(pw) then return end
    local sid = nil
    pcall(function()
        local a = pw:GetInteractedActor()                  -- measured: during trade this is the trader pawn
        if a and a:IsValid() then local q = a:K2_GetActorLocation(); sid = M.traderSid(ST, q.X, q.Y, q.Z) end
    end)
    if sid and not T.open then
        local items, money = invSnap(pw)
        T.open = {sid = sid, snap = items, money = money or 0}
    elseif T.open and sid ~= T.open.sid then
        local after, money = invSnap(pw)
        local spent, bought, dup, clean = T.open.money - (money or 0), {}, {}, 0
        if spent > 0 then
            for _, it in ipairs(LOOT_SIDS) do
                local d = (after[it] or 0) - (T.open.snap[it] or 0)
                if d > 0 and S2L.trSlot(T, it) then
                    local k = T.open.sid .. "/" .. it
                    local e = T.other[k]
                    if e and S2L.trClock() - e.at > FL.trwindow then T.other[k] = nil; e = nil end
                    local nd = e and math.min(d, e.n) or 0
                    if nd > 0 then                          -- the other player already has this one from this trader
                        pcall(M.itemRemove, ST, "P", it, nd); e.n = e.n - nd; dup[#dup + 1] = it .. "x" .. nd
                    end
                    if d - nd > 0 then bought[#bought + 1] = it .. "x" .. (d - nd); pcall(M.itemSend, ST, "T", T.open.sid, it, d - nd) end
                elseif d > 0 then clean = clean + 1 end     -- consumables, ammo: free to buy on both PCs
            end
        end
        if #dup > 0 then
            local refund = (#bought == 0 and clean == 0) and spent or 0
            if refund > 0 then pcall(function() pw:ModifyMoney(refund) end) end
            log(string.format("S2CORE trade: compra duplicada em %s: %s ja e do outro jogador -> tirado de mim, dinheiro devolvido %d", T.open.sid, table.concat(dup, ","), refund))
        end
        if #bought > 0 then log(string.format("S2CORE trade: comprado de %s por %d: %s (avisado ao outro PC)", T.open.sid, spent, table.concat(bought, ","))) end
        T.open = nil
    end
end

-- ===== batch 73: F10 dropped items (#DR, flag idrop), F12 player-triggered anomalies (#AN, flag anomsync), stash believed box (flag ssbox)
-- Tables (generated by b73items/gen_b73.py from the user's install, copied next to the core in the bridge folder):
--   s2u_items.lua   SID -> {m = world static mesh, s = max stack, k = item kind}  (only items whose drop can be detected and mirrored)
--   s2u_anomaly.lua anomaly BP class -> {r = radius cm, h = collision height cm, cd = cooldown s, n = native class} (one-shot traps only)
-- No UObject is kept between ticks: every scan stores names, meshes and positions only.
function B73.ld(T, name)
    if T[name] ~= nil then return T[name] end
    T[name] = false
    local f = io.open(api.BR .. name, "r"); if not f then log("S2CORE b73: falta " .. name); return false end
    local src = f:read("*a"); f:close()
    local fn, e = load(src, name); if not fn then log("S2CORE b73: " .. name .. " erro " .. tostring(e)); return false end
    local ok, t = pcall(fn); if ok and type(t) == "table" then T[name] = t; local n = 0; for _ in pairs(t) do n = n + 1 end; log("S2CORE b73: " .. name .. " carregado, " .. n .. " linhas") end
    return T[name]
end
B73.T = {}   -- loaded tables; every helper is a field of B73 (the live core is at 195 of Lua's 200 top-level locals)
function B73.pos(o) local q = o:K2_GetActorLocation(); return q.X, q.Y, q.Z end
function B73.cls(o) return (o:GetFullName():match("^(%S+)")) end

-- ---------- (a) dropped items. Dropper: an inventory count of a tracked SID goes down while no container, trader, stash or item node is
-- busy, and within idropr cm of me a NEW BP_StaticItemContainer_C (not in the 2-s baseline) with that SID's world mesh appears and comes to
-- rest -> "#DR:<seq>;<sid>;<n>;<x>,<y>,<z>;<mesh>" rides the snapshots FL.resend s. Receiver: XSpawnItemInCoordinatesBySID once (now when
-- within idropm cm, else when I come that close, for 30 min). Picking up either copy = the existing ground #G rule (class + mesh, 150 cm).
B73.DCLS = "BP_StaticItemContainer_C"
function B73.dScan(px, py, pz, r)
    local t = {}
    for _, o in ipairs(FindAllOf(B73.DCLS) or {}) do pcall(function()
        if o:IsValid() and not o.bHidden then
            local x, y, z = B73.pos(o)
            if (x - px) ^ 2 + (y - py) ^ 2 + (z - pz) ^ 2 < r * r then
                local m = "?"; pcall(function() if M.meshName then m = M.meshName(o, "MeshComponent") or "?" else m = o.MeshComponent.StaticMesh:GetFName():ToString() end end)
                t[#t + 1] = {x = x, y = y, z = z, m = m}
            end
        end end) end
    return t
end
function B73.dKnown(base, c)
    for _, b in ipairs(base) do if b.m == c.m and (b.x - c.x) ^ 2 + (b.y - c.y) ^ 2 + (b.z - c.z) ^ 2 < 40 ^ 2 then return true end end
    return false
end
function B73.dropTick(ST)
    if FL.idrop ~= 1 then return end
    local D = ST.b73d or {seq = 0, out = {}, seen = {}, pend = {}, arm = {}, base = {}, baseAt = 0, snapAt = 0, busyAt = -100, spawnAt = 0}
    ST.b73d = D
    local TAB = B73.ld(B73.T, "s2u_items.lua"); if not TAB then return end
    local pw = S2.io and S2.io.pawn; if not valid(pw) then return end
    local now = os.clock()
    local px, py, pz = B73.pos(pw)
    local addr = pw:GetAddress()
    if addr ~= D.addr then D.addr = addr; D.snap = nil; D.arm = {}; D.base = {}; D.base2 = nil; D.baseAt = 0; D.busyAt = now end   -- a load / new pawn
    local busy = (ST.lock and ST.lock.mine ~= nil) or (ST.tr and ST.tr.open ~= nil) or (ST.ss and ST.ss.open ~= nil) or #((ST.it and ST.it.run) or {}) > 0
    if busy then D.busyAt = now end
    -- (1) my inventory, once a second
    if now >= D.snapAt then
        D.snapAt = now + 1
        local snap = invSnap(pw)
        if D.snap and now - D.busyAt > 3 then
            for sid, n0 in pairs(D.snap) do
                local dn = n0 - (snap[sid] or 0)
                if dn > 0 and TAB[sid] then D.arm[#D.arm + 1] = {sid = sid, n = dn, m = TAB[sid].m, untilT = now + 5} end
            end
        end
        D.snap = snap
    end
    -- (2) baseline of the ground items around me (every 2 s while nothing is armed), or the search for the dropped one (every 1 s)
    if #D.arm == 0 then
        if now >= D.baseAt then D.baseAt = now + 2; D.base = D.base2 or {}; D.base2 = B73.dScan(px, py, pz, (FL.idropr or 400) + 300) end   -- D.base = the scan before the newest: older than any drop the 1-s count can see
    elseif now >= (D.scanAt or 0) then
        D.scanAt = now + 1
        local cands = B73.dScan(px, py, pz, FL.idropr or 400)
        for i = #D.arm, 1, -1 do
            local a = D.arm[i]
            local best, bd = nil, nil
            for _, c in ipairs(cands) do
                if c.m == a.m and not c.used and not B73.dKnown(D.base, c) then
                    local d = (c.x - px) ^ 2 + (c.y - py) ^ 2
                    if not bd or d < bd then best, bd = c, d end
                end
            end
            if best then
                best.used = true
                if a.c and (a.c.x - best.x) ^ 2 + (a.c.y - best.y) ^ 2 + (a.c.z - best.z) ^ 2 < 10 ^ 2 then   -- at rest: send
                    D.seq = D.seq + 1
                    local e = string.format("%d;%s;%d;%.0f,%.0f,%.0f;%s", D.seq, a.sid, a.n, best.x, best.y, best.z, best.m)
                    D.out[#D.out + 1] = {e = e, untilT = now + (FL.resend or 6)}
                    D.base[#D.base + 1] = {x = best.x, y = best.y, z = best.z, m = best.m}; if D.base2 then D.base2[#D.base2 + 1] = D.base[#D.base] end
                    table.remove(D.arm, i)
                    log("S2CORE DROP: larguei " .. e)
                else a.c = {x = best.x, y = best.y, z = best.z}; a.untilT = math.max(a.untilT, now + 2) end
            elseif now > a.untilT then
                table.remove(D.arm, i)
                log(string.format("S2CORE DROP: %s -%d sem item novo no chao (consumido/vendido/usado)", a.sid, a.n))
            end
        end
        if #D.arm == 0 then D.baseAt = 0 end
    end
    -- (3) the other player's drops
    local echo = ST.peer and ST.peer.loopback
    for ent in ((not echo and x1part("DR")) or ""):gmatch("[^|]+") do
        if not D.seen[ent] then
            D.seen[ent] = now
            local seq, sid, n, x, y, z, m = ent:match("^(%d+);([%w_]+);(%d+);([%-%d]+),([%-%d]+),([%-%d]+);([%w_%?]+)$")
            if seq and TAB[sid] then
                D.pend[#D.pend + 1] = {e = ent, sid = sid, n = tonumber(n), x = tonumber(x), y = tonumber(y), z = tonumber(z), m = m, untilT = now + 1800}
            elseif seq then log("S2CORE DROP: " .. ent .. " ignorado (SID fora da tabela)") end
        end
    end
    for k, t in pairs(D.seen) do if now - t > 120 then D.seen[k] = nil end end
    if #D.pend > 0 and now >= D.spawnAt then
        D.spawnAt = now + 1                                                -- at most one spawn a second
        for i, p in ipairs(D.pend) do
            local far = (p.x - px) ^ 2 + (p.y - py) ^ 2 > (FL.idropm or 8000) ^ 2
            if now > p.untilT then table.remove(D.pend, i); log("S2CORE DROP: " .. p.e .. " expirou (longe 30 min)"); break end
            if not far and not M.inWorld(nil, p.x, p.y, p.z) then table.remove(D.pend, i); log("S2CORE DROP: " .. p.e .. " fora do mundo (kzsafe); ignorado"); break end
            if not far then
                table.remove(D.pend, i)
                local ok, e = pcall(function() FindFirstOf("CustomConsoleManagerBuild"):XSpawnItemInCoordinatesBySID(p.sid, p.x, p.y, p.z + 30) end)
                log(string.format("S2CORE DROP: copia de %s criada ok=%s %s", p.e, tostring(ok), ok and "" or tostring(e)))
                break
            end
        end
    end
end
function B73.dropOut(ST)
    local D = ST.b73d; if not D or FL.idrop ~= 1 then return "" end
    local now, r, keep = os.clock(), {}, {}
    for _, o in ipairs(D.out) do if now < o.untilT then r[#r + 1] = o.e; keep[#keep + 1] = o end end
    D.out = keep
    while #r > 6 do table.remove(r, 1) end
    return #r > 0 and ("#DR:" .. table.concat(r, "|")) or ""
end

-- ---------- (c) anomalies a player sets off. Sender: every 2 s the one-shot anomalies (s2u_anomaly.lua) within anomr cm are listed (class,
-- position, radius x actor scale); each tick, my pawn ENTERING one (horizontal distance <= r, |dz| <= h) sends "#AN:<seq>;<class>;<x>,<y>,<z>"
-- (per anomaly at most once per max(cooldown, anomcd) s). Receiver: the nearest anomaly of that class within 300 cm runs its own native
-- ForceActivateOnce() (VFX, sound and damage of the game's own code). Echo guard: an anomaly I entered myself in the last 3 s is not forced.
-- The hidden target body of the other player is in that anomaly too: its damage is not forwarded (#D) for 3 s ("anomalia espelhada").
function B73.anomTick(ST)
    if FL.anomsync ~= 1 then return end
    local A = ST.b73a or {seq = 0, out = {}, seen = {}, list = {}, listAt = 0, inside = {}, sent = {}, forcedAt = {}}
    ST.b73a = A
    local TAB = B73.ld(B73.T, "s2u_anomaly.lua"); if not TAB then return end
    local pw = S2.io and S2.io.pawn; if not valid(pw) then return end
    local now = os.clock()
    local px, py, pz = B73.pos(pw)
    if now >= A.listAt then
        A.listAt = now + 2
        local R, L = FL.anomr or 6000, {}
        for _, o in ipairs(FindAllOf("Anomaly") or {}) do pcall(function()
            if o:IsValid() then
                local c = B73.cls(o); local row = c and TAB[c]
                if row then
                    local x, y, z = B73.pos(o)
                    if (x - px) ^ 2 + (y - py) ^ 2 < R * R then
                        local s = {X = 1, Y = 1, Z = 1}; pcall(function() s = o:GetActorScale3D() end)
                        L[#L + 1] = {k = string.format("%.0f,%.0f,%.0f", x, y, z), c = c, x = x, y = y, z = z,
                                     r = row.r * math.max(s.X or 1, s.Y or 1), h = (row.h or 200) * (s.Z or 1), cd = math.max(row.cd or 0, FL.anomcd or 3)}
                    end
                end
            end end) end
        A.list = L
    end
    for _, a in ipairs(A.list) do
        local inside = (a.x - px) ^ 2 + (a.y - py) ^ 2 <= a.r * a.r and math.abs(pz - a.z) <= math.max(a.h, 200)
        if inside and not A.inside[a.k] and now - (A.sent[a.k] or -1e9) >= a.cd then
            A.sent[a.k] = now; A.seq = A.seq + 1
            local e = string.format("%d;%s;%s", A.seq, a.c, a.k)
            A.out[#A.out + 1] = {e = e, untilT = now + 3}
            log("S2CORE ANOM: entrei em " .. e)
        end
        A.inside[a.k] = inside or nil
    end
    local echo = ST.peer and ST.peer.loopback
    for ent in ((not echo and x1part("AN")) or ""):gmatch("[^|]+") do
        if not A.seen[ent] then
            A.seen[ent] = now
            local seq, c, x, y, z = ent:match("^(%d+);([%w_]+);([%-%d]+),([%-%d]+),([%-%d]+)$")
            x, y, z = tonumber(x), tonumber(y), tonumber(z)
            if seq and TAB[c] then
                local mine = false
                for k, t in pairs(A.sent) do
                    local kx, ky = k:match("^([%-%d]+),([%-%d]+)")
                    if now - t < 3 and (tonumber(kx) - x) ^ 2 + (tonumber(ky) - y) ^ 2 < 300 ^ 2 then mine = true end
                end
                if mine then log("S2CORE ANOM: " .. ent .. " ja disparada por mim (eco)")
                else
                    local best, bd = nil, 300 ^ 2
                    for _, o in ipairs(FindAllOf("Anomaly") or {}) do pcall(function()
                        if o:IsValid() and B73.cls(o) == c then
                            local ox, oy, oz = B73.pos(o)
                            local d = (ox - x) ^ 2 + (oy - y) ^ 2 + (oz - z) ^ 2
                            if d < bd then best, bd = o, d end
                        end end) end
                    if best then
                        ST.anomQuiet = {untilT = now + 3, x = x, y = y, r = TAB[c].r + 300}
                        local ok, e = pcall(function() best:ForceActivateOnce() end)
                        log(string.format("S2CORE ANOM: %s -> ForceActivateOnce a %.0f cm ok=%s %s", ent, math.sqrt(bd), tostring(ok), ok and "" or tostring(e)))
                    else log("S2CORE ANOM: " .. ent .. " sem essa anomalia a 3 m aqui (nao carregada ou nao existe neste PC)") end
                end
            end
        end
    end
    for k, t in pairs(A.seen) do if now - t > 30 then A.seen[k] = nil end end
end
function B73.anomOut(ST)
    local A = ST.b73a; if not A or FL.anomsync ~= 1 then return "" end
    local now, r, keep = os.clock(), {}, {}
    for _, o in ipairs(A.out) do if now < o.untilT then r[#r + 1] = o.e; keep[#keep + 1] = o end end
    A.out = keep
    while #r > 4 do table.remove(r, 1) end
    return #r > 0 and ("#AN:" .. table.concat(r, "|")) or ""
end

-- ---------- (b) stash, client side (flag ssbox, with sstash=1 and csave=1): the client keeps its own save, so its box copy already holds what
-- was put in before. ss_box.txt = what the mod believes is in MY box (sid<TAB>count). The once-per-pawn fill adds only ledger - believed.
function B73.boxLoad(S)
    if S.box then return S.box end
    S.box = {}
    local f = io.open(api.BR .. "ss_box.txt", "r")
    if f then for ln in f:lines() do local sid, n = ln:match("^([%w_]+)\t(%d+)$"); if sid then S.box[sid] = tonumber(n) end end; f:close() end
    return S.box
end
function B73.boxAdd(S, sid, n)
    if FL.ssbox ~= 1 or api.ROLE == "host" then return end
    local bx = B73.boxLoad(S)
    local v = (bx[sid] or 0) + n; bx[sid] = v > 0 and v or nil
    local f = io.open(api.BR .. "ss_box.txt", "w"); if not f then return end
    local keys = {}; for k in pairs(bx) do keys[#keys + 1] = k end; table.sort(keys)
    for _, k in ipairs(keys) do f:write(k .. "\t" .. bx[k] .. "\n") end
    f:close()
end

function B73.tick(ST)
    local okD, eD = pcall(B73.dropTick, ST)
    local okA, eA = pcall(B73.anomTick, ST)
    if not (okD and okA) then
        S2.errs.n = S2.errs.n + 1; S2.errs.last = "b73: " .. tostring(okD and eA or eD):sub(1, 160)
        ST.b73err = (ST.b73err or 0) + 1; if ST.b73err <= 5 then log("S2CORE b73 erro: " .. tostring(okD and eA or eD)) end
    end
end
function B73.out(ST) return (select(2, pcall(B73.dropOut, ST)) or "") .. (select(2, pcall(B73.anomOut, ST)) or "") end
-- ===== end batch 73

function M.snapExtra(ST, pawn, p)
    local okW, ws = pcall(weaponStr, ST, pawn)
    local okL, ls = pcall(lifeStr, ST, pawn)
    local okH, hs = pcall(helloStr, ST)
    return (okW and ws or "") .. dmgStr(ST) .. (select(2, pcall(B73.out, ST)) or "") .. (okL and ls or "") .. "#K:" .. ((ST.lock and ST.lock.mine) or "") .. (okH and hs or "") .. (select(2, pcall(lootOut, ST)) or "") .. (select(2, pcall(doorOut, ST)) or "") .. (select(2, pcall(trigOut, ST)) or "") .. (select(2, pcall(dobjOut, ST)) or "") .. (select(2, pcall(ienOut, ST)) or "") .. (select(2, pcall(groundOut, ST)) or "") .. (select(2, pcall(rewardOut, ST)) or "") .. (select(2, pcall(questOut, ST)) or "") .. (select(2, pcall(catchupOut, ST)) or "") .. (select(2, pcall(itOut, ST)) or "") .. (select(2, pcall(cineOut, ST)) or "") .. (select(2, pcall(dmirOut, ST)) or "") .. (select(2, pcall(S2L.emOut, ST)) or "") .. (select(2, pcall(rbOut, ST)) or "") .. (select(2, pcall(ssOut, ST)) or "") .. (select(2, pcall(artOut, ST)) or "") .. (select(2, pcall(csOut, ST)) or "") .. (select(2, pcall(S2L.ftOut, ST)) or "") .. (select(2, pcall(iaOut, ST)) or "") .. (select(2, pcall(S2L.qiOut, ST)) or "") .. (select(2, pcall(S2L.ldOut, ST)) or "") .. (select(2, pcall(S2L.qpOut, ST)) or "")
end

-- ===== roadmap (e): container locks, minimal version =====
-- Measured 2026-10-11: the loot window is open when the widget InventoryNew is visible; Pawn:GetInteractedActor() is the container (it is also
-- valid while the prompt is only shown); Pawn:IsInteractionInProgress() stays false; Pawn:CloseLootMenu() closes the window.
-- NEVER call CppMediator:GetGUID on a container actor: it crashed the game (it is only safe on Obj pawns).
-- Id = class name without the instance number + position rounded to 10 cm (same world data on both PCs). The holder sends "#K:<id>" in x1.
-- Rule: first come keeps it; if both have it open at once the host keeps it and the client closes.
-- Batch 42 (lockfirst=1): the host opening a container the client already holds is denied the same way (user decision 2026-10-12);
-- "both have it open" is then only a race inside one snapshot round trip, and the client gives way.
local function lockTick(ST)
    if FL.locks ~= 1 then return end
    local K = ST.lock or {}; ST.lock = K
    local pw = S2.io and S2.io.pawn; if not valid(pw) then return end
    local w = K.widget; if not valid(w) then w = FindFirstOf("InventoryNew"); K.widget = w end
    local open = false
    if valid(w) then pcall(function() open = w:IsVisible() and true or false end) end
    local id = nil
    if open then pcall(function()
        local a = pw:GetInteractedActor()
        if a and a:IsValid() then
            local q = a:K2_GetActorLocation()
            id = string.format("%s@%.0f,%.0f,%.0f", (a:GetFullName():match("^(%S+)") or "?"), q.X / 10, q.Y / 10, q.Z / 10)
        end end) end
    if id and S2L.clIs and S2L.clIs(ST, pw) then return end   -- b138loot: our loot body is no container lock / taken key
    if id and (FL.cloot or 0) == 1 and (FL.b164off or 0) == 0 and not M.LS.body(ST, id) then local dead = false; pcall(function() dead = pw:GetInteractedActor():IsAlive() == false end); if dead then return end end   -- b164: a corpse never gets a #K lock / TAKEN key (the client's twin corpse poisoned the host's for good: TAKEN_DENY forever)
    local remote = x1part("K") or ""
    local held = (remote ~= "" and id ~= nil and M.kf.close(remote, id)) or (FL.locktest == 1 and id ~= nil)   -- b71kf: neighbour key
    local tkn = FL.taken == 1 and id and ST.taken and M.kf.get(ST.taken, id) and not (FL.takenown == 1 and takenMineSet(ST)[id]) and not M.partialSid(ST, id) and not id:find("PlayerStash", 1, true) and not S2L.qiHostKeep(ST, id, pw)   -- b87qitem; core-22: partial-take containers are never "taken whole"
    if tkn and id ~= K.mine and not K.denied then          -- already taken for good by one of the players
        K.denied = id
        local ok, e = pcall(function() pw:CloseLootMenu() end)
        log("S2CORE TAKEN_DENY " .. id .. " (ja levado) janela fechada ok=" .. tostring(ok))
    elseif id and id ~= K.mine and not K.denied then           -- just opened
        if held and (FL.locktest == 1 or api.ROLE ~= "host" or FL.lockfirst == 1) then   -- batch 42: lockfirst=1 denies the host too
            K.denied = id; K.denies = (K.denies or 0) + 1
            local ok, e = pcall(function() pw:CloseLootMenu() end)
            log("S2CORE LOCK_DENY " .. id .. " (em uso pelo outro jogador) janela fechada ok=" .. tostring(ok) .. " " .. (ok and "" or tostring(e)))
        else
            K.mine = id; K.grants = (K.grants or 0) + 1
            log("S2CORE LOCK_REQ " .. id); log("S2CORE LOCK_GRANT " .. id .. (held and " (conflito: host fica)" or ""))
            K.psid = M.partialSid(ST, id); K.psnap = nil
            if K.psid then K.psnap = invSnap(pw); log("S2CORE ptake: recipiente " .. K.psid .. " aberto; inventario anotado") end
            pcall(M.LS.open, ST, id, pw)   -- b90loot (lootshare)
        end
    elseif K.mine and id == K.mine and held and api.ROLE ~= "host" and FL.locktest ~= 1 then   -- both opened it: the client gives way
        pcall(function() pw:CloseLootMenu() end); log("S2CORE LOCK_DENY " .. K.mine .. " (conflito: o host fica) janela fechada"); K.mine = nil
    end
    if not open then
        if K.mine then
            log("S2CORE LOCK_REL " .. K.mine)
            if K.psid and K.psnap then                       -- core-22: partial take: what went up in MY inventory came out of the container
                local after = invSnap(pw)
                local items = invDelta(K.psnap, after)
                if items ~= "" then
                    local key = string.format("ctake@%s;%s%d;%s", K.psid, api.ROLE:sub(1, 1), math.random(1, 999999), items)
                    ptInit(ST).applied[key] = true            -- my own copy already lacks them
                    if FL.taken == 1 then
                        if api.ROLE == "host" then takenAdd(ST, key) else ST.tOut = {id = key, untilT = os.clock() + 3}; takenAdd(ST, key) end
                    end
                    log("S2CORE ptake: tirado do recipiente " .. K.psid .. ": " .. items)
                else log("S2CORE ptake: recipiente " .. K.psid .. " fechado sem tirar nada") end
                K.psid = nil; K.psnap = nil
            elseif FL.taken == 1 and not K.mine:find("PlayerStash", 1, true) then   -- core-25: the personal stash box is never "taken" (measured: after the first open it could not be opened again)
                if FL.takenown == 1 then takenMineAdd(ST, K.mine) end   -- batch 50: mine: never denied to me
                if api.ROLE == "host" then takenAdd(ST, K.mine) else ST.tOut = {id = K.mine, untilT = os.clock() + 3}; takenAdd(ST, K.mine) end
            end
            K.mine = nil
        end
        K.denied = nil
    end
end

-- ===== puppet body of the remote player (avatarmode=npcpuppet) =====
function S2L.pupAnim(P, speed)
    if FL.pupanimbp == 1 then                         -- b80crash: never single-node on a game Character (crash 0x658)
        if P.abpFor ~= P.pawn and not P.state and (FL.pupnoreinit or 0) == 1 and S2L.humanAnim(P.pawn) then   -- b86pupctl: healthy body, no re-init
            P.abpFor = P.pawn; P.rate = nil
            log("S2CORE puppet: AnimBP ja ativo (pupnoreinit) -> sem SetAnimationMode")
        end
        if P.abpFor ~= P.pawn or P.state then         -- once per adopted pawn: b112 (#2) mark only, NEVER SetAnimationMode (D10: crash 0x588)
            log("S2CORE puppet: AnimBP do jogo mantido (b112: sem SetAnimationMode) humano=" .. tostring(S2L.humanAnim(P.pawn)) .. " estava=" .. tostring(P.state))
            P.abpFor = P.pawn; P.state = nil; P.rate = nil
        end
        if FL.pupabpstill == 1 and not S2L.pmVel() then pcall(function() local cm = P.pawn.CharacterMovement; if cm.MovementMode ~= 0 then cm:DisableMovement() end end) end   -- b89: not in pupmove 1/3
        return
    end
    local st = (speed < 30 and "idle") or (speed < WALK_MAX and "walk") or "run"
    if st ~= P.state then
        local q = seqs[st]; if not valid(q) then q = asset(PUP_ANIM[st]); seqs[st] = q end
        if not q then return end
        local ok, e = pcall(function() P.pawn.Mesh:PlayAnimation(q, true) end)
        log(string.format("S2CORE puppet(v3): anim %s -> %s (v=%.0f) ok=%s %s", tostring(P.state), st, speed, tostring(ok), ok and "" or tostring(e)))
        if not ok then if tostring(e):find("nullptr", 1, true) then P.broken = true end return end   -- batch 52: dropped by pupTick next tick
        P.state = st; P.rate = nil
    end
    local r = 1.0
    if PUP_NAT[st] then r = math.max(0.6, math.min(1.8, speed / PUP_NAT[st])) end
    if not P.rate or math.abs(r - P.rate) > 0.15 then P.rate = r; pcall(function() P.pawn.Mesh:SetPlayRate(r) end) end
end

-- ===== b89pupmove (flag pupmove, default 0; needs pupanimbp=1). Measured 2026-10-15 00:04-00:12 (pupnoreinit=1): the puppet keeps its
-- AnimBP_Human_C + HumanAnimController but plays its own idle (sway/turn) and jumps sideways while the host stands still; it never walks.
-- Measured 2026-10-10: AnimBP_Human_C RootMotionMode=2, motion-matching movement component, Velocity writes moved it 0 cm (root motion on).
-- INFERRED: the AnimBP's root motion moves/turns the capsule between our 8.5 Hz teleports (2nd writer) and overrides any Velocity.
-- pupmove: RootMotionMode=1 on the puppet (pupmvrm), then 1 = the CharacterMovement moves it every frame from a Velocity we steer at
-- 10 Hz (ONE per-frame writer, real velocity for the AnimBP), 2 = idle/walk/run clips via the AnimBP slot + the old teleport, 3 = both.
-- Never SetAnimationMode/PlayAnimation here (single-node = crash 0x658 + lost controller).
function S2L.pmVel() local m = FL.pupmove or 0; return (m == 1 or m == 3) and FL.pupanimbp == 1 and (FL.nsmooth or 0) ~= 1 end   -- b114: nsmooth = one mover
function S2L.pmAI(P)
    local ai; pcall(function() ai = P.pawn.Mesh:GetAnimInstance() end)
    if valid(ai) then return ai end
end
function S2L.pmAddr(o) local a = 0; pcall(function() a = o:GetAddress() end); return a end
function S2L.pmWrap(base)                                  -- snapExtra + "#MV:<crouch>,<aim>" of the LOCAL player while pupmove>0
    return function(ST, pawn, p)
        local s = base(ST, pawn, p)
        if (FL.pupmove or 0) > 0 then local ok, e = pcall(S2L.pmOut, ST, pawn); if ok and e then s = (s or "") .. e end end
        if (FL.pupfeet or 0) == 1 then local ok, e = pcall(S2L.pfOut, pawn); if ok and e then s = (s or "") .. e end end   -- b123pupcrouch
        return s
    end
end
function S2L.pmOut(ST, pawn)
    local c, a = 0, 0
    if not pcall(function() c = pawn:IsCrouching() and 1 or 0 end) then pcall(function() c = pawn.CharacterMovement:IsCrouching() and 1 or 0 end) end
    pcall(function() a = pawn:IsAiming() and 1 or 0 end)
    if (FL.pupanim or 0) == 1 then                           -- b119pupanim: 3rd bit = falling (jump/fall)
        local f = 0
        if not pcall(function() f = (pawn:IsFalling() or pawn:IsJumping()) and 1 or 0 end) then pcall(function() f = pawn.CharacterMovement:IsFalling() and 1 or 0 end) end
        return "#MV:" .. c .. "," .. a .. "," .. f
    end
    return "#MV:" .. c .. "," .. a
end
function S2L.pupMove(ST, P, vx, vy, speed, dt, now)
    local mv = FL.pupmove or 0
    local vel, clips = S2L.pmVel(), (mv == 2 or mv == 3)
    S2L.pupAnim(P, speed)                                   -- AnimBP bookkeeping of the pupanimbp branch (once per pawn; no single-node)
    if P.broken or not valid(P.pawn) then return end
    local Q = P.pm
    if not Q or Q.pawn ~= P.pawn then
        Q = {pawn = P.pawn, rm = 0, mm = 0, snaps = 0, clipN = 0, clipBad = 0, tagN = 0}; P.pm = Q
        local ai0 = S2L.pmAI(P)
        pcall(function() Q.rm0 = ai0.RootMotionMode end)
        pcall(function() Q.mm0 = P.pawn.CharacterMovement.MovementMode end)
        local n0 = "?"; pcall(function() n0 = P.pawn:GetFName():ToString() end)
        log(string.format("S2CORE pupmove: boneco %s pupmove=%d (velocidade=%s clipes=%s) RootMotionMode=%s MovementMode=%s uid=%s",
            n0, mv, tostring(vel), tostring(clips), tostring(Q.rm0), tostring(Q.mm0), tostring(P.uid)))
    end
    local ai = S2L.pmAI(P)
    if ai and (FL.pupmvrm or 1) == 1 then                   -- the AnimBP's root motion never moves/turns the capsule again
        local rm; pcall(function() rm = ai.RootMotionMode end)
        if rm ~= 1 then
            local ok = pcall(function() ai.RootMotionMode = 1 end); Q.rm = Q.rm + 1
            if Q.rm <= 3 or Q.rm % 50 == 0 then log("S2CORE pupmove: RootMotionMode " .. tostring(rm) .. " -> 1 (IgnoreRootMotion) ok=" .. tostring(ok) .. " vez=" .. Q.rm) end
        end
    end
    local cm; pcall(function() cm = P.pawn.CharacterMovement end)
    if vel and valid(cm) then
        if not Q.velSet then
            Q.velSet = true
            local r9 = {}
            for _, kv in ipairs({{"bRunPhysicsWithNoController", true}, {"BrakingFrictionFactor", 0.0}, {"BrakingDecelerationFlying", 0.0}, {"GroundFriction", 0.0}, {"BrakingDecelerationWalking", 0.0}}) do
                r9[#r9 + 1] = kv[1] .. "=" .. tostring((pcall(function() cm[kv[1]] = kv[2] end)))
            end
            log("S2CORE pupmove: CharacterMovement " .. table.concat(r9, " "))
        end
        local want = FL.pupmvmode or 5
        local mm; pcall(function() mm = cm.MovementMode end)
        if mm ~= want then
            local ok = pcall(function() cm:SetMovementMode(want, 0) end); Q.mm = Q.mm + 1
            if Q.mm <= 3 or Q.mm % 50 == 0 then log("S2CORE pupmove: MovementMode " .. tostring(mm) .. " -> " .. want .. " ok=" .. tostring(ok) .. " vez=" .. Q.mm) end
        end
        local A; pcall(function() A = P.pawn:K2_GetActorLocation() end)
        if not A then return end
        local ex, ey, ez = P.pos.X - A.X, P.pos.Y - A.Y, P.pos.Z - A.Z
        local snap = FL.pupmvsnap or 300
        if ex * ex + ey * ey > snap * snap or math.abs(ez) > snap then   -- first placement, jumps, lost tracking: one teleport
            P.pawn:K2_SetActorLocation(P.pos, false, {}, true); Q.snaps = Q.snaps + 1
            if Q.snaps <= 5 or Q.snaps % 50 == 0 then log(string.format("S2CORE pupmove: teleporte de correcao %.0f cm (vez %d)", math.sqrt(ex * ex + ey * ey + ez * ez), Q.snaps)) end
            ex, ey, ez = 0, 0, 0
        end
        local kp, kz = FL.pupmvgain or 3, FL.pupmvgainz or 6
        local cx, cy, cz = vx + ex * kp, vy + ey * kp, ez * kz
        if speed < 30 and ex * ex + ey * ey < (FL.pupmvdead or 8) ^ 2 then cx, cy = 0, 0 end
        if math.abs(ez) < 3 then cz = 0 end
        local sp, mx = math.sqrt(cx * cx + cy * cy), FL.pupmvvmax or 1500
        if sp > mx then cx, cy = cx * mx / sp, cy * mx / sp end
        cm.Velocity = {X = cx, Y = cy, Z = cz}
        P.pmW = nil
    else
        local okNs, nsOn = pcall(S2L.nsPup, ST, P, now)                -- b114smooth: per-frame mover (nsmooth=1) instead of the 10 Hz teleport
        if not okNs then S2L.nsErr(ST, "nsPup", nsOn) end
        if okNs and nsOn then P.pmW = nil else P.pawn:K2_SetActorLocation(P.pos, false, {}, true); P.pmW = P.pos end
    end
    if not P.nsYaw then P.pawn:K2_SetActorRotation({Pitch = 0, Yaw = P.yaw, Roll = 0}, false) end; P.pmY = P.yaw
    if clips and ai then if (FL.pupanim or 0) == 1 then S2L.paClips(P, Q, ai, speed, vx, vy) else S2L.pmClips(P, Q, ai, speed) end elseif Q.clip then S2L.pmClipStop(Q, ai) end   -- b119pupanim
    S2L.pmTags(P, Q, speed)
end
function S2L.pmClips(P, Q, ai, speed)                       -- the old clips, but inside the AnimBP (slot montage): instance stays AnimInstanceHuman
    if Q.clipBad >= 5 then return end
    local st = (speed < 30 and "idle") or (speed < WALK_MAX and "walk") or "run"
    local rate = 1.0
    if PUP_NAT[st] then rate = math.max(0.6, math.min(1.8, speed / PUP_NAT[st])) end
    local aa = S2L.pmAddr(ai)
    if st ~= Q.clip or aa ~= Q.clipAI or not valid(Q.mon) then
        local q = seqs[st]; if not valid(q) then q = asset(PUP_ANIM[st]); seqs[st] = q end
        if not q then Q.clipBad = Q.clipBad + 1; return end
        local mon
        local ok, e = pcall(function() mon = ai:PlaySlotAnimationAsDynamicMontage(q, FName(FL.pupmvslot or "DefaultSlot"), 0.25, 0.25, rate, 100000, -1.0, 0.0) end)
        Q.clipN = Q.clipN + 1
        if not (ok and valid(mon)) then Q.clipBad = Q.clipBad + 1 end
        if Q.clipN <= 20 or Q.clipN % 50 == 0 or Q.clipBad == 5 then
            log(string.format("S2CORE pupmove: clipe %s -> %s (v=%.0f ritmo=%.2f slot=%s) ok=%s montagem=%s%s%s", tostring(Q.clip), st, speed, rate,
                tostring(FL.pupmvslot), tostring(ok), tostring(valid(mon)), ok and "" or " " .. tostring(e), Q.clipBad == 5 and " -> clipes desligados para este corpo" or ""))
        end
        Q.clip = st; Q.clipAI = aa; Q.mon = mon; Q.rate = rate
    elseif math.abs(rate - (Q.rate or 1)) > 0.15 then
        Q.rate = rate; pcall(function() ai:Montage_SetPlayRate(Q.mon, rate) end)
    end
end
function S2L.pmClipStop(Q, ai)
    if ai then pcall(function() ai:StopSlotAnimation(0.25, FName(FL.pupmvslot or "DefaultSlot")) end) end
    Q.clip = nil; Q.mon = nil; Q.clipAI = nil
end
-- ===== b119pupanim (flag pupanim, default 0; needs pupmove 2/3 + pupanimbp=1). User 2026-10-15 19:55: "only walk and idle work; check
-- run, sprint, crouch idle/walk, aim, jump, turn-in-place, weapon pose". ONE state table (S2L.PA) + ONE picker, from data the host already
-- sends (remote speed, velocity direction vs the sender's yaw, #MV crouch/aim) plus ONE new #MV bit (3rd = falling, sent only with
-- pupanim=1; old receivers read "c,a" and ignore it). Clips = the game's own third-person AnimSequences, real paths from the IoStore listing
-- (retoc list, 2026-10-15). Family by the remote weapon mesh path (/weapons/<cat>/): pt = pistol, other guns = ar, none/knife/item = bh;
-- a state with no clip in that family uses row.any, then row.fb (fallback state). The game has NO third-person crouch-walk, jump or fall
-- clip for humans: cwalk = the crouch idle (legs still), fall = Korshunov's boss jump cycle (pafall=0: no fall state). Strafe/backward
-- clips exist only in the ar combat set (used by every family). nat = the clip's natural speed in cm/s (rate = speed / nat, 0.6..pamaxr);
-- nil = unmeasured, rate 1 (probe S1 measures them). Facing: forward states face the velocity (as before); strafe/back/aim/idle states face
-- the sender's yaw (P.paFace, read by pupTick/nsPup through S2L.paYaw). A new state must win pahold samples in a row (no flicker).
S2L.PA_DIR = "/Game/_STALKER2/Animations/Characters/AnimSequences/"
S2L.PA = {
    idle   = {bh = {"bh/Idles/AS_tp_bh_idle"}, ar = {"ar/relax/idle/AS_tp_ar_relax_stand_idle"}, pt = {"pt/relax/idle/AS_tp_pt_relax_stand_idle"}},
    aidle  = {ar = {"ar/combat/idle/AS_tp_ar_combat_idle"}, pt = {"pt/combat/Idles/AS_tp_pt_combat_idle"}, fb = "idle"},
    cidle  = {any = {"ar/combat/idle/AS_tp_ar_crouch_idle"}, pt = {"pt/combat/Idles/AS_tp_pt_lowcrouch_idle"}, fb = "idle"},
    cwalk  = {fb = "cidle"},
    turnl  = {ar = {"ar/relax/turn_in_place/stand/AS_tp_ar_relax_turn_l_90"}, any = {"bh/turn_in_place/stand/AS_tp_bh_turn_l_90"}, fb = "idle"},
    turnr  = {ar = {"ar/relax/turn_in_place/stand/AS_tp_ar_relax_turn_r_90"}, any = {"bh/turn_in_place/stand/AS_tp_bh_turn_r_90"}, fb = "idle"},
    walk   = {bh = {"bh/locomotion/stand/AS_tp_bh_walk_fwd", 114}, ar = {"ar/relax/locomotion/stand/AS_tp_ar_relax_walk_fwd", 114}, pt = {"bh/locomotion/stand/AS_tp_bh_walk_fwd", 114}},
    awalk  = {ar = {"ar/combat/locomotion/strafe/stand/AS_tp_ar_strafe_walk_fwd", 156}, fb = "walk"},
    run    = {bh = {"bh/locomotion/stand/AS_tp_bh_run_fwd", 285}, ar = {"ar/relax/locomotion/stand/AS_tp_ar_relax_run_fwd", 285}, pt = {"bh/locomotion/stand/AS_tp_bh_run_fwd", 285}},
    sprint = {ar = {"ar/combat/locomotion/base/stand/AS_tp_ar_combat_sprint_fwd", 414}, fb = "run"},
    left   = {any = {"ar/combat/locomotion/strafe/stand/root_rotate/AS_tp_ar_strafe_walk_l", 156}, fb = "walk"},
    right  = {any = {"ar/combat/locomotion/strafe/stand/root_rotate/AS_tp_ar_strafe_walk_r", 156}, fb = "walk"},
    bleft  = {any = {"ar/combat/locomotion/strafe/stand/root_rotate/AS_tp_ar_strafe_walk_bl", 126}, fb = "back"},
    bright = {any = {"ar/combat/locomotion/strafe/stand/root_rotate/AS_tp_ar_strafe_walk_br", 126}, fb = "back"},
    back   = {any = {"ar/combat/locomotion/strafe/stand/AS_tp_ar_strafe_walk_bwd", 126}, fb = "walk"},
    fall   = {any = {"bossfight/Korshunov/AS_tp_ar_combat_korshunov_jump_cycle"}, fb = "idle"},
}
S2L.paMiss = {}
-- ===== b123pupcrouch (flags pupfeet, pabh; default 0). User 2026-10-15 21:20: "the crouch animation the feet is getting underground" +
-- "any crouch and back step animation without an arm". Measured live: host crouch = capsule half 90 -> 57.5 and actor z -32.5; the puppet
-- (pupcmoff=1) keeps its capsule (88) and was placed at the host's capsule CENTRE z -> its mesh/feet 32 cm under the ground.
-- pupfeet=1: the sender adds x1 "#FH:<own scaled capsule half-height>" (feet = sent z - FH); the receiver puts the puppet capsule at
-- feet + its OWN half-height. S2L.pfZ is the ONE target-z function of the puppet: pupTick (10 Hz placement / pupmove) and nsPup (b114
-- per-frame mover) both take it. No #FH (old sender, sender pupfeet 0, silent peer) or pupfeet 0 here = the old centre z.
-- pabh=1 (needs pupanim=1): bare-hands rows ".bhn" (no rifle arms): crouch idle = the unarmed squat (the game has NO tp crouch-walk: cwalk
-- slides it, as before), strafe/back = the bh/locomotion/stand walk_l|r_90/135/180 loops (8-way set with _start/_stop variants; strafe vs
-- turning arc is measured by probe S1), and at or above WALK_MAX the run_ versions (states rleft/rright/rbleft/rbright/rback; armed
-- families have no row there -> fb = the walk state = the b119 rifle strafe clip, unchanged). nat nil = unmeasured (rate 1) until S1.
-- Direction check (task 3): main.lua sends yaw = pawn:K2_GetActorRotation().Yaw (the ACTOR yaw, same tick as CharacterMovement.Velocity);
-- S2L.paSector compares the velocity with that r.yaw and the puppet's "look" facing is that same r.yaw -> sector and body facing already
-- use the actor yaw on both ends. No code change there; probe R1 logs actor vs control yaw on the sender.
S2L.PA.cidle.bhn  = {"bh/Contextual/stand/tp_bh_ca_squatting/AS_tp_bh_ca_squatting_idle"}
S2L.PA.left.bhn   = {"bh/locomotion/stand/AS_tp_bh_walk_l_90"}
S2L.PA.right.bhn  = {"bh/locomotion/stand/AS_tp_bh_walk_r_90"}
S2L.PA.bleft.bhn  = {"bh/locomotion/stand/AS_tp_bh_walk_l_135"}
S2L.PA.bright.bhn = {"bh/locomotion/stand/AS_tp_bh_walk_r_135"}
S2L.PA.back.bhn   = {"bh/locomotion/stand/AS_tp_bh_walk_l_180"}
S2L.PA.rleft  = {bhn = {"bh/locomotion/stand/AS_tp_bh_run_l_90"}, fb = "left"}
S2L.PA.rright = {bhn = {"bh/locomotion/stand/AS_tp_bh_run_r_90"}, fb = "right"}
S2L.PA.rbleft = {bhn = {"bh/locomotion/stand/AS_tp_bh_run_l_135"}, fb = "bleft"}
S2L.PA.rbright = {bhn = {"bh/locomotion/stand/AS_tp_bh_run_r_135"}, fb = "bright"}
S2L.PA.rback  = {bhn = {"bh/locomotion/stand/AS_tp_bh_run_l_180"}, fb = "back"}
S2L.PA_SEC = {l = "left", r = "right", bl = "bleft", br = "bright", b = "back"}
function S2L.pfOut(pawn)                                   -- sender: "#FH:<scaled capsule half-height>" (feet = the sent z - FH)
    local h; pcall(function() h = pawn.CapsuleComponent:GetScaledCapsuleHalfHeight() end)
    if type(h) == "number" and h > 1 then return string.format("#FH:%.1f", h) end
end
function S2L.pfZ(ST, P, r)                                 -- the ONE target z of the puppet capsule (pupTick + nsPup)
    local z = r.z + (ST.offz or 0)
    if (FL.pupfeet or 0) ~= 1 then return z end
    local hh = tonumber(x1part("FH") or "")
    if not hh or hh < 10 or hh > 200 then return z end      -- no part = the old centre z (old sender / sender pupfeet 0)
    local own; pcall(function() own = P.pawn.CapsuleComponent:GetScaledCapsuleHalfHeight() end)
    if type(own) == "number" and own >= 10 and own <= 200 then P.pfH = own else own = P.pfH end
    if not own then return z end
    local F = ST.pf or {n = 0}; ST.pf = F
    if not F.hh or math.abs(F.hh - hh) > 1 or math.abs(F.own - own) > 1 then
        F.n = F.n + 1; F.hh, F.own = hh, own
        if F.n <= 30 or F.n % 50 == 0 then
            log(string.format("S2CORE pupfeet: remetente meia_altura=%.1f boneco meia_altura=%.1f -> z do boneco %+.1f cm vs centro (vez %d)", hh, own, own - hh, F.n))
        end
    end
    return z - hh + own
end
-- ===== end b123pupcrouch
function S2L.paPath(rel) return S2L.PA_DIR .. rel .. "." .. rel:match("[^/]+$") end
function S2L.paAsset(rel)                                  -- cached by full path in the core's seqs; a failed load is not retried until reload
    local full = S2L.paPath(rel)
    local q = seqs[full]
    if valid(q) then return q end
    if S2L.paMiss[full] then return nil end
    q = asset(full)
    if q then seqs[full] = q else S2L.paMiss[full] = true; log("S2CORE pupanim: clipe nao carregou " .. full) end
    return q
end
function S2L.paClip(st, fam)                               -- -> asset, table entry {rel, nat}, state that gave it
    for _ = 1, 5 do
        local row = S2L.PA[st]; if not row then return nil end
        local e = (fam == "bh" and (FL.pabh or 0) == 1 and row.bhn) or row[fam]   -- b123pupcrouch: bare-hands rows
            or (fam == "pt" and (FL.papt or 1) == 1 and row.bhn) or row.any                   -- b172: pistol = bare-hands legs, never the rifle strafe
        if e then local q = S2L.paAsset(e[1]); if q then return q, e, st end end
        st = row.fb; if not st then return nil end
    end
end
function S2L.paFam(P)                                      -- weapon family from the remote weapon mesh path (pupWeapon's P.wstr)
    local c = tostring(P.wstr or ""):match("/[Ww]eapons/(%a+)/")
    if c == "pt" then return "pt" end
    if c and c ~= "knifes" and c ~= "grenades" and c ~= "items" and c ~= "attaches" then return "ar" end
    return "bh"
end
function S2L.paSector(ry, vx, vy, speed)                   -- move direction vs the sender's yaw: f / l / r / bl / br / b (UE yaw +90 = right)
    if speed < 30 or type(ry) ~= "number" then return "f" end
    local d = api.angDiff(math.deg(math.atan(vy, vx)), ry); local a = math.abs(d)
    if a <= 60 then return "f" elseif a <= 120 then return d < 0 and "l" or "r" elseif a <= 150 then return d < 0 and "bl" or "br" end
    return "b"
end
function S2L.paPick(speed, sec, c, a, f, turn)             -- pure: -> state, facing ("move" = velocity as before, "look" = sender yaw)
    if f and (FL.pafall or 1) == 1 then return "fall", "move" end
    if speed < 30 then
        if c then return "cidle", "look" end
        if turn then return turn, "look" end
        return (a and "aidle" or "idle"), "look"
    end
    if c then return "cwalk", ((sec == "f" and not a) and "move" or "look") end
    if (FL.pabh or 0) == 1 and speed >= WALK_MAX and S2L.PA_SEC and S2L.PA_SEC[sec] then return "r" .. S2L.PA_SEC[sec], "look" end   -- b123: run_ loops
    if sec == "l" then return "left", "look" elseif sec == "r" then return "right", "look" elseif sec == "bl" then return "bleft", "look"
    elseif sec == "br" then return "bright", "look" elseif sec == "b" then return "back", "look" end
    if speed >= (FL.pupmvsprint or 520) then return "sprint", "move" end
    if speed >= WALK_MAX then return "run", (a and "look" or "move") end
    return (a and "awalk" or "walk"), (a and "look" or "move")
end
function S2L.paYaw(P, r, vx, vy, speed)                   -- pupTick / nsPup: the sender's yaw for "look" states, nil = the old rule
    local mv = FL.pupmove or 0
    if (FL.pupanim or 0) ~= 1 or FL.pupanimbp ~= 1 or (mv ~= 2 and mv ~= 3) then return nil end
    if P.paFace == "look" and r and type(r.yaw) == "number" then return r.yaw end
    return nil
end
function S2L.paClips(P, Q, ai, speed, vx, vy)             -- pupmove clips, table driven (replaces pmClips while pupanim=1)
    if Q.clipBad >= 5 then return end
    local r = api.getRemote()
    local ry = r and type(r.yaw) == "number" and r.yaw or nil
    local now = os.clock()
    if ry and r.recv ~= Q.paRecv then                      -- turn in place: the sender's yaw rate between snapshots
        local t = (type(r.t) == "number" and r.t) or r.recv
        if Q.paRy and Q.paRt and t > Q.paRt then
            local w = api.angDiff(ry, Q.paRy) / math.max(0.05, t - Q.paRt)
            if math.abs(w) >= (FL.patrn or 45) then Q.paTurn = (w > 0 and "turnr" or "turnl"); Q.paTurnUntil = now + 0.4 end
        end
        Q.paRy, Q.paRt, Q.paRecv = ry, t, r.recv
    end
    local turn = (Q.paTurnUntil and now < Q.paTurnUntil) and Q.paTurn or nil
    local c, a, f = (x1part("MV") or ""):match("^(%d),(%d),?(%d?)")
    local sec = S2L.paSector(ry, vx or 0, vy or 0, speed)
    local st, face = S2L.paPick(speed, sec, c == "1", a == "1", f == "1", turn)
    if (FL.papt or 1) == 1 and (FL.pabh or 0) ~= 1 and speed >= WALK_MAX and S2L.PA_SEC[sec] == st and S2L.paFam(P) == "pt" then st = "r" .. st end   -- b172: pistol run strafe = bh run_ loops
    local key = st .. "|" .. face
    if key == Q.paCand then Q.paCn = (Q.paCn or 0) + 1 else Q.paCand = key; Q.paCn = 1 end
    if not Q.paSt or Q.paCn >= (FL.pahold or 2) then Q.paSt, Q.paFace = st, face end
    P.paFace = Q.paFace
    local fam = S2L.paFam(P)
    local q, e, rst = S2L.paClip(Q.paSt, fam)
    if not q then
        Q.clipBad = Q.clipBad + 1
        log(string.format("S2CORE pupanim: estado %s familia %s sem clipe (falha %d)%s", Q.paSt, fam, Q.clipBad, Q.clipBad == 5 and " -> clipes desligados para este corpo" or ""))
        return
    end
    local rate = 1.0
    if e[2] and speed >= 30 then rate = math.max(0.6, math.min(FL.pamaxr or 2.5, speed / e[2])) end
    local ck = "pa:" .. e[1]
    local aa = S2L.pmAddr(ai)
    if ck ~= Q.clip or aa ~= Q.clipAI or not valid(Q.mon) then
        local mon
        local ok, er = pcall(function() mon = ai:PlaySlotAnimationAsDynamicMontage(q, FName(FL.pupmvslot or "DefaultSlot"), 0.25, 0.25, rate, 100000, -1.0, 0.0) end)
        Q.clipN = Q.clipN + 1
        if not (ok and valid(mon)) then Q.clipBad = Q.clipBad + 1 end
        if Q.clipN <= 30 or Q.clipN % 50 == 0 or Q.clipBad == 5 then
            log(string.format("S2CORE pupanim: estado %s -> %s (clipe de %s, familia %s, v=%.0f dir=%s agach=%s mira=%s queda=%s giro=%s olhar=%s ritmo=%.2f) %s ok=%s montagem=%s%s%s",
                tostring(Q.paLog), Q.paSt, rst, fam, speed, sec, tostring(c), tostring(a), tostring(f), tostring(turn), Q.paFace, rate, e[1]:match("[^/]+$"),
                tostring(ok), tostring(valid(mon)), ok and "" or " " .. tostring(er), Q.clipBad == 5 and " -> clipes desligados para este corpo" or ""))
        end
        Q.clip = ck; Q.clipAI = aa; Q.mon = mon; Q.rate = rate; Q.paLog = Q.paSt
    elseif math.abs(rate - (Q.rate or 1)) > 0.15 then
        Q.rate = rate; pcall(function() ai:Montage_SetPlayRate(Q.mon, rate) end)
    end
end
M.paTable = S2L.PA; M.paPick = S2L.paPick; M.paSector = S2L.paSector; M.paClipOf = S2L.paClip; M.paFam = S2L.paFam; M.paPath = S2L.paPath   -- probes
-- ===== end b119pupanim
function S2L.pmTagSet(P, Q, want)                           -- EStateTag bits (DUMP): Sprint 16 Walk 32 Run 64 MoveForward 128 Crouch 1024 Aim 8192
    local old = Q.tags or 0
    if want == old or type(P.uid) ~= "number" or P.uid <= 0 then return end
    local OP; pcall(function() OP = FindFirstOf("CustomConsoleManagerOP") end)
    if not valid(OP) then return end
    local r = {}
    for _, b in ipairs({16, 32, 64, 128, 1024, 8192}) do
        local on, was = (want & b) ~= 0, (old & b) ~= 0
        if on ~= was then local ok = pcall(function() OP:XNpcSetStateTag(P.uid, b, on) end); r[#r + 1] = b .. (on and "+" or "-") .. (ok and "" or "!") end
    end
    Q.tags = want; Q.tagN = Q.tagN + 1
    if Q.tagN <= 20 or Q.tagN % 50 == 0 then log("S2CORE pupmove: etiquetas " .. old .. " -> " .. want .. " (" .. table.concat(r, " ") .. ")") end
end
function S2L.pmTags(P, Q, speed)
    local tg, cr = FL.pupmvtag or 0, FL.pupmvcr or 0
    if tg ~= 1 and cr ~= 1 and (Q.tags or 0) == 0 and not Q.cr then return end
    local c, a = (x1part("MV") or ""):match("^(%d),(%d)")
    if tg == 1 then
        local want = 0
        if speed >= 30 then want = 128 | ((speed < WALK_MAX and 32) or (speed < (FL.pupmvsprint or 520) and 64) or 16) end
        if c == "1" then want = want | 1024 end
        if a == "1" then want = want | 8192 end
        S2L.pmTagSet(P, Q, want)
    elseif (Q.tags or 0) ~= 0 then S2L.pmTagSet(P, Q, 0) end
    local w = (cr == 1 and c == "1") or false
    if w ~= (Q.cr or false) then
        local ok = pcall(function() if w then P.pawn:Crouch(false) else P.pawn:UnCrouch(false) end end)
        Q.cr = w
        log("S2CORE pupmove: " .. (w and "Crouch" or "UnCrouch") .. " ok=" .. tostring(ok))
    end
end
function S2L.pmOff(P)                                       -- pupmove back to 0: give the body back as it was
    local Q = P.pm; P.pm = nil
    if not (Q and Q.pawn == P.pawn and valid(P.pawn)) then return end
    local ai, r = S2L.pmAI(P), {}
    if Q.clip then S2L.pmClipStop(Q, ai); r[#r + 1] = "clipe parado" end
    if Q.rm > 0 and ai and Q.rm0 then r[#r + 1] = "RootMotionMode=" .. tostring(Q.rm0) .. " ok=" .. tostring((pcall(function() ai.RootMotionMode = Q.rm0 end))) end
    if Q.velSet then
        r[#r + 1] = "MovementMode=" .. tostring(Q.mm0) .. " ok=" .. tostring((pcall(function() local cm = P.pawn.CharacterMovement; cm.Velocity = {X = 0, Y = 0, Z = 0}; if Q.mm0 then cm:SetMovementMode(Q.mm0, 0) end end)))
    end
    if (Q.tags or 0) ~= 0 then S2L.pmTagSet(P, Q, 0); r[#r + 1] = "etiquetas limpas" end
    if Q.cr then pcall(function() P.pawn:UnCrouch(false) end); r[#r + 1] = "UnCrouch" end
    log("S2CORE pupmove: desligado -> boneco devolvido (" .. table.concat(r, " ") .. ")")
end
function S2L.pmDiag(ST, P, speed, now)                      -- pupmvlog=1, READ ONLY: before our write, how far did something else move the body?
    local D = ST.pmD
    if not D or now - D.at > 30 then D = {at = now, n = 0, nf = 0, fs = 0, fmax = 0, f5 = 0, ymax = 0, es = 0, emax = 0, vs = 0}; ST.pmD = D end
    local A, R; pcall(function() A = P.pawn:K2_GetActorLocation() end); pcall(function() R = P.pawn:K2_GetActorRotation() end)
    if not A then return end
    D.n = D.n + 1; D.vs = D.vs + speed
    if P.pmW then
        local f = math.sqrt((A.X - P.pmW.X) ^ 2 + (A.Y - P.pmW.Y) ^ 2); D.nf = D.nf + 1; D.fs = D.fs + f
        if f > D.fmax then D.fmax = f end; if f > 5 then D.f5 = D.f5 + 1 end
    end
    if R and P.pmY then local y = math.abs(api.angDiff(R.Yaw, P.pmY)); if y > D.ymax then D.ymax = y end end
    if P.pos then local e = math.sqrt((A.X - P.pos.X) ^ 2 + (A.Y - P.pos.Y) ^ 2); D.es = D.es + e; if e > D.emax then D.emax = e end end
    if now - D.at < 10 then return end
    local mm, v, rm, rmp, st, mb = "?", "?", "?", "?", "?", "?"
    pcall(function() local cm = P.pawn.CharacterMovement; mm = tostring(cm.MovementMode); local q = cm.Velocity; v = string.format("%.0f", math.sqrt(q.X ^ 2 + q.Y ^ 2)) end)
    local ai = S2L.pmAI(P)
    pcall(function() rm = tostring(ai.RootMotionMode) end)
    pcall(function() rmp = tostring(P.pawn:IsPlayingRootMotion()) end)
    pcall(function() local s = ai.StateData; st = string.format("mov=%s and=%s cor=%s agach=%s gira=%s gait=%.2f", tostring(s.bMoving), tostring(s.bWalking), tostring(s.bRunning), tostring(s.bCrouching), tostring(s.bRotating), s.CurveGaitValue) end)
    pcall(function() mb = tostring(P.pawn:GetMovementBehaviour()) end)
    local Q = P.pm or {}
    log(string.format("S2CORE pupmove diag 10 s (pupmove=%s): n=%d fora_media=%.1f fora_max=%.1f fora>5cm=%d/%d yaw_fora_max=%.1f erro_media=%.1f erro_max=%.1f | MovementMode=%s vel=%s RootMotionMode=%s rootmotion=%s | anim %s comportamento=%s | remoto v=%.0f | teleportes=%s modo_reposto=%s rm_reposto=%s clipe=%s etiquetas=%s mv=%s",
        tostring(FL.pupmove), D.n, D.fs / math.max(1, D.nf), D.fmax, D.f5, D.nf, D.ymax, D.es / D.n, D.emax, mm, v, rm, rmp, st, mb, D.vs / D.n,
        tostring(Q.snaps), tostring(Q.mm), tostring(Q.rm), tostring(Q.clip), tostring(Q.tags), tostring(x1part("MV"))))
    ST.pmD = nil
end
function S2L.pfxTick(ST, P, speed, now)            -- b110 pupfix: before our write; true = the body was swapped (pupTick returns)
    if not (P.pmW and P.pawn and valid(P.pawn)) then return false end
    local A; pcall(function() A = P.pawn:K2_GetActorLocation() end)
    if not A then return false end
    local f = math.sqrt((A.X - P.pmW.X) ^ 2 + (A.Y - P.pmW.Y) ^ 2)
    if f <= (FL.pupfixcm or 150) then return false end
    local H = ST.pfxH
    if not H or H.path ~= P.path or H.uid ~= P.uid then H = {path = P.path, uid = P.uid, t = {}}; ST.pfxH = H end
    local t = {}
    for _, x in ipairs(H.t) do if now - x < 10 then t[#t + 1] = x end end
    t[#t + 1] = now; H.t = t
    if #t < (FL.pupfixn or 3) then return false end
    local k = {}
    for _, x in ipairs(ST.pfxR or {}) do if now - x < 600 then k[#k + 1] = x end end
    ST.pfxR = k
    if #k >= (FL.pupfixmax or 3) then
        if not H.cap then H.cap = true; log(string.format("S2CORE pupfix: boneco movido de volta de novo (%.0f cm) mas ja houve %d trocas em 600 s -> corpo mantido", f, #k)) end
        return false
    end
    k[#k + 1] = now
    log(string.format("S2CORE pupfix: boneco movido de volta por outro sistema (%.0f cm de onde o pusemos, %d vezes em 10 s; ancora %.0f,%.0f,%.0f; remoto v=%.0f) -> corpo trocado (%d/%d em 600 s)",
        f, #t, A.X, A.Y, A.Z, speed or 0, #k, FL.pupfixmax or 3))
    M.pupRecycle(ST, "pupfix")
    return true
end
function M.pupRecycle(ST, why)                     -- b110: drop the puppet body for good: hidden + removed from the A-Life by GUID; a new one is adopted/requested
    local P = ST and ST.pup
    if not (P and P.pawn) then return false end
    local how, ok, name, now = "ja invalido", true, "?", os.clock()
    if valid(P.pawn) then
        pcall(function() name = P.pawn:GetFName():ToString() end)
        pcall(function() P.pawn:SetActorHiddenInGame(true) end)
        ok = false
        if type(P.uid) == "number" and P.uid > 0 then
            how = "XDestroyModel(" .. P.uid .. ")"; ok = pcall(function() FindFirstOf("CustomConsoleManagerAI2"):XDestroyModel(P.uid) end)
        end
        if not ok then how = "K2_DestroyActor"; ok = pcall(function() P.pawn:K2_DestroyActor() end) end
    end
    P.bad = P.bad or {}; if P.path then P.bad[P.path] = true end            -- never adopted again
    log("S2CORE pupfix: corpo " .. name .. " largado (" .. tostring(why) .. ") -> escondido + " .. how .. " ok=" .. tostring(ok) .. "; corpo novo sera adotado/pedido")
    P.pawn = nil; P.path = nil; P.state = nil; P.hidden = nil; P.abpFor = nil; P.naFor = nil; P.uid = nil; P.pm = nil; P.pmW = nil; P.pos = nil
    pcall(api.setAvatar, nil); P.retryAt = now + 2
    return ok
end
M.snapExtra = S2L.pmWrap(M.snapExtra)

-- ===== b83anim (flag pupanimfix, default 0). Measured 2026-10-14 22:27 (host): the re-adopted puppet had Mesh.AnimScriptInstance = nil
-- with AnimationMode 0, although b80 had set AnimBP on it at adoption (ok=true). nil fails the game's Cast<AnimInstanceHuman> like single-node
-- (crash 0x658). Every second: puppet + hidden target without an AnimInstanceHuman instance -> SetAnimationMode(0, true), then
-- SetAnimInstanceClass(class of a healthy body's instance). Healthy bodies are only read. 2 = a body still broken after 5 tries is hidden + dropped.
function S2L.animFix(ST)
    local fx = FL.pupanimfix or 0
    if fx ~= 0 and not S2L.afOff then S2L.afOff = true; log("S2CORE animfix: pupanimfix=" .. tostring(fx) .. " ignorado (b112: SetAnimationMode quebra o controlador, D10; abpguard cuida)") end
    do return end                                      -- b112 (#2): no-op; the code below is kept unreachable for reference
    if FL.bodysafe == 1 and not (ST.wg and ST.wg.open) then return end     -- batch 44: bodies only in a stable world
    if ST.far and ST.far.on then return end                                -- core-21: far body, no NPC refs held
    local function nm(o) local s = "nulo"; pcall(function() if valid(o) then s = o:GetFName():ToString() end end); return s end
    local function human(mesh)                                             -- class chain walk, as the lamp check (GetSuperStruct)
        local ok, ai = false, nil
        pcall(function()
            ai = mesh:GetAnimInstance(); if not valid(ai) then return end
            local c = ai:GetClass()
            for _ = 1, 8 do if not valid(c) then return end; if c:GetFName():ToString() == "AnimInstanceHuman" then ok = true; return end; c = c:GetSuperStruct() end
        end)
        return ok, ai
    end
    if not valid(S2L.afCls) then                                          -- learn the repair class first (target, else puppet)
        for _, B in ipairs({ST.tgt or {}, ST.pup or {}}) do if not valid(S2L.afCls) and bodyOk(B) then
            pcall(function() local ok, ai = human(B.pawn.Mesh); if ok then S2L.afCls = ai:GetClass() end end)
        end end
    end
    local now = os.clock()
    for _, e in ipairs({{"boneco", ST.pup}, {"alvo", ST.tgt}}) do
        local tag, B = e[1], e[2]
        if B and (tag ~= "boneco" or FL.pupanimbp == 1) and bodyOk(B) then
            local mesh; pcall(function() mesh = B.pawn.Mesh end)
            local key = B.path or nm(B.pawn)
            if B.afKey ~= key then B.afKey = key; B.afN = 0 end
            local ok, ai = false, nil
            if valid(mesh) then ok, ai = human(mesh) end
            if ok then
                if not valid(S2L.afCls) then pcall(function() S2L.afCls = ai:GetClass() end) end   -- the repair class (normally AnimBP_Human_C)
                B.afN = 0
            elseif valid(mesh) and B.afN < 5 then
                B.afN = B.afN + 1
                local was = "?"
                pcall(function() was = string.format("instancia=%s modo=%s AnimClass=%s malha=%s", nm(ai), tostring(mesh:GetAnimationMode()), nm(mesh.AnimClass), nm(mesh:GetSkeletalMeshAsset())) end)
                local okM = pcall(function() mesh:SetAnimationMode(0, true) end)
                local how = "modo0 ok=" .. tostring(okM)
                ok, ai = human(mesh)
                if not ok and valid(S2L.afCls) then
                    local own; pcall(function() own = mesh.AnimClass end)
                    if nm(own) ~= nm(S2L.afCls) then                       -- the same class would be a no-op in UE
                        local okC = pcall(function() mesh:SetAnimInstanceClass(S2L.afCls) end)
                        how = how .. " +classe " .. nm(S2L.afCls) .. " ok=" .. tostring(okC)
                        ok, ai = human(mesh)
                    end
                end
                if ok and tag == "boneco" then B.abpFor = B.pawn; B.state = nil; B.rate = nil end
                if ok or B.afN == 1 or B.afN == 5 then
                    log(string.format("S2CORE animfix: %s %s sem AnimInstanceHuman (%s) -> %s -> %s (tentativa %d)", tag, key:match("[^%.]+$") or key, was, how,
                        ok and ("restaurado " .. nm(ai)) or ("ainda " .. nm(ai)), B.afN))
                end
                if not ok and B.afN == 5 and fx == 2 then                  -- fallback: the game must not keep reading this body
                    pcall(function() B.pawn:SetActorHiddenInGame(true) end)
                    if tag == "boneco" then
                        B.bad = B.bad or {}; if B.path then B.bad[B.path] = true end   -- batch 52 list: never adopted again
                        B.pawn = nil; B.path = nil; B.state = nil; B.hidden = nil; pcall(api.setAvatar, nil)
                    else B.pawn = nil; B.path = nil; B.setup = nil end
                    B.retryAt = now + 2
                    log("S2CORE animfix: " .. tag .. " irrecuperavel -> escondido e largado (novo corpo sera adotado/pedido)")
                end
            end
        end
    end
end

function S2L.pupNoGun(P)                       -- b184g19 (pupnogun=1): the NPC body's own gun (prototype item generator) out of its hands
    local now = os.clock()
    if now < (P.ngAt or 0) or not valid(P.pawn) then return end
    P.ngAt = now + 1
    local w = P.pawn:GetWeaponInHandsMeshComponent()
    if not valid(w) then return end
    local nm = "?"; pcall(function() nm = w:GetSkeletalMeshAsset():GetFName():ToString() end)
    local ok = pcall(function() P.pawn:RemoveWeaponFromHands() end)
    pcall(function() w:SetVisibility(false, true) end)
    P.ngN = (P.ngN or 0) + 1
    if P.ngN <= 10 or P.ngN % 50 == 0 then log(string.format("S2CORE pupnogun: arma propria do boneco %s fora das maos ok=%s (vez %d)", nm, tostring(ok), P.ngN)) end
end
-- remote weapon on the puppet: a PoseableMeshComponent attached to the NPC's hand socket + its static attachments
function S2L.pupWeapon(P)
    if (FL.pupnogun or 0) == 1 then pcall(S2L.pupNoGun, P) end   -- b184g19
    if (FL.pwsnap or 0) == 1 then local w1 = P.wcomps and P.wcomps[1]; if valid(w1) then P.wsnap = (P.wsnap == 0.001) and 0 or 0.001; pcall(function() w1:K2_SetRelativeLocation({X = P.wsnap, Y = 0, Z = 0}, false, {}, true) end) end end   -- b184g19
    local head = S2.io and S2.io.head
    local x1 = (FL.x1c or 0) == 1 and S2L.x1Raw(head) or head and head:match('"x1"%s*:%s*"(.-)"') or ""   -- b143perf x1c
    x1 = x1:match("^([^#]*)") or ""                      -- weapon part only (damage events follow the #)
    if x1:sub(1, 3) == "W2~" then                         -- by hash: empty until main.lua has the text (it asks for it in "need")
        x1 = api.lookGet and api.lookGet(x1:sub(4)) or ""
    end
    if FL.pupweapon ~= 1 then x1 = "W1~~" end
    if x1 == "" or x1 == P.wstr then return end
    P.wstr = x1
    for _, c in ipairs(P.wcomps or {}) do pcall(function() if c:IsValid() then c:K2_DestroyComponent(c) end end) end
    P.wcomps = {}
    local part, statics = x1:match("^W1~(.-)~(.*)$")
    if not part or part == "" then log("S2CORE puppet: arma remota = nenhuma"); return end
    local pth, _, pname, psock = part:match("^(.-)@(.-)@([^@]*)@?(.*)$")
    local wa = asset(pth)
    if not wa then log("S2CORE puppet: mesh da arma indisponivel " .. tostring(pth)); return end
    local mesh = P.pawn.Mesh
    local sock = (psock ~= "" and psock) or "GripPointTPP"
    local has = false; pcall(function() has = mesh:DoesSocketExist(FName(sock)) end)
    local wc = P.pawn:AddComponentByClass(StaticFindObject("/Script/Engine.PoseableMeshComponent"), true, T0, false)
    if not valid(wc) then log("S2CORE puppet: AddComponentByClass da arma falhou (corpo invalido?)"); return end
    wc:SetSkinnedAssetAndUpdate(wa, true)
    wc:K2_AttachToComponent(mesh, FName(sock), 2, 2, 2, false)
    P.wcomps[#P.wcomps + 1] = wc
    local byName, ns, pend = {[pname] = wc}, 0, {}
    for ent in statics:gmatch("[^|]+") do pend[#pend + 1] = ent end
    for _ = 1, 5 do
        local rest = {}
        for _, ent in ipairs(pend) do
            local snm, spar, ssock, spath, tr = ent:match("^(.-)>(.-)>(.-)>(.-)>(.*)$")
            if snm and byName[spar] then
                pcall(function()
                    local sa = asset(spath); if not sa then return end
                    local v = {}; for num in tr:gmatch("[^,]+") do v[#v + 1] = tonumber(num) end
                    local sc = P.pawn:AddComponentByClass(StaticFindObject("/Script/Engine.StaticMeshComponent"), true, T0, false)
                    sc:SetStaticMesh(sa); sc:SetCollisionEnabled(0)
                    sc:K2_AttachToComponent(byName[spar], FName(ssock), 2, 2, 2, false)
                    sc:K2_SetRelativeLocationAndRotation({X = v[1], Y = v[2], Z = v[3]}, {Pitch = v[4], Yaw = v[5], Roll = v[6]}, false, {}, true)
                    sc:SetRelativeScale3D({X = v[7], Y = v[8], Z = v[9]})
                    byName[snm] = sc; ns = ns + 1; P.wcomps[#P.wcomps + 1] = sc
                end)
            elseif snm then rest[#rest + 1] = ent end
        end
        pend = rest; if #pend == 0 then break end
    end
    log(string.format("S2CORE puppet: arma remota %s soquete=%s (existe=%s) estaticos=%d sem_pai=%d", pth:match("[^%.]+$"), sock, tostring(has), ns, #pend))
end

-- ===== batch 44: bodies only in a stable world. Crash 2026-10-12 11:22:42 / 11:45:06 (host, read 0x0) = XSpawnObjBySID called from Lua
-- during a load (exe+0x6711e78 is inside the exec of XSpawnObjBySID). The local pawn is the clock: gone or changed = the world is being
-- replaced, so the stored puppet / target / far-body references are dropped WITHOUT any call on them, and nothing touches or spawns a body
-- until the (new) local pawn has been the same live object for FL.bodywait s.
function S2L.bodyGate(ST)
    if FL.bodysafe ~= 1 then return true end
    local W = ST.wg or {}; ST.wg = W
    local now = os.clock()
    local key
    pcall(function()
        local pw = FindFirstOf("BP_Stalker2Character_C")
        if pw and pw:IsValid() then key = tostring(pw:GetAddress()) .. "|" .. pw:GetFullName() end
    end)
    local function drop(why)                       -- batch 52: shared by "pawn gone/changed" and "own big move"
            local P, T, F = ST.pup or {}, ST.tgt or {}, ST.far or {}
            local had = (P.pawn or T.pawn or P.spawnAt or T.spawnAt or F.actor) and true or false
            for _, B in ipairs({P, T}) do
                B.pawn = nil; B.path = nil; B.setup = nil; B.ref = nil; B.state = nil; B.pos = nil; B.wcomps = {}; B.wstr = nil
                B.spawnAt = nil; B.before = nil; B.asks = 0; B.rounds = nil; B.gaveUp = nil; B.retryAt = 0; B.adoptOnly = nil; B.findAt = nil
            end
            P.src = nil; F.actor = nil; F.hidden = nil
            pcall(api.setAvatar, nil)
            log("S2CORE corpos: " .. why .. " -> referencias de boneco/alvo largadas sem tocar (havia=" .. tostring(had) .. "); corpos parados")
    end
    if key ~= W.key then
        if W.key then drop("jogador local " .. (key and "trocado" or "sumiu") .. " (load?)") end
        W.key = key; W.since = key and now or nil; W.lp = nil
    end
    if key and FL.tpdrop > 0 then                  -- batch 52: an own teleport (leash, join) moves the world under the bodies like a load
        local q; pcall(function() q = FindFirstOf("BP_Stalker2Character_C"):K2_GetActorLocation() end)
        if q then
            if W.lp and (q.X - W.lp.X) ^ 2 + (q.Y - W.lp.Y) ^ 2 > FL.tpdrop * FL.tpdrop then
                drop(string.format("jogador local saltou %.0f m (teleporte)", math.sqrt((q.X - W.lp.X) ^ 2 + (q.Y - W.lp.Y) ^ 2) / 100))
                W.since = now
            end
            W.lp = {X = q.X, Y = q.Y}
        end
    end
    local open = key ~= nil and now - (W.since or now) >= FL.bodywait
    if open and not W.open then log(string.format("S2CORE corpos: jogador local estavel ha %d s -> corpos liberados", FL.bodywait)) end
    W.open = open
    return open
end
function S2L.bodyHeld(B)                     -- batch 44: a stored body is used only after the path check, never on IsValid() alone
    if FL.bodysafe == 1 then return bodyOk(B) end
    return valid(B.pawn)
end
local function bodyLost(ST)                    -- batch 44: one of our bodies was freed: treat it as a world change, hold the bodies bodywait s
    if FL.bodysafe == 1 and ST.wg and ST.wg.since then ST.wg.since = math.max(ST.wg.since, os.clock()); ST.wg.open = false end
end
-- ===== batch 45: the puppet takes any live body of its own prototype. Round 37: puppet bodies came later than the 20 s window, were then
-- listed in the next request's "before" set and never adopted; every request leaked one more visible body (13 at the end).
function S2L.pupNum(o) local n; pcall(function() n = tonumber(o:GetFName():ToString():match("(%d+)$")) end); return n or 0 end
function S2L.pupFind(ST, P, lp, hpLo, hpHi)
    local T = ST.tgt
    local best, bNew, bNum
    for _, o in ipairs(FindAllOf("Pawn") or {}) do pcall(function()
        if not o:IsValid() or o == P.pawn or (T and o == T.pawn) then return end
        if not o:GetFullName():find("BP_AI_Human_C", 1, true) then return end
        local mh = o:GetMaxHP(); if mh < hpLo or mh > hpHi then return end
        if not o:IsAlive() then return end                         -- a pooled body (alive=false at 0,0,0) is not taken
        if P.bad and P.bad[(o:GetFullName():gsub("^%S+ ", ""))] then return end   -- batch 52: a body dropped for a null mesh is not taken back
        local mok = false; pcall(function() local m = o.Mesh; mok = m ~= nil and m:IsValid() and true or false end)
        if not mok then return end                                 -- batch 52: nor any body without a live mesh
        local q = o:K2_GetActorLocation()
        if (q.X - lp.X)^2 + (q.Y - lp.Y)^2 > FL.pupadoptr^2 then return end
        local new = not (P.before and P.before[o:GetFullName()])
        local num = S2L.pupNum(o)
        -- first a body that appeared after the current request, then the newest name (instance numbers count down: r36/r37 logs)
        if not best or (new and not bNew) or (new == bNew and num < bNum) then best, bNew, bNum = o, new, num end
    end) end
    return best
end
local function pupSpawn(ST, P, r, lp, now, sid, hpLo, hpHi, tag)
    sid = sid or "S2Coop_RemotePlayer"; hpLo = hpLo or 90000; hpHi = hpHi or 1e9; tag = tag or "puppet"
    if P.spawnAt then                                   -- waiting for the pawn requested earlier (polled once per second)
        if now < P.spawnAt then return end
        P.spawnAt = now + 1
        local best, bd
        if tag == "puppet" and FL.pupadopt == 1 then best = S2L.pupFind(ST, P, lp, hpLo, hpHi); bd = 0 end   -- batch 45: any body of ours, any time
        for _, o in ipairs(bd == nil and FindAllOf("Pawn") or {}) do pcall(function()
            if o:IsValid() and not P.before[o:GetFullName()] and o:GetFullName():find("BP_AI_Human_C", 1, true) and o:GetMaxHP() >= hpLo and o:GetMaxHP() <= hpHi then
                local q = o:K2_GetActorLocation(); local d = (q.X - P.sx)^2 + (q.Y - P.sy)^2
                if d < 800^2 and (not bd or d < bd) then best, bd = o, d end
            end end) end
        if not best then
            if P.adoptOnly then P.adoptOnly = nil; P.spawnAt = nil; P.before = nil; return end   -- batch 45: the existing body went away; a request may follow
            local win = (tag == "puppet" and FL.pupadopt == 1) and FL.pupwin or 20
            if now - P.askedAt < win then return end       -- still waiting; do NOT spawn another one
            P.spawnAt = nil; P.before = nil; P.retryAt = now + 10
            log("S2CORE " .. tag .. ": nenhum pawn novo em " .. tostring(win) .. " s (pedidos=" .. tostring(P.asks) .. ")"); return
        end
        local adoptOnly, askedAt = P.adoptOnly, P.askedAt; P.adoptOnly = nil
        P.spawnAt = nil; P.before = nil; P.asks = 0; P.gaveUp = nil; P.rounds = nil   -- core-14c: a successful adoption resets the request counter
        pcall(function() P.path = (best:GetFullName():gsub("^%S+ ", "")) end)
        P.pawn = best; P.state = nil; P.rate = nil; P.hidden = false; P.pos = nil; P.yaw = nil; P.wstr = nil; P.wcomps = {}; P.ref = nil
        pcall(function() P.uid = StaticFindObject("/Script/Stalker2.Default__CppMediator"):GetGUID(best) end)
        if tag == "target" then pcall(function() best:SetActorHiddenInGame(true) end) end      -- batch 38: hidden in the call that adopts it
        if tag == "puppet" then
            pcall(function() best:SetActorHiddenInGame(false) end)                             -- batch 38: it may have been hidden while pending
            api.setAvatar(best)
            local okC = pcall(function() best:SetActorEnableCollision(false) end)   -- visuals only: bullets must pass through to the hidden target
            log("S2CORE puppet: colisao desligada ok=" .. tostring(okC))
        end
        log("S2CORE " .. tag .. ": adotado " .. best:GetFullName():match("[^%.]+$") .. " uid=" .. tostring(P.uid) .. " maxhp=" .. tostring(best:GetMaxHP())
            .. ((tag == "puppet" and FL.pupadopt == 1) and string.format(" (%s %.1f s)", adoptOnly and "corpo ja existente, sem pedido novo;" or "depois do pedido:", now - (askedAt or now)) or ""))
        return
    end
    local ses = ST.peer and ST.peer.sessions                  -- batch 43: a new session of the other side starts the counting again
    if FL.pupretry == 1 and P.ses ~= ses then
        if P.ses ~= nil and ((P.asks or 0) > 0 or P.rounds or P.gaveUp) then log("S2CORE " .. tag .. ": nova sessao do outro lado; pedidos de spawn zerados") end
        P.ses = ses; P.asks = 0; P.rounds = nil; P.gaveUp = nil; P.retryAt = 0
    end
    if tag == "puppet" and FL.pupadopt == 1 and now >= (P.findAt or 0) then   -- batch 45: a body of ours already exists: adopt it, no new request
        P.findAt = now + 1
        if S2L.pupFind(ST, P, lp, hpLo, hpHi) then P.before = {}; P.sx, P.sy = lp.X, lp.Y; P.askedAt = now; P.spawnAt = now; P.adoptOnly = true; return end
    end
    if now < (P.retryAt or 0) then return end
    if FL.pupretry == 1 and (P.asks or 0) >= 4 then           -- batch 43: never give up while the other player is up; wait, then 4 more
        P.rounds = (P.rounds or 0) + 1
        local w = FL.pupback * (P.rounds > 1 and 2 or 1)
        P.asks = 0; P.gaveUp = nil; P.retryAt = now + w
        log(string.format("S2CORE %s: 4 pedidos de spawn sem sucesso; nova rodada em %d s (rodada %d)", tag, w, P.rounds))
        return
    end
    if (P.asks or 0) >= 4 then if not P.gaveUp then P.gaveUp = true; log("S2CORE " .. tag .. ": 4 pedidos de spawn sem sucesso; parei (S2.state." .. (tag == "puppet" and "pup" or "tgt") .. ".asks = 0 para tentar de novo)") end; return end
    if not P.force and FL.maxdist > 0 and (r.x - lp.X)^2 + (r.y - lp.Y)^2 > FL.maxdist^2 then return end
    if ST.cl and ST.cl.pend then return end   -- b138loot: one spawn in flight (a loot body is being made)
    if FL.bodysafe == 1 and now - (P.lastSpawn or -1e9) < FL.spawngap then return end   -- batch 44: at most one request per body per spawngap s
    local sx, sy, sz = r.x, r.y, r.z
    if (sx - lp.X)^2 + (sy - lp.Y)^2 > 3000^2 then sx, sy, sz = lp.X + 300, lp.Y, lp.Z end   -- XSpawnObjBySID only works near the local player
    P.before = {}; for _, o in ipairs(FindAllOf("Pawn") or {}) do P.before[o:GetFullName()] = true end
    P.sx, P.sy = sx, sy
    P.lastSpawn = now; P.adoptOnly = nil                  -- batch 45: a real request waits its full window
    local ok, e = pcall(function() FindFirstOf("CustomConsoleManagerRK"):XSpawnObjBySID(sid, 1, S2L.bodyOD(P, tag), sx, sy, sz + 50, 0.0, 0.0, 0.0) end)
    log("S2CORE " .. tag .. ": XSpawnObjBySID " .. sid .. " ok=" .. tostring(ok) .. " " .. (ok and "" or tostring(e)))
    P.spawnAt = now + 1.5; P.askedAt = now; P.asks = (P.asks or 0) + 1; P.gaveUp = nil
end

-- ===== core-21: FAR BODY. The NPC puppet is switched offline (pooled) by the A-Life beyond its online radius around the local
-- player (CoreVariables ALifeGridVisionRadius = 8500 cm; measured pooled at 100-160 m). Beyond FL.pupfar the remote player is shown by a
-- plain Actor (SkeletalMeshComponents copied from the puppet: never pooled, no AI, no collision); within pupfar - 10 m the NPC puppet is back.
-- The look (mesh + material paths) is captured once from a live puppet and kept in <bridge>/farlook.txt.
function S2L.farLookFile() return (rawget(_G, "BRIDGE_DIR") or "") .. "farlook.txt" end
function S2L.farCapture(ST, pawn)
    local list = {}
    for _, c in ipairs(api.compList(pawn, "/Script/Engine.SkeletalMeshComponent") or {}) do pcall(function()
        local a = c:GetSkeletalMeshAsset(); if not valid(a) then return end
        local mats = {}
        for i = 0, 15 do
            local ok, m = pcall(function() return c:GetMaterial(i) end)
            if not ok or not valid(m) then break end
            mats[#mats + 1] = (m:GetFullName():gsub("^%S+ ", ""))
        end
        list[#list + 1] = {path = (a:GetFullName():gsub("^%S+ ", "")), mats = mats, main = (c:GetFName():ToString() == "CharacterMesh0")}
    end) end
    if #list == 0 then return nil end
    table.sort(list, function(x, y) return x.main and not y.main end)
    local lines = {}
    for _, e in ipairs(list) do lines[#lines + 1] = e.path .. "|" .. table.concat(e.mats, ",") end
    local f = io.open(S2L.farLookFile(), "w"); if f then f:write(table.concat(lines, "\n")); f:close() end
    log("S2CORE farbody: aparencia capturada do boneco: " .. #list .. " malhas")
    return list
end
function S2L.farLoad()
    local f = io.open(S2L.farLookFile(), "r"); if not f then return nil end
    local list = {}
    for ln in f:lines() do
        local p, m = ln:match("^([^|]+)|?(.*)$")
        if p then local mats = {}; for x in (m or ""):gmatch("[^,]+") do mats[#mats + 1] = x end; list[#list + 1] = {path = p, mats = mats} end
    end
    f:close()
    return #list > 0 and list or nil
end
function S2L.farBuild(ST, F, pos)
    local look = ST.farLook
    local a = FindFirstOf("PlayerController"):GetWorld():SpawnActor(StaticFindObject("/Script/Engine.Actor"), pos, {Pitch = 0, Yaw = 0, Roll = 0})
    if not valid(a) then return false end
    local main
    for i, e in ipairs(look) do pcall(function()
        local sk = asset(e.path); if not sk then return end
        local c = a:AddComponentByClass(StaticFindObject("/Script/Engine.SkeletalMeshComponent"), false, T0, false)
        c:SetSkinnedAssetAndUpdate(sk, true)
        for mi, mp in ipairs(e.mats) do local m = asset(mp); if m then pcall(function() c:SetMaterial(mi - 1, m) end) end end
        pcall(function() c:SetCollisionEnabled(0) end)
        pcall(function() c.VisibilityBasedAnimTickOption = 0 end)
        if not main then main = c else pcall(function() c:SetLeaderPoseComponent(main, true, true) end) end
    end) end
    if not main then pcall(function() a:K2_DestroyActor() end); return false end
    F.actor = a; F.main = main; F.state = nil; F.pos = nil
    log("S2CORE farbody: corpo distante criado (" .. #look .. " malhas)")
    return true
end
function S2L.farTick(ST, F, r, lp, now)
    local P = ST.pup
    if bodyOk(P) then                                   -- the NPC puppet cannot live this far: take its look, then hide it
        if not ST.farLook then ST.farLook = S2L.farCapture(ST, P.pawn) end
        if not P.hidden then P.hidden = true; pcall(function() P.pawn:SetActorHiddenInGame(true) end) end
        -- b70c: drop the references while the bodies are still alive. The A-Life pools them past ~85 m and the stale
        -- pointers crashed the 50 m return (host 2026-10-12 22:35 and 2026-10-14 17:27, "boneco NPC de volta").
        P.pawn = nil; P.path = nil; P.state = nil; P.hidden = nil; api.setAvatar(nil)
        local T = ST.tgt; if T then T.pawn = nil; T.path = nil; T.setup = nil end
        log("S2CORE farbody: referencias do boneco e do alvo largadas (corpo escondido)")
    end
    if not ST.farLook then ST.farLook = S2L.farLoad(); if ST.farLook then log("S2CORE farbody: aparencia lida de farlook.txt") end end
    if not ST.farLook then                             -- first time ever: spawn the puppet once near the local player only to copy its look
        if not bodyOk(P) and FL.avatar == 1 and now - r.recv < 5 then P.force = true; pupSpawn(ST, P, r, lp, now) end
        return
    end
    local age = now - r.recv
    local away = age > 5 or FL.avatar ~= 1 or (M.remoteLife()) == "d" or M.remoteCine(ST)
    if not valid(F.actor) then
        F.actor = nil
        if away or now < (F.retryAt or 0) then return end
        F.retryAt = now + 5
        if not S2L.farBuild(ST, F, {X = r.x, Y = r.y, Z = r.z - 90}) then return end
    end
    if away ~= (F.hidden or false) then F.hidden = away; pcall(function() F.actor:SetActorHiddenInGame(away) end) end
    if away then return end
    local dt = math.min(math.max(now - (F.lastT or now), 0.001), 0.25); F.lastT = now
    local vx, vy = S2L.rVel(ST, r)                          -- b116b (rvder)
    if age > 0.6 then vx, vy = 0, 0 end
    local ex = math.min(age, 0.3) + FL.avlead / 1000
    local tx, ty, tz = r.x + vx * ex, r.y + vy * ex, r.z - 90           -- actor origin at the feet (the pawn's location is the capsule centre)
    if not F.pos or (tx - F.pos.X)^2 + (ty - F.pos.Y)^2 > 2000 * 2000 then F.pos = {X = tx, Y = ty, Z = tz} end
    local k = 1 - math.exp(-dt * FL.avgain)
    F.pos = {X = F.pos.X + (tx - F.pos.X) * k, Y = F.pos.Y + (ty - F.pos.Y) * k, Z = F.pos.Z + (tz - F.pos.Z) * k}
    local speed = math.sqrt(vx * vx + vy * vy)
    local want = (speed > 100 and math.deg(math.atan(vy, vx))) or r.yaw or F.yaw or 0
    F.yaw = (F.yaw or want) + api.angDiff(want, F.yaw or want) * (1 - math.exp(-dt * 10))
    F.actor:K2_SetActorLocation(F.pos, false, {}, true)
    F.actor:K2_SetActorRotation({Pitch = 0, Yaw = F.yaw - 90, Roll = 0}, false)   -- character meshes face +Y; the pawn's mesh carries the same -90
    local st = (speed < 30 and "idle") or (speed < WALK_MAX and "walk") or "run"
    if st ~= F.state then
        local q = seqs[st]; if not valid(q) then q = asset(PUP_ANIM[st]); seqs[st] = q end
        if q and pcall(function() F.main:PlayAnimation(q, true) end) then F.state = st end
    end
end
function S2L.farHide(F)
    if valid(F.actor) then pcall(function() F.actor:SetActorHiddenInGame(true) end) end
    F.hidden = true
end

-- ===== b114smooth (flags nsmooth / nsuro / pupcmoff, default 0). User 2026-10-15: "the other player skips frames", "client NPCs move like
-- low framerate", "predict the movements between real data". WHOLE FAMILY = every actor the mod places from network data: the remote-player
-- puppet (pupTick/pupMove) and the client NPC mirrors (main.lua applyMirrors). Excluded: hidden AI target (invisible), far body (farTick),
-- dmir (off). Rate: no per-frame Lua hook exists and Lua loops < 100 ms are banned, so Lua corrects at 10 Hz and the ENGINE moves the actor
-- every frame: ONE ProjectileMovementComponent per actor (gravity 0, no sweep, no bounce), Velocity = sample velocity x share of the next tick
-- still inside nscap ms + (target - actor) x min(nsgain, 0.9/tick); error > nssnap = one teleport. Mirror velocity = change of the last two
-- CHANGED samples / sender clock "t" (an unchanged sample while the host stands = the NPC stopped; while the host walks its scan is paused,
-- so an unchanged sample is stale and only runs out at nscap). Mirrors: main.lua still places the raw sample, the M.applyMirrors wrapper
-- takes it as the target and puts the actor back on its smooth path in the same call. Puppet orphan guard: mover zeroed when pupTick stops
-- driving it (hidden, far, held) and in the same tick its body is dropped (ref <= 1 tick old, valid() first).
function S2L.nsErr(ST, where, e)
    S2.errs.n = S2.errs.n + 1; S2.errs.last = where .. ": " .. tostring(e):sub(1, 160)
    ST.nsErrN = (ST.nsErrN or 0) + 1
    if ST.nsErrN <= 5 or ST.nsErrN % 100 == 0 then log("S2CORE nsmooth erro " .. where .. " #" .. ST.nsErrN .. ": " .. tostring(e)) end
end
function S2L.nsS0(now) return {at = now, n = 0, dr = 0, sn = 0, held = 0, es = 0, en = 0, emax = 0, ms = 0, msmax = 0, mk = 0, fail = 0, mir = 0, pup = 0, smp = 0} end
function S2L.nsSt(ST)
    local N = ST.ns
    if not N then N = {k = {}, s = S2L.nsS0(os.clock())}; ST.ns = N end
    return N
end
function S2L.nsUp(f, name)                                 -- upvalue `name` of function f (debug library), else nil
    local dg = rawget(_G, "debug")
    if type(f) ~= "function" or not (dg and dg.getupvalue) then return nil end
    for i = 1, 255 do
        local n, v = dg.getupvalue(f, i)
        if n == nil then return nil end
        if n == name then return v end
    end
end
function S2L.nsMirTab(N, now)                              -- main.lua's local `mirrors` table and applyMirrors, found once
    if N.mt and N.orig then return N.mt end
    if now < (N.mtTry or 0) then return nil end
    N.mtTry = now + 10
    local am = S2L.nsUp(api.mirrorsClear, "applyMirrors")
    local mt = S2L.nsUp(am, "mirrors")
    if type(am) == "function" and type(mt) == "table" then N.orig, N.mt = am, mt end
    if N.mt or not N.mtLog then
        N.mtLog = true
        log("S2CORE nsmooth: espelhos do main.lua " .. (N.mt and "achados (applyMirrors + mirrors por debug.getupvalue)" or ("NAO achados (debug=" .. type(rawget(_G, "debug")) .. "); so o boneco e suavizado")))
    end
    return N.mt
end
function S2L.nsRmc(N, K, a)                                -- nsyaw=1: RotatingMovementComponent, rate 0 at once (its default is 180 deg/s)
    N.cR = N.cR or StaticFindObject("/Script/Engine.RotatingMovementComponent")
    if not valid(N.cR) then N.cR = nil; return end
    local rmc
    pcall(function() rmc = a:GetComponentByClass(N.cR) end)
    if not valid(rmc) then pcall(function() rmc = a:AddComponentByClass(N.cR, false, T0, false) end) end
    if not valid(rmc) then N.s.fail = N.s.fail + 1; return end
    pcall(function() rmc.RotationRate = {Pitch = 0, Yaw = 0, Roll = 0} end)
    pcall(function() rmc.bRotationInLocalSpace = false end)
    pcall(function() rmc.PivotTranslation = {X = 0, Y = 0, Z = 0} end)
    K.rmc = rmc
end
function S2L.nsMover(N, a, key)                            -- the actor's mover record (ProjectileMovementComponent), made once per actor
    local addr = S2L.pmAddr(a)
    local K = N.k[key]
    if K and K.addr == addr and valid(K.pmc) then
        if FL.nsyaw == 1 and not K.rmc then S2L.nsRmc(N, K, a) end
        K.a = a
        return K, false
    end
    N.cP = N.cP or StaticFindObject("/Script/Engine.ProjectileMovementComponent")
    if not valid(N.cP) then N.cP = nil; N.s.fail = N.s.fail + 1; return nil end
    local pmc, made = nil, false
    pcall(function() pmc = a:GetComponentByClass(N.cP) end)
    if not valid(pmc) then pcall(function() pmc = a:AddComponentByClass(N.cP, false, T0, false) end); made = true end
    if not valid(pmc) then N.s.fail = N.s.fail + 1; return nil end
    local bad = {}
    for _, kv in ipairs({{"ProjectileGravityScale", 0.0}, {"bSweepCollision", false}, {"bShouldBounce", false}, {"bRotationFollowsVelocity", false},
                         {"bInitialVelocityInLocalSpace", false}, {"MaxSpeed", 0.0}, {"bSimulationEnabled", true}}) do
        if not pcall(function() pmc[kv[1]] = kv[2] end) then bad[#bad + 1] = kv[1] end
    end
    pcall(function() pmc.Velocity = {X = 0, Y = 0, Z = 0} end)             -- its default is (1,0,0)
    pcall(function() if not pmc:IsActive() then pmc:Activate(true) end end)
    local upd = false
    pcall(function() local u = pmc.UpdatedComponent; if not valid(u) then pmc:SetUpdatedComponent(a:K2_GetRootComponent()); u = pmc.UpdatedComponent end; upd = valid(u) end)
    K = {addr = addr, a = a, pmc = pmc, st = os.clock(), vx = 0, vy = 0, vz = 0}
    if FL.nsyaw == 1 then S2L.nsRmc(N, K, a) end
    N.k[key] = K
    if made then N.s.mk = N.s.mk + 1 end
    N.logMk = (N.logMk or 0) + 1
    if N.logMk <= 10 or N.logMk % 100 == 0 then
        local cn = "?"; if valid(a) then pcall(function() cn = a:GetFName():ToString() end) end
        log(string.format("S2CORE nsmooth: movedor %s em %s (%s) UpdatedComponent=%s yaw=%s%s", made and "criado" or "reusado", key, cn, tostring(upd),
            K.rmc and "por quadro" or "10 Hz", #bad > 0 and (" falhas=" .. table.concat(bad, ",")) or ""))
    end
    return K, true
end
function S2L.nsSample(K, x, y, z, yaw, ts, now, vx, vy, vz, still)   -- a sample in; vx nil = derive from the last CHANGED sample
    S2L.nsHist(K, x, y, z, ts, now, vx ~= nil)                -- b148pose: path history (used by nsip / nsnoback only)
    if yaw ~= nil then K.yaw = yaw end
    if vx then K.x, K.y, K.z, K.vx, K.vy, K.vz, K.st, K.ts = x, y, z, vx, vy, vz or 0, now, ts; return true end
    if K.x == nil then K.x, K.y, K.z, K.vx, K.vy, K.vz, K.st, K.ts = x, y, z, 0, 0, 0, now, ts; return true end
    local dx, dy, dz = x - K.x, y - K.y, z - K.z
    if dx * dx + dy * dy + dz * dz < 0.25 then
        if still then K.vx, K.vy, K.vz, K.st = 0, 0, 0, now end           -- fresh scan, same spot: the NPC stopped (K.ts stays = last change)
        return false
    end
    local dt = (K.ts and ts) and (ts - K.ts) or 0
    local vx2, vy2, vz2 = 0, 0, 0
    if dt > 0.03 and dt < 1 then vx2, vy2, vz2 = dx / dt, dy / dt, dz / dt end
    local mx = FL.nsvmax or 1500
    if vx2 * vx2 + vy2 * vy2 + vz2 * vz2 > mx * mx then vx2, vy2, vz2 = 0, 0, 0 end   -- a jump, not motion
    K.x, K.y, K.z, K.vx, K.vy, K.vz, K.st, K.ts = x, y, z, vx2, vy2, vz2, now, ts
    return true
end
-- ===== b148pose (flags nsip / nsnoback, default 1; nscap default 250 -> 0). User 2026-10-15 21:47/22:00: the host's NPC mirrors on the
-- client move "like they are rollbacking". Measured 22:02-22:04 (rollback_probe.lua, per frame 3 s): with nscap=250 a dog that moved 394 cm
-- had 10 backward steps (max 36 cm), a bandit 23 = b114's dead reckoning overshoots and its error pull drags it back. WHOLE FAMILY = every
-- actor nsDrive moves (puppet, client NPC mirrors, npcsync-driven NPCs npsmode 2) + npcsync npsmode 1 (cmDrive: guard only).
-- nsip=1: every sample is kept with its sender time (S2L.nsHist, last 5); nsDrive follows the sample PATH one interval behind (K.T = EMA
-- of the sample gaps, + nsippad): velocity = (the path point 0.1 s ahead - actor) / 0.1 s = the path's own speed + the error pull, the
-- same whatever the real tick gap (a late tick neither stalls nor overshoots); never past the newest sample (end of path: 90 % of the gap). nsnoback=1: the XY velocity never points back along the path direction while
-- the target moved in the last nsbhold s, and the actor's own last velocity is never reversed faster than nsbmax cm/s (a late tick
-- overshoots a corner); later a backward settle is capped at nsbmax cm/s (< 2 cm per frame). nsip=0 nsnoback=0 nscap=250 = b114.
function S2L.nbSt(now)                                     -- b148pose: counters + the 10 s line
    local NB = S2L.NB
    if not NB then NB = {at = now, ip = 0, fim = 0, cut = 0, slow = 0, lag = 0}; S2L.NB = NB end
    if now - NB.at >= 10 then
        if NB.ip > 0 or NB.cut > 0 or NB.slow > 0 then
            log(string.format("S2CORE nsip 10 s: passos_interpolados=%d fim_do_caminho=%d atraso_med=%.0f ms recuos_cortados=%d recuos_lentos=%d (nsip=%s nsnoback=%s nscap=%s)",
                NB.ip, NB.fim, NB.lag / math.max(1, NB.ip) * 1000, NB.cut, NB.slow, tostring(FL.nsip), tostring(FL.nsnoback), tostring(FL.nscap)))
        end
        NB.at, NB.ip, NB.fim, NB.cut, NB.slow, NB.lag = now, 0, 0, 0, 0, 0
    end
    return NB
end
function S2L.nsHist(K, x, y, z, ts, now, fresh)            -- b148pose: last 5 samples {t = sender clock, x, y, z}; K.T gap EMA, K.off clock offset
    local t = type(ts) == "number" and ts or now
    local h = K.h
    if not h then h = {}; K.h = h end
    local L = h[#h]
    local moved = (not L) or (x - L.x) ^ 2 + (y - L.y) ^ 2 + (z - L.z) ^ 2 >= 0.25
    if not moved and not (fresh and now - (K.hAt or -10) >= 0.2) then return end   -- mirrors: unchanged = not read again yet
    if L and t < L.t - 1 then h = {}; K.h = h; K.off = nil; L = nil end   -- b152clock: the sender clock went back (host restart / load): new history
    if L and t <= L.t then
        if t == L.t and moved then L.x, L.y, L.z = x, y, z end      -- same sender time, new spot: keep the newest
        return                                                       -- older sender time (reorder / duplicate): dropped
    end
    local o = now - t
    if not K.off or o < K.off then K.off = o else K.off = K.off + (o - K.off) * 0.02 end
    if L then
        local g, T = t - L.t, K.T or 0.1
        if moved and g > math.max(2.5 * T, 0.4) then                  -- it stood (or the data paused): the motion starts one interval ago
            h[#h + 1] = {t = t - T, x = L.x, y = L.y, z = L.z}
        elseif g > 0.03 and g < 0.6 then K.T = T * 0.7 + g * 0.3 end
        if moved then K.dx, K.dy, K.mvAt = x - L.x, y - L.y, now end
    else K.mvAt = now end
    h[#h + 1] = {t = t, x = x, y = y, z = z}
    K.hAt = now
    while #h > 5 do table.remove(h, 1) end
end
function S2L.nsPath(K, now, hz)                            -- b148pose: the path point one interval behind, hz s ahead -> x, y, z, gain, dirx, diry
    local h = K.h
    local n = h and #h or 0
    if n == 0 or not K.off then return nil end
    local D = math.min(math.max(K.T or 0.1, 0.05), FL.nsipmax or 0.5) + (FL.nsippad or 0.05)
    local rt = now - K.off - D                              -- D covers the hz look-ahead: the aim is a known point
    local NB = S2L.nbSt(now)
    NB.ip, NB.lag = NB.ip + 1, NB.lag + D
    local L = h[n]
    if n == 1 or rt >= L.t then
        NB.fim = NB.fim + 1
        local P = h[n - 1]
        return L.x, L.y, L.z, 0.9, P and (L.x - P.x) or 0, P and (L.y - P.y) or 0   -- end: 90 % of the way, no extrapolation
    end
    for i = n - 1, 1, -1 do
        local P = h[i]
        if rt >= P.t or i == 1 then
            local Q = h[i + 1]
            local u = math.max(0, (rt - P.t) / (Q.t - P.t))
            return P.x + (Q.x - P.x) * u, P.y + (Q.y - P.y) * u, P.z + (Q.z - P.z) * u, 1, Q.x - P.x, Q.y - P.y
        end
    end
end
function S2L.nsNoBack(K, vx, vy, dx, dy, now, ox, oy)     -- b148pose: XY velocity never points back along (dx, dy); then vs (ox, oy)
    if ox and ox * ox + oy * oy > 900 then                   -- the actor's own last velocity (>= 30 cm/s): reversing it <= nsbmax cm/s
        vx, vy = S2L.nsNoBack(K, vx, vy, dx, dy, now)        -- (a late 10 Hz tick overshoots a corner: no fast pull back)
        dx, dy = ox, oy
        if now - (K.mvAt or -10) >= (FL.nsbhold or 0.5) then return vx, vy end
        local d = math.sqrt(dx * dx + dy * dy)
        local a = (vx * dx + vy * dy) / d
        local lim = -(FL.nsbmax or 60)
        if a < lim then local NB = S2L.nbSt(now); NB.slow = NB.slow + 1; vx, vy = vx + (lim - a) * dx / d, vy + (lim - a) * dy / d end
        return vx, vy
    end
    local d2 = (dx or 0) ^ 2 + (dy or 0) ^ 2
    if d2 < 1 then return vx, vy end
    local d = math.sqrt(d2)
    local ux, uy = dx / d, dy / d
    local a = vx * ux + vy * uy
    if a >= 0 then return vx, vy end
    local lim = (now - (K.mvAt or -10) < (FL.nsbhold or 0.5)) and 0 or -(FL.nsbmax or 60)
    if a >= lim then return vx, vy end
    local NB = S2L.nbSt(now)
    if lim == 0 then NB.cut = NB.cut + 1 else NB.slow = NB.slow + 1 end
    return vx + (lim - a) * ux, vy + (lim - a) * uy
end
-- ===== end b148pose (core)
function S2L.nsStop(K)
    if valid(K.pmc) then K.pmc.Velocity = {X = 0, Y = 0, Z = 0} end
    if K.rmc and valid(K.rmc) then K.rmc.RotationRate = {Pitch = 0, Yaw = 0, Roll = 0} end
    K.stopped = true
end
function S2L.nsDrive(N, K, a, now)                         -- THE shared step: one velocity (and yaw rate) per actor per tick
    if K.x == nil then return false end
    local A; pcall(function() A = a:K2_GetActorLocation() end)
    if not A then return false end
    local dtT = math.min(math.max(now - (K.at or (now - 0.1)), 0.05), 0.3); K.at = now; K.stopped = false
    local cap = (FL.nscap or 250) / 1000
    local age = math.max(0, now - K.st)
    local ext = math.min(age, cap)
    local tx, ty, tz = K.x + K.vx * ext, K.y + K.vy * ext, K.z + K.vz * ext
    local ipx, ipy, ipz, ipg, ipdx, ipdy                    -- b148pose (nsip): the sample path one interval behind, 0.1 s ahead
    if (FL.nsip or 0) == 1 then ipx, ipy, ipz, ipg, ipdx, ipdy = S2L.nsPath(K, now, 0.1) end
    if ipx then tx, ty, tz = ipx, ipy, ipz end
    local ex, ey, ez = tx - A.X, ty - A.Y, tz - A.Z
    local e = math.sqrt(ex * ex + ey * ey + ez * ez)
    local S = N.s; S.dr = S.dr + 1; S.es = S.es + e; S.en = S.en + 1; if e > S.emax then S.emax = e end
    if e > (FL.nssnap or 300) then
        a:K2_SetActorLocation({X = tx, Y = ty, Z = tz}, false, {}, true); ex, ey, ez = 0, 0, 0; S.sn = S.sn + 1
    end
    local f = math.max(0, math.min(1, (cap - age) / dtT))  -- share of the next tick still inside the extrapolation cap
    local k = math.min(FL.nsgain or 7, 0.9 / dtT)          -- never more than 90 % of the error per tick (no overshoot)
    local vx, vy, vz = K.vx * f + ex * k, K.vy * f + ey * k, K.vz * f + ez * k
    if ipx then vx, vy, vz = ex * ipg / 0.1, ey * ipg / 0.1, ez * ipg / 0.1 end   -- b148pose: on the path 0.1 s from now (path speed + pull)
    if (FL.nsnoback or 0) == 1 then vx, vy = S2L.nsNoBack(K, vx, vy, ipx and ipdx or K.dx, ipx and ipdy or K.dy, now, K.nbvx, K.nbvy); K.nbvx, K.nbvy = vx, vy end
    local dz = FL.nsdead or 3
    if K.vx * K.vx + K.vy * K.vy + K.vz * K.vz < 25 and ex * ex + ey * ey + ez * ez < dz * dz then vx, vy, vz = 0, 0, 0 end
    local sp, mx = math.sqrt(vx * vx + vy * vy + vz * vz), FL.nsvmax or 1500
    if sp > mx then vx, vy, vz = vx * mx / sp, vy * mx / sp, vz * mx / sp end
    K.pmc.Velocity = {X = vx, Y = vy, Z = vz}
    if K.rmc and FL.nsyaw ~= 1 and not K.rmcOff then K.rmc.RotationRate = {Pitch = 0, Yaw = 0, Roll = 0}; K.rmcOff = true end
    if K.yaw ~= nil then
        local cur; pcall(function() cur = a:K2_GetActorRotation().Yaw end)
        if cur then
            local d = api.angDiff(K.yaw, cur)
            if K.rmc and FL.nsyaw == 1 then
                K.rmcOff = nil
                local w = math.abs(d) < 0.5 and 0 or d * math.min(FL.nsyawgain or 10, 0.9 / dtT)
                if w > 720 then w = 720 elseif w < -720 then w = -720 end
                K.rmc.RotationRate = {Pitch = 0, Yaw = w, Roll = 0}
            elseif math.abs(d) > 0.3 then
                a:K2_SetActorRotation({Pitch = 0, Yaw = cur + d * (1 - math.exp(-dtT * (FL.nsyawgain or 10))), Roll = 0}, false)
            end
        end
    end
    return true
end
-- ===== b116b (flag rvder): the remote velocity for every body. main.lua builds a new `remote` table per changed snapshot with the SENT
-- velocity (the host's CharacterMovement.Velocity). Live it read 0,0 while the host walked; a 0 makes the puppet's PMC follow by the error
-- pull only, its anim idle and its dead reckoning stop. rVel: the sent velocity when >= 5 cm/s; else the one derived from the last two
-- snapshots (sender clock r.t; >= 30 cm/s, <= nsvmax); else the sent one.
function S2L.rVel(ST, r)
    if not r then return 0, 0 end
    local sx, sy = r.vx or 0, r.vy or 0
    local D = ST.rvel
    if not D then D = {der = 0, sent = 0, at = os.clock()}; ST.rvel = D end
    local t = r.t or r.recv
    if r ~= D.r and type(t) == "number" and type(r.x) == "number" and type(r.y) == "number" then   -- a new snapshot
        local vx, vy = 0, 0
        if D.t and t > D.t then
            local dt = t - D.t
            if dt >= 0.03 and dt <= 1 then
                vx, vy = (r.x - D.x) / dt, (r.y - D.y) / dt
                local mx = FL.nsvmax or 1500
                if vx * vx + vy * vy > mx * mx then vx, vy = 0, 0 end      -- a jump, not motion
            end
        end
        if not (D.t and t <= D.t) then D.vx, D.vy = vx, vy end
        D.r, D.t, D.x, D.y = r, t, r.x, r.y
        if sx * sx + sy * sy < 25 and vx * vx + vy * vy >= 900 then D.der = D.der + 1 elseif sx * sx + sy * sy >= 25 then D.sent = D.sent + 1 end
        local now = os.clock()
        if now - D.at >= 10 then
            if D.der > 0 then log(string.format("S2CORE rvel 10 s: velocidade enviada ~0 com o remoto andando em %d snapshots (rvder=%s; enviada ok em %d)", D.der, tostring(FL.rvder), D.sent)) end
            D.der, D.sent, D.at = 0, 0, now
        end
    end
    if (FL.rvder or 0) ~= 1 or sx * sx + sy * sy >= 25 then return sx, sy end
    local dx, dy = D.vx or 0, D.vy or 0
    if dx * dx + dy * dy >= 900 then return dx, dy end
    return sx, sy
end
function S2L.nsPup(ST, P, now)                             -- puppet adapter (pupMove / pupTick writer): true = the mover drives it
    P.nsYaw = false
    if (FL.nsmooth or 0) ~= 1 or (ST.pmHold and now < ST.pmHold) then return false end
    local r = api.getRemote()
    if not (r and valid(P.pawn)) then return false end
    local N = S2L.nsSt(ST)
    local K, new = S2L.nsMover(N, P.pawn, "pup")
    if not K then return false end
    local vx, vy = S2L.rVel(ST, r)                          -- b116b (rvder)
    if new or K.rt ~= r.recv then
        K.rt = r.recv; N.s.smp = N.s.smp + 1
        S2L.nsSample(K, r.x + (FL.avoffx or 0) + (ST.offx or 0), r.y + (ST.offy or 0), S2L.pfZ(ST, P, r), nil, r.t, r.recv, vx, vy, 0)   -- b123
    end
    local sp = math.sqrt(vx * vx + vy * vy)
    K.yaw = (FL.nsyaw == 1 and K.rmc) and (S2L.paYaw(P, r, vx, vy, sp) or (sp > 100 and math.deg(math.atan(vy, vx))) or r.yaw or P.yaw) or nil   -- b119
    local ok = S2L.nsDrive(N, K, P.pawn, now)
    if ok then N.s.pup = N.s.pup + 1 end
    P.nsYaw = ok and K.yaw ~= nil
    return ok
end
function S2L.nsApply(ST, t, lp)                            -- M.applyMirrors (main.lua calls it per new snapshot instead of its own body)
    local N, mod = S2L.nsSt(ST), S2.mod
    local orig, mt = N.orig, N.mt
    if not (orig and mt) then if mod and mod.applyMirrors == S2L.nsApply then mod.applyMirrors = nil end return end
    local now, pre = os.clock(), {}
    for id, m in pairs(mt) do
        local K = N.k["m:" .. tostring(id)]
        if K and K.drive and type(m) == "table" and valid(m.actor) and S2L.pmAddr(m.actor) == K.addr then
            local A, R; pcall(function() A = m.actor:K2_GetActorLocation(); R = m.actor:K2_GetActorRotation() end)
            if A and R then pre[id] = {X = A.X, Y = A.Y, Z = A.Z, yaw = R.Yaw} end
        end
    end
    if mod then mod.applyMirrors = nil end
    if M.NP and M.NP.filt then local okF, tf = pcall(M.NP.filt, ST, t); if okF and type(tf) == "string" then t = tf end end   -- b122npcsync
    local ok, e = pcall(orig, t, lp)                        -- main.lua's own applyMirrors: create / move / pose / destroy as before
    if mod then mod.applyMirrors = S2L.nsApply end
    if not ok then S2L.nsErr(ST, "applyMirrors", e) end
    local ts = tonumber(((S2.io and S2.io.head) or ""):match('"t"%s*:%s*([%-%d%.eE%+]+)')) or now
    local r = api.getRemote()
    local still = r ~= nil and (r.vx or 0) ^ 2 + (r.vy or 0) ^ 2 <= 900   -- the host scans its NPCs only while it stands (scanmove=0)
    local snap = FL.nssnap or 300
    for id, p in pairs(pre) do
        local m, K = mt[id], N.k["m:" .. tostring(id)]
        if m and K and m.x and valid(m.actor) and S2L.pmAddr(m.actor) == K.addr then
            local yaw = p.yaw
            if FL.rotate == 1 then pcall(function() yaw = m.actor:K2_GetActorRotation().Yaw end) end
            if S2L.nsSample(K, m.x, m.y, m.z, yaw, ts, now, nil, nil, nil, still) then N.s.smp = N.s.smp + 1 end
            local ex, ey, ez = m.x - p.X, m.y - p.Y, m.z - p.Z
            if ex * ex + ey * ey + ez * ez <= snap * snap then
                m.actor:K2_SetActorLocation({X = p.X, Y = p.Y, Z = p.Z}, false, {}, true)      -- back on the smooth path (same frame)
                if FL.rotate == 1 then m.actor:K2_SetActorRotation({Pitch = 0, Yaw = p.yaw, Roll = 0}, true) end
                N.s.held = N.s.held + 1
            else N.s.sn = N.s.sn + 1 end                                                         -- big jump: main.lua's placement stays
        end
    end
end
function S2L.cmOff(ST, P, now)                             -- pupcmoff: the puppet's CharacterMovement never moves the body
    local want = (FL.pupcmoff or 0) == 1 and not S2L.pmVel()
    local C = ST.cmo
    if not valid(P.pawn) then return end
    local addr = S2L.pmAddr(P.pawn)
    if C and C.addr ~= addr then C = nil; ST.cmo = nil end  -- body swap / respawn: the old one was handled by nsTick
    if not want then
        if C then
            local ok = pcall(function() P.pawn.CharacterMovement:SetComponentTickEnabled(true) end)
            log("S2CORE pupcmoff: desligado -> tick do CharacterMovement devolvido ok=" .. tostring(ok)); ST.cmo = nil
        end
        return
    end
    local cm; pcall(function() cm = P.pawn.CharacterMovement end)
    if not valid(cm) then return end
    local on = true; pcall(function() on = cm:IsComponentTickEnabled() end)
    if C then C.a = P.pawn; C.at = now end
    if C and not on then return end
    pcall(function() cm:StopMovementImmediately() end)
    local ok = pcall(function() cm:SetComponentTickEnabled(false) end)
    if not C then
        C = {addr = addr, a = P.pawn, at = now, back = 0}; ST.cmo = C
        local cn = "?"; pcall(function() cn = cm:GetClass():GetFName():ToString() end)
        ST.cmoN = (ST.cmoN or 0) + 1
        log(string.format("S2CORE pupcmoff: boneco #%d: %s StopMovementImmediately + tick desligado ok=%s (tick antes=%s)", ST.cmoN, cn, tostring(ok), tostring(on)))
    else
        C.back = C.back + 1; ST.cmoBack = (ST.cmoBack or 0) + 1
        if C.back <= 5 or C.back % 50 == 0 then log(string.format("S2CORE pupcmoff: o jogo religou o tick do CharacterMovement (vez %d, total %d); desligado de novo ok=%s", C.back, ST.cmoBack, tostring(ok))) end
    end
end
function S2L.nsUro(ST, N)                                  -- nsuro: the puppet mesh animates every frame (once per pawn; restored when off)
    local want = (FL.nsuro or 0) == 1 and (FL.nsmooth or 0) == 1
    local P = ST.pup
    local pawn = P and P.pawn
    if not valid(pawn) then N.uro = nil; return end        -- body gone: its settings went with it
    local addr, U = S2L.pmAddr(pawn), N.uro
    if U and U.addr ~= addr then U = nil; N.uro = nil end
    if want and not U then
        local m; pcall(function() m = pawn.Mesh end)
        if not valid(m) then return end
        U = {addr = addr}; local r = {}
        pcall(function() U.uro = m.bEnableUpdateRateOptimizations end)
        pcall(function() U.vb = m.VisibilityBasedAnimTickOption end)
        pcall(function() U.ti = m:GetComponentTickInterval() end)
        r[#r + 1] = "URO " .. tostring(U.uro) .. "->false ok=" .. tostring((pcall(function() m.bEnableUpdateRateOptimizations = false end)))
        r[#r + 1] = "VisibilityBasedAnimTickOption " .. tostring(U.vb) .. "->0 ok=" .. tostring((pcall(function() m.VisibilityBasedAnimTickOption = 0 end)))
        if type(U.ti) == "number" and U.ti > 0 then r[#r + 1] = "TickInterval " .. U.ti .. "->0 ok=" .. tostring((pcall(function() m:SetComponentTickInterval(0) end))) end
        local cn = "?"; pcall(function() cn = m:GetClass():GetFName():ToString() end)
        log("S2CORE nsmooth: malha do boneco anima todo quadro (" .. table.concat(r, " ") .. ") classe=" .. cn .. (cn:find("Budgeted", 1, true) and " (AnimationBudgetAllocator: o alocador segue no controle)" or ""))
        N.uro = U
    elseif not want and U then
        local m; pcall(function() m = pawn.Mesh end)
        if valid(m) then
            pcall(function() if U.uro ~= nil then m.bEnableUpdateRateOptimizations = U.uro end end)
            pcall(function() if U.vb ~= nil then m.VisibilityBasedAnimTickOption = U.vb end end)
            pcall(function() if type(U.ti) == "number" and U.ti > 0 then m:SetComponentTickInterval(U.ti) end end)
        end
        log("S2CORE nsmooth: malha do boneco devolvida (URO=" .. tostring(U.uro) .. " VBAT=" .. tostring(U.vb) .. " intervalo=" .. tostring(U.ti) .. ")")
        N.uro = nil
    end
end
function S2L.nsOff(ST, N)
    if M.applyMirrors == S2L.nsApply then M.applyMirrors = nil end
    local P = ST.pup; if P then P.nsYaw = nil end
    local K = N.k.pup
    if K and P and valid(P.pawn) and S2L.pmAddr(P.pawn) == K.addr then pcall(S2L.nsStop, K) end
    for id, m in pairs(N.mt or {}) do
        local Km = N.k["m:" .. tostring(id)]
        if Km and type(m) == "table" and valid(m.actor) and S2L.pmAddr(m.actor) == Km.addr then pcall(S2L.nsStop, Km) end
    end
    N.k = {}; N.live = false
    log("S2CORE nsmooth: desligado -> movedores zerados, espelhos voltam ao main.lua")
end
function S2L.nsTick(ST)                                    -- tick100, after pupTick
    local t0 = os.clock(); local now = t0
    local C = ST.cmo                                       -- pupcmoff: a body dropped this tick gets its CharacterMovement tick back
    if C then
        local cur = ST.pup and ST.pup.pawn
        if not (cur and valid(cur) and S2L.pmAddr(cur) == C.addr) then
            if now - (C.at or 0) < 0.25 and valid(C.a) then pcall(function() C.a.CharacterMovement:SetComponentTickEnabled(true) end) end
            ST.cmo = nil
        end
    end
    local N = ST.ns
    if N then pcall(S2L.nsUro, ST, N) end
    if (FL.nsmooth or 0) ~= 1 then
        if N and N.live then S2L.nsOff(ST, N) end
        if M.applyMirrors == S2L.nsApply then M.applyMirrors = nil end
        return
    end
    N = S2L.nsSt(ST); N.live = true
    local P, K = ST.pup, N.k.pup
    if K then                                              -- puppet watchdog
        local cur = P and P.pawn
        if not (cur and valid(cur) and S2L.pmAddr(cur) == K.addr) then
            if now - (K.at or 0) < 0.25 and not K.stopped and valid(K.a) then pcall(S2L.nsStop, K) end   -- dropped this tick
            N.k.pup = nil
        elseif now - (K.at or 0) > 0.15 and not K.stopped then pcall(S2L.nsStop, K) end            -- pupTick did not drive it: no drift
    end
    if P and not (N.k.pup and now - (N.k.pup.at or 0) <= 0.15) then P.nsYaw = nil end
    local nm = 0
    if FL.mirrors == 1 or FL.mirrortest == 1 then
        local mt = S2L.nsMirTab(N, now)
        if mt then
            if S2.mod == M and M.applyMirrors ~= S2L.nsApply then M.applyMirrors = S2L.nsApply end
            local lp = api.getLocal()
            local L = {}
            for id, m in pairs(mt) do
                if type(m) == "table" and m.x and valid(m.actor) then
                    L[#L + 1] = {id = id, m = m, d2 = lp and ((m.x - lp.X) ^ 2 + (m.y - lp.Y) ^ 2) or 0}
                end
            end
            table.sort(L, function(p, q) return p.d2 < q.d2 end)
            local cap, live = FL.nsmax or 8, {}
            for i, e in ipairs(L) do
                local key = "m:" .. tostring(e.id); live[key] = true
                local Km = N.k[key]
                if i <= cap then
                    local new
                    Km, new = S2L.nsMover(N, e.m.actor, key)
                    if Km then
                        if new or Km.x == nil then
                            Km.x, Km.y, Km.z, Km.vx, Km.vy, Km.vz, Km.st, Km.ts = e.m.x, e.m.y, e.m.z, 0, 0, 0, now, nil
                            local R; pcall(function() R = e.m.actor:K2_GetActorRotation() end); Km.yaw = R and R.Yaw
                        end
                        Km.drive = true
                        if S2L.nsDrive(N, Km, e.m.actor, now) then nm = nm + 1 end
                    end
                elseif Km and Km.drive and S2L.pmAddr(e.m.actor) == Km.addr then Km.drive = false; pcall(S2L.nsStop, Km) end
            end
            for key in pairs(N.k) do if key:sub(1, 2) == "m:" and not live[key] then N.k[key] = nil end end
        end
    elseif M.applyMirrors == S2L.nsApply then M.applyMirrors = nil end
    local S = N.s
    S.mir = math.max(S.mir, nm); S.n = S.n + 1
    local ms = (os.clock() - t0) * 1000; S.ms = S.ms + ms; if ms > S.msmax then S.msmax = ms end
    if now - S.at >= 10 then
        if (FL.nslog or 1) == 1 and S.dr > 0 then
            log(string.format("S2CORE nsmooth 10 s: boneco_comandos=%d espelhos_max=%d (nsmax %d) comandos=%d amostras=%d erro_med=%.1f erro_max=%.0f cm snaps=%d retidos=%d criados=%d falhas=%d yaw=%s uro=%s cmoff_religado=%d | custo med=%.2f max=%.2f ms/tick",
                S.pup, S.mir, FL.nsmax or 8, S.dr, S.smp, S.es / math.max(1, S.en), S.emax, S.sn, S.held, S.mk, S.fail, FL.nsyaw == 1 and "quadro" or "10Hz",
                tostring(N.uro ~= nil), ST.cmoBack or 0, S.ms / math.max(1, S.n), S.msmax))
        end
        N.s = S2L.nsS0(now)
    end
end

-- ===== batch 49: the live probe's measurement: per-tick step of the puppet and interval between new samples, 10 s histograms
function S2L.smStat(ST, r, now, step)
    if FL.smoothlog ~= 1 then return end
    local S = ST.sm
    if not S then S = {at = now, st = {0, 0, 0, 0, 0, 0}, iv = {0, 0, 0, 0}, max = 0}; ST.sm = S end
    if r.recv ~= S.recv then
        if S.recv then local ms = (r.recv - S.recv) * 1000; local b = (ms < 110 and 1) or (ms < 160 and 2) or (ms < 250 and 3) or 4; S.iv[b] = S.iv[b] + 1 end
        S.recv = r.recv
    end
    local b = (step < 1 and 1) or (step < 5 and 2) or (step < 15 and 3) or (step < 30 and 4) or (step < 60 and 5) or 6
    S.st[b] = S.st[b] + 1; if step > S.max then S.max = step end
    if now - S.at >= 10 then
        if S.st[3] + S.st[4] + S.st[5] + S.st[6] > 0 then
            log(string.format("S2CORE smooth 10 s (%s): passos cm <1:%d 1-5:%d 5-15:%d 15-30:%d 30-60:%d >60:%d max %.0f | amostras ms <110:%d 110-160:%d 160-250:%d >250:%d",
                FL.smooth == 1 and "suave" or "antigo", S.st[1], S.st[2], S.st[3], S.st[4], S.st[5], S.st[6], S.max, S.iv[1], S.iv[2], S.iv[3], S.iv[4]))
        end
        S.at = now; S.st = {0, 0, 0, 0, 0, 0}; S.iv = {0, 0, 0, 0}; S.max = 0
    end
end
function S2L.pupTick(ST)
    local P = ST.pup
    local r = api.getRemote(); local lp = api.getLocal()
    if r and (FL.kzsafe or 0) == 1 and not S2L.remoteIn("boneco") then return S2L.kzHold(ST) end   -- b81kz
    if not (r and lp) then return end
    if (FL.emparkpup or 0) == 1 and ST.emp and ST.emp.park then return end   -- b86pupctl: no puppet during the emission + emgrace s
    local now = os.clock()
    local F = ST.far or {}; ST.far = F
    if FL.farbody == 1 then
        local d2 = (r.x - lp.X)^2 + (r.y - lp.Y)^2
        if (FL.kzsafe or 0) == 1 and r.z and lp.Z then d2 = d2 + (r.z - lp.Z)^2 end   -- b81kz: far below/above = far (A-Life pools the NPC)
        if not F.on and d2 > FL.pupfar^2 then
            F.on = true; log(string.format("S2CORE farbody: remoto a %.0f m -> corpo distante", math.sqrt(d2) / 100))
        elseif F.on and d2 < (FL.pupfar - 1000)^2 then
            F.on = false; S2L.farHide(F); P.force = nil
            log(string.format("S2CORE farbody: remoto a %.0f m -> boneco NPC de volta", math.sqrt(d2) / 100))
        end
    elseif F.on then F.on = false; S2L.farHide(F) end
    if F.on then return S2L.farTick(ST, F, r, lp, now) end
    local meshOk = true                                 -- batch 52: a pooled NPC keeps its name but loses its mesh (friend session 18:26)
    if FL.bodysafe == 1 and P.pawn and ((FL.kzsafe or 0) ~= 1 or valid(P.pawn)) then   -- b81kz: a freed pawn goes to bodyOk ("pawn invalido")
        meshOk = false; pcall(function() local m = P.pawn.Mesh; meshOk = m ~= nil and m:IsValid() and true or false end)
        if P.broken then meshOk = false end
    end
    if not meshOk then
        log("S2CORE puppet: malha nula ou chamada nullptr (corpo guardado pelo jogo?); referencia largada, sera recriado")
        P.bad = P.bad or {}; if P.path then P.bad[P.path] = true end          -- never adopted again (b45 adopts any body of the prototype)
        P.pawn = nil; P.path = nil; P.broken = nil; P.state = nil; api.setAvatar(nil); P.retryAt = now + 2; bodyLost(ST)
    end
    if not bodyOk(P) then
        if P.pawn then log("S2CORE puppet: pawn invalido; sera recriado"); P.pawn = nil; P.path = nil; api.setAvatar(nil); P.retryAt = now + 2; bodyLost(ST) end
        if FL.avatar == 1 and now - r.recv < 5 then pupSpawn(ST, P, r, lp, now) end
        return
    end
    do local okCm, eCm = pcall(S2L.cmOff, ST, P, now); if not okCm then S2L.nsErr(ST, "cmOff", eCm) end end   -- b114 pupcmoff
    local age = now - r.recv
    local away = age > 5 or FL.avatar ~= 1 or (M.remoteLife()) == "d" or M.remoteCine(ST)   -- batch 6: dead = hidden; downed stays visible so it can be reached
    if away ~= (P.hidden or false) then
        P.hidden = away
        local ok = pcall(function() P.pawn:SetActorHiddenInGame(away) end)
        log("S2CORE puppet: escondido=" .. tostring(away) .. " ok=" .. tostring(ok))
    end
    if away then return end
    local dt = math.min(math.max(now - (P.lastT or now), 0.001), 0.25); P.lastT = now
    local vx, vy = S2L.rVel(ST, r)                          -- b116b (rvder)
    if age > 0.6 then vx, vy = 0, 0 end
    local ex = math.min(age, FL.smooth == 1 and 0.6 or 0.3) + FL.avlead / 1000          -- sample age + configured lead (batch 49: 0.6 s with smooth=1)
    local tx, ty, tz = r.x + vx * ex + (FL.avoffx or 0) + (ST.offx or 0), r.y + vy * ex + (ST.offy or 0), S2L.pfZ(ST, P, r)   -- b123pupcrouch
    -- core-14: far remote player -> a World Partition streaming source follows it, and the bodies only move there once that area had 6 s to load
    -- (hypothesis for the 0x658 crashes at +530066/+da5243: a body moved by a big jump into a cell that is not streamed in)
    local fdx, fdy = tx - lp.X, ty - lp.Y
    local farMode = fdx * fdx + fdy * fdy > 8000 * 8000
    if farMode or P.src then
        if farMode and not (P.src and valid(P.src)) then
            P.src = nil
            pcall(function()
                local a = FindFirstOf("PlayerController"):GetWorld():SpawnActor(StaticFindObject("/Script/Engine.TargetPoint"), {X = tx, Y = ty, Z = tz + 500}, {Pitch = 0, Yaw = 0, Roll = 0})
                local c = a:AddComponentByClass(StaticFindObject("/Script/Engine.WorldPartitionStreamingSourceComponent"), false, {Rotation = {X = 0, Y = 0, Z = 0, W = 1}, Translation = {X = 0, Y = 0, Z = 0}, Scale3D = {X = 1, Y = 1, Z = 1}}, false)
                c.bStreamingSourceEnabled = true; c.TargetState = 1
                P.src = a; P.srcAt = now
            end)
            log("S2CORE puppet: remoto longe (" .. string.format("%.0f", math.sqrt(fdx * fdx + fdy * fdy) / 100) .. " m): fonte de streaming criada=" .. tostring(P.src ~= nil))
        end
        if P.src and valid(P.src) then
            pcall(function() P.src:K2_SetActorLocation({X = tx, Y = ty, Z = tz + 500}, false, {}, true) end)
            if not farMode and fdx * fdx + fdy * fdy < 5000 * 5000 then
                pcall(function() P.src:K2_DestroyActor() end); P.src = nil
                log("S2CORE puppet: remoto perto de novo; fonte de streaming removida")
            end
        end
        if farMode and P.src and now - (P.srcAt or now) < 6 then return end     -- not loaded yet: leave the bodies where they are
    end
    if not P.pos then P.pos = {X = tx, Y = ty, Z = tz} end
    local dx, dy = tx - P.pos.X, ty - P.pos.Y
    local k = 1 - math.exp(-dt * FL.avgain)
    if dx * dx + dy * dy > 2000 * 2000 then
        k = 1; ST.jumpUntil = now + 1.5
        log(string.format("S2CORE puppet: salto de %.0f m; corpos movidos de uma vez, leitura de dano suspensa 1.5 s", math.sqrt(dx * dx + dy * dy) / 100))
    end
    local ox, oy = P.pos.X, P.pos.Y
    if FL.smooth == 1 and k < 1 then                   -- batch 49: dead reckoning by the remote velocity, then a gentle pull toward the target
        local nx, ny = P.pos.X + vx * dt, P.pos.Y + vy * dt
        local ex2, ey2 = tx - nx, ty - ny
        local kc = (ex2 * ex2 + ey2 * ey2 > FL.smoothsnap * FL.smoothsnap) and 1 or (1 - math.exp(-dt * FL.smoothgain))
        P.pos = {X = nx + ex2 * kc, Y = ny + ey2 * kc, Z = P.pos.Z + (tz - P.pos.Z) * (1 - math.exp(-dt * 10))}
    else
        P.pos = {X = P.pos.X + dx * k, Y = P.pos.Y + dy * k, Z = P.pos.Z + (tz - P.pos.Z) * k}
    end
    pcall(S2L.smStat, ST, r, now, math.sqrt((P.pos.X - ox) ^ 2 + (P.pos.Y - oy) ^ 2))
    local speed = math.sqrt(vx * vx + vy * vy)
    local want = S2L.paYaw(P, r, vx, vy, speed) or (speed > 100 and math.deg(math.atan(vy, vx))) or r.yaw or P.yaw or 0   -- b119pupanim
    P.yaw = (P.yaw or want) + api.angDiff(want, P.yaw or want) * (1 - math.exp(-dt * 10))
    if (FL.pupfix or 0) == 1 then local okFx, gone = pcall(S2L.pfxTick, ST, P, speed, now); if okFx and gone then return end end   -- b110
    if (FL.pupmvlog or 0) == 1 then pcall(S2L.pmDiag, ST, P, speed, now) end   -- b89pupmove (read only)
    if ST.pmHold and now < ST.pmHold then                                     -- b89pupmove: a STATE-CHANGING probe drives the body
    elseif (FL.pupmove or 0) > 0 and FL.pupanimbp == 1 then                    -- b89pupmove: one writer per mode
        local okPm, ePm = pcall(S2L.pupMove, ST, P, vx, vy, speed, dt, now)
        if not okPm then S2.errs.n = S2.errs.n + 1; S2.errs.last = "pupMove: " .. tostring(ePm):sub(1, 160); ST.pmErr = (ST.pmErr or 0) + 1; if ST.pmErr <= 5 then log("S2CORE pupmove erro: " .. tostring(ePm)) end end
    else
        if P.pm then pcall(S2L.pmOff, P) end
    local okNs, nsOn = pcall(S2L.nsPup, ST, P, now)                       -- b114smooth: per-frame mover (nsmooth=1) instead of the 10 Hz teleport
    if not okNs then S2L.nsErr(ST, "nsPup", nsOn) end
    nsOn = okNs and nsOn
    if not nsOn then P.pawn:K2_SetActorLocation(P.pos, false, {}, true) end
    if not P.nsYaw then P.pawn:K2_SetActorRotation({Pitch = 0, Yaw = P.yaw, Roll = 0}, false) end
    S2L.pupAnim(P, speed)
    P.pmW = (not nsOn) and P.pos or nil; P.pmY = P.yaw
    end
    S2L.pupWeapon(P)
end

-- ===== hidden AI target of the remote player (S2Coop_RemoteTarget): hostiles see and hit THIS; the puppet is only the picture =====
local TGT_HP = 5000
local function tgtTick(ST)
    local T, P = ST.tgt, ST.pup
    local r = api.getRemote(); local lp = api.getLocal()
    if r and (FL.kzsafe or 0) == 1 and not S2L.remoteIn("alvo") then pcall(S2L.tgPark, ST, T, "kz"); return end   -- b81kz: the hidden target stays where it is
    if not (r and lp) then return end
    local now = os.clock()
    if ST.far and ST.far.on then pcall(S2L.tgPark, ST, T, "longe"); return end   -- core-21: far remote player: no NPC target (it would be pooled by the A-Life)
    pcall(S2L.tgBack, ST, T)                              -- b131combat F3 (tgtpark)
    if not bodyOk(T) then
        if T.pawn then log("S2CORE target: pawn invalido; sera recriado"); pcall(S2L.tgNote, ST, "invalido", -1); T.pawn = nil; T.path = nil; T.setup = nil; T.retryAt = now + 2; bodyLost(ST) end
        if P.spawnAt or not bodyOk(P) then return end        -- one spawn at a time: puppet first
        if FL.avatar == 1 and now - r.recv < 5 then
            if FL.tgtsafe == 1 and not T.spawnAt and now - (ST.anyShotAt or -1e9) < FL.tgtcalm then   -- b72: not into a fight
                T.waitFrom = T.waitFrom or now
                if now - T.waitFrom < FL.tgtwaitmax then
                    if not T.waitLog then T.waitLog = true; log(string.format("S2CORE target: recriacao adiada (tiro ouvido ha %.1f s; espera %d s sem tiros, no maximo %d s)", now - ST.anyShotAt, FL.tgtcalm, FL.tgtwaitmax)) end
                    return
                end
                if not T.waitMaxLog then T.waitMaxLog = true; log(string.format("S2CORE target: espera maxima de %d s; recriando mesmo com tiros", FL.tgtwaitmax)) end
            end
            pupSpawn(ST, T, r, lp, now, "S2Coop_RemoteTarget", 4000, 6000, "target")
            if T.spawnAt and T.waitFrom then
                log(string.format("S2CORE target: pedido de recriacao depois de %.1f s de espera", now - T.waitFrom))
                T.waitFrom = nil; T.waitLog = nil; T.waitMaxLog = nil
            end
        end
        return
    end
    T.waitFrom = nil; T.waitLog = nil; T.waitMaxLog = nil
    if not T.setup then
        T.setup = true; T.god = nil; T.godAt = nil; T.deadAt = nil
        pcall(S2L.tgNote, ST, "pronto", -1)   -- b131combat F3
        local okH = pcall(function() T.pawn:SetActorHiddenInGame(true) end)
        log("S2CORE target: escondido ok=" .. tostring(okH))
    end
    if FL.tgtgod == 1 and now >= (T.godAt or 0) then      -- b112 (#1): Unkillable, the nodeath call (re-asserted every 5 s like nodeath)
        T.godAt = now + 5
        local ok, e = pcall(function()
            if not T.uid then T.uid = StaticFindObject("/Script/Stalker2.Default__CppMediator"):GetGUID(T.pawn) end
            FindFirstOf("CustomConsoleManagerBuild"):XSetEntityUnkillableByUID(T.uid, true)
        end)
        if not T.god or not ok then log("S2CORE target: Unkillable uid=" .. tostring(T.uid) .. " ok=" .. tostring(ok) .. " " .. (ok and "" or tostring(e))) end
        T.god = ok or nil
    end
    local pos = P.pos or {X = r.x, Y = r.y, Z = r.z}
    local dgs = nil
    if FL.tgtdiag == 1 then                                -- b72: read-only sample (drift since the last teleport, vertical speed)
        dgs = {t = now, z = pos.Z, drift = 0, vz = 0, hp = -1}
        pcall(function() local q = T.pawn:K2_GetActorLocation(); if T.lastZ then dgs.drift = q.Z - T.lastZ end end)
        pcall(function() dgs.vz = T.pawn:GetVelocity().Z end)
        T.ring = T.ring or {}; T.ring[#T.ring + 1] = dgs; if #T.ring > 10 then table.remove(T.ring, 1) end
        local G = T.dg10 or {at = now, shots = ST.anyShots or 0, loss = 0, lossSum = 0, vzMin = 0, driftMin = 0}; T.dg10 = G
        if dgs.vz < G.vzMin then G.vzMin = dgs.vz end
        if dgs.drift < G.driftMin then G.driftMin = dgs.drift end
    end
    T.pawn:K2_SetActorLocation(pos, false, {}, true)
    T.lastZ = pos.Z
    if FL.tgtnofall == 1 then                              -- b72: no fall speed carried from one teleport to the next
        local okS = pcall(function() T.pawn.CharacterMovement:StopMovementImmediately() end)
        if not okS then T.nofallErr = (T.nofallErr or 0) + 1; if T.nofallErr <= 3 then log("S2CORE target: StopMovementImmediately falhou") end end
    end
    local hp = T.pawn:GetHP()
    if dgs then
        dgs.hp = hp
        local G = T.dg10
        if T.ref and hp < T.ref - 0.05 then G.loss = G.loss + 1; G.lossSum = G.lossSum + (T.ref - hp) end
        if now - G.at >= 10 then
            local shots = (ST.anyShots or 0) - G.shots
            if shots > 0 or G.loss > 0 then
                log(string.format("S2CORE target 10s: tiros_ouvidos=%d perdas_hp=%d soma=%.1f vz_min=%.0f queda_max=%.0f cm", shots, G.loss, G.lossSum, G.vzMin, -G.driftMin))
            end
            T.dg10 = {at = now, shots = ST.anyShots or 0, loss = 0, lossSum = 0, vzMin = 0, driftMin = 0}
        end
    end
    local dead = not T.pawn:IsAlive()
    if dead and T.god and FL.tgtgod == 1 then             -- b112 (#1): Unkillable reads IsAlive=false for a moment; ForceSetHP below brings it back
        T.deadAt = T.deadAt or now
        if now - T.deadAt < (FL.tgtgodwait or 2) then
            dead = false
            T.held = (T.held or 0) + (T.deadAt == now and 1 or 0)
            if T.deadAt == now and (T.held <= 5 or T.held % 50 == 0) then log(string.format("S2CORE target: golpe letal segurado (Unkillable) hp %.1f ref %s -> refill (%d vezes)", hp, tostring(T.ref), T.held)) end
        end
    else T.deadAt = nil end
    if dead then
        log(string.format("S2CORE target: MORREU (hp %.1f, ref %s); sera recriado em 5 s", hp, tostring(T.ref)))
        pcall(S2L.tgNote, ST, "morreu", hp)   -- b131combat F3
        if FL.tgtdiag == 1 then                            -- b72: what the body did in its last second
            local parts = {}
            for _, s in ipairs(T.ring or {}) do parts[#parts + 1] = string.format("%.1fs z=%.0f queda=%.0f vz=%.0f hp=%.0f", s.t - now, s.z, -s.drift, s.vz, s.hp) end
            log(string.format("S2CORE target diag: tiro ouvido ha %s s; salto ha %s s | %s", ST.anyShotAt and string.format("%.1f", now - ST.anyShotAt) or "-",
                ST.jumpUntil and string.format("%.1f", now - (ST.jumpUntil - 1.5)) or "-", table.concat(parts, " | ")))
            T.ring = {}
        end
        if FL.bodysafe ~= 1 then pcall(function() T.pawn:SetActorHiddenInGame(true) end) end   -- batch 44: a dead body is not touched again
        T.pawn = nil; T.path = nil; T.setup = nil; T.ref = nil; T.retryAt = now + 5
        return
    end
    if T.ref and hp < T.ref - 0.05 and M.remoteDown() then
        T.ignored = (T.ignored or 0) + (T.ref - hp)        -- hits on the target of a dead/downed player are not sent
    elseif T.ref and hp < T.ref - 0.05 then
        local D = ST.dmg; local amount = (T.ref - hp) * FL.dmgscale
        local why = nil
        if (T.ref - hp) >= TGT_HP * 0.9 then why = "golpe inteiro de uma vez (script do jogo, b120)"   -- b120: the game wipes the whole pool in one tick (Zalissya bar trigger); no weapon does that
        elseif now < (ST.jumpUntil or 0) then why = "salto"
        elseif ST.anomQuiet and now < ST.anomQuiet.untilT and (pos.X - ST.anomQuiet.x) ^ 2 + (pos.Y - ST.anomQuiet.y) ^ 2 < ST.anomQuiet.r ^ 2 then why = "anomalia espelhada"   -- batch 73
        else
            local L = ST.lshot            -- last local shot: time + ray (set by the shot hook below)
            if L and now - L.at < 0.6 then
                local t = (pos.X - L.ox) * L.dx + (pos.Y - L.oy) * L.dy + (pos.Z - L.oz) * L.dz
                if t > 0 then
                    local hx, hy, hz = L.ox + L.dx * t, L.oy + L.dy * t, L.oz + L.dz * t
                    if math.sqrt((hx - pos.X)^2 + (hy - pos.Y)^2) < 120 and math.abs(hz - pos.Z) < 140 then why = "tiro local" end
                end
            end
            if not why and FL.dmgmin > 0 then
                if amount >= FL.dmgmin then T.realAt = now
                elseif now - (T.realAt or -10) > 2 then why = "tique<" .. FL.dmgmin end
            end
        end
        if why then
            D.dropped = (D.dropped or 0) + 1
            if D.dropped <= 20 or D.dropped % 50 == 0 then log(string.format("S2CORE target: dano %.1f IGNORADO (%s) ignorados=%d", amount, why, D.dropped)) end
        else
            D.seq = D.seq + 1; D.total = D.total + amount
            D.list[#D.list + 1] = {seq = D.seq, amount = amount, at = now}
            log(string.format("S2CORE target: DANO seq=%d %.1f (hp %.1f -> %.1f) total=%.1f vivo=%s", D.seq, amount, T.ref, hp, D.total, tostring(T.pawn:IsAlive())))
        end
    end
    if hp < TGT_HP - 0.05 then
        pcall(function() T.pawn:ForceSetHP(TGT_HP) end); hp = T.pawn:GetHP()
        pcall(function()                                   -- clear the bleeding the hit left behind (it kept ticking for minutes)
            local b = T.pawn:GetBleeding()
            if b > 0 then T.pawn:ForceSetBleeding(0.0); T.bleedCleared = (T.bleedCleared or 0) + 1
                if T.bleedCleared <= 5 or T.bleedCleared % 50 == 0 then log(string.format("S2CORE target: sangramento %.2f -> %.2f (limpo %d vezes)", b, T.pawn:GetBleeding(), T.bleedCleared)) end end
        end)
    end
    T.ref = hp
end
-- ===== core-24: at most ONE visible body per remote player. Measured 2026-10-11 12:17 (host, loopback): the hidden AI target NPC
-- (S2Coop_RemoteTarget) was visible again (bHidden=false) although it had been hidden once at adoption - the game shows an NPC again
-- when it brings it back online - so two bodies stood inside each other. The guard re-asserts every tick: target always hidden;
-- far mode -> puppet hidden; near mode -> far body hidden; plus any other pawn of our two prototypes (max HP 100000 / 5000 within
-- 3 m of the remote position) that is not the adopted puppet/target is hidden. Each correction logs "AVATAR dup".
local function dupGuard(ST)
    if FL.bodysafe == 1 and not (ST.wg and ST.wg.open) then return end   -- batch 44: no body is touched while the world is not stable
    local G = ST.dg or {n = 0, sweepAt = 0}; ST.dg = G
    local function hide(o, what)
        local h = true; pcall(function() h = o.bHidden end)
        if h == true then return end
        local ok = pcall(function() o:SetActorHiddenInGame(true) end)
        G.n = G.n + 1
        if G.n <= 20 or G.n % 50 == 0 then log(string.format("S2CORE AVATAR dup: %s estava visivel; escondido ok=%s (correcoes=%d)", what, tostring(ok), G.n)) end
    end
    local T, P, F = ST.tgt, ST.pup, ST.far
    if T and S2L.bodyHeld(T) then hide(T.pawn, "alvo oculto " .. T.pawn:GetFName():ToString()) end
    for _, Q in ipairs({T or false, P or false}) do      -- batch 38: a spawn is pending: every NEW body near its spawn point is hidden at once
        if Q and Q.spawnAt and Q.before and Q.sx and not (FL.pupretry == 1 and Q == P) then   -- batch 43: the puppet waiting to be adopted is not hidden
            for _, o in ipairs(FindAllOf("Pawn") or {}) do pcall(function()
                if o:IsValid() and not (P and o == P.pawn) and not Q.before[o:GetFullName()] and o:GetFullName():find("BP_AI_Human_C", 1, true) and not (S2L.clOwn and S2L.clOwn(ST, o)) then
                    local q = o:K2_GetActorLocation()
                    if (q.X - Q.sx) ^ 2 + (q.Y - Q.sy) ^ 2 < 800 ^ 2 then hide(o, "corpo novo antes da adocao " .. o:GetFName():ToString()) end
                end
            end) end
        end
    end
    local farOn = F and F.on
    if farOn and P and S2L.bodyHeld(P) then hide(P.pawn, "boneco NPC (modo distante)") end
    if not farOn and F and valid(F.actor) then hide(F.actor, "corpo distante (modo perto)") end
    local now = os.clock()
    if now < G.sweepAt then return end
    G.sweepAt = now + 2
    local r = api.getRemote(); if not r then return end
    for _, o in ipairs(FindAllOf("BP_AI_Human_C") or {}) do pcall(function()
        if S2L.clOwn and S2L.clOwn(ST, o) then pcall(function() if o.bHidden and not ((FL.natnpc or 0) == 1 and not (function() local ad = o:GetAddress(); for _, B in pairs((ST.cl and ST.cl.B) or {}) do if B.ad == ad then return true end end; return false end)()) then o:SetActorHiddenInGame(false); log("S2CORE AVATAR dup: corpo de saque " .. o:GetFName():ToString() .. " estava escondido; mostrado de novo (b141)") end end); return end   -- b141: a loot body is never a duplicate avatar
        if o:IsValid() and not (P and o == P.pawn) and not (T and o == T.pawn)
           and not (FL.pupretry == 1 and P and P.spawnAt and P.before and not P.before[o:GetFullName()]) then   -- batch 43: a pending puppet is exempt
            local mh = o:GetMaxHP()
            if mh == 100000 or mh == 5000 then
                local q = o:K2_GetActorLocation()
                if (q.X - r.x)^2 + (q.Y - r.y)^2 < 300 * 300 then hide(o, "corpo extra " .. o:GetFName():ToString() .. " maxhp=" .. mh)
                elseif FL.pupadopt == 1 and mh == 100000 and P and S2L.bodyHeld(P) then hide(o, "boneco sobrando " .. o:GetFName():ToString()) end   -- batch 45: leaked, anywhere
                if FL.pupadopt == 1 and mh == 100000 then                -- batch 45: a body of ours that is not the puppet never blocks anyone
                    G.nocol = G.nocol or {}
                    local a = o:GetAddress()
                    if not G.nocol[a] then G.nocol[a] = true; pcall(function() o:SetActorEnableCollision(false) end) end
                end
            end
        end end) end
end

-- ===== b78fix D3: vendors are never mirrored (flag vendmir, default 0; user 2026-10-14: "npcs that are vendors, cannot be synced").
-- <bridge>/s2u_vendor.lua (gen_vendor.py from the game data): every placed trader/medic/bartender/technician {x, y, z, kind, obj}.
-- An NPC within vendr cm of a row is that vendor: positions only, no call on the NPC. Rows are bucketed in 10000 cm cells.
function S2L.vendRows()
    if S2L.VR then return S2L.VR end
    S2L.VR, S2L.VB = {}, {}
    local ok, t = pcall(function()
        local f = io.open(api.BR .. "s2u_vendor.lua", "r"); if not f then return nil end
        local s = f:read("*a"); f:close()
        return load(s, "s2u_vendor.lua")()
    end)
    if ok and type(t) == "table" and type(t.rows) == "table" then S2L.VR = t.rows end
    for _, v in ipairs(S2L.VR) do
        local k = math.floor(v[1] / 10000) .. ":" .. math.floor(v[2] / 10000)
        S2L.VB[k] = S2L.VB[k] or {}; table.insert(S2L.VB[k], v)
    end
    log("S2CORE vend: " .. #S2L.VR .. " vendedores em s2u_vendor.lua" .. ((ok and type(t) == "table") and "" or (" (arquivo ausente ou com erro: " .. tostring(t) .. ")")))
    return S2L.VR
end
function S2L.vendorAt(q, rr)                  -- -> nearest row within rr (default vendr) cm {x, y, z, kind, obj}, distance cm | nil
    local rows, r = S2L.vendRows(), rr or FL.vendr or 4000
    local best, bd = nil, r * r
    local function scan(list) for _, v in ipairs(list or {}) do
        local dx, dy, dz = q.X - v[1], q.Y - v[2], q.Z - v[3]
        local d = dx * dx + dy * dy + dz * dz
        if d < bd then best, bd = v, d end
    end end
    if r > 10000 then scan(rows) else
        local cx, cy = math.floor(q.X / 10000), math.floor(q.Y / 10000)
        for i = -1, 1 do for j = -1, 1 do scan(S2L.VB[(cx + i) .. ":" .. (cy + j)]) end end
    end
    if best then return best, math.sqrt(bd) end
    return nil
end
-- b124vendone (flag vendone, default 1; measured: vendr 40 m took 36 of 37 Zalissya NPCs as vendors, so the host mirrored none of them): a vendor
-- is only the SINGLE nearest live NPC pawn to each s2u_vendor.lua row, within vendr. Same rule on the host (scan skip, vendSkip) and on the client
-- (its own vendor kept, alifeTick), so both sides pick the same body. Rebuilt at most every 2 s from one FindAllOf("Pawn"); kept by address only
-- (no UObject ref). Left out: the local player and our own bodies (ST.pup / ST.tgt pawns), dead pawns. vendone=0 = the old rule (any NPC within vendr).
function S2L.vendSet(ST)                        -- -> {[pawn address] = {row, distance cm}}
    local now = os.clock()
    if S2L.VS and now >= (S2L.VSt or 0) and now - (S2L.VSt or 0) < 2 then return S2L.VS end
    S2L.VSt = now
    local rows, r = S2L.vendRows(), FL.vendr or 4000
    local r2, skip, best = r * r, {}, {}
    local mine = {S2.io and S2.io.pawn or false, ST and ST.pup and ST.pup.pawn or false, ST and ST.tgt and ST.tgt.pawn or false}
    for i = 1, 3 do local p = mine[i]; if p and valid(p) then pcall(function() skip[p:GetAddress()] = true end) end end
    local function scan(list, q, ad) for _, v in ipairs(list or {}) do
        local d = (q.X - v[1]) ^ 2 + (q.Y - v[2]) ^ 2 + (q.Z - v[3]) ^ 2
        if d < r2 and (not best[v] or d < best[v][2]) then best[v] = {ad, d} end
    end end
    for _, o in ipairs(FindAllOf("Pawn") or {}) do pcall(function()
        if not valid(o) then return end
        local ad = o:GetAddress()
        if skip[ad] or not o:IsAlive() then return end
        local q = o:K2_GetActorLocation()
        if r > 10000 then scan(rows, q, ad) else
            local cx, cy = math.floor(q.X / 10000), math.floor(q.Y / 10000)
            for i = -1, 1 do for j = -1, 1 do scan(S2L.VB[(cx + i) .. ":" .. (cy + j)], q, ad) end end
        end
    end) end
    local set = {}
    for v, b in pairs(best) do local d = math.sqrt(b[2]); if not set[b[1]] or d < set[b[1]][2] then set[b[1]] = {v, d} end end
    S2L.VS = set
    return set
end
function S2L.vendOf(ST, o, q)                   -- -> row, distance cm | nil: this NPC is a vendor (vendone=1: in vendSet; 0: any row within vendr)
    if (FL.vendone or 0) == 1 then
        local e = S2L.vendSet(ST)[o:GetAddress()]
        if e then return e[1], e[2] end
        return nil
    end
    return S2L.vendorAt(q)
end
function M.vendSet(ST) return S2L.vendSet(ST or (S2 and S2.state)) end   -- b124: read-only view for probes ({[address] = {row, cm}})
function S2L.vendSkip(ST, o)                   -- host scan (vendmir): true = this NPC gets no mirror on the client
    local ok, skip = pcall(function()
        local q = o:K2_GetActorLocation()
        local v, d = S2L.vendOf(ST, o, q)           -- b124vendone
        local why = v and string.format("%s %s a %.0f m", v[4], v[5], d / 100)
        if not v and FL.vendmir == 2 and (S2L.nearHub(q.X, q.Y) or S2L.nearTrade(ST.alife or {}, q.X, q.Y)) then why = "hub/lugar de comercio (vendmir=2)" end
        if why then
            S2L.VL = S2L.VL or {}
            local k = string.format("%.0f,%.0f", q.X / 500, q.Y / 500)
            if not S2L.VL[k] then S2L.VL[k] = true; log(string.format("S2CORE vend: NPC em %.0f,%.0f,%.0f sem espelho: %s", q.X, q.Y, q.Z, why)) end
        end
        return why ~= nil
    end)
    return ok and skip == true
end

function M.isOwnBody(ST, o)                    -- main.lua's scan and the shot resolver skip these pawns
    return (ST.pup and o == ST.pup.pawn) or (ST.tgt and o == ST.tgt.pawn) or ((FL.vendmir or 0) > 0 and api.ROLE == "host" and S2L.vendSkip(ST, o)) or S2L.pawnOut(o) or (S2L.clOwn and S2L.clOwn(ST, o)) or (rawget(_G, "s2n_nat_own") ~= nil and o ~= nil and s2n_nat_own(o:GetAddress()) == true) or false   -- b78fix vendmir; b81kz; b138loot; b158natnpc
end

-- ===== b78fix D3b: a vendor talk on the CLIENT = its trade screen only (flag vendtrade, default 0; user 21:15: "make the client shows
-- only his trader windows, and occult the options, because it maybe can desync the host"). When this side's dialog view opens with an
-- NPC that is a row of s2u_vendor.lua, the answers are hidden (AnswerContainer collapsed) and the trade screen is opened at once
-- (DialogueViewFolder:UIDialogOpenTrade; technician: UIDialogOpenUpgrade). When the dialog view shows again (trade closed) it is closed
-- (UIDialogClose) and the answers given back. If the view stays up 3 s after the open call, the answers are given back (no trade screen).
-- vendtrade=2 = dry run (logs only). dm70 is paused meanwhile. Views are found again each 200 ms step (no widget kept across steps).
function S2L.vendView()
    local vw
    for _, w in ipairs(FindAllOf("W_DialogueView_C") or {}) do
        if valid(w) then pcall(function() if w:IsInViewport() and w:IsVisible() then vw = w end end) end
    end
    return vw
end
function S2L.vendShow(V, vw, why)              -- give the answers back (on this view)
    if V.av == nil or not valid(vw) then return end
    local ok = pcall(function() vw.AnswerContainer:SetVisibility(V.av) end)
    log("S2CORE vendtrade: respostas devolvidas (" .. why .. ") ok=" .. tostring(ok))
    V.av = nil
end
function S2L.vendTradeTick(ST)
    local V = ST.vt
    if (FL.vendtrade or 0) == 0 or api.ROLE ~= "client" then
        if V and V.on then V.on = false; log("S2CORE vendtrade: desligado no meio (vendtrade=0)") end
        return
    end
    V = V or {}; ST.vt = V
    local now = os.clock()
    if now - (V.at or -1) < 0.2 then return end
    V.at = now
    local vw = S2L.vendView()
    if not V.on then
        if not vw then V.seen = nil; return end
        if V.av ~= nil then S2L.vendShow(V, vw, "dialogo novo") end   -- a view hidden earlier and closed before it was given back
        if V.seen then return end
        V.t0 = V.t0 or now
        local row, d, q
        pcall(function()
            local a = S2L.ioPawn():GetInteractedActor()
            if valid(a) then q = a:K2_GetActorLocation(); row, d = S2L.vendorAt(q, FL.vendtr or 1500) end
        end)
        if not q and now - V.t0 < 2 then return end                  -- no NPC read yet: try for 2 s
        V.seen = true; V.t0 = nil
        if not row then return end                                   -- not a vendor: the normal dialog
        local hint; pcall(function() hint = vw.ButtonHintTrade:GetVisibility() end)
        local fn = row[4] == "technician" and "UIDialogOpenUpgrade" or "UIDialogOpenTrade"
        local tag = string.format("%s %s a %.0f m (dica de troca visibilidade=%s)", row[4], row[5], d / 100, tostring(hint))
        if FL.vendtrade ~= 1 then log("S2CORE vendtrade: TESTE dialogo com " .. tag .. " -> " .. fn .. " (vendtrade=2: nada feito)"); return end
        pcall(function() V.av = vw.AnswerContainer:GetVisibility(); vw.AnswerContainer:SetVisibility(1) end)
        local ok, e = pcall(function() vw[fn](vw) end)
        V.on, V.st, V.t1 = true, "open", now
        log("S2CORE vendtrade: dialogo com " .. tag .. "; respostas escondidas; " .. fn .. " ok=" .. tostring(ok) .. (ok and "" or (" " .. tostring(e))))
        return
    end
    if V.st == "open" then
        if not vw then V.st = "trade"; log(string.format("S2CORE vendtrade: tela de troca aberta (%.1f s)", now - V.t1))
        elseif now - V.t1 >= 3 then V.on = false; S2L.vendShow(V, vw, "a vista do dialogo continuou visivel 3 s; sem tela de troca") end
    elseif V.st == "trade" then
        if vw then
            S2L.vendShow(V, vw, "troca fechada")
            local ok = pcall(function() vw:UIDialogClose() end)
            V.st, V.t1 = "closing", now
            log("S2CORE vendtrade: troca fechada; UIDialogClose ok=" .. tostring(ok))
        else
            local still = false
            pcall(function() still = valid(S2L.ioPawn():GetInteractedActor()) end)
            if not still then V.on = false; log("S2CORE vendtrade: troca fechada e o dialogo ja tinha terminado") end
        end
    elseif V.st == "closing" then
        if not vw then V.on = false; log(string.format("S2CORE vendtrade: dialogo fechado (%.1f s)", now - V.t1))
        elseif now - V.t1 >= 2 then V.on = false; log("S2CORE vendtrade: a vista nao fechou em 2 s; dialogo devolvido ao jogador") end
    end
end

-- ===== b108tpfloor (flag tpfloor, default 0): every "put me next to the other player" teleport lands on that player's FLOOR.
-- Live 2026-10-15 (Zalissya, host indoors at z 995): main.lua's leash safeTeleport(x + 300) traced the ground from z + 2000 down, hit the
-- ROOF and put the client at z 1627. One rule for the whole family (S2L.TPF_SITES): S2L.tpfSpot = tpfback cm behind the other player along
-- its yaw at ITS z if the eye-level line is clear and the spot has a floor near its feet, else its exact spot; the z never comes from a trace.
-- Far pulls (job, tpfTick 1/s): main.lua's streaming source, wait until the floor under the other player is loaded (trace from ITS eye level
-- down), place in walk mode; never loaded in tpfwait s -> its exact spot in fly mode, walk when a short trace under the pawn hits.
-- main.lua is not edited: its local safeTeleport is swapped once per Lua state (debug.setupvalue on S2_toggleBridge) for a trampoline that
-- calls the CURRENT module's M.tpfLeash when tpfloor=1, else the original (flag 0 = old behaviour at every site).
S2L.TPF_SITES = {   -- whole family (scan of main.lua + s2core.lua 2026-10-15); route: job = far pull, now = same tick
    {"L1", "main.lua leash safeTeleport(x + 300, y, z)", "client", "job (trampoline -> M.tpfLeash)"},
    {"L2", "cine follow: leashm -> cinefollow", "client", "via L1"}, {"L3", "join: leashm -> joinm", "client", "via L1"},
    {"L4", "nodeath respawn: leashm -> ndleash", "client", "via L1"}, {"L5", "ft client follow: leashm -> ftleash", "client", "via L1"},
    {"FT2", "ft host follow (traveller + ftoff)", "host", "job"},
    {"CJ1", "cjoin dialog (host + cjoff, z + 50)", "client", "now"}, {"CJ2", "cj68.lua beside() through G.teleport (scenes)", "both", "now"},
    {"GS", "gsnap (own x,y at host z + 150)", "client", "now"}, {"CF", "cfall (host + 100, z + 150)", "client", "now"},
}   -- not in the family: main.lua dev keys (F12, END, NUM3, NUM9, tele, godtest), ia71 stand point, NPC bodies, cfollow step (keeps own z)
--@TPFLIB-BEGIN
function S2L.tpfTrace(ax, ay, az, bx, by, bz)      -- one LineTraceSingle (Visibility, main.lua's arguments): hit, impact z; nil = no trace
    local ksl = StaticFindObject("/Script/Engine.Default__KismetSystemLibrary")
    if not (ksl and ksl:IsValid()) then return nil end
    local ok, h, iz = pcall(function()
        local out = {}
        local hit = ksl:LineTraceSingle(api.UEHelpers.GetWorld(), {X = ax, Y = ay, Z = az}, {X = bx, Y = by, Z = bz}, 0, false, {}, 0, out, true, {R = 1, G = 0, B = 0, A = 1}, {R = 0, G = 1, B = 0, A = 1}, 0)
        return hit == true, out.ImpactPoint and out.ImpactPoint.Z
    end)
    if not ok then return nil end
    return h, iz
end
function S2L.tpfFloor(x, y, z)                     -- floor under a capsule centre (x,y,z) traced from ITS eye level down: impact z near its feet, or nil
    local h, iz = S2L.tpfTrace(x, y, z + 60, x, y, z - 300)
    if h and iz and math.abs(iz - (z - 90)) <= 150 then return iz end
end
function S2L.tpfSpot(x, y, z, yaw)                 -- THE rule: x, y, z, how, trace text (z = the other player's z + 10, never a trace impact)
    local b = FL.tpfback or 100
    if b > 0 and yaw then
        local a = math.rad(yaw + 180)
        local bx, by = x + math.cos(a) * b, y + math.sin(a) * b
        local blk = S2L.tpfTrace(x, y, z + 60, bx, by, z + 60)
        local fz = blk == false and S2L.tpfFloor(bx, by, z) or nil
        if fz then return bx, by, z + 10, "atras", string.format("linha livre, chao %.0f", fz) end
        return x, y, z + 10, "ponto do outro", blk == false and "atras sem chao" or ("atras " .. (blk and "parede" or "sem trace"))
    end
    return x, y, z + 10, "ponto do outro", "sem yaw/tpfback"
end
function S2L.tpfCands(x, y, yaw)                   -- b113tpfit: candidate spots around the other player, in try order: {x, y, label}
    local b, r, y0, C = FL.tpfback or 100, FL.tpfring or 150, yaw or 0, {}
    for _, c in ipairs({{b, 180, "atras"}, {b, -90, "esquerda"}, {b, 90, "direita"}, {b, 0, "frente"}}) do
        if c[1] > 0 then local a = math.rad(y0 + c[2]); C[#C + 1] = {x + math.cos(a) * c[1], y + math.sin(a) * c[1], c[3]} end
    end
    if r > 0 then for k = 0, 7 do local a = math.rad(y0 + 180 + k * 45); C[#C + 1] = {x + math.cos(a) * r, y + math.sin(a) * r, "anel " .. r .. " #" .. (k + 1)} end end
    C[#C + 1] = {x, y, "ponto do outro"}
    return C
end
function S2L.tpfFit(pw, x, y, z, yaw, floor, why)  -- b113tpfit: first candidate whose K2_TeleportTo is true wins (the engine refuses a spot the capsule
    local rot = {Pitch = 0, Yaw = yaw or 0, Roll = 0}  -- does not fit and does not move). ok, x, y, z, label; none -> false + my own spot (never forced)
    pcall(function() local q = pw:K2_GetActorRotation(); if q and q.Yaw then rot.Yaw = q.Yaw end end)
    local C, nt, ns = S2L.tpfCands(x, y, yaw), 0, 0
    for n, c in ipairs(C) do
        local skip = false
        if c[1] ~= x or c[2] ~= y then          -- b108 checks: eye-level line from the other player clear, floor near its feet (when loaded)
            if S2L.tpfTrace(x, y, z + 60, c[1], c[2], z + 60) == true then skip = "parede"
            elseif floor and S2L.tpfFloor(c[1], c[2], z) == nil then skip = "sem chao" end
        end
        if skip then ns = ns + 1
        else
            nt = nt + 1
            local pk, res = pcall(function() return pw:K2_TeleportTo({X = c[1], Y = c[2], Z = z + 10}, rot) end)
            if pk and res == true then
                local q; pcall(function() q = pw:K2_GetActorLocation() end)
                q = q or {X = c[1], Y = c[2], Z = z + 10}
                log(string.format("S2CORE tpfloor: %s: K2_TeleportTo tentativa %d ok (%s; %d recusadas, %d puladas) -> %.0f,%.0f,%.0f", why, n, c[3], nt - 1, ns, q.X, q.Y, q.Z))
                return true, q.X, q.Y, q.Z, c[3]
            end
        end
    end
    local q; pcall(function() q = pw:K2_GetActorLocation() end)
    q = q or {X = 0, Y = 0, Z = 0}
    log(string.format("S2CORE tpfloor: %s: K2_TeleportTo recusado nas %d tentativas (%d puladas por parede/sem chao) -> fico onde estou %.0f,%.0f,%.0f (nunca dentro de objetos)", why, nt, ns, q.X, q.Y, q.Z))
    return false, q.X, q.Y, q.Z, "fiquei onde estava"
end
--@TPFLIB-END
function S2L.tpfInit(ST) ST.tpf = ST.tpf or {n = 0} return ST.tpf end
function S2L.tpfEnd(T, why)                        -- job over: streaming source gone
    local J = T.job; if not J then return end
    T.job = nil
    if J.src then pcall(function() if J.src:IsValid() then J.src:K2_DestroyActor() end end) end
    if why then log("S2CORE tpfloor: " .. J.why .. ": " .. why) end
end
function S2L.tpfStart(ST, a, why)                  -- far pull: main.lua safeTeleport's streaming source, then tpfTick places
    local T = S2L.tpfInit(ST)
    if T.job then return false end
    if not M.inWorld(nil, a.x, a.y, a.z) then log("S2CORE tpfloor: " .. why .. ": destino fora do mundo; nada"); return false end
    local src
    pcall(function()
        src = api.UEHelpers.GetWorld():SpawnActor(StaticFindObject("/Script/Engine.TargetPoint"), {X = a.x, Y = a.y, Z = a.z + 500}, {Pitch = 0, Yaw = 0, Roll = 0})
        local c = src:AddComponentByClass(StaticFindObject("/Script/Engine.WorldPartitionStreamingSourceComponent"), false, T0, false)
        c.bStreamingSourceEnabled = true
        c.TargetState = 1
    end)
    local now = os.clock()
    T.n = T.n + 1
    T.job = {why = why, x = a.x, y = a.y, z = a.z, yaw = a.yaw, t0 = now, next = now + 1, stage = "wait", w = 0, m = 0, src = src}
    log(string.format("S2CORE tpfloor: %s (#%d): streaming em %.0f,%.0f,%.0f; espero o chao do outro jogador", why, T.n, a.x, a.y, a.z))
    return true
end
function S2L.tpfNow(ST, a, why)                    -- immediate pull (same tick); floor not loaded -> the other player's spot in fly mode
    local pw = S2L.ioPawn()
    if not (valid(pw) and a and a.x and a.y and a.z) then return false end
    local T = S2L.tpfInit(ST)
    S2L.tpfEnd(T, T.job and ("substituido por " .. why) or nil)
    local yaw = a.yaw
    if yaw == nil then local r = S2L.remoteIn("tpfloor"); if r and r.x and (r.x - a.x) ^ 2 + (r.y - a.y) ^ 2 < 250000 then yaw = r.yaw end end
    local fly = S2L.tpfFloor(a.x, a.y, a.z) == nil
    local tx, ty, tz, how, tr
    if fly then tx, ty, tz, how, tr = a.x, a.y, a.z + 10, "ponto do outro", "chao do outro nao carregado"
    else tx, ty, tz, how, tr = S2L.tpfSpot(a.x, a.y, a.z, yaw) end
    local ok
    if (FL.tpfit or 0) == 1 then ok, tx, ty, tz, how = S2L.tpfFit(pw, a.x, a.y, a.z, yaw, not fly, why); tr = "b113 K2_TeleportTo"   -- b113tpfit
    else ok = pcall(function() pw:K2_SetActorLocation({X = tx, Y = ty, Z = tz}, false, {}, true) end) end
    if fly and (ok or (FL.tpfit or 0) ~= 1) then   -- b113: a refused fit starts no fly job
        pcall(function() pw.CharacterMovement:SetMovementMode(5, 0) end)
        local now = os.clock()
        T.n = T.n + 1
        T.job = {why = why, x = a.x, y = a.y, z = a.z, t0 = now, next = now + 1, stage = "land", w = 0, m = 0, tx = tx, ty = ty, tz = tz}
    end
    log(string.format("S2CORE tpfloor: %s: -> %.0f,%.0f,%.0f (%s; outro z=%.0f dz=%.0f) ok=%s%s | %s", why, tx, ty, tz, how, a.z, tz - a.z, tostring(ok), fly and ", voo ate o chao" or "", tostring(tr)))
    return ok
end
function M.tpfLeash(ST, pawn, x, y, z)             -- main.lua leash through the trampoline (x = host x + 300): true = handled here
    if (FL.tpfloor or 0) ~= 1 or api.ROLE ~= "client" then return false end
    local T = S2L.tpfInit(ST)
    if T.job then return true end                  -- busy (main.lua's tpBusy)
    local a = {x = x - 300, y = y, z = z}
    local r = S2L.remoteIn("tpfloor")
    if r and r.x and math.abs(r.x + 300 - x) < 5 and math.abs(r.y - y) < 5 then a.yaw = r.yaw end
    S2L.tpfStart(ST, a, "coleira")
    return true
end
function S2L.tpfHook(T)                            -- once per Lua state: main.lua's local safeTeleport -> trampoline (R1 probe reads it)
    local f, dbg = rawget(_G, "S2_toggleBridge"), rawget(_G, "debug")
    if type(f) == "function" and type(dbg) == "table" and dbg.getupvalue and dbg.setupvalue then
        for i = 1, 255 do
            local name, v = dbg.getupvalue(f, i)
            if name == nil then break end
            if name == "safeTeleport" and type(v) == "function" then
                if v ~= T.tramp then
                    local orig = v
                    T.tramp = function(pawn, x, y, z)
                        local m, fl = S2 and S2.mod, rawget(_G, "FL")
                        if fl and fl.tpfloor == 1 and m and m.tpfLeash then
                            local ok, done = pcall(m.tpfLeash, S2.state, pawn, x, y, z)
                            if ok and done then return end
                        end
                        return orig(pawn, x, y, z)
                    end
                    dbg.setupvalue(f, i, T.tramp)
                    log("S2CORE tpfloor: coleira do main.lua (safeTeleport, upvalue #" .. i .. ") passa pela regra do chao")
                end
                T.hooked = true
                return
            end
        end
    end
    if not T.noHook then T.noHook = true; log("S2CORE tpfloor: AVISO sem debug.setupvalue / upvalue safeTeleport em S2_toggleBridge -> coleira do main.lua SEM a regra do chao") end
end
function S2L.tpfStep(T, J, pw, now)
    if J.stage == "wait" then
        J.w = J.w + 1
        local r = S2L.remoteIn("tpfloor")              -- the other player's live spot (fresh, within 100 m of the job's)
        if r and r.x and now - (r.recv or -1000) < 2 and (r.x - J.x) ^ 2 + (r.y - J.y) ^ 2 < 1e8 then J.x, J.y, J.z, J.yaw = r.x, r.y, r.z, r.yaw or J.yaw end
        local loaded = S2L.tpfFloor(J.x, J.y, J.z) ~= nil
        if not loaded and J.w < (FL.tpfwait or 40) then return end
        local tx, ty, tz, how, tr = J.x, J.y, J.z + 10, "ponto do outro", "chao do outro nao carregado em " .. J.w .. " s"
        if loaded then tx, ty, tz, how, tr = S2L.tpfSpot(J.x, J.y, J.z, J.yaw) end
        local ok
        if (FL.tpfit or 0) == 1 then ok, tx, ty, tz, how = S2L.tpfFit(pw, J.x, J.y, J.z, J.yaw, loaded, J.why); tr = "b113 K2_TeleportTo"   -- b113tpfit
        else ok = pcall(function() pw:K2_SetActorLocation({X = tx, Y = ty, Z = tz}, false, {}, true) end) end
        if not ok and (FL.tpfit or 0) == 1 then
            S2L.tpfEnd(T, string.format("teleporte apos %d s: nenhum ponto livre perto do outro jogador -> fico onde estou", J.w))
            pcall(function() pw.CharacterMovement:SetMovementMode(1, 0) end)
            return
        end
        if not loaded then pcall(function() pw.CharacterMovement:SetMovementMode(5, 0) end) end
        J.tx, J.ty, J.tz, J.stage = tx, ty, tz, "land"
        log(string.format("S2CORE tpfloor: %s: teleporte apos %d s -> %.0f,%.0f,%.0f (%s; outro z=%.0f dz=%.0f) ok=%s%s | %s", J.why, J.w, tx, ty, tz, how, J.z, tz - J.z, tostring(ok), loaded and "" or ", modo voo", tr))
        return
    end
    J.m = J.m + 1
    local q; pcall(function() q = pw:K2_GetActorLocation() end)
    if not q then return end
    if q.Z < J.tz - 3000 and (J.falls or 0) < 3 then
        J.falls = (J.falls or 0) + 1
        if (FL.tpfit or 0) == 1 then
            local okF, fx, fy, fz = S2L.tpfFit(pw, J.x, J.y, J.z, J.yaw, false, J.why .. " (caiu)")   -- b113tpfit
            if okF then J.tx, J.ty, J.tz = fx, fy, fz end
        else pcall(function() pw:K2_SetActorLocation({X = J.tx, Y = J.ty, Z = J.tz}, false, {}, true) end) end
        pcall(function() pw.CharacterMovement:SetMovementMode(5, 0) end)
        log(string.format("S2CORE tpfloor: %s: caiu (z %.0f) -> voo de novo (%d)", J.why, q.Z, J.falls))
        return
    end
    local g = S2L.tpfFloor(q.X, q.Y, q.Z)
    if g or J.m >= 60 then
        pcall(function() pw.CharacterMovement:SetMovementMode(1, 0) end)
        local r = S2L.remoteIn("tpfloor")
        local oz = (r and r.z) or J.z
        log(string.format("S2CORE tpfloor: CHEGOU (%s) em %.0f s: eu z=%.0f outro z=%.0f dz=%.0f cm chao=%s %s", J.why, now - J.t0, q.Z, oz, q.Z - oz,
            tostring(g ~= nil), math.abs(q.Z - oz) <= 150 and "MESMO ANDAR" or "ANDAR DIFERENTE"))
        S2L.tpfEnd(T, nil)
    end
end
function S2L.tpfTick(ST)                           -- tick100; job steps 1/s
    local T, now = S2L.tpfInit(ST), os.clock()
    if (FL.tpfloor or 0) ~= 1 then
        if T.job then S2L.tpfEnd(T, "tpfloor desligado"); local pw = S2L.ioPawn(); if valid(pw) then pcall(function() pw.CharacterMovement:SetMovementMode(1, 0) end) end end
        return
    end
    if not T.hooked and api.ROLE == "client" and now >= (T.hookAt or -1000) then T.hookAt = now + 5; S2L.tpfHook(T) end
    local J = T.job
    if not J or now < J.next then return end
    J.next = now + 1
    local pw = S2L.ioPawn()
    if now - J.t0 > (FL.tpfwait or 40) + 75 then
        S2L.tpfEnd(T, "desisti (tempo)")
        if valid(pw) then pcall(function() pw.CharacterMovement:SetMovementMode(1, 0) end) end
        return
    end
    if valid(pw) then S2L.tpfStep(T, J, pw, now) end
end
-- ===== end b108tpfloor
-- ===== b100ft (flag ftfollow, default 0; user rule N6 "fast travel (guides): both players travel together"). Simple design (agreed):
-- whoever travels with a guide NPC (either player, N14), the other is teleported along after the load screen.
-- Game data (gen_ft_table.py): a guide's answer runs EDialogAction::FastTravel (DialogActionParam = location index in FastTravelPrototypes);
-- the native FastTravelManager (no reflected functions) takes the money, skips GuideDelay, shows a loading screen
-- (ELoadingDestination::FastTravel) and puts the player at the location's PlayerLocation (FastTravelLocationPrototypes: 14 points, 793 m apart at least).
-- Detector (both roles): the own pawn (same object) jumps >= ftjump cm and lands within ftrad cm of a PlayerLocation, outside our own follow
-- window and not next to the other player (leash, cjoin and follow teleports land there). After ftsettle s (cinematic lock: up to 30 s) it sends
-- "#FT:<seq>,<loc>,<x>,<y>,<z>,<os.time()>" for ftkeep s. Follower: held while in a dialog / cinematic / frozen / dead (queue); already within
-- ftnear cm = nothing; else client: main.lua's own leash (safeTeleport: streaming + ground wait) lowered to ftleash m for 1 s (batch 31 switch);
-- host: K2_SetActorLocation beside the traveller (+ftoff cm) in fly mode, walk when main.lua's ground trace hits (1 Hz), fall guard 15 s.
-- Client leash park: main.lua pulls the client back to the host beyond leashm, so the client's leash is parked while it is in a dialog, ftgrace s
-- after it, and after its own travel until the host arrived (2*ftwait s max); the 5 s flags reload keeps the park. Tie: the host wins (N14).
S2L.FT_LOC = {   -- gen_ft_table.py: FastTravelLocationPrototypes.cfg PlayerLocation (cm), whole game
    {"terricon", 421180, 412370, 2120}, {"asylum", 478540, 526470, 340}, {"sultansk", 665202, 617990, 252}, {"rookievillage", 445964, 657845, -2349},
    {"concreteplant", 498260, 379420, 849}, {"rostok", 321267, 412054, 360}, {"yanov", 215900, 270210, 946}, {"malahit", 107594, 471669, 424},
    {"himzavod", 318986, 526376, 613}, {"zalesie", 398700, 545500, 990}, {"pripyat", 232150, 159790, 1114}, {"kazkovy", 259517, 599445, 568},
    {"purification_facility", 409220, 258260, 1040}, {"bunker", 384673, 341937, 565},
}
function S2L.ftDist(a, b) return math.sqrt((a.x - b.x) ^ 2 + (a.y - b.y) ^ 2 + (a.z - b.z) ^ 2) end
function S2L.ftNear(x, y, z)                     -- nearest guide arrival point: sid, cm
    local best, bd
    for _, r in ipairs(S2L.FT_LOC) do
        local d = math.sqrt((x - r[2]) ^ 2 + (y - r[3]) ^ 2 + (z - r[4]) ^ 2)
        if not bd or d < bd then best, bd = r[1], d end
    end
    return best, bd
end
function S2L.ftRemote(now, maxAge)               -- the other player's live position (fresh snapshot), or nil
    local r = S2L.remoteIn("ft")
    if r and r.x and r.y and r.z and now - (r.recv or -1000) < (maxAge or 1) then return {x = r.x, y = r.y, z = r.z} end
end
function S2L.ftMe(pw)
    local q; pcall(function() q = pw:K2_GetActorLocation() end)
    if q then return {x = q.X, y = q.Y, z = q.Z} end
end
function S2L.ftLock(ST, F, now)                  -- why this side must not be moved now (nil = free); the dialog view read is cineTick0's (measured)
    local pc = F.pc
    if not valid(pc) then pc = FindFirstOf("PC"); F.pc = pc end
    local cine = false
    if valid(pc) then pcall(function() cine = pc.bInCinematicSequence == true and pc:IsMoveInputIgnored() == true end) end
    if cine then return "cinematica" end
    if not valid(F.dw) or now - (F.dwAt or -1000) > 2 then
        F.dwAt = now; F.dw = nil
        for _, w in ipairs(FindAllOf("W_DialogueView_C") or {}) do if valid(w) then pcall(function() if w:IsInViewport() then F.dw = w end end) end end
    end
    local dlg = false
    if valid(F.dw) then pcall(function() dlg = F.dw:IsInViewport() == true and F.dw:IsVisible() == true end) end
    if dlg then return "dialogo" end
    if ST.cv and ST.cv.fz ~= nil then return "congelado (cinematica do host)" end
    if (ST.nd and ST.nd.dead ~= nil) or (ST.life and ST.life.down ~= nil) then return "morto/caido" end
    return nil
end
function S2L.ftWrap()                            -- client: the 5 s flags reload (main.lua S2_loadFlags) must not undo our leash park
    if rawget(_G, "S2_loadFlags_ft") ~= nil or type(rawget(_G, "S2_loadFlags")) ~= "function" then return end
    local prev, fl = rawget(_G, "S2_loadFlags"), FL
    rawset(_G, "S2_loadFlags_ft", prev)
    rawset(_G, "S2_loadFlags", function(quiet)
        local F = S2.state and S2.state.ft
        local hold = F and F.park and F.lsave
        if hold then fl.leashm = F.lsave end          -- the reload compares with the real value (no "1e9 -> 350")
        local r = prev(quiet)
        if hold and F.park then F.lsave = fl.leashm; fl.leashm = 1e9 end
        return r
    end)
end
function S2L.ftPark(ST, F, on, why)              -- client: main.lua's leash off (leashm 1e9) / back to the real value
    if (FL.leashown or 0) == 1 then                -- b115leash: a SOFT park request (a pull beats it, as before); no save/restore here
        if on then F.park = why; S2L.lrSet(ST, "ft_park", 1e9, nil, why, false)
        elseif F.park then F.park = nil; S2L.lrDrop(ST, "ft_park", why) end
        return
    end
    if on then
        if not F.park then
            if FL.leashm ~= 1e9 then F.lsave = FL.leashm; FL.leashm = 1e9 else F.lsave = nil end
            log("S2CORE ft: coleira parada (" .. why .. "; era " .. tostring(F.lsave or "ja parada") .. ")")
        elseif FL.leashm ~= 1e9 and not ((ST.cv and ST.cv.leashSave) or (ST.nd and ST.nd.leashSave) or (ST.join and ST.join.leashSave)) then
            F.lsave = FL.leashm; FL.leashm = 1e9           -- given back meanwhile (join unpark): parked again; another switch's 1 s window is left alone
        end
        F.park = why
    elseif F.park then
        local was = F.park; F.park = nil
        if F.lsave and FL.leashm == 1e9 then FL.leashm = F.lsave end
        F.lsave = nil
        log("S2CORE ft: coleira de volta a " .. tostring(FL.leashm) .. " (" .. why .. "; estava parada por " .. tostring(was) .. ")")
    end
end
function S2L.ftSee(ST, F, pw, now)               -- own pawn sample: did I just travel with a guide?
    local q = S2L.ftMe(pw); if not q then return end
    local addr; pcall(function() addr = pw:GetAddress() end)
    q.addr = addr
    local L = F.lp; F.lp = q
    if not L then return end
    local d = S2L.ftDist(q, L)
    if d < (FL.ftjump or 30000) then return end
    local sid, dd = S2L.ftNear(q.x, q.y, q.z)
    local why
    if dd > (FL.ftrad or 10000) then why = "longe dos pontos de guia"
    elseif addr ~= L.addr and (FL.ftpawn or 0) ~= 1 then why = "pawn trocado (load)"
    elseif now < (F.selfUntil or -1000) then why = "teleporte nosso (seguir)"
    else
        local r = S2L.ftRemote(now, 2)
        if r and S2L.ftDist(r, q) <= (FL.ftnear or 5000) then why = "o outro jogador ja esta aqui" end
    end
    log(string.format("S2CORE ft: salto de %.0f m, ponto de guia mais perto %s a %.0f m -> %s", d / 100, sid, dd / 100, why or "VIAGEM RAPIDA"))
    if not why then F.cand = {sid = sid, at = now, d = d}; F.dlgAt = nil end   -- the travel ends the after-dialog park; "my travel" holds it now
end
function S2L.ftSend(ST, F, pw, now)              -- after the load screen: tell the other side where I am
    local C = F.cand; if not C then return end
    if now - C.at < (FL.ftsettle or 3) then return end
    if F.lk == "cinematica" and now - C.at < 30 then return end
    local q = S2L.ftMe(pw); if not q then return end
    local sid, dd = S2L.ftNear(q.x, q.y, q.z)
    if sid ~= C.sid or dd > (FL.ftrad or 10000) then F.cand = nil; log("S2CORE ft: saiu do ponto " .. C.sid .. " antes do envio (outro teleporte?); nada enviado"); return end
    F.seq = (F.seq or 0) + 1
    F.ev = string.format("#FT:%d,%s,%.0f,%.0f,%.0f,%d", F.seq, C.sid, q.x, q.y, q.z, os.time())
    F.evUntil = now + (FL.ftkeep or 30)
    F.mine = {at = now, sid = C.sid, seq = F.seq}
    F.cand = nil
    log(string.format("S2CORE ft: VIAGEM RAPIDA minha para %s (salto de %.0f m) -> #FT %d enviado; o outro jogador vem junto", C.sid, C.d / 100, F.seq))
end
function S2L.ftWaitOther(ST, F, pw, now)          -- traveller: PASS line when the other player arrived
    local Mi = F.mine; if not Mi then return end
    local q, r = S2L.ftMe(pw), S2L.ftRemote(now, 1)
    if q and r and S2L.ftDist(q, r) <= (FL.ftnear or 5000) then
        log(string.format("S2CORE ft: o outro jogador chegou junto (%.0f m) %.0f s depois da minha viagem para %s", S2L.ftDist(q, r) / 100, now - Mi.at, Mi.sid))
        F.mine = nil
    elseif now - Mi.at > 2 * (FL.ftwait or 45) then
        log(string.format("S2CORE ft: o outro jogador NAO chegou em %.0f s (viagem para %s)", now - Mi.at, Mi.sid))
        F.mine = nil
    end
end
function S2L.ftIn(ST, F, now)                     -- the other side's #FT
    local sg = x1part("FT"); if not sg then return end
    local seq, sid, x, y, z, ts = sg:match("^(%d+),([%w_]+),(%-?[%d%.]+),(%-?[%d%.]+),(%-?[%d%.]+),(%d+)")
    if not seq then return end
    local key = seq .. "@" .. ts
    if key == F.rkey then return end
    F.rkey = key
    local age = os.time() - tonumber(ts)
    if age > (FL.ftmaxage or 120) then log("S2CORE ft: #FT " .. seq .. " (" .. sid .. ") antigo (" .. age .. " s), ignorado"); return end
    local own = F.cand or (F.mine and now - F.mine.at < (FL.ftkeep or 30) and F.mine) or nil
    if own then                                    -- both travelled (tie): the host wins (N14)
        if api.ROLE == "host" then log("S2CORE ft: os dois viajaram (" .. own.sid .. " / " .. sid .. "): o host vence, nao sigo o cliente"); return end
        log("S2CORE ft: os dois viajaram (" .. sid .. " / " .. own.sid .. "): o host vence, sigo o host")
    end
    F.cand = nil; F.mine = nil
    F.pend = {seq = seq, sid = sid, x = tonumber(x), y = tonumber(y), z = tonumber(z), at = now, tries = 0}
    log(string.format("S2CORE ft: o outro jogador viajou para %s (#FT %s) -> vou junto", sid, seq))
end
function S2L.ftArrived(F, q, now, t0)             -- one PASS line for both roles
    local r = S2L.ftRemote(now, 2)
    local d = (q and r) and S2L.ftDist(q, r) or nil
    if d and d <= (FL.ftnear or 5000) then log(string.format("S2CORE ft: CHEGUEI junto do outro jogador (%.0f m) em %.1f s", d / 100, now - t0))
    else log(string.format("S2CORE ft: seguir terminou LONGE do outro jogador (%s m) em %.1f s", d and string.format("%.0f", d / 100) or "?", now - t0)) end
    return d ~= nil and d <= (FL.ftnear or 5000)
end
function S2L.ftGround(x, y, z)                   -- main.lua groundBelow: same call and arguments (at most once a second here)
    local ksl = StaticFindObject("/Script/Engine.Default__KismetSystemLibrary")
    if not valid(ksl) then return false end
    local ok, hit, iz = pcall(function()
        local out = {}
        local h = ksl:LineTraceSingle(api.UEHelpers.GetWorld(), {X = x, Y = y, Z = z + 2000}, {X = x, Y = y, Z = z - 6000}, 0, false, {}, 0, out, true, {R = 1, G = 0, B = 0, A = 1}, {R = 0, G = 1, B = 0, A = 1}, 0)
        return h == true, out.ImpactPoint and out.ImpactPoint.Z
    end)
    return ok and hit == true and iz ~= nil and math.abs(iz - z) < 3000
end
function S2L.ftTpStep(ST, F, pw, now)            -- host follower: fly beside the traveller, walk on ground, fall guard
    local T = F.tp
    if T.tpf then   -- b108 FT2: the tpfloor job places the host; done when the job is gone
        if not (ST.tpf and ST.tpf.job) then S2L.ftArrived(F, S2L.ftMe(pw), now, T.t0); F.pend = nil; F.tp = nil end
        return
    end
    if now < (T.next or -1000) then return end
    T.next = now + 1
    local q = S2L.ftMe(pw)
    local function go()
        local ok = pcall(function() pw:K2_SetActorLocation({X = T.x, Y = T.y, Z = T.z + 100}, false, {}, true) end)
        local okF = pcall(function() pw.CharacterMovement:SetMovementMode(5, 0) end)
        T.stage, T.t1 = "fly", now
        return ok, okF
    end
    if T.stage == "go" then
        local ok, okF = go()
        log(string.format("S2CORE ft: host teleportado para junto do cliente (%s; %.0f,%.0f,%.0f) ok=%s voo=%s; espero o chao", T.sid, T.x, T.y, T.z, tostring(ok), tostring(okF)))
    elseif T.stage == "fly" then
        local g = now - T.t1 >= 3 and S2L.ftGround(T.x, T.y, T.z)
        if g or now - T.t1 >= (FL.ftfly or 40) then
            local okW = pcall(function() pw.CharacterMovement:SetMovementMode(1, 0) end)
            T.stage, T.t2 = "walk", now
            log(string.format("S2CORE ft: modo andando ok=%s (chao=%s apos %.0f s)", tostring(okW), tostring(g == true), now - T.t1))
            if not T.told then T.told = true; S2L.ftArrived(F, q, now, T.t0); F.pend = nil end
        end
    elseif T.stage == "walk" then
        if q and q.z < T.z - 300 and (T.falls or 0) < 2 then
            T.falls = (T.falls or 0) + 1
            local ok = go()
            log(string.format("S2CORE ft: host caiu (z %.0f < %.0f) -> voo de novo ok=%s (%d)", q.z, T.z, tostring(ok), T.falls))
        elseif now - T.t2 >= 15 then F.tp = nil end
    end
end
function S2L.ftFollow(ST, F, pw, now)
    if F.tp then return S2L.ftTpStep(ST, F, pw, now) end
    local P, Fo = F.pend, F.fol
    if Fo then                                     -- client: main.lua's leash is doing it; give the leash back after 1 s, check arrival
        if Fo.lowUntil and now >= Fo.lowUntil then Fo.lowUntil = nil; if FL.leashm == Fo.temp then FL.leashm = Fo.real end end
        local q, r = S2L.ftMe(pw), S2L.ftRemote(now, 1)
        if q and r and S2L.ftDist(q, r) <= (FL.ftnear or 5000) then
            if FL.leashm == Fo.temp then FL.leashm = Fo.real end
            S2L.ftArrived(F, q, now, Fo.t0); F.fol = nil; F.pend = nil
        elseif now > Fo.waitUntil then
            if FL.leashm == Fo.temp then FL.leashm = Fo.real end
            F.fol = nil
            if P and Fo.tries >= 3 then log("S2CORE ft: seguir nao funcionou em 3 tentativas; desisto (" .. P.sid .. ")"); F.pend = nil
            elseif P then P.tries = Fo.tries; log("S2CORE ft: seguir sem efeito em " .. tostring(FL.ftwait or 45) .. " s; de novo") end
        end
        return
    end
    if not P then return end
    if now - P.at > (FL.ftqmax or 300) then log("S2CORE ft: seguir para " .. P.sid .. " esperou demais (" .. tostring(P.held) .. "); desisto"); F.pend = nil; return end
    if F.lk then
        if P.held ~= F.lk then P.held = F.lk; log("S2CORE ft: seguir segurado: estou em " .. F.lk .. " (na fila)") end
        return
    end
    if P.held then log("S2CORE ft: " .. P.held .. " acabou -> sigo agora"); P.held = nil end
    local tx, ty, tz = P.x, P.y, P.z
    local r = S2L.ftRemote(now, 1)
    if r and S2L.ftDist(r, P) <= (FL.ftrad or 10000) then tx, ty, tz = r.x, r.y, r.z end   -- the traveller's live position beats the sent one
    if not M.inWorld(nil, tx, ty, tz) then log("S2CORE ft: destino fora do mundo; nao sigo"); F.pend = nil; return end
    local q = S2L.ftMe(pw); if not q then return end
    local d = S2L.ftDist(q, {x = tx, y = ty, z = tz})
    if d <= (FL.ftnear or 5000) then log(string.format("S2CORE ft: ja estou no destino %s (%.0f m) -> nada a fazer", P.sid, d / 100)); F.pend = nil; return end
    F.selfUntil = now + (FL.ftwait or 45) + 20
    if api.ROLE == "client" and (FL.leashown or 0) == 1 then   -- b115leash: a 1 s pull request; refused = F.pend waits (asked again in 0.2 s)
        if not S2L.lrPull(ST, "ft", FL.ftleash or 30, 1, P.sid) then return end
        if F.park then F.park = nil; S2L.lrDrop(ST, "ft_park", "seguir") end
        F.dlgAt = nil
        F.fol = {lowUntil = now + 1, waitUntil = now + (FL.ftwait or 45), t0 = now, tries = (P.tries or 0) + 1}
        log(string.format("S2CORE ft: cliente segue o host ate %s: coleira em %s m por 1 s (%.0f m ate o destino, tentativa %d)", P.sid, tostring(FL.ftleash or 30), d / 100, F.fol.tries))
    elseif api.ROLE == "client" then
        local real = FL.leashm
        if F.park then real = F.lsave or real; F.park = nil; F.lsave = nil end
        if real == 1e9 then real = (ST.join and ST.join.parked) or 350 end
        F.dlgAt = nil
        F.fol = {real = real, temp = FL.ftleash or 30, lowUntil = now + 1, waitUntil = now + (FL.ftwait or 45), t0 = now, tries = (P.tries or 0) + 1}
        FL.leashm = F.fol.temp
        log(string.format("S2CORE ft: cliente segue o host ate %s: coleira em %s m por 1 s (%.0f m ate o destino, tentativa %d)", P.sid, tostring(F.fol.temp), d / 100, F.fol.tries))
    elseif (FL.tpfloor or 0) == 1 and S2L.tpfStart(ST, {x = tx, y = ty, z = tz}, "ft host " .. P.sid) then   -- b108 FT2
        F.tp = {tpf = true, t0 = now, sid = P.sid}
    else
        F.tp = {x = tx + (FL.ftoff or 200), y = ty, z = tz, stage = "go", t0 = now, sid = P.sid}
        S2L.ftTpStep(ST, F, pw, now)
    end
end
function S2L.ftOut(ST)                            -- x1 part, only for ftkeep s after my own travel
    local F = ST.ft
    if (FL.ftfollow or 0) ~= 1 or not F or not F.ev or os.clock() > (F.evUntil or 0) then return "" end
    return F.ev
end
function S2L.ftTick(ST)
    local F = ST.ft
    if (FL.ftfollow or 0) ~= 1 then                 -- off: only give back what we hold
        if F and (F.park or F.fol or F.tp or F.pend or F.cand or F.mine or F.lp) then
            if F.park then S2L.ftPark(ST, F, false, "ftfollow desligado") end
            if F.fol and FL.leashm == F.fol.temp then FL.leashm = F.fol.real end
            if F.tp then local pw = S2L.ioPawn(); if valid(pw) then pcall(function() pw.CharacterMovement:SetMovementMode(1, 0) end) end end
            F.fol, F.tp, F.pend, F.cand, F.mine, F.lp = nil, nil, nil, nil, nil, nil
        end
        return
    end
    if not F then F = {}; ST.ft = F end
    local now = os.clock()
    if now < (F.nextAt or -1000) then return end
    F.nextAt = now + 0.2
    if api.ROLE == "client" then S2L.ftWrap() end
    local pw = S2L.ioPawn()
    if not valid(pw) then return end
    F.lk = S2L.ftLock(ST, F, now)
    S2L.ftIn(ST, F, now)
    S2L.ftSee(ST, F, pw, now)
    S2L.ftSend(ST, F, pw, now)
    S2L.ftWaitOther(ST, F, pw, now)
    S2L.ftFollow(ST, F, pw, now)
    if api.ROLE == "client" then
        if F.lk == "dialogo" or F.lk == "cinematica" then F.dlgAt = now end
        local want = not F.fol and (((F.lk == "dialogo" or F.lk == "cinematica") and F.lk) or (now - (F.dlgAt or -1000) < (FL.ftgrace or 15) and "logo depois de um dialogo")
            or (F.cand and "minha viagem") or (F.mine and "minha viagem: espero o host")) or nil
        if want then S2L.ftPark(ST, F, true, want) elseif F.park then S2L.ftPark(ST, F, false, F.fol and "seguir" or "livre") end
    end
end
-- ===== end b100ft
-- ===== client leash (flag cfollow=1): the client steps toward the host, cfollowstep m per 100 ms tick =====
-- Same idea as batch 54 hostfollow, for the CLIENT: instead of a hard 350 m teleport (main.lua leash) it walks
-- 3 m closer every tick, so it stays tethered to the host without a jump. Pulls only when farther than cfollowm m.
-- ===== batch 59 probe (dev, flag iprobe=1): list the interactables near the local player and log each state change.
-- Purpose: see how a lever/door/monitor reports its activation on the host and whether the client has the matching object.
-- Only primitive reads are kept: unknown names come back as a fresh TrivialObject every access (would spam the log).
function S2L.iprobeTick(ST)
    if FL.iprobe ~= 1 then return end
    local now = os.clock()
    if now - (ST.ipAt or 0) < 1.0 then return end
    ST.ipAt = now
    local lp = S2.io and S2.io.pawn; if not valid(lp) then return end
    local p = lp:K2_GetActorLocation()
    local P = ST.ip or {}; ST.ip = P
    local rr = FL.ipr or 2500
    for _, cls in ipairs({"Lever", "DoorView", "Door", "InteractableObject", "InteractableMonitorSection", "BP_DestructibleObject_C", "BP_DestructibleObject_Small_C"}) do
        for _, o in ipairs(FindAllOf(cls) or {}) do pcall(function()
            if not o:IsValid() then return end
            local q = o:K2_GetActorLocation()
            if (q.X - p.X) ^ 2 + (q.Y - p.Y) ^ 2 > rr * rr then return end
            local k = string.format("%s %.0f,%.0f,%.0f", cls, q.X / 10, q.Y / 10, q.Z / 10)
            local s = ""
            local function f(name, v) local t = type(v); if t == "number" or t == "boolean" or t == "string" then s = s .. string.format(" %s=%s", name, tostring(v)) end end
            do local v; pcall(function() v = o.CurrentValue end); f("val", v) end
            do local v; pcall(function() v = o.bInteractionActive end); f("act", v) end
            do local v; pcall(function() v = o.bEnabled end); f("en", v) end
            do local v; pcall(function() v = o.bBroken end); f("brk", v) end
            do local v; pcall(function() v = o:IsInteractionActive() end); f("A", v) end
            do local v; pcall(function() v = o:IsEnabled() end); f("E", v) end
            do local c; pcall(function() c = o.FirstLeafMeshComponent end); if c and c:IsValid() then pcall(function() s = s .. string.format(" yaw=%.0f", c:K2_GetComponentRotation().Yaw) end) end end
            for _, fn in ipairs({"bBroken", "bIsBroken", "bDestroyed", "bUsed", "Health", "CurrentHealth"}) do do local v; pcall(function() v = o[fn] end); f(fn, v) end end
            do local m; pcall(function() m = o.Mesh end); if m and m:IsValid() then pcall(function() s = s .. " vis=" .. tostring(m:IsVisible()) end); pcall(function() local sm = m.StaticMesh; if sm and sm:IsValid() then s = s .. " sm=" .. sm:GetFName():ToString() end end); pcall(function() s = s .. " phys=" .. tostring(m:IsSimulatingPhysics()) end) end end
            if s == "" then return end
            pcall(function() s = s .. " " .. tostring(o:GetFullName()) end)
            if P[k] ~= s then P[k] = s; log("S2IP " .. k .. s) end
        end) end
    end
    if FL.iprd == 1 then                              -- discover mode: one line per unique actor class near the player
        local cd = ST.ipc or {}; ST.ipc = cd
        for _, o in ipairs(FindAllOf("Actor") or {}) do pcall(function()
            if not o:IsValid() then return end
            local q = o:K2_GetActorLocation()
            if (q.X - p.X) ^ 2 + (q.Y - p.Y) ^ 2 > rr * rr then return end
            local cn
            pcall(function() cn = o:GetClass():GetFName():ToString() end)
            if cn and not cd[cn] then
                cd[cn] = true
                log(string.format("S2IPD %s %.0f,%.0f,%.0f %s", cn, q.X / 10, q.Y / 10, q.Z / 10, tostring(o:GetFullName())))
            end
        end) end
    end
end
-- b70f probe (flag iactlog=1): which object the local player is using and when an interaction animation runs. Read-only, 5 Hz.
function S2L.iactTick(ST)
    if FL.iactlog ~= 1 then return end
    local I = ST.ia or {}; ST.ia = I
    local now = os.clock(); if now - (I.at or 0) < 0.2 then return end; I.at = now
    local pc = I.pc; if not valid(pc) then pc = FindFirstOf("PC"); I.pc = pc end
    if not valid(pc) then return end
    local s = ""
    pcall(function() s = s .. "prog=" .. tostring(pc:IsInteractionInProgress()) end)
    pcall(function() local a = pc:GetInteractedActor(); if a and a:IsValid() then local q = a:K2_GetActorLocation(); s = s .. string.format(" ator=%s@%.0f,%.0f,%.0f", a:GetClass():GetFName():ToString(), q.X, q.Y, q.Z) else s = s .. " ator=-" end end)
    pcall(function() s = s .. " len=" .. tostring(pc:GetInteractLengthType()) end)
    pcall(function() s = s .. " moveIgn=" .. tostring(pc:IsMoveInputIgnored()) end)
    pcall(function() s = s .. " cin=" .. tostring(pc.bInCinematicSequence) end)
    if s ~= I.last then I.last = s; log("S2CORE iact: " .. s) end
end
function S2L.cfollowTick(ST)
    if api.ROLE ~= "client" then return end
    local r = S2L.remoteIn("cfollow/cfall"); if not r then return end
    if not (ST.peer and ST.peer.state == "up") then return end
    local now = os.clock()
    if now - (r.recv or 0) > 3 then return end
    local pw = S2.io and S2.io.pawn; if not valid(pw) then return end
    local p = pw:K2_GetActorLocation()
    -- b70 fall rescue: a client that fell through the world (falling cfallsec s and cfallm m below the host) is put beside the host
    if FL.cfall == 1 then
        local falling = false
        pcall(function() falling = pw.CharacterMovement:IsFalling() end)
        if falling then ST.cfFallT = ST.cfFallT or now else ST.cfFallT = nil end
        if ST.cfFallT and now - ST.cfFallT >= (FL.cfallsec or 3) and r.z and p.Z < r.z - (FL.cfallm or 30) * 100 then
            ST.cfFallT = nil
            local ok   -- b108 CF
            if (FL.tpfloor or 0) == 1 then ok = S2L.tpfNow(ST, r, "cfall")
            else ok = pcall(function() pw:K2_SetActorLocation({X = r.x + 100, Y = r.y, Z = r.z + 150}, false, {}, true) end) end
            local okS = pcall(function() pw.CharacterMovement:StopMovementImmediately() end)
            log(string.format("S2CORE cfall: cliente caindo fora do mundo (z=%.0f, host z=%.0f) -> posto ao lado do host ok=%s stop=%s", p.Z, r.z, tostring(ok), tostring(okS)))
            return
        end
    end
    if FL.cfollow ~= 1 then return end
    do local f = false; pcall(function() f = pw.CharacterMovement:IsFalling() end); if f then return end end   -- b70b: wait until it lands
    local dx, dy = r.x - p.X, r.y - p.Y
    local d = math.sqrt(dx * dx + dy * dy)
    local keep = (FL.cfollowm or 30) * 100
    if d <= keep then return end
    local step = math.min((FL.cfollowstep or 3) * 100, d - keep)
    local nx, ny = p.X + dx / d * step, p.Y + dy / d * step
    local ok, e = pcall(function() pw:K2_SetActorLocation({X = nx, Y = ny, Z = p.Z + (FL.cfollowlift or 100)}, false, {}, true) end)
    if not ok then log("S2CORE cfollow erro: " .. tostring(e))
    elseif (ST.cfLog or 0) < now then ST.cfLog = now + 2; log(string.format("S2CORE cfollow: %.0f m -> passo %.0f cm", d / 100, step)) end
end

-- ===== b173 natbr (flag natbroff, default 0): client natnpc tick right after the bridge read of in.json (snapshot age); tick100 = fallback
function S2L.natCl(ST, via)
    local okC, eC = pcall(function() local on = (FL.natnpc or 0) == 1; local lp = on and (S2.io and S2.io.pawn) or nil; return s2n_nat_client(on and 1 or 0, on and (x1part("NN") or "") or "", on and (x1part("NL") or "") or "", valid(lp) and lp:GetAddress() or 0, (ST.pup and valid(ST.pup.pawn)) and ST.pup.pawn:GetAddress() or 0, (ST.tgt and valid(ST.tgt.pawn)) and ST.tgt.pawn:GetAddress() or 0, ((ST.pup and ST.pup.before) or (ST.tgt and ST.tgt.before) or (ST.cl and ST.cl.pend)) and 1 or 0, via or 0) end)
    if not okC then ST.natErr = (ST.natErr or 0) + 1; if ST.natErr <= 5 then log("S2CORE natnpc erro (cliente): " .. tostring(eC)) end end
    if via == 1 then ST.natBrAt = os.clock() end
end
function S2L.natBrOn(ST)                       -- FL.natbroff 0 and the DLL's natbr == 1 (an old DLL has no natbr key: off); read once per s
    local now = os.clock()
    if ST.natBrT and now - ST.natBrT < 1 then return ST.natBrV end
    local v = false
    if (FL.natbroff or 0) ~= 1 and rawget(_G, "s2n_nat_cfg") then local ok, b, n = pcall(s2n_nat_cfg, "natbr"); v = ok and b == true and n == 1 end
    ST.natBrT, ST.natBrV = now, v
    return v
end
function S2L.natBrRecent(ST) return S2L.natBrOn(ST) and ST.natBrAt ~= nil and os.clock() - ST.natBrAt < 0.35 end
function S2L.natBrFire()                       -- called by the S2.io proxy when main.lua sets S2.io.at (in.json just read)
    local ST = S2.state
    if not ST or api.ROLE == "host" or not rawget(_G, "s2n_nat_client") or not S2L.natBrOn(ST) then return end
    S2L.natCl(ST, 1)
end
function S2L.natBrIo(ST)                       -- once per S2.io table: proxy whose __newindex stores into a backing table
    S2.natBrCb = S2L.natBrFire                 -- the current core's function (hot reload safe: the proxy looks it up each call)
    local io0 = S2.io
    if type(io0) ~= "table" then return end
    local mt0 = getmetatable(io0)
    if mt0 and mt0.s2br then return end
    if mt0 then return end                     -- someone else's metatable: leave it (tick100 keeps the natnpc call)
    local back = {}
    for k, v in pairs(io0) do back[k] = v end
    local px = setmetatable({}, {s2br = true, __index = back, __newindex = function(_, k, v) back[k] = v; if k == "at" then local cb = S2.natBrCb; if cb then pcall(cb) end end end})
    S2.io = px
    log("S2CORE natbr: S2.io proxy ativo (natnpc do cliente logo apos a leitura do in.json)")
end

function M.tick100(ST)
    if (FL.b164off or 0) == 0 and not (FL.qitem == 0 and FL.qirm == 0 and FL.qdrop == 0 and FL.clammo == 2 and (api.ROLE ~= "host" or FL.qmrmhost == 1)) then FL.qitem, FL.qirm, FL.qdrop, FL.clammo = 0, 0, 0, 2; if api.ROLE == "host" then FL.qmrmhost = 1 end; if not S2L.b164fl then S2L.b164fl = true; log("S2CORE b164: item de missao para os dois: qitem=0 qirm=0 qdrop=0 qmrmhost=1 (host) e clammo=2 forcados pelo codigo; flags.txt nao volta (b164off=1 devolve); lead=" .. tostring(FL.lead) .. " qimirror=" .. tostring(FL.qimirror)) end end   -- b164
    pcall(S2L.ffInstall)                                             -- b143perf ffc
    pcall(S2L.acInstall)                                             -- b149hitch acache
    local okNp, eNp = pcall(S2L.noPause, ST)                         -- b88nopause
    if not okNp then S2.errs.n = S2.errs.n + 1; S2.errs.last = "noPause: " .. tostring(eNp):sub(1, 160); ST.npErr = (ST.npErr or 0) + 1; if ST.npErr <= 5 then log("S2CORE noPause erro: " .. tostring(eNp)) end end
    local okW, gateOk = pcall(S2L.bodyGate, ST)                      -- batch 44: before the dup guard, the target and the puppet
    if not okW then S2.errs.n = S2.errs.n + 1; S2.errs.last = "bodyGate: " .. tostring(gateOk):sub(1, 160); gateOk = false end
    for _, nf in ipairs({{"peerTick", peerTick}, {"cineTick", cineTick}, {"joinTick", joinTick}, {"wxTick", wxTick}, {"ndTick", ndTick}, {"lifeGuard", lifeGuard}, {"rbTick", rbTick}, {"slTick", slTick}, {"questIn", questIn}, {"csTick", csTick}, {"n7Tick", S2L.n7Tick}, {"catchupIn", catchupIn}, {"itIn", itIn}, {"ssTick", ssTick}, {"trTick", trTick}, {"artTick", artTick}, {"itRun", itRun}, {"emTick", emTick}, {"cpTick", cpTick}}) do   -- fixed order: the session check runs first
        local name, f = nf[1], nf[2]
        local ok, e = pcall(f, ST)
        if not ok then S2.errs.n = S2.errs.n + 1; S2.errs.last = name .. ": " .. tostring(e):sub(1, 160); ST.b6err = (ST.b6err or 0) + 1; if ST.b6err <= 5 then log("S2CORE " .. name .. " erro: " .. tostring(e)) end end
    end
    local okK, eK = pcall(lockTick, ST)
    local okO, eO = pcall(lootTick, ST)
    if not okO then S2.errs.n = S2.errs.n + 1; S2.errs.last = "lootTick: " .. tostring(eO):sub(1, 160); ST.lootErr = (ST.lootErr or 0) + 1; if ST.lootErr <= 5 then log("S2CORE lootTick erro " .. tostring(eO)) end end
    if not okK then S2.errs.n = S2.errs.n + 1; S2.errs.last = "lockTick: " .. tostring(eK):sub(1, 160) end
    local okG, eG = pcall(groundTick, ST)
    if not okG then S2.errs.n = S2.errs.n + 1; S2.errs.last = "groundTick: " .. tostring(eG):sub(1, 160); ST.gErr = (ST.gErr or 0) + 1; if ST.gErr <= 5 then log("S2CORE groundTick erro " .. tostring(eG)) end end
    for name, f in pairs({rewardTick = rewardTick, ptakeTick = ptakeTick, cineWatch = cineWatch, dupGuard = dupGuard}) do       -- core-22/23 (independent of each other)
        local ok, e = pcall(f, ST)
        if not ok then S2.errs.n = S2.errs.n + 1; S2.errs.last = name .. ": " .. tostring(e):sub(1, 160); ST.c22err = (ST.c22err or 0) + 1; if ST.c22err <= 5 then log("S2CORE " .. name .. " erro " .. tostring(e)) end end
    end
    local okD, eD = pcall(dmgIn, ST)
    pcall(doorTick, ST)   -- batch 53
    pcall(trigTick, ST)   -- batch 55 (ported b62)
    local okBj, eBj = pcall(dobjTick, ST)   -- batch 58
    if not okBj then S2.errs.n = S2.errs.n + 1; S2.errs.last = "dobjTick: " .. tostring(eBj):sub(1, 160); ST.bjErr = (ST.bjErr or 0) + 1; if ST.bjErr <= 5 then log("S2CORE dobjTick erro: " .. tostring(eBj)) end end
    local okIe, eIe = pcall(ienTick, ST)    -- batch 59
    if not okIe then S2.errs.n = S2.errs.n + 1; S2.errs.last = "ienTick: " .. tostring(eIe):sub(1, 160); ST.ieErr = (ST.ieErr or 0) + 1; if ST.ieErr <= 5 then log("S2CORE ienTick erro: " .. tostring(eIe)) end end
    pcall(kitTick, ST)    -- batch 53
    pcall(S2L.vendTradeTick, ST)    -- b78fix D3b
    pcall(S2L.ldTick, ST)    -- b92lead (lead)
    pcall(S2L.qpTick, ST)    -- b99qpick (qpick)
    local okDm, eDm = pcall(dmirTick, ST)    -- b70dlg
    if not okDm then S2.errs.n = S2.errs.n + 1; S2.errs.last = "dmirTick: " .. tostring(eDm):sub(1, 160); ST.dmErr = (ST.dmErr or 0) + 1; if ST.dmErr <= 5 then log("S2CORE dmirTick erro: " .. tostring(eDm)) end end
    pcall(S2L.cfollowTick, ST) -- client leash (cfollow)
    do local okMs, eMs = pcall(S2L.msTick, ST)   -- b131combat F1 (mslot)
        if not okMs then ST.msErr = (ST.msErr or 0) + 1; if ST.msErr <= 5 then log("S2CORE msTick erro: " .. tostring(eMs)) end end end
    local okFt, eFt = pcall(S2L.ftTick, ST)   -- b100ft (ftfollow)
    if not okFt then S2.errs.n = S2.errs.n + 1; S2.errs.last = "ftTick: " .. tostring(eFt):sub(1, 160); ST.ftErr = (ST.ftErr or 0) + 1; if ST.ftErr <= 5 then log("S2CORE ftTick erro: " .. tostring(eFt)) end end
    local okLr, eLr = pcall(S2L.lrApply, ST)   -- b115leash (leashown): the one writer of FL.leashm
    if not okLr then S2.errs.n = S2.errs.n + 1; S2.errs.last = "lrApply: " .. tostring(eLr):sub(1, 160); ST.lrErr = (ST.lrErr or 0) + 1; if ST.lrErr <= 5 then log("S2CORE lrApply erro: " .. tostring(eLr)) end end
    local okTp, eTp = pcall(S2L.tpfTick, ST)   -- b108tpfloor (tpfloor)
    if not okTp then S2.errs.n = S2.errs.n + 1; S2.errs.last = "tpfTick: " .. tostring(eTp):sub(1, 160); ST.tpfErr = (ST.tpfErr or 0) + 1; if ST.tpfErr <= 5 then log("S2CORE tpfTick erro: " .. tostring(eTp)) end end
    pcall(S2L.iactTick, ST)    -- b70f probe
    pcall(B73.tick, ST)    -- batch 73 (idrop / anomsync; each flag 0 = nothing runs)
    pcall(iaTick, ST)      -- b71ia interaction mirror (iamir)
    pcall(S2L.qiTick, ST)  -- b87qitem (qitem)
    pcall(S2L.qdrTick, ST) -- b132qhold (qdrop: hold D drops a quest item); errors logged inside
    pcall(S2L.sleepTick, ST)    -- b102sleep (sleepsync); errors counted inside
    pcall(S2L.hostcfgTick, ST)  -- b105ver (hostcfg); errors counted inside
    pcall(M.LS.tick, ST)   -- b90loot (lootshare)
    pcall(S2L.clTick, ST)  -- b138loot (cloot); errors logged inside
    pcall(S2L.wsTick, ST)  -- b144combat (wshot / wsfx / wvoice); errors logged inside
    if not okD then S2.errs.n = S2.errs.n + 1; S2.errs.last = "dmgIn: " .. tostring(eD):sub(1, 160); if (ST.din.errLog or 0) < 5 then ST.din.errLog = (ST.din.errLog or 0) + 1; log("S2CORE dmgin erro: " .. tostring(eD)) end end
    if M.ownsAvatar(ST) and FL.coretarget == 1 and gateOk then tgtTick(ST) end
    if M.ownsAvatar(ST) then if gateOk then S2L.pupTick(ST) end
    elseif gateOk and ST.pup and ST.pup.state and S2L.bodyHeld(ST.pup) then   -- mode switched off: give the NPC back to the game's AnimBP
        ST.pup.state = nil                             -- b112 (#2): no SetAnimationMode on our bodies (D10); only the mark is cleared
        log("S2CORE puppet: modo desligado (b112: sem SetAnimationMode)")
    end
    local okNs, eNs = pcall(S2L.nsTick, ST)   -- b114smooth (nsmooth/nsuro/pupcmoff): movers of the puppet and the client NPC mirrors, after pupTick
    if not okNs then S2L.nsErr(ST, "nsTick", eNs) end
    if M.NP then M.NP.tickP(ST) end            -- b122npcsync (npcsync): the client's kept NPCs driven from the host's #NP, after nsTick
    if rawget(_G, "s2n_nat_cfg") and ST.b168f ~= (FL.b168off or 0) then ST.b168f = (FL.b168off or 0); pcall(s2n_nat_cfg, "natcb", ST.b168f == 1 and 0 or 1) end   -- b168
    if api.ROLE ~= "host" and rawget(_G, "s2n_nat_client") then pcall(S2L.natBrIo, ST); if not S2L.natBrRecent(ST) then S2L.natCl(ST, 0) end end   -- b158natnpc; b173 natbr
end

-- ===== b102sleep (flag sleepsync, default 0; user rule N5 "Sleep: both must click sleep; both see the same sleep and the same time skip").
-- Agreed simple design: bed prompt to the other + both confirm + same skip. No reflected call puts our own text on the screen (none measured,
-- b91 found the same), so the "prompt" is AUTO-FOLLOW (sleepmode 1): the other side's core answers yes at once and puts that player to sleep
-- (XStartSleep; if the game's sleep has not started after sleepnat s, the sleep effect is applied by script). sleepmode 2 = the other player
-- must lie down in a bed itself within sleepwait s (the "click"); a busy player (dialog, cinematic, fight, dead) refuses in both modes.
-- Game (dump + GameData): a bed is a BedView (BP_Bed*_C, BedHoldComponent hold); DefaultSleepParams: AllowSleepThreshold 50, MinSleepHours 7,
-- SleepHoursMultiplier 0.08, no sleep in an emission; after the sleep SleepSleepinessRestore -100%, SleepHealthRestore 300, stamina 100%, SleepHunger.
-- Signal: Obj:IsSleeping on the own pawn (read every tick100). The first one to sleep (N14 first starter) sends #SL; the other answers
-- Y (sleeps too) / D (woke) / B,F,X,T,O (no). Both sleep at once (bridge delay) = both agreed.
-- Time: the host clock stays master. Each side's own sleep moves its own clock and the client's synctime (main.lua) then follows the host.
-- A client that sleeps first parks synctime (S2_syncAt) until the host answered and woke; a host without a game sleep skips the client's hours
-- (b106sleepclk: S2_setGameTime to its time before the sleep + h, read back; NOT XSkipTimeHours(h) = 24*h game hours; only
-- XSkipTimeHours(1) = +24 h if the set moved gs back; flag sleepskip is no longer read). Refused / no answer in time / the starter gave up: the sleeper's clocks go back to
-- the pre-sleep time + the real time passed (S2_setGameTime, the synctime call) and its sleepiness to the pre-sleep value.
-- b107sleepcancel: so that a sleep is not refused at all, each side sends its busy state (#SB:<B|F|X|->, slBusy's codes) every 0.5 s while
-- awake, and while the OTHER is busy every loaded bed's BedMeshComponent collision is 0 ("cama bloqueada"); free again = saved value back.
-- x1: #SL:<seq>,<st>,<hours>,<ts>,<aseq>,<ats>,<ans>   my request (st S asleep, W woke, A done together, C cancelled) + my answer to the other's
-- request <aseq>@<ats>. Each part is kept sleepkeep s after its end; requests are deduped by seq@ts (reload safe), dropped after sleepmaxage s.
S2L.SL_WHY = {B = "ocupado (dialogo/cinematica)", F = "em combate", X = "morto/caido", T = "nao deitou numa cama a tempo", O = "sem pawn"}
function S2L.slHM(t)
    if type(t) ~= "number" then return "?" end
    t = t % 86400
    return string.format("%02d:%02d", math.floor(t / 3600), math.floor(t % 3600 / 60))
end
function S2L.slClock(S)                          -- gs = TimeManager.GameTimeSeconds; gt / wt = time of day (s) of the game / weather clocks (main.lua's formulas)
    local tm = S.tm
    if not valid(tm) then tm = FindFirstOf("TimeManager"); S.tm = tm end
    if not valid(tm) then return nil end
    local gs, wt
    pcall(function() gs = tm.GameTimeSeconds end)
    pcall(function() wt = tm.GameHours * 3600 + tm.GameMinutes * 60 + tm.GameSecondsClamped end)
    if type(gs) ~= "number" then return nil end
    return {gs = gs, gt = ((gs / 150 + 9) % 24) * 3600, wt = type(wt) == "number" and wt or nil}
end
function S2L.slStats(pw)
    local r = {}
    pcall(function() r.sl = pw:GetCurrentSleepinessPoints() end)
    pcall(function() r.hp = pw:GetHP() end)
    pcall(function() r.hu = pw:GetCurrentHungerPoints() end)
    for k, v in pairs(r) do if type(v) ~= "number" then r[k] = nil end end
    return r
end
function S2L.slSetTod(gt, wt, why)                -- clocks to a time of day (s) with main.lua's S2_setGameTime (synctime's call); nil = that clock untouched
    local f = rawget(_G, "S2_setGameTime")
    if type(f) ~= "function" then log("S2CORE dormir: S2_setGameTime nao existe (main.lua antigo?); relogio nao mexido (" .. why .. ")"); return false end
    local function hms(t) t = math.floor(t % 86400); return math.floor(t / 3600), math.floor(t % 3600 / 60), t % 60 end
    local ok = true
    if gt then local h, m, s = hms(gt); local o, r = pcall(f, h, m, s, why, false); ok = ok and o and r ~= false end
    if wt then local h, m, s = hms(wt); local o, r = pcall(f, h, m, s, why, true); ok = ok and o and r ~= false end
    return ok
end
function S2L.slSetChk(S, gt, wt, why, c0)        -- b106sleepclk: clocks to a time of day with S2_setGameTime (synctime's call), then READ BACK:
    c0 = c0 or S2L.slClock(S)                      -- each clock must land within 300 s (5 game min) of its target, else ok=false + AVISO
    local okS = S2L.slSetTod(gt, wt, why)
    local c1 = S2L.slClock(S)
    local function off(a, b)
        if type(a) ~= "number" or type(b) ~= "number" then return nil end
        local d = (a - b) % 86400
        return d > 43200 and d - 86400 or d
    end
    local dg, dw, bad = off(c1 and c1.gt, gt), off(c1 and c1.wt, wt), {}
    if not okS then bad[#bad + 1] = "S2_setGameTime falhou" end
    if gt and not (dg and math.abs(dg) <= 300) then bad[#bad + 1] = "jogo " .. (dg and string.format("%+.0f s", dg) or "sem leitura") .. " do alvo" end
    if wt and not (dw and math.abs(dw) <= 300) then bad[#bad + 1] = "clima " .. (dw and string.format("%+.0f s", dw) or "sem leitura") .. " do alvo" end
    local s = string.format("jogo %s -> %s (alvo %s), clima %s -> %s (alvo %s), gs %+.0f", S2L.slHM(c0 and c0.gt), S2L.slHM(c1 and c1.gt), S2L.slHM(gt),
        S2L.slHM(c0 and c0.wt), S2L.slHM(c1 and c1.wt), S2L.slHM(wt), (c0 and c1) and (c1.gs - c0.gs) or 0)
    if #bad > 0 then s = s .. "; AVISO: o relogio NAO chegou ao alvo (" .. table.concat(bad, ", ") .. ")" end
    return #bad == 0, s, c0, c1
end
function S2L.slSkip(S, h, why, pre)               -- host follower without a game sleep: both clocks = time of day before the sleep + h, checked
    -- b106sleepclk: NOT XSkipTimeHours. Measured: XSkipTimeHours(h) adds h*3600 to GameTimeSeconds while 1 game hour = 150, i.e. it skips 24*h
    -- game hours: (1) = +24 h, time of day unchanged (S1 12:12); (7.03) = 7 days + 44 min (live 12:25, synctime then pulled the client 6 h back).
    -- XSetGameTime moves GameTimeSeconds itself (the game clock IS GameTimeSeconds). Only if that set went BACK (a same-day rule when the sleep
    -- crosses midnight; unmeasured, probe S1 step 2) is XSkipTimeHours(1) used: measured +24 h of GameTimeSeconds with the time of day unchanged,
    -- so A-Life/quest time does not run backwards. The time of day is read back again after it.
    local c0 = S2L.slClock(S)
    if not c0 then return false, "sem TimeManager" end
    local b = (pre and pre.c and os.clock() - (pre.at or -1000) < 120) and pre.c or c0
    local add, bw = h * 3600, b.wt or c0.wt
    local tg, tw = b.gt + add, bw and (bw + add) or nil
    local ok, s, _, c1 = S2L.slSetChk(S, tg, tw, why, c0)
    if c1 and c1.gs < c0.gs - 150 then
        local cm = FindFirstOf("CustomConsoleManagerIK")
        local okK = valid(cm) and pcall(function() cm:XSkipTimeHours(1) end)
        local c2 = S2L.slClock(S)
        local fwd = c2 and (c2.gs - c0.gs) or 0
        local dd = c2 and math.abs((c2.gt - c1.gt + 43200) % 86400 - 43200) or 99999
        local good = fwd > 0 and dd <= 300
        ok = ok and good
        s = s .. string.format("; gs voltou %+.0f -> XSkipTimeHours(1) ok=%s: jogo %s, clima %s, gs total %+.0f%s", c1.gs - c0.gs, tostring(okK), S2L.slHM(c2 and c2.gt),
            S2L.slHM(c2 and c2.wt), fwd, good and " (+24 h, hora do dia igual)" or " AVISO: gs ainda atras ou a hora do dia mudou")
    end
    return ok, string.format("S2_setGameTime: antes do sono %s + %.2f h; %s", S2L.slHM(b.gt), h, s)
end
function S2L.slUndo(S, pw, pre, now, why)          -- the sleeper back to before the sleep: clocks = pre + real time passed (checked), sleepiness = pre
    if not (pre and pre.c) then log("S2CORE dormir: CANCELADO (" .. why .. ") mas sem a hora de antes do sono; nada desfeito"); return end
    local back = (now - pre.at) * (FL.sleeprate or 24)
    local gt, wt = pre.c.gt + back, pre.c.wt and (pre.c.wt + back) or nil
    local ok, how, c0, c1 = S2L.slSetChk(S, gt, wt, "dormir cancelado")
    local s0, s1, okS = nil, nil, "-"
    pcall(function() s0 = pw:GetCurrentSleepinessPoints() end)
    if pre.st and pre.st.sl then okS = tostring(pcall(function() pw:SetCurrentSleepinessPoints(pre.st.sl) end)) end
    pcall(function() s1 = pw:GetCurrentSleepinessPoints() end)
    local dgs = (c0 and c1) and (c1.gs - c0.gs) or 0
    log(string.format("S2CORE dormir: CANCELADO (%s) -> relogio de volta ok=%s (%s)%s; sonolencia %s -> %s (antes do sono %s) ok=%s; vida/fome do sono ficam",
        why, tostring(ok), how, dgs > 300 and " (AVISO: o dia andou para frente)" or "", tostring(s0), tostring(s1), tostring(pre.st and pre.st.sl), okS))
end
function S2L.slBed(ST, S, pw, now)                 -- b107sleepcancel: my busy state out (#SB, every 0.5 s while awake) + the beds OFF while the OTHER is busy
    S.busyTx = S2L.slBusy(ST, S, pw, now) or "-"
    local ob = x1part("SB")                        -- nil = old core / bridge silent / not busy
    ob = ob and ob:match("^([BFX])") or nil
    S.obusy = ob
    if (S.mine and not S.mine.endAt) or (S.fol and S.fol.phase ~= "done" and S.fol.phase ~= "no") then return end   -- a vote is open: beds as they are
    S2L.slBeds(S, ob, now)
end
function S2L.slBeds(S, ob, now)                    -- while ob: every loaded bed's BedMeshComponent:SetCollisionEnabled(0) (measured: no prompt, F hold
    if not ob and not S.bedOff then return end     -- = no sleep; SetInteractiveState / SleepComponent:Deactivate do NOT block), re-applied every 3 s
    if ob and S.bedOff == ob and now < (S.bedAt or 0) then return end   -- (streamed beds); free again = each bed's SAVED value back. No refs kept:
    local pw = S2L.ioPawn()                        -- the saved values are keyed by the bed's full name. Never while I lie in a bed / sleep.
    local slp = false
    if valid(pw) then pcall(function() slp = pw:IsSleeping() == true end) end
    if slp then return end
    S.bedSv = S.bedSv or {}
    local n, okN = 0, 0
    for _, b in ipairs(FindAllOf("BedView") or {}) do
        if valid(b) then
            local key = ""
            pcall(function() key = b:GetFullName() end)
            if key == "" then pcall(function() key = b:GetFName():ToString() end) end
            local m
            pcall(function() m = b.BedMeshComponent end)
            if key ~= "" and not key:find("Default__", 1, true) and valid(m) then
                local cur
                pcall(function() cur = m:GetCollisionEnabled() end)
                if ob then
                    if S.bedSv[key] == nil and type(cur) == "number" and cur ~= 0 then S.bedSv[key] = cur end
                    if S.bedSv[key] ~= nil then
                        n = n + 1
                        if cur == 0 or pcall(function() m:SetCollisionEnabled(0) end) then okN = okN + 1 end
                    end
                elseif S.bedSv[key] ~= nil then
                    n = n + 1
                    local v = S.bedSv[key]
                    if pcall(function() m:SetCollisionEnabled(v) end) then okN = okN + 1 end
                end
            end
        end
    end
    if ob and S.bedOff ~= ob then log(string.format("S2CORE dormir: cama bloqueada: o outro esta %s; %d/%d camas sem colisao", S2L.SL_WHY[ob] or ob, okN, n))
    elseif not ob then log(string.format("S2CORE dormir: cama liberada (o outro esta livre); %d/%d camas com a colisao de antes", okN, n)); S.bedSv = {} end
    S.bedOff, S.bedAt = ob, now + 3
end
function S2L.slPark(S, untilT)                     -- client: main.lua's synctime waits until untilT; nil = give it back (only if still ours)
    if api.ROLE ~= "client" then return end
    local A = rawget(_G, "S2_syncAt")
    if type(A) ~= "table" then A = {}; rawset(_G, "S2_syncAt", A) end
    if untilT then
        A.gt = math.max(A.gt or 0, untilT); A.wt = math.max(A.wt or 0, untilT)
        if not S.park then log(string.format("S2CORE dormir: synctime parado (%.0f s no maximo)", untilT - os.clock())) end
        S.park = A.gt
    elseif S.park then
        if A.gt == S.park then A.gt = 0 end
        if A.wt == S.park then A.wt = 0 end
        S.park = nil
        log("S2CORE dormir: synctime de volta (o relogio segue o host)")
    end
end
function S2L.slBusy(ST, S, pw, now)                -- why this player cannot sleep now (nil = free)
    local alive, hp = true, nil
    pcall(function() alive = pw:IsAlive() and true or false end)
    pcall(function() hp = pw:GetHP() end)
    if not alive or (type(hp) == "number" and hp <= 0) or (ST.nd and ST.nd.dead ~= nil) or (ST.life and ST.life.down ~= nil) then return "X" end
    local lk = S2L.ftLock and S2L.ftLock(ST, S, now)   -- b100ft's read: own cinematic / dialog view ("congelado" = the host's sleep or scene: ignored)
    if lk == "dialogo" or lk == "cinematica" then return "B" end
    if now - (S.hurtAt or -1000) < (FL.sleepfight or 10) then return "F" end
    return nil
end
function S2L.slAnswer(S, Fo, a, now)
    S.ans = {seq = Fo.seq, ts = Fo.ts, a = a, untilT = now + (FL.sleepkeep or 30)}
end
function S2L.slAsk(ST, S, pw, now, key, seq, ts, h)  -- the other player went to sleep: follow / refuse
    local Fo = S.fol
    if Fo and Fo.phase ~= "done" and Fo.phase ~= "no" then log("S2CORE dormir: pedido novo (#SL " .. seq .. ") com o anterior (#SL " .. tostring(Fo.seq) .. ") aberto; o anterior fica para tras") end
    Fo = {key = key, seq = seq, ts = ts, t0 = now, pre = S.pre, h = h}
    S.fol = Fo
    S2L.slPark(S, now + (FL.sleepans or 15) + (FL.sleepwait or 60) + 180)   -- client: my clock follows the host only when this request is over
    local Mi = S.mine
    if Mi and not Mi.endAt then                    -- both went to bed inside the bridge delay: both agreed
        Fo.phase, Fo.pre = "tie", Mi.pre
        S2L.slAnswer(S, Fo, "Y", now)
        log(string.format("S2CORE dormir: os dois foram dormir ao mesmo tempo (#SL meu %d / do outro %d): os dois concordaram", Mi.seq, seq))
        return
    end
    local why = S2L.slBusy(ST, S, pw, now)
    if why then
        Fo.phase = "no"
        S2L.slAnswer(S, Fo, why, now)
        log(string.format("S2CORE dormir: o outro jogador foi dormir (#SL %d) mas NAO posso: %s -> recusado", seq, S2L.SL_WHY[why] or why))
        return
    end
    pcall(M.slPull, ST, seq)                       -- b110 sleeppull: first next to the sleeper (both modes)
    if (FL.sleepmode or 1) == 2 then
        Fo.phase = "wait"
        S2L.slAnswer(S, Fo, "-", now)
        log(string.format("S2CORE dormir: o outro jogador foi dormir (#SL %d) -> deite numa cama em %d s para dormir junto (modo 2)", seq, FL.sleepwait or 60))
        return
    end
    Fo.phase = "start"
    S2L.slAnswer(S, Fo, "Y", now)
    local cm = FindFirstOf("CustomConsoleManagerMP")
    local ok = valid(cm) and pcall(function() cm:XStartSleep() end)
    log(string.format("S2CORE dormir: o outro jogador foi dormir (#SL %d) -> durmo junto (modo 1): XStartSleep ok=%s", seq, tostring(ok)))
end
function M.slPull(ST, seq)                         -- b110 sleeppull: the follower of a sleep goes next to the sleeper (b108 tpfNow / tpfSpot)
    if (FL.sleeppull or 0) ~= 1 then return false end
    local r, pw = S2L.remoteIn("dormir"), S2L.ioPawn()
    local q; if valid(pw) then pcall(function() q = pw:K2_GetActorLocation() end) end
    if not (r and r.x and r.y and r.z and r.recv and q) or os.clock() - r.recv > 5 then
        log("S2CORE dormir: puxar para perto de quem dorme (#SL " .. tostring(seq) .. "): sem posicao recente do outro ou sem pawn -> fico onde estou")
        return false
    end
    local d = math.sqrt((q.X - r.x) ^ 2 + (q.Y - r.y) ^ 2 + (q.Z - r.z) ^ 2)
    if d <= (FL.sleeppullr or 300) then
        log(string.format("S2CORE dormir: ja perto de quem dorme (#SL %s, %.1f m) -> sem puxar", tostring(seq), d / 100))
        return false
    end
    local ok = S2L.tpfNow(ST, {x = r.x, y = r.y, z = r.z, yaw = r.yaw}, "dormir") == true
    local a; pcall(function() a = pw:K2_GetActorLocation() end)
    log(string.format("S2CORE dormir: puxado para perto de quem dorme (#SL %s, estava a %.0f m) ok=%s dz=%s dxy=%s", tostring(seq), d / 100, tostring(ok),
        a and string.format("%.0f", a.Z - r.z) or "?", a and string.format("%.0f", math.sqrt((a.X - r.x) ^ 2 + (a.Y - r.y) ^ 2)) or "?"))
    return ok
end
function S2L.slIn(ST, S, pw, now)                  -- the other side's #SL
    local sg = x1part("SL"); if not sg then return end
    local rseq, rst, rh, rts, aseq, ats, ans = sg:match("^(%d+),([%a%-]),([%d%.%-]+),(%d+),(%d+),(%d+),([%a%-])")
    if not rseq then return end
    local Mi = S.mine                              -- 1) the answer to MY request
    if Mi and not Mi.endAt and tonumber(aseq) == Mi.seq and tonumber(ats) == Mi.ts and ans ~= "-" and ans ~= Mi.ra then
        Mi.ra = ans
        if ans == "Y" or ans == "D" then
            if not Mi.ok then Mi.ok = now; log(string.format("S2CORE dormir: o outro ACEITOU (#SL %d) %.1f s depois do pedido", Mi.seq, now - Mi.t0)) end
            if ans == "D" then Mi.done = now; log(string.format("S2CORE dormir: o outro acordou (#SL %d)", Mi.seq)) end
        else
            Mi.no = S2L.SL_WHY[ans] or ans
            log(string.format("S2CORE dormir: o outro RECUSOU (#SL %d): %s", Mi.seq, Mi.no))
        end
    end
    if rseq == "0" or rst == "-" then return end   -- 2) the other's own request
    local key = rseq .. "@" .. rts
    local Fo = S.fol
    if Fo and Fo.key == key then
        local h = tonumber(rh)
        if rst == "W" and h and not Fo.h then Fo.h = h end
        if rst == "C" and not Fo.cancel then Fo.cancel = now; log(string.format("S2CORE dormir: o outro desistiu do sono (#SL %s)", rseq)) end
        if rst == "A" and not Fo.closed then Fo.closed = now; log(string.format("S2CORE dormir: o outro confirmou: DORMIMOS JUNTOS (#SL %s, ele dormiu %.2f h)", rseq, tonumber(rh) or 0)) end
        return
    end
    if key == S.rkey then return end
    S.rkey = key
    if rst ~= "S" and rst ~= "W" then return end   -- a finished request seen for the first time (reload / late start): nothing to do
    local age = os.time() - tonumber(rts)
    if age > (FL.sleepmaxage or 120) then log("S2CORE dormir: #SL " .. rseq .. " antigo (" .. age .. " s), ignorado"); return end
    S2L.slAsk(ST, S, pw, now, key, tonumber(rseq), tonumber(rts), rst == "W" and tonumber(rh) or nil)   -- hours only once the starter woke
end
function S2L.slStep(ST)
    local S = ST.sl
    if (FL.sleepsync or 0) ~= 1 then              -- off: give back what we hold
        if S and (S.park or S.mine or S.fol) then S2L.slPark(S, nil); S.mine, S.fol = nil, nil; log("S2CORE dormir: sleepsync desligado; estado limpo") end
        if S and S.bedOff then S2L.slBeds(S, nil, os.clock()) end   -- b107sleepcancel: beds back on
        return
    end
    if not S then S = {seq = 0}; ST.sl = S end
    local now, keep = os.clock(), FL.sleepkeep or 30
    local pw = S2L.ioPawn()
    if not valid(pw) then S.wasSlp = nil; return end
    local addr = pw:GetAddress()
    if addr ~= S.addr then S.addr = addr; S.wasSlp = nil end   -- new pawn (load): no edge from the old one
    local slp = false
    pcall(function() slp = pw:IsSleeping() == true end)
    local prev = S.wasSlp
    S.wasSlp = slp
    if prev == nil then return end                 -- first sample of this pawn: no edge
    local rise, fall = slp and not prev, prev and not slp
    if not rise and not fall and now < (S.nextAt or -1000) then return end
    S.nextAt = now + 0.5
    if not slp and not fall then                   -- awake sample = the pre-sleep state
        local c, st = S2L.slClock(S), S2L.slStats(pw)
        if st.hp and S.lastHp and st.hp < S.lastHp - 0.5 then S.hurtAt = now end
        S.lastHp = st.hp
        if c then
            if S.pre and S.pre.c and c.gs - S.pre.c.gs > (now - S.pre.at) + 300 then S.jumpAt = now end   -- the clock jumped while awake
            S.prePrev = S.pre
            S.pre = {c = c, st = st, at = now}
        end
        S2L.slBed(ST, S, pw, now)                  -- b107sleepcancel: #SB out, beds off while the other is busy
    end
    if rise then
        local Fo = S.fol
        if Fo and (Fo.phase == "start" or Fo.phase == "wait" or Fo.phase == "virt") then
            if Fo.virt then log(string.format("S2CORE dormir: o sono do jogo comecou DEPOIS do efeito por script (#SL %d); o relogio pode pular duas vezes", Fo.seq)) end
            Fo.phase, Fo.nat = "sleep", now
            S2L.slAnswer(S, Fo, "Y", now)
            log(string.format("S2CORE dormir: durmo junto (#SL %d): o sono do jogo comecou %.1f s depois do pedido", Fo.seq, now - Fo.t0))
        elseif Fo and (Fo.phase == "tie" or Fo.phase == "sleep") then
            -- already counted
        elseif now < (S.coolUntil or -1000) then
            log("S2CORE dormir: dormi de novo logo depois de um sono junto; nao conta como pedido")
        else
            local pre = S.pre
            if S.prePrev and now - (S.jumpAt or -1000) < 2 then pre = S.prePrev end   -- the sleep moved the clock before IsSleeping turned true
            S.seq = S.seq + 1
            local lim = (FL.sleepans or 15) + (((FL.sleepmode or 1) == 2) and (FL.sleepwait or 60) or 0)
            S.mine = {seq = S.seq, ts = os.time(), st = "S", h = 0, t0 = now, pre = pre, lim = lim, untilT = now + keep}
            S2L.slPark(S, now + lim + (FL.sleepwait or 60) + 120)
            log(string.format("S2CORE dormir: DORMI (pedido #SL %d enviado; hora %s, sonolencia %s); o outro tem %d s", S.seq, S2L.slHM(pre and pre.c and pre.c.gt),
                tostring(pre and pre.st and pre.st.sl), lim))
        end
    end
    if fall then
        local Mi = S.mine
        if Mi and not Mi.endAt and not Mi.woke then
            Mi.woke = now
            local c = S2L.slClock(S)
            if c and Mi.pre and Mi.pre.c then Mi.h = math.max(0, (c.gs - Mi.pre.c.gs) / 150) end
            Mi.st = "W"
            log(string.format("S2CORE dormir: acordei (#SL %d): dormi %.2f h de jogo em %.0f s (jogo %s -> %s, clima %s -> %s)", Mi.seq, Mi.h or 0, now - Mi.t0,
                S2L.slHM(Mi.pre and Mi.pre.c and Mi.pre.c.gt), S2L.slHM(c and c.gt), S2L.slHM(Mi.pre and Mi.pre.c and Mi.pre.c.wt), S2L.slHM(c and c.wt)))
        end
        local Fo = S.fol
        if Fo and Fo.phase == "sleep" then
            Fo.phase, Fo.doneAt = "done", now
            S.coolUntil = now + (FL.sleepcool or 30)
            S2L.slAnswer(S, Fo, "D", now)
            local c = S2L.slClock(S)
            log(string.format("S2CORE dormir: acordei junto (#SL %d) depois de %.0f s (jogo %s) -> D enviado", Fo.seq, now - Fo.nat, S2L.slHM(c and c.gt)))
        end
    end
    S2L.slIn(ST, S, pw, now)
    local Fo = S.fol                               -- follower
    if Fo then
        if Fo.phase ~= "done" and S.ans and S.ans.seq == Fo.seq then S.ans.untilT = now + keep end
        local cap = (FL.sleepans or 15) + (FL.sleepwait or 60) + 180
        if Fo.phase == "tie" and not slp then
            Fo.phase, Fo.doneAt, Fo.nat = "done", now, Fo.nat or Fo.t0
            S.coolUntil = now + (FL.sleepcool or 30)
            S2L.slAnswer(S, Fo, "D", now)
        elseif Fo.phase == "wait" and now - Fo.t0 > (FL.sleepwait or 60) then
            Fo.phase = "no"
            S2L.slAnswer(S, Fo, "T", now)
            log(string.format("S2CORE dormir: nao deitei numa cama em %d s (#SL %d) -> recusado", FL.sleepwait or 60, Fo.seq))
        elseif Fo.phase == "start" and now - Fo.t0 > (FL.sleepnat or 4) then
            local s0, s1
            pcall(function() s0 = pw:GetCurrentSleepinessPoints() end)
            local ok = pcall(function() pw:SetCurrentSleepinessPoints(FL.sleepvirt or 0) end)
            pcall(function() s1 = pw:GetCurrentSleepinessPoints() end)
            Fo.virt, Fo.phase = now, "virt"
            log(string.format("S2CORE dormir: o sono do jogo nao comecou em %s s -> efeito por script (#SL %d): sonolencia %s -> %s ok=%s", tostring(FL.sleepnat or 4), Fo.seq,
                tostring(s0), tostring(s1), tostring(ok)))
        end
        if Fo.phase == "virt" then
            if api.ROLE ~= "host" then             -- client: the clock follows the host (synctime)
                Fo.phase, Fo.doneAt = "done", now
                S.coolUntil = now + (FL.sleepcool or 30)
                S2L.slAnswer(S, Fo, "D", now)
                log(string.format("S2CORE dormir: dormi junto por script (#SL %d); o relogio segue o do host -> D enviado", Fo.seq))
            elseif Fo.h then                       -- host: the client's hours
                local ok, how = true, "o relogio do cliente nao andou: nada a pular"
                if Fo.h > 0.01 then ok, how = S2L.slSkip(S, Fo.h, "dormir junto: host pula as horas do cliente", Fo.pre) end   -- b106sleepclk: + pre
                Fo.skip = ok
                Fo.phase, Fo.doneAt = "done", now
                S.coolUntil = now + (FL.sleepcool or 30)
                S2L.slAnswer(S, Fo, "D", now)
                log(string.format("S2CORE dormir: host dormiu junto por script (#SL %d): pulei %.2f h como o cliente ok=%s (%s) -> D enviado", Fo.seq, Fo.h, tostring(ok), how))
            elseif now - Fo.t0 > cap then
                Fo.phase = "no"
                S2L.slAnswer(S, Fo, "O", now)
                log(string.format("S2CORE dormir: as horas do cliente nao chegaram (#SL %d) -> desisto", Fo.seq))
            end
        end
        if Fo.cancel and not Fo.undone and not slp then   -- the starter gave up: I go back too (if I slept)
            Fo.undone = now
            if Fo.nat or Fo.virt then S2L.slUndo(S, pw, Fo.pre, now, "o outro desistiu (#SL " .. Fo.seq .. ")") end
            if Fo.phase ~= "done" then Fo.phase = "no" end
        end
        if Fo.closed or Fo.undone or now - Fo.t0 > cap then
            if not (Fo.closed or Fo.undone) then log(string.format("S2CORE dormir: o pedido #SL %d nao fechou em %.0f s; esqueco", Fo.seq, cap)) end
            if Fo.phase == "done" or Fo.phase == "no" or now - Fo.t0 > cap then S.fol = nil end
        end
    end
    local Mi = S.mine                              -- starter
    if Mi and not Mi.endAt then
        Mi.untilT = now + keep
        if not Mi.ok and not Mi.no and now - Mi.t0 > Mi.lim then
            Mi.no = string.format("sem resposta em %d s", Mi.lim)
            log(string.format("S2CORE dormir: o outro NAO respondeu (#SL %d) em %d s", Mi.seq, Mi.lim))
        end
        if Mi.no then
            if not slp then
                S2L.slUndo(S, pw, Mi.pre, now, "#SL " .. Mi.seq .. ": " .. Mi.no)
                Mi.st, Mi.endAt = "C", now
                S.coolUntil = now + (FL.sleepcool or 30)
            end
        elseif Mi.ok and Mi.done and Mi.woke then
            Mi.st, Mi.endAt = "A", now
            S.coolUntil = now + (FL.sleepcool or 30)
            local c = S2L.slClock(S)
            log(string.format("S2CORE dormir: DORMIMOS JUNTOS (#SL %d): eu dormi %.2f h, o outro acordou %.0f s depois de mim; hora aqui %s (o host manda no relogio)",
                Mi.seq, Mi.h or 0, Mi.done - Mi.woke, S2L.slHM(c and c.gt)))
        elseif Mi.ok and now - Mi.t0 > Mi.lim + (FL.sleepwait or 60) + 120 and not slp then
            Mi.st, Mi.endAt = "A", now
            log(string.format("S2CORE dormir: o outro aceitou mas nao avisou que acordou (#SL %d); fecho assim", Mi.seq))
        end
    end
    if S.park and not ((S.mine and not S.mine.endAt) or (S.fol and S.fol.phase ~= "done")) then S2L.slPark(S, nil) end   -- client: nothing open -> synctime back
end
function S2L.sleepTick(ST)
    local ok, e = pcall(S2L.slStep, ST)
    if not ok then
        S2.errs.n = S2.errs.n + 1; S2.errs.last = "sleepTick: " .. tostring(e):sub(1, 160)
        ST.slErr = (ST.slErr or 0) + 1
        if ST.slErr <= 5 then log("S2CORE sleepTick erro: " .. tostring(e)) end
    end
end
function S2L.slOut(ST)                             -- x1 part: my request and my answer, each kept sleepkeep s after its end
    local S = ST.sl
    if (FL.sleepsync or 0) ~= 1 or not S then return "" end
    local now = os.clock()
    local m = (S.mine and now < (S.mine.untilT or 0)) and S.mine or nil
    local a = (S.ans and now < (S.ans.untilT or 0)) and S.ans or nil
    local sb = S.busyTx and ("#SB:" .. S.busyTx) or ""   -- b107sleepcancel: my busy state, always
    if not m and not a then return sb end
    return string.format("#SL:%d,%s,%.2f,%d,%d,%d,%s", m and m.seq or 0, m and m.st or "-", m and m.h or 0, m and m.ts or 0, a and a.seq or 0, a and a.ts or 0, a and a.a or "-") .. sb
end
S2L.slSnap0 = M.snapExtra                          -- #SL rides on x1 through a wrap (the long snapExtra line stays as it is)
function M.snapExtra(ST, pawn, p)
    local base = S2L.slSnap0 and S2L.slSnap0(ST, pawn, p) or ""
    local ok, s = pcall(S2L.slOut, ST)
    return (base or "") .. ((ok and type(s) == "string") and s or "")
end
-- ===== end b102sleep
-- ===== b105ver (flag hostcfg, default 0; user rule N11 "Mod version and game settings (difficulty): the host's apply / are checked").
-- Simplest design: one x1 part "#HC" on the existing snapshot (wrap of M.snapExtra, like #SL). Set hostcfg=1 on BOTH.
-- Version: each side hashes ITS OWN s2core.lua file (S2.corePath, read once per core load, md5 over hcblk 64-byte blocks per tick100, so no
-- stall) and sends "#HC:<md5 8 hex>". A different md5 on the other side is logged once per session + md5 pair (AVISO); code is never swapped.
-- VERSION / main.lua MOD_VER stay checked by #H (batch 6); VERSION alone does not change between patched cores, the md5 does.
-- Settings: the host adds ";k=v;k=v..." = its value of every SHARED flag (S2L.HC_KEYS; rebuilt every s, so a flags.txt change on the host
-- reaches the client within ~6 s). The client takes the host's value of each listed flag it also has (same type as its own), logs each change
-- once, and keeps its own flags.txt value to give back (hostcfg=0 on the client, or the host stops listing that flag). A silent host changes
-- nothing (its values stay). main.lua's 5 s flags reload is wrapped: it neither undoes an adopted value nor logs it again; a flags.txt change
-- of an adopted flag only updates the value kept to give back.
-- SHARED = read by BOTH roles for one shared mechanism (a sender + a receiver, or the same rule on both PCs). NOT listed: role-only flags
-- (csave, n7sv*, n7eq, cfollow*, vendtrade, qmrmhost, qnode, qworld, qextra, alifeoff*, trigself, cinedlg, lockfirst, ssstrict, shotattr,
-- joinport, dlgend*, synctime, syncweather, wx*, em* tunables), local crash guards and puppet look (pup*, tgt*, kzsafe, abp*, orph*, meshsafe,
-- bodysafe, smooth*), tunables of one side, probes / logs / tests.
S2L.HC_KEYS = "qsync qrollback qrelay lead ldtrig ldkill ldhold ldclose ldblk ldblkr mqpull mqwin qpick choice n3kill n3trig n3use qitem qimirror n7 dmir dmirpick cjoin " ..
    "locks taken takenown ground loot ptake qreward itake tsync lootshare lscont sstash artsync idrop " ..
    "emsync cine doors dobj dobjgen dobjfx ien ientab ienprompt ienanim ienlamp ienhatch ientoggle anomsync iamir vendmir vendone npsmove " ..
    "lifeguard downhp downsecs revivehp reviveradius revivesecs nopause ftfollow sleepsync sleepmode difficulty"
function S2L.hcSet()
    if not S2L.hcSetT then
        local t = {}
        for k in S2L.HC_KEYS:gmatch("%S+") do t[k] = true end
        S2L.hcSetT = t
    end
    return S2L.hcSetT
end
function S2L.hcMd5Init()
    if S2L.MD5K then return end
    local K, R, r = {}, {}, {{7, 12, 17, 22}, {5, 9, 14, 20}, {4, 11, 16, 23}, {6, 10, 15, 21}}
    for i = 1, 64 do K[i] = math.floor(math.abs(math.sin(i)) * 4294967296) end
    for i = 0, 63 do R[i + 1] = r[i // 16 + 1][i % 4 + 1] end
    S2L.MD5K, S2L.MD5R = K, R
end
function S2L.hcMd5Blocks(Mx, nmax)                -- nmax 64-byte blocks of Mx.s from Mx.i (RFC 1321, Lua 5.4 integers)
    local K, R, s = S2L.MD5K, S2L.MD5R, Mx.s
    local a0, b0, c0, d0, i, n, len = Mx.a, Mx.b, Mx.c, Mx.d, Mx.i, 0, #s
    while n < nmax and i <= len do
        local X = {string.unpack("<I4I4I4I4I4I4I4I4I4I4I4I4I4I4I4I4", s, i)}
        local a, b, c, d = a0, b0, c0, d0
        for j = 0, 63 do
            local f, g
            if j < 16 then f = (b & c) | (~b & d); g = j
            elseif j < 32 then f = (d & b) | (~d & c); g = (5 * j + 1) % 16
            elseif j < 48 then f = b ~ c ~ d; g = (3 * j + 5) % 16
            else f = c ~ (b | ~d); g = (7 * j) % 16 end
            f = (f + a + K[j + 1] + X[g + 1]) & 0xFFFFFFFF
            a, d, c = d, c, b
            local sh = R[j + 1]
            b = (b + (((f << sh) | (f >> (32 - sh))) & 0xFFFFFFFF)) & 0xFFFFFFFF
        end
        a0, b0, c0, d0 = (a0 + a) & 0xFFFFFFFF, (b0 + b) & 0xFFFFFFFF, (c0 + c) & 0xFFFFFFFF, (d0 + d) & 0xFFFFFFFF
        i, n = i + 64, n + 1
    end
    Mx.a, Mx.b, Mx.c, Mx.d, Mx.i = a0, b0, c0, d0, i
end
function S2L.hcMd5Step(H)                         -- the md5 of the s2core.lua file this generation loaded (main.lua S2.corePath)
    if H.md5 and H.mdGen == S2.gen then return end
    local Mx = H.mdS
    if not Mx or Mx.gen ~= S2.gen then
        H.md5, H.mdS = nil, nil
        if H.mdFail == S2.gen then return end
        local f = S2.corePath and io.open(S2.corePath, "rb")
        if not f then H.mdFail = S2.gen; log("S2CORE hostcfg: nao abri " .. tostring(S2.corePath) .. ": md5 desconhecido (vai '-')"); return end
        local s = f:read("*a"); f:close()
        S2L.hcMd5Init()
        local n = #s
        Mx = {gen = S2.gen, s = s .. "\128" .. string.rep("\0", (55 - n) % 64) .. string.pack("<I8", n * 8), i = 1, n = n,
            a = 0x67452301, b = 0xefcdab89, c = 0x98badcfe, d = 0x10325476, t0 = os.clock(), ticks = 0}
        H.mdS = Mx
    end
    S2L.hcMd5Blocks(Mx, math.max(1, math.floor(tonumber(FL.hcblk) or 128)))
    Mx.ticks = Mx.ticks + 1
    if Mx.i > #Mx.s then
        local d = string.pack("<I4I4I4I4", Mx.a, Mx.b, Mx.c, Mx.d):gsub(".", function(ch) return string.format("%02x", ch:byte()) end)
        H.md5full, H.md5, H.mdGen, H.mdS = d, d:sub(1, 8), S2.gen, nil
        log(string.format("S2CORE hostcfg: md5 deste s2core.lua = %s (%d bytes, %d passos, %.1f s) ver=%s", d, Mx.n, Mx.ticks, os.clock() - Mx.t0, tostring(M.VERSION)))
    end
end
function S2L.hcWrap()                            -- main.lua's 5 s flags reload must not undo (nor log again) an adopted host value
    if rawget(_G, "S2_loadFlags_hc") ~= nil or type(rawget(_G, "S2_loadFlags")) ~= "function" then return end
    local prev, fl = rawget(_G, "S2_loadFlags"), FL
    rawset(_G, "S2_loadFlags_hc", prev)
    rawset(_G, "S2_loadFlags", function(quiet)
        local H = S2.state and S2.state.hc
        local ad = H and H.ad
        local on = ad ~= nil and next(ad) ~= nil
        if on then for k in pairs(ad) do if H.own[k] ~= nil then fl[k] = H.own[k] end end end   -- the reload compares with the file's own value
        local ok, r = pcall(prev, quiet)
        if on then for k, v in pairs(ad) do H.own[k] = fl[k]; fl[k] = v end end                 -- a new file value is the one to give back
        if not ok then error(r, 0) end
        return r
    end)
end
function S2L.hcGiveBack(H, why, only)            -- client: own flags.txt values back (all, or the one flag `only`)
    local ch = {}
    for k, v in pairs(H.ad) do
        if only == nil or only == k then
            local ov = H.own[k]
            if ov ~= nil then FL[k] = ov end
            ch[#ch + 1] = k .. " " .. tostring(v) .. " -> " .. tostring(ov)
            H.ad[k], H.own[k] = nil, nil
        end
    end
    table.sort(ch)
    if #ch > 0 then log("S2CORE hostcfg: de volta ao valor daqui (" .. why .. "): " .. table.concat(ch, ", ")) end
end
function S2L.hcAdopt(ST, H, rest)                -- client: the host's value of every shared flag both sides have
    local set, seen, n = S2L.hcSet(), {}, 0
    for k, v in rest:gmatch("([%w_]+)=([^;]*)") do
        local cur = FL[k]
        if set[k] and cur ~= nil and not seen[k] then
            local hv = v
            if type(cur) == "number" then hv = tonumber(v) end
            if hv ~= nil then
                seen[k] = true; n = n + 1
                if H.ad[k] ~= nil and hv == H.own[k] then                    -- host value = our own flags.txt value: nothing held
                    FL[k] = hv; H.ad[k], H.own[k] = nil, nil
                    log(string.format("S2CORE hostcfg: %s = %s (igual ao flags.txt daqui; antes %s)", k, tostring(hv), tostring(cur)))
                elseif cur ~= hv then
                    if H.ad[k] == nil then H.own[k] = cur end
                    FL[k] = hv; H.ad[k] = hv
                    log(string.format("S2CORE hostcfg: %s %s -> %s (valor do host; flags.txt daqui = %s)", k, tostring(cur), tostring(hv), tostring(H.own[k])))
                end
            end
        end
    end
    for k in pairs(H.ad) do if not seen[k] then S2L.hcGiveBack(H, "o host nao manda mais " .. k, k) end end
    local P = ST.peer or {}
    if H.sumKey ~= P.id then
        H.sumKey = P.id
        local na = 0; for _ in pairs(H.ad) do na = na + 1 end
        log(string.format("S2CORE hostcfg: %d flags compartilhadas recebidas do host (sessao %s); %d com o valor do host diferente de flags.txt daqui", n, tostring(P.id), na))
    end
end
function S2L.hcTick(ST)
    local H = ST.hc
    if not H then H = {own = {}, ad = {}}; ST.hc = H end
    if (FL.hostcfg or 0) ~= 1 then
        if next(H.ad) then S2L.hcGiveBack(H, "hostcfg=0") end
        H.sumKey, H.mdKey = nil, nil
        return
    end
    S2L.hcWrap()
    S2L.hcMd5Step(H)
    local P, now = ST.peer or {}, os.clock()
    local sg = x1part("HC")
    if not sg then
        if P.state == "up" and P.live ~= false and not P.loopback then
            H.noAt = H.noAt or now
            if now - H.noAt >= 10 and H.noLog ~= P.id then
                H.noLog = P.id
                log("S2CORE hostcfg: o outro lado nao manda #HC ha 10 s (hostcfg=0 la, ou core antigo): versao e flags NAO conferidas")
            end
        else H.noAt = nil end
        return
    end
    H.noAt = nil
    if P.loopback then return end
    local rmd, rest = sg:match("^([^;]*);?(.*)$")
    if H.md5 and rmd and rmd ~= "-" and rmd ~= "" then
        local key = tostring(P.id) .. "/" .. rmd .. "/" .. H.md5
        if H.mdKey ~= key then
            H.mdKey = key
            if rmd ~= H.md5 then
                log(string.format("S2CORE hostcfg AVISO: s2core.lua DIFERENTE do outro lado (aqui md5 %s ver=%s; la (%s) md5 %s ver=%s): o codigo NAO e trocado, instale o mesmo s2core.lua nos dois",
                    H.md5, tostring(M.VERSION), tostring(P.role), rmd, tostring(P.core)))
            else
                log("S2CORE hostcfg: s2core.lua igual nos dois lados (md5 " .. rmd .. ")")
            end
        end
    end
    if api.ROLE ~= "client" then return end
    S2L.hcAdopt(ST, H, rest or "")
end
function S2L.hcOut(ST)                            -- x1 part: "#HC:<md5>" (client) / "#HC:<md5>;k=v;..." (host)
    if (FL.hostcfg or 0) ~= 1 then return "" end
    local H = ST.hc or {}
    local md = H.md5 or "-"
    if api.ROLE ~= "host" then return "#HC:" .. md end
    local now = os.clock()
    if not H.outS or now - (H.outAt or 0) >= 1 then
        local t = {}
        for k in S2L.HC_KEYS:gmatch("%S+") do
            local v = FL[k]
            if v ~= nil then t[#t + 1] = k .. "=" .. (tostring(v):gsub("[^%w%.%-_]", "")) end
        end
        H.outS, H.outAt = table.concat(t, ";"), now
    end
    return "#HC:" .. md .. ";" .. H.outS
end
function S2L.hostcfgTick(ST)
    local ok, e = pcall(S2L.hcTick, ST)
    if not ok then
        S2.errs.n = S2.errs.n + 1; S2.errs.last = "hcTick: " .. tostring(e):sub(1, 160)
        ST.hcErr = (ST.hcErr or 0) + 1
        if ST.hcErr <= 5 then log("S2CORE hostcfg erro: " .. tostring(e)) end
    end
end
S2L.hcSnap0 = M.snapExtra                          -- #HC rides on x1 through a wrap (the long snapExtra line stays as it is)
function M.snapExtra(ST, pawn, p)
    local base = S2L.hcSnap0 and S2L.hcSnap0(ST, pawn, p) or ""
    local ok, s = pcall(S2L.hcOut, ST)
    return (base or "") .. ((ok and type(s) == "string") and s or "")
end
-- ===== end b105ver
-- ===== roadmap (a): local A-Life off (flag alifeoff=1, meant for the CLIENT) =====
-- Measured on the host 2026-10-11: none of the switches (AIFeature All / ALifeDirector / ALifePopulationManager off, XALifeDisable) blocks
-- XSpawnObjBySID, so our own bodies can still be spawned. XDespawnNPCsAtRadius removes every NPC in the radius INCLUDING our bodies (pawn objects
-- go to a pool at 0,0,0 with alive=false), so it is not used here. What this does: all AI features off + XALifeDisable (once), and every NPC or
-- mutant that is not one of our bodies is hidden and loses its collision (checked every 2 s, so anything the game spawns later is covered too).
local AI_ALL = 4194303
local HUBS = {{421180,412370},{478540,526470},{665202,617990},{445964,657845},{498260,379420},{321267,412054},{215900,270210},{107594,471669},
    {318986,526376},{398700,545500},{232150,159790},{259517,599445},{409220,258260},{384673,341937}}   -- ZoneKit FastTravelLocationPrototypes.cfg (X,Y in cm)
local HUB_R = 12000
function S2L.nearHub(x, y)
    for _, h in ipairs(HUBS) do local dx, dy = x - h[1], y - h[2]; if dx * dx + dy * dy < HUB_R * HUB_R then return true end end
    return false
end
local function tradePlaces(A)                         -- batch 29: named trade places from the offline table (no call on any NPC)
    if A.tp then return A.tp end
    A.tp = {}
    local f = io.open(api.BR .. "s2u_trade.tsv", "r")
    if not f then log("S2CORE alife: s2u_trade.tsv nao esta na ponte: so os hubs mantem NPCs locais"); return A.tp end
    for ln in f:lines() do
        local c = {}
        for v in (ln .. "\t"):gmatch("([^\t]*)\t") do c[#c + 1] = v end
        local x, y = tonumber(c[4]), tonumber(c[5])
        if x and y and c[7] and not c[7]:find("^BasicTrader") then A.tp[#A.tp + 1] = {x, y} end
    end
    f:close()
    log("S2CORE alife: " .. #A.tp .. " lugares de comerciantes de s2u_trade.tsv mantem NPCs locais (raio " .. tostring(FL.alifetrader) .. " cm)")
    return A.tp
end
function S2L.nearTrade(A, x, y)
    local r2 = FL.alifetrader * FL.alifetrader
    for _, p in ipairs(tradePlaces(A)) do local dx, dy = x - p[1], y - p[2]; if dx * dx + dy * dy < r2 then return true end end
    return false
end
function S2L.alifeTick(ST)
    local A = ST.alife or {}; ST.alife = A
    local want = FL.alifeoff == 1
    local am, ss = FindFirstOf("CustomConsoleManagerAM"), FindFirstOf("CustomConsoleManagerSS")
    local lp = S2.io and S2.io.pawn
    if valid(lp) then                                   -- batch 6: new local pawn (save loaded, respawn) = new world state: apply again
        local ad = lp:GetAddress()
        if A.pawnAddr and ad ~= A.pawnAddr and A.off then A.off = false; A.hid = {}; log("S2CORE alife: pawn local novo; reaplicando") end
        A.pawnAddr = ad
    end
    if want ~= (A.off or false) and (valid(lp) or not want) then   -- core-20b: only once the world (pawn) exists, and retried until both calls succeed
        local r = {}
        local ok1 = pcall(function() if want then am:XDeactivateAIFeature(FL.aimask) else am:XActivateAIFeature(AI_ALL) end end)
        local ok2 = pcall(function() if want then ss:XALifeDisable() else ss:XALifeEnable() end end)
        r[1] = tostring(ok1); r[2] = tostring(ok2)
        local retry = false
        if ok1 and ok2 then A.off = want; A.fails = 0
        else
            A.fails = (A.fails or 0) + 1
            if A.fails <= 3 or A.fails % 30 == 0 then log("S2CORE alife: chamadas falharam (ok=" .. r[1] .. "," .. r[2] .. ") tentativa " .. A.fails .. "; tento de novo") end
            if want then retry = true else A.off = want end   -- switching off keeps retrying (the NPC sweep below still runs meanwhile)
        end
        local back = 0
        if not want then                      -- switched back on: show the NPCs again
            for _, o in ipairs(FindAllOf("Pawn") or {}) do pcall(function()
                if o ~= lp and not M.isOwnBody(ST, o) then o:SetActorHiddenInGame(false); o:SetActorEnableCollision(true); back = back + 1 end end) end
            A.hid = {}
        end
        if not retry then log("S2CORE alife: " .. (want and "DESLIGADO" or "LIGADO") .. " (AIFeature " .. tostring(FL.aimask) .. ", XALife" .. (want and "Disable" or "Enable") .. ") ok=" .. table.concat(r, ",") .. (want and "" or (" npcs_mostrados=" .. back))) end
    end
    if not want or not valid(lp) then return end
    A.hid = A.hid or {}
    local new, total, names = 0, 0, {}
    for _, o in ipairs(FindAllOf("Pawn") or {}) do pcall(function()
        if o ~= lp and A.hid[o:GetAddress()] and not o:IsAlive() and not (rawget(_G, "s2n_nat_own") ~= nil and o ~= nil and s2n_nat_own(o:GetAddress()) == true) then   -- b158natnpc; b142: a hidden local NPC that died stays hidden (death showed its corpse on the client)
            local h; pcall(function() h = o.bHidden end)
            if not h then o:SetActorHiddenInGame(true); o:SetActorEnableCollision(false); A.dh = (A.dh or 0) + 1; if A.dh <= 10 then log("S2CORE alife: NPC local morto escondido de novo " .. o:GetFName():ToString()) end end
        end
        if o ~= lp and not M.isOwnBody(ST, o) and o:IsAlive() then
            total = total + 1
            local ad = o:GetAddress()
            local keep = false
            if FL.alifekeepquest == 1 and FL.alifeguid == 1 then      -- batch 29: off by default (crash risk)
                A.gk = A.gk or {}
                local g = A.gk[ad]
                if g == nil then
                    g = false
                    pcall(function()
                        local lib = StaticFindObject("/Script/Stalker2.Default__QuestBindingFunctionLibrary")
                        local gu = lib:GetPlaceholderGuidByUObject(o)
                        g = (gu.A ~= 0 or gu.B ~= 0 or gu.C ~= 0 or gu.D ~= 0)
                    end)
                    A.gk[ad] = g
                    if g then A.gkn = (A.gkn or 0) + 1; log("S2CORE alife: NPC com GUID de placeholder mantido: " .. o:GetFullName():match("[^%.:]+$")) end
                end
                if g then
                    keep = true
                    if A.hid[ad] then A.hid[ad] = nil; o:SetActorHiddenInGame(false); o:SetActorEnableCollision(true) end
                end
            end
            if not keep and (FL.alifekeephubs == 1 or FL.alifekeepquest == 1 or (FL.vendmir or 0) > 0) then
                local q = o:K2_GetActorLocation()
                keep = (FL.alifekeephubs == 1 and S2L.nearHub(q.X, q.Y)) or (FL.alifekeepquest == 1 and S2L.nearTrade(A, q.X, q.Y))   -- batch 29: positions only
                    or ((FL.vendmir or 0) > 0 and S2L.vendOf(ST, o, q) ~= nil)   -- b78fix vendmir: the client's own vendor stays (b124vendone: vendOf)
                if keep and A.hid[ad] then A.hid[ad] = nil; o:SetActorHiddenInGame(false); o:SetActorEnableCollision(true) end
                if keep then A.kept = (A.kept or 0) + 1 end
            end
            if not keep and not A.hid[ad] then
                A.hid[ad] = true; new = new + 1
                o:SetActorHiddenInGame(true); o:SetActorEnableCollision(false)
                if #names < 4 then names[#names + 1] = o:GetFullName():match("^(%S+)") end
            end
            if A.hid[ad] then pcall(S2L.hnSafe, ST, A, o, ad, lp) end   -- b135fixes 1 (hidnpc)
        end end) end
    A.total = total
    if new > 0 then A.hidden = (A.hidden or 0) + new; log(string.format("S2CORE alife: %d NPC(s) locais escondidos e sem colisao [%s] (vivos nao nossos agora=%d, escondidos no total=%d)", new, table.concat(names, " "), total, A.hidden)) end
end

-- ===== b135fixes (flags hidnpc / qdead / qfall, default 1): three whole-family rules
-- (1) hidnpc, CLIENT with alifeoff=1. Measured live 2026-10-15 (b131 R0 on B; supervisor 18:38): alifeTick hides the client's own NPCs
-- (hidden + no collision) but they stay ALIVE with their brains on (client aimask=0; XALifeDisable is an empty stub): 5-6 hidden blind dogs
-- in threat, focus = the client, 1-58 m, vel 0 for minutes, then they ran and hurt the client (HP 30 -> 13.5 in 14 s, no dmgin = local).
-- Rule: inside alifeTick's own 2-s pawn loop (so NPCs the game streams in or spawns later are covered) a HIDDEN live NPC that is in
-- threat, focused on the local player or within hidnpcd cm of it is made harmless for good: hidnpcvia 1 = DC:XKillNpcByUID(uid) (the
-- supervisor's live stopgap killed 5 dogs this way), 2 = AI2:XDestroyModel(uid) (b84: gone, no respawn); uid = CppMediator:GetGUID(pawn).
-- Never: our bodies (isOwnBody, and max HP >= hidnpchp: puppet 100000, target 5000), npcsync pairs (ST.nps.pa), kept NPCs (hubs / trade
-- places / vendors are never in A.hid), story NPCs of s2u_choice.lua (S2L.chProt, humans only). The same uid alive again 5 s later ->
-- one retry with the other call, then it stays hidden only (one log line).
function S2L.hnSafe(ST, A, o, ad, lp)
    if (FL.hidnpc or 1) ~= 1 or not A.hid or not A.hid[ad] then return end
    local H = A.hn
    if not H or H.pa ~= A.pawnAddr then H = {pa = A.pawnAddr, u = {}, n = 0, sk = {}}; A.hn = H end
    if ST.nps and ST.nps.pa and ST.nps.pa[ad] then return end           -- b122 npcsync pair (driven from the host)
    if rawget(_G, "s2n_nat_listed_addr") and s2n_nat_listed_addr(ad) == true then return end   -- b162 (audit 182): natnpc lists / owns it
    local mh; pcall(function() mh = o:GetMaxHP() end)
    if type(mh) ~= "number" or mh >= (FL.hidnpchp or 4999) then return end
    local med = StaticFindObject("/Script/Stalker2.Default__CppMediator")
    if not valid(med) then return end
    local thr, foc = false, false
    pcall(function() thr = o:IsInThreat() == true end)
    if not thr then pcall(function() local f = med:GetFocusedEnemy(o); foc = valid(f) and f:GetAddress() == lp:GetAddress() end) end
    local q, p = o:K2_GetActorLocation(), lp:K2_GetActorLocation()
    local d = math.sqrt((q.X - p.X) ^ 2 + (q.Y - p.Y) ^ 2 + (q.Z - p.Z) ^ 2)
    if not (thr or foc or ((FL.hidnpcd or 0) > 0 and d <= FL.hidnpcd)) then return end   -- b142: distance alone killed peaceful copies (user: NPCs die when the client comes close); hidnpcd 0 = off
    local full = o:GetFullName()
    local name = full:match("[^%.:]+$") or "?"
    if full:find("BP_AI_Human", 1, true) and S2L.chProt then
        local okP, g = pcall(S2L.chProt, ST, o)
        if okP and g then
            if not H.sk[ad] then H.sk[ad] = true; log("S2CORE hidnpc: " .. name .. " e NPC de historia (s2u_choice " .. tostring(g) .. "): fica so escondido") end
            return
        end
    end
    local uid; pcall(function() uid = med:GetGUID(o) end)
    uid = type(uid) == "number" and math.tointeger(uid) or nil
    if uid and rawget(_G, "s2n_nat_listed") and s2n_nat_listed(uid) == true then if not H.sk[ad] then H.sk[ad] = true; log("S2CORE hidnpc: " .. name .. " (uid " .. uid .. ") esta na lista natnpc do host: nao morto (b162)") end; return end
    if not uid or uid <= 0 then
        if not H.sk[ad] then H.sk[ad] = true; log("S2CORE hidnpc: " .. name .. " sem uid: fica so escondido") end
        return
    end
    local now = os.clock()
    local U = H.u[uid]
    if U and now - U.t < 5 then return end
    if U and U.k >= 2 then
        if not U.lg then U.lg = true; log(string.format("S2CORE hidnpc: %s (uid %d) ainda vivo depois de 2 tentativas: fica so escondido", name, uid)) end
        return
    end
    local k = U and (U.k + 1) or 1
    local via = (FL.hidnpcvia or 1) == 2 and 2 or 1
    if k == 2 then via = 3 - via end
    local ok, e = pcall(function()
        if via == 2 then FindFirstOf("CustomConsoleManagerAI2"):XDestroyModel(uid) else FindFirstOf("CustomConsoleManagerDC"):XKillNpcByUID(uid) end
    end)
    H.u[uid] = {t = now, k = k}; H.n = H.n + 1
    log(string.format("S2CORE hidnpc: NPC local escondido %s (%s, %.0f m, maxhp %.0f) -> %s(%d) ok=%s%s #%d", name,
        thr and "em ameaca" or (foc and "foco em mim" or "perto"), d / 100, mh, via == 2 and "XDestroyModel" or "XKillNpcByUID", uid,
        tostring(ok), ok and "" or (" " .. tostring(e)), H.n))
end
-- (2) qdead, both roles. Live 2026-10-15: host dropped BasePDA_Ward -> client S2QI (1 -> 0) -> the client died -> 12 s later, on the DEATH
-- SCREEN, qimirror read 'BasePDA_Ward 0 -> 1 aqui (estavel 3 s)' and sent it (host +1 = duplicate); the load came 4 s later (qmload guards
-- the NEW pawn only). Rule for every quest-item count sender (qimirror qmTick, lead ldQi, qitem qiTick): no count sweep while the local
-- player is dead (IsAlive false / HP <= 0 / nodeath holding it) or downed (lifeguard); pending changes are dropped. The window ends on a
-- NEW pawn (qmload / this re-read the base) or when the same pawn is alive and standing for qmset s; after a DEATH on the same pawn the
-- base is read again (qimirror / lead), never sent. Incoming counts (#QI from the other player) are still applied meanwhile.
-- lootshare / lscont / idrop send nothing on a count sweep (event driven: opening a body or container, a drop seen in the world).
function S2L.qgDead(ST, Q, pw, now, rebase)            -- true = no count sweep now
    if (FL.qdead or 1) ~= 1 then Q.qg = nil; return false end
    local a = pw:GetAddress()
    local alive, hp = true, nil
    pcall(function() alive = pw:IsAlive() and true or false end)
    pcall(function() hp = pw:GetHP() end)
    local dead = (not alive) or (type(hp) == "number" and hp <= 0) or (ST.nd ~= nil and ST.nd.dead ~= nil)
    local down = ST.life ~= nil and ST.life.down ~= nil
    local G = Q.qg
    if dead or down then
        if not G or G.a ~= a then
            G = {a = a, at = now}; Q.qg = G
            log(string.format("S2CORE qdead: jogador local %s: contagens de itens de missao NAO enviadas (pendentes descartados)", dead and "morto (hp 0 / tela de morte)" or "caido"))
        end
        if dead then G.dead = true end
        G.ok = nil
        if Q.pend and next(Q.pend) then Q.pend = {} end
        return true
    end
    if not G then return false end
    local why
    if G.a ~= a then why = "pawn novo"
    else
        G.ok = G.ok or now
        if now - G.ok < (FL.qmset or 3) then return true end
        why = G.dead and "mesmo pawn de volta depois da morte" or "de pe de novo"
    end
    Q.qg = nil
    if rebase and (G.dead or G.a ~= a) then
        Q.based = nil; Q.bn = 0; Q.sent = {}; if Q.pend then Q.pend = {} end
        why = why .. "; base relida, nada enviado"
    end
    log(string.format("S2CORE qdead: %s depois de %.0f s: contagens voltam a ser enviadas", why, now - G.at))
    return false
end
-- (3) qfall, both roles. Measured live 2026-10-15: a quest PDA dropped (qdrop) OUTDOORS on grass: its BP_StaticItemContainer_C was 40 m below
-- the player 0.1 s later and 62 m below at rest; vodka at the same spot rests at dz -90; the same PDA indoors on concrete stayed. Rule (every
-- item container, not only quest items): a drop window opens when the inventory is seen open (1 Hz) or a qdrop hold starts, with a baseline
-- of the containers within qfallr cm (XY) of the player; until qfallwin s after the inventory closes, a container that is NEW there (not in
-- the baseline, or moved > 1 m from it), visible, with collision on, and more than qfalldz cm below the player -> physics off
-- (MeshComponent:SetSimulatePhysics(false)) and put back on the ground: one LineTraceSingle down at its x,y from the player's height (impact
-- within the player's feet -300/+150 cm), else at the player's feet z. Up to 3 times per container per window. No UObject kept in ST.
-- Hidden copies (ground rule gHide: hidden + no collision, they fall on purpose) and the pool at 0,0,0 are never touched.
function S2L.qfScan(px, py, pz)
    local t, r2 = {}, (FL.qfallr or 400) ^ 2
    for _, o in ipairs(FindAllOf("BP_StaticItemContainer_C") or {}) do pcall(function()
        if not o:IsValid() or o.bHidden then return end
        local q = o:K2_GetActorLocation()
        if (q.X - px) ^ 2 + (q.Y - py) ^ 2 > r2 or q.Z > pz + 300 or q.Z < pz - 30000 then return end
        local col = true; pcall(function() col = o:GetActorEnableCollision() ~= false end)
        if not col then return end
        t[#t + 1] = {o = o, a = o:GetAddress(), x = q.X, y = q.Y, z = q.Z}
    end) end
    return t
end
function S2L.qfArm(ST, why)
    if (FL.qfall or 1) ~= 1 then return end
    local pw = S2.io and S2.io.pawn; if not valid(pw) then return end
    local now, pa = os.clock(), pw:GetAddress()
    local F = ST.qf
    if F and F.pa == pa and now <= F.untilT then F.untilT = math.max(F.untilT, now + (FL.qfallwin or 8)); return end
    local q = pw:K2_GetActorLocation()
    F = {base = {}, fix = {}, px = q.X, py = q.Y, pz = q.Z, pa = pa, at = now, untilT = now + (FL.qfallwin or 8), why = why}
    for _, c in ipairs(S2L.qfScan(q.X, q.Y, q.Z)) do F.base[c.a] = {x = c.x, y = c.y, z = c.z} end
    ST.qf = F
end
function S2L.qfFix(F, c)
    local k = (F.fix[c.a] or 0) + 1
    if k > 3 then return end
    F.fix[c.a] = k
    local o = c.o
    local frz = pcall(function() o.MeshComponent:SetSimulatePhysics(false) end)
    local feet = F.pz - 90
    local h, iz = S2L.tpfTrace(c.x, c.y, F.pz + 50, c.x, c.y, F.pz - 1000)
    local tz, how
    if h and type(iz) == "number" and iz >= feet - 300 and iz <= feet + 150 then tz = iz + (FL.qfallup or 5); how = "chao pelo trace"
    else tz = feet + (FL.qfallup or 5); how = "pes do jogador (trace " .. (h == nil and "falhou" or (h and string.format("z %.0f fora", iz or 0) or "sem chao")) .. ")" end
    local ok = pcall(function() o:K2_SetActorLocation({X = c.x, Y = c.y, Z = tz}, false, {}, true) end)
    F.base[c.a] = {x = c.x, y = c.y, z = tz}
    local m = "?"; pcall(function() m = M.meshName and M.meshName(o, "MeshComponent") or "?" end)
    log(string.format("S2CORE qfall: item no chao caiu %.0f m abaixo do jogador (%s, %s) -> fisica off=%s, de volta em z %.0f (%s) ok=%s #%d",
        (F.pz - c.z) / 100, tostring(m), F.why, tostring(frz), tz, how, tostring(ok), k))
end
function S2L.qfTick(ST)                                  -- tick1000, both roles
    if (FL.qfall or 1) ~= 1 then ST.qf = nil; return end
    local pw = S2.io and S2.io.pawn; if not valid(pw) then ST.qf = nil; return end
    if S2L.qdrInv() then S2L.qfArm(ST, "inventario") end
    local F = ST.qf; if not F then return end
    local now = os.clock()
    if now > F.untilT or F.pa ~= pw:GetAddress() then ST.qf = nil; return end
    local lim = FL.qfalldz or 500
    for _, c in ipairs(S2L.qfScan(F.px, F.py, F.pz)) do
        local b = F.base[c.a]
        local new = not b or (b.x - c.x) ^ 2 + (b.y - c.y) ^ 2 + (b.z - c.z) ^ 2 > 100 ^ 2
        if new and c.z < F.pz - lim then
            local ok, e = pcall(S2L.qfFix, F, c)
            if not ok then log("S2CORE qfall erro: " .. tostring(e)) end
        end
    end
end
-- ===== end b135fixes
-- ===== b78fix D4: our own bodies take no interaction (flag tgtnoint, default 0). Measured 21:22 on the host: the hidden target
-- (S2Coop_RemoteTarget, NPCComponent bIsInteractionActive=true) took the host's F at a lever -> dialog "Skif: Ola". Each second the
-- NPCComponent (an InteractionComponent) of the target and of the puppet is set inactive when it reads active.
function S2L.noInteract(ST)
    if (FL.tgtnoint or 0) ~= 1 then return end
    for _, P in ipairs({ST.tgt or {}, ST.pup or {}}) do
        local p = P.pawn
        if valid(p) then pcall(function()
            local c = p.NPCComponent
            if valid(c) and c.bIsInteractionActive == true then
                c:SetInteractionActive(false)
                ST.niN = (ST.niN or 0) + 1
                if ST.niN <= 5 or ST.niN % 100 == 0 then log(string.format("S2CORE noint: interacao desligada no %s (#%d; agora ativa=%s)", P == ST.tgt and "alvo" or "boneco", ST.niN, tostring(c.bIsInteractionActive))) end
            end
        end) end
    end
end
-- ===== b82orph: orphan sweep (flag orphsweep, default 0; 2 = dry run). Measured 2026-10-14 (host): the dead hidden target stays in the world
-- after MORREU (NPCComponent still interactable, MP5 + vest on it), and after a teleport-drop ("corpos parados") old bodies of ours stay alive:
-- a VISIBLE 5000-hp target 3 m from the host and a hidden 100000-hp puppet body that a hostile took as its AI focus. A body is ours only if it
-- is in our HP band AND (a) the same UObject as a body adopted here, or (b) has the A-Life GUID of one, or (c) has the prototype id of our
-- adopted body of that kind, once 3 real NPCs were seen with another id and none with ours. Never the adopted bodies, never the pool at 0,0,0,
-- nothing while a spawn/adoption is pending, no puppet body while no puppet is adopted (pupadopt may take it).
-- ===== b84alife: orphrm=3 removes an orphan of ours from the A-LIFE by its GUID. Measured 2026-10-14 22:39 (client): K2_DestroyActor removes
-- only the actor; the A-Life re-creates it within ~3 s with a new name and the SAME GUID -> endless destroy loop. GetGUID's int is the console
-- UID (measured: XAddMoveTo(uid), XSetEntityUnkillableByUID(uid)). orphvia=1: CustomConsoleManagerAI2:XDestroyModel(UID) (agent model = A-Life
-- object: INFERRED from the name); orphvia=2: DC:XKillNpcByUID(UID) + AA:XDespawnDeadNPCsAtRadius (measured 2026-10-10 23:20 on a saved body of
-- ours), only if no other corpse lies within that radius. One removal per GUID per world (never two within 60 s); a removed GUID seen again
-- -> one log line, then hide only for it. The adopted bodies' GUIDs are never removed.
function S2L.orphGone(ST, O, h, now)
    local g = h.g
    if type(g) ~= "number" or g <= 0 then return " alife=sem guid (so escondido)" end
    O.gone = O.gone or {}; O.back = O.back or {}
    for _, B in ipairs({ST.tgt or {}, ST.pup or {}}) do if B.uid == g and B.pawn then return " alife=guid " .. g .. " e do corpo adotado (so escondido)" end end
    if O.back[g] then return " alife=guid " .. g .. " so escondido" end
    local G = O.gone[g]
    if G and (G.key == O.key or now - G.t < 60) then
        O.back[g] = true
        log(string.format("S2CORE orfao: guid %d voltou %.0f s depois da remocao (%s) -> so esconder este guid daqui em diante", g, now - G.t, h.k:match("[^%.]+$") or h.k))
        return " alife=guid " .. g .. " voltou (so escondido)"
    end
    O.gone[g] = {t = now, key = O.key}
    local via = FL.orphvia == 2 and "XKillNpcByUID+XDespawnDeadNPCsAtRadius" or "XDestroyModel"
    local ok, e = pcall(function()
        if FL.orphvia ~= 2 then FindFirstOf("CustomConsoleManagerAI2"):XDestroyModel(g); return end
        local lp = api.getLocal()
        if not lp or h.d < 0 then error("sem jogador local") end
        local R = h.d * 100 + 300
        for _, o in ipairs(FindAllOf("BP_AI_Human_C") or {}) do
            if o ~= h.o and valid(o) and not o:IsAlive() then
                local q = o:K2_GetActorLocation()
                if not (math.abs(q.X) < 100 and math.abs(q.Y) < 100) and (q.X - lp.X) ^ 2 + (q.Y - lp.Y) ^ 2 <= (R + 500) ^ 2 then error("outro corpo a menos de " .. math.floor(R / 100 + 5) .. " m") end
            end
        end
        FindFirstOf("CustomConsoleManagerDC"):XKillNpcByUID(g)
        FindFirstOf("CustomConsoleManagerAA"):XDespawnDeadNPCsAtRadius(R)
    end)
    if not ok then O.back[g] = true end              -- a failed removal is not tried again: hide only
    return " alife=" .. via .. "(" .. g .. ") ok=" .. tostring(ok) .. (ok and "" or (" " .. tostring(e)))
end
-- b84alife: bodyoffdestroy - the 3rd arg of XSpawnObjBySID (dump: PrototypeSID, Rank, bDestroyOnOffline, Px, Py, Pz, Pitch, Yaw, Roll). With true the
-- A-Life deletes a body of ours when it goes offline (no orphans come back), but a parked/hidden body cannot be re-adopted after that. 2026-10-10:
-- a spawn with true showed NO pawn in 20 s, so a request with true that got no body switches this body kind back to false for the session.
function S2L.bodyOD(P, tag)
    local m = FL.bodyoffdestroy or 0
    if not (m == 1 or (m == 2 and tag == "target")) or P.odOff then P.odPend = nil; return false end
    if P.odPend and (P.asks or 0) > 0 then       -- the last request (with true) was not followed by an adoption
        P.odOff = true; P.odPend = nil
        log("S2CORE " .. tostring(tag) .. ": pedido com bDestroyOnOffline=true sem corpo; volto a false nesta sessao (bodyoffdestroy)")
        return false
    end
    if not P.odLog then P.odLog = true; log("S2CORE " .. tostring(tag) .. ": pedidos com bDestroyOnOffline=true (bodyoffdestroy)") end
    P.odPend = true
    return true
end
function S2L.orphSweep(ST)
    local mode = FL.orphsweep or 0
    if mode ~= 1 and mode ~= 2 then return end
    local O = ST.orph
    if not O then O = {at = 0, n = 0}; ST.orph = O end
    local T, P, W = ST.tgt or {}, ST.pup or {}, ST.wg
    local wkey = W and W.key
    if wkey ~= O.key or not O.ra or not O.ru then          -- a load (new local pawn): addresses and GUIDs of the old world mean nothing here
        O.key = wkey; O.ra = {}; O.guid = {}; O.pid = {}; O.np = {}; O.proof = 0; O.pidBad = nil; O.done = {}; O.ru = {}; O.back = {}   -- b84alife: O.gone kept (60 s guard)
    end
    local now = os.clock()
    local med = nil
    local function pidOf(o)
        local ok, id = pcall(function() return med:GetPrototypeID(o) end)
        if ok and type(id) == "number" and id > 0 then return id end
    end
    local keep = {}
    for _, B in ipairs({{T, "alvo"}, {P, "boneco"}}) do      -- register the adopted bodies (once per body)
        local b, kind = B[1], B[2]
        if bodyOk(b) then pcall(function()
            local o = b.pawn
            local k = tostring(o:GetAddress()) .. "|" .. o:GetFullName()
            keep[k] = true
            if O.ra[k] then return end
            O.ra[k] = kind; O.ru[k] = b.uid   -- b84alife
            if b.uid ~= nil then O.guid[b.uid] = kind end
            if not med then med = StaticFindObject("/Script/Stalker2.Default__CppMediator"); if not valid(med) then med = false end end
            local id = med and pidOf(o) or nil
            if id then O.pid[kind] = id end
            log(string.format("S2CORE orfao: %s registrado %s guid=%s prototipo=%s", kind, k:match("[^%.]+$") or k, tostring(b.uid), tostring(id)))
        end) end
    end
    local tOk = bodyOk(T)
    if O.tk and not tOk then O.at = 0 end        -- the adopted target just went away (MORREU, dropped): sweep as soon as allowed
    O.tk = tOk or nil
    if now < O.at then return end
    if FL.bodysafe == 1 and not (W and W.open) then return end   -- no body is touched while the world is not stable
    if T.spawnAt or P.spawnAt or (ST.cl and ST.cl.pend) then return end   -- a body waiting for adoption is never touched (b138loot: nor a loot body)
    O.at = now + math.max(FL.orphsec or 3, 1)
    if not med then med = StaticFindObject("/Script/Stalker2.Default__CppMediator"); if not valid(med) then med = false end end
    local pidOk = (not O.pidBad) and O.proof >= 3
    local lp = api.getLocal()
    local hits, samples = {}, 0
    for _, o in ipairs(FindAllOf("BP_AI_Human_C") or {}) do pcall(function()
        if not valid(o) or (T.pawn and o == T.pawn) or (P.pawn and o == P.pawn) then return end
        local k = tostring(o:GetAddress()) .. "|" .. o:GetFullName()
        if keep[k] or O.done[k] == mode then return end
        if (rawget(_G, "s2n_nat_own") ~= nil and o ~= nil and s2n_nat_own(o:GetAddress()) == true) then return end   -- b158natnpc
        local mh = o:GetMaxHP()
        local kind = (mh >= 4000 and mh <= ((FL.orphsave or 0) == 1 and 6500 or 6000) and "alvo") or (mh >= 90000 and "boneco") or nil
        local alive = o:IsAlive()
        if kind and #hits >= math.max(FL.orphmax or 4, 1) then return end   -- b94sweep: a few per sweep
        if not kind then                                   -- a real NPC: proves (or disproves) that the prototype id tells ours apart
            if med and alive and not pidOk and not O.pidBad and (O.pid.alvo or O.pid.boneco) and samples < 3 and not O.np[k] then
                samples = samples + 1; O.np[k] = true
                local id = pidOf(o)
                if id and (id == O.pid.alvo or id == O.pid.boneco) then
                    O.pidBad = true; log("S2CORE orfao: prototipo " .. id .. " tambem num NPC real " .. (k:match("[^%.]+$") or k) .. " (maxhp " .. mh .. "); identificacao por prototipo DESLIGADA")
                elseif id then
                    O.proof = O.proof + 1
                    if O.proof == 3 then log(string.format("S2CORE orfao: prototipo dos nossos (alvo=%s boneco=%s) difere de 3 NPCs reais; identificacao por prototipo ligada", tostring(O.pid.alvo), tostring(O.pid.boneco))) end
                end
            end
            return
        end
        local q = o:K2_GetActorLocation()
        if math.abs(q.X) < 100 and math.abs(q.Y) < 100 then return end   -- the A-Life pool at 0,0,0: never touched
        local why
        if O.ra[k] == kind then why = "corpo adotado antes"
        elseif alive and med then                          -- GUID / prototype only on live bodies (no CppMediator call on corpses)
            local g; pcall(function() g = med:GetGUID(o) end)
            if g ~= nil and O.guid[g] == kind then why = "guid " .. tostring(g)
            elseif pidOk then local id = pidOf(o); if id and id == O.pid[kind] then why = "prototipo " .. id end end
            if not why and (FL.orphsave or 0) == 1 and g ~= nil then   -- b94sweep: a save leftover = our band + the mesh of our adopted body
                local B = kind == "alvo" and T or P
                if bodyOk(B) then
                    local m1, m2
                    pcall(function() local c = o.Mesh; local s = valid(c) and c.SkeletalMesh; if valid(s) then m1 = s:GetFName():ToString() end end)   -- b96mesh: never GetFName a null mesh
                    pcall(function() local c = B.pawn.Mesh; local s = valid(c) and c.SkeletalMesh; if valid(s) then m2 = s:GetFName():ToString() end end)
                    if m1 and (m1 == m2 or kind == "boneco") then why = "guid " .. tostring(g) .. " sobra de save (malha " .. m1 .. ")"; O.guid[g] = kind end
                end
            end
        end
        if not why then return end
        if kind == "boneco" and not bodyOk(P) then return end   -- no puppet adopted: pupadopt may take this body next
        local d = lp and math.sqrt((q.X - lp.X) ^ 2 + (q.Y - lp.Y) ^ 2) / 100 or -1
        hits[#hits + 1] = {o = o, k = k, kind = kind, alive = alive, mh = mh, why = why, d = d, g = O.ru[k] or (why:find("^guid ") and tonumber(why:match("^guid (%d+)")))}
    end) end
    for _, h in ipairs(hits) do
        O.done[h.k] = mode; O.n = O.n + 1
        local act
        if mode == 2 then act = "TESTE: faria esconder+sem colisao+sem interacao" .. (FL.orphrm == 2 and "+K2_DestroyActor" or FL.orphrm == 3 and ("+remocao A-Life guid=" .. tostring(h.g)) or "")
        else
            local o = h.o
            local okH = pcall(function() o:SetActorHiddenInGame(true) end)
            local okC = pcall(function() o:SetActorEnableCollision(false) end)
            local okI = pcall(function() local c = o.NPCComponent; if valid(c) then c:SetInteractionActive(false) else error("sem NPCComponent") end end)
            act = string.format("escondido=%s sem_colisao=%s sem_interacao=%s", tostring(okH), tostring(okC), tostring(okI))
            if FL.orphrm == 2 then local okD = pcall(function() o:K2_DestroyActor() end); act = act .. " destruido=" .. tostring(okD) end
            if FL.orphrm == 3 then act = act .. S2L.orphGone(ST, O, h, now) end   -- b84alife
        end
        if O.n <= 30 or O.n % 50 == 0 then
            log(string.format("S2CORE orfao: %s (%s, vivo=%s, maxhp=%.0f, %.0f m, por %s) -> %s (total=%d)", h.k:match("[^%.]+$") or h.k, h.kind, tostring(h.alive), h.mh, h.d, h.why, act, O.n))
        end
    end
end
local beat = 0
function S2L.autoLoad(ST)                           -- batch 39
    if FL.autoload ~= 1 then return end
    local A = ST.al or {}; ST.al = A
    if A.done then return end
    local pw; pcall(function() pw = api.UEHelpers.GetPlayerController().Pawn end)
    if valid(pw) then A.done = true; log("S2CORE autoload: pawn no mundo; autoload encerrado neste processo"); return end
    local menu = FindFirstOf("MainMenuView")
    if not (menu and menu:IsValid()) then return end
    local now = os.clock()
    if now < (A.nextAt or 0) then return end
    A.nextAt = now + 20; A.n = (A.n or 0) + 1
    local ok, e = pcall(function() FindFirstOf("CustomConsoleManagerMP"):XLoadLastSave() end)
    log(string.format("S2CORE autoload: menu sem pawn -> XLoadLastSave #%d ok=%s %s (na tela de titulo so funciona depois de uma tecla)", A.n, tostring(ok), ok and "" or tostring(e)))
end
-- ===== b85crash (flag abpguard, default 0). Host crash 22:46:47: AV 0x588 at exe+f19a42, 0.3 s after the emission end. An NPC AI move-to
-- action (exe+67de656) reads Agent+0xAC8 (native HumanAnimController) with no HasAnimController check; it was null. The game builds it
-- once (Obj BeginPlay-like exe+d9a588 -> vfunc 0xab0) and only if the mesh's instance is an AnimInstanceHuman then; a later instance
-- repair (b80/b83) cannot rebuild it. UE4SS reads the raw pointer through a custom Int64 property (0xAC8 human, 0xAD0 mutant).
function S2L.abpGuard(ST)
    local md = FL.abpguard or 0
    if md == 0 then return end
    local G = ST.abg; if not G then G = {seen = {}, nseen = 0, kill = {}, cap = {}}; ST.abg = G end
    local now = os.clock()
    if not G.reg then                                                      -- once per process (ST survives S2.reload, not a relaunch)
        G.reg = true
        local ok, e = pcall(function()
            RegisterCustomProperty({Name = "S2AnimCtlH", Type = PropertyTypes.Int64Property, BelongsToClass = "/Script/Stalker2.Agent", OffsetInternal = 0xAC8})
            RegisterCustomProperty({Name = "S2AnimCtlM", Type = PropertyTypes.Int64Property, BelongsToClass = "/Script/Stalker2.Agent", OffsetInternal = 0xAD0})
        end)
        G.regOk = ok
        log("S2CORE abpguard: propriedades do controlador de animacao (Agent+0xAC8/+0xAD0) registradas ok=" .. tostring(ok) .. (ok and "" or (" " .. tostring(e) .. " -> so a classe da instancia")))
    end
    if now < (G.next or 0) then return end
    G.next = now + math.max(1, FL.abpsec or 2)
    local function nm(o) local s = "nulo"; pcall(function() if valid(o) then s = o:GetFName():ToString() end end); return s end
    local function human(ai)                                               -- class chain walk to AnimInstanceHuman (b83 lamp-check style)
        local ok = false
        pcall(function()
            if not valid(ai) then return end
            local c = ai:GetClass()
            for _ = 1, 8 do if not valid(c) then return end; if c:GetFName():ToString() == "AnimInstanceHuman" then ok = true; return end; c = c:GetSuperStruct() end
        end)
        return ok
    end
    if not valid(S2L.abgMed) then pcall(function() S2L.abgMed = S2L.emMed() end) end   -- a CDO: never pooled
    local em = "?"; pcall(function() em = tostring(S2L.abgMed:IsEmissionActive() == true) end)
    local P, T = ST.pup or {}, ST.tgt or {}
    local pa, ta = -1, -1
    if valid(P.pawn) then pcall(function() pa = P.pawn:GetAddress() end) end
    if valid(T.pawn) then pcall(function() ta = T.pawn:GetAddress() end) end
    local lp = S2L.ioPawn(); local lpos
    if valid(lp) then pcall(function() lpos = lp:K2_GetActorLocation() end) end
    local host = api.ROLE == "host"
    local gate = not (FL.bodysafe == 1 and not (ST.wg and ST.wg.open)) and not (ST.far and ST.far.on)
    local list = {}; pcall(function() list = FindAllOf("BP_AI_Human_C") or {} end)
    local n, bad, names, acts = 0, 0, {}, {}
    for _, o in ipairs(list) do
        pcall(function()
            if not o:IsValid() or not o:IsAlive() then return end
            n = n + 1
            local h, m
            if G.regOk then pcall(function() h = o.S2AnimCtlH; m = o.S2AnimCtlM end) end
            local ai; pcall(function() ai = o.Mesh:GetAnimInstance() end)
            local hum = human(ai)
            local noCtl
            if type(h) == "number" and type(m) == "number" then noCtl = (h == 0 and m == 0) else noCtl = not hum end   -- fallback: no instance = suspect
            if not noCtl then return end
            bad = bad + 1
            local name = nm(o)
            local ad = -2; pcall(function() ad = o:GetAddress() end)
            local tag = (ad == pa and "boneco") or (ad == ta and "alvo") or "outro"
            names[#names + 1] = name .. "/" .. tag
            local key = name .. "|" .. tostring(hum)
            if not G.seen[key] then
                if G.nseen > 500 then G.seen = {}; G.nseen = 0 end
                G.seen[key] = true; G.nseen = G.nseen + 1
                local d, hid, mode = -1, "?", "?"
                pcall(function() local q = o:K2_GetActorLocation(); if lpos then d = math.sqrt((q.X - lpos.X)^2 + (q.Y - lpos.Y)^2 + (q.Z - lpos.Z)^2) / 100 end end)
                pcall(function() hid = tostring(o.bHidden) end)
                pcall(function() mode = tostring(o.Mesh:GetAnimationMode()) end)
                log(string.format("S2CORE abpguard: humano SEM controlador de animacao %s (%s) ctl=%s/%s instancia=%s humana=%s modo=%s escondido=%s dist=%.0f m emissao=%s",
                    name, tag, tostring(h), tostring(m), nm(ai), tostring(hum), mode, hid, d, em))
            end
            if (tag ~= "outro" and md >= 2 and gate and (tag == "alvo" or M.ownsAvatar(ST))) or (tag == "outro" and md >= 3 and host and gate) then
                acts[#acts + 1] = {o = o, tag = tag, name = name}
            end
        end)
    end
    if bad ~= (G.lastBad or 0) then
        G.lastBad = bad
        log(string.format("S2CORE abpguard: censo humanos vivos=%d sem controlador=%d emissao=%s %s", n, bad, em, table.concat(names, " ", 1, math.min(#names, 12))))
    end
    for _, a in ipairs(acts) do
        local role = a.tag == "outro" and "outro" or a.tag
        local q = {}; for _, at in ipairs(G.kill[role] or {}) do if now - at < 600 then q[#q + 1] = at end end; G.kill[role] = q
        local cap = role == "outro" and (FL.abpmax3 or 6) or (FL.abpmax or 4)
        if #q >= cap then
            if not G.cap[role] or now - G.cap[role] > 600 then G.cap[role] = now; log("S2CORE abpguard: limite de " .. cap .. " reciclagens em 600 s (" .. role .. "); so log ate la") end
        else
            q[#q + 1] = now
            pcall(function() a.o:SetActorHiddenInGame(true) end)
            local okD = pcall(function() a.o:K2_DestroyActor() end)
            if role == "boneco" then
                P.pawn = nil; P.path = nil; P.state = nil; P.hidden = nil; P.abpFor = nil; pcall(api.setAvatar, nil); P.retryAt = now + 2; bodyLost(ST)
            elseif role == "alvo" then
                T.pawn = nil; T.path = nil; T.setup = nil; T.retryAt = now + 2; bodyLost(ST)
            end
            log(string.format("S2CORE abpguard: %s %s sem controlador -> escondido + K2_DestroyActor ok=%s (%d/%d em 600 s; corpo novo = BeginPlay novo)", role, a.name, tostring(okD), #q, cap))
        end
    end
end
-- ===== b86pupctl (flags pupnoreinit / pupnoai / emparkpup, default 0). S2Coop_RemotePlayer bodies lost Agent+0xAC8 right after b80's
-- SetAnimationMode(0, true) re-init of their healthy AnimBP (log 23:22-23:33); the target, never re-inited, kept it.
function S2L.humanAnim(pawn)                                              -- mesh in mode 0 running an AnimInstanceHuman (class chain walk)
    local ok = false
    pcall(function()
        local mesh = pawn.Mesh; if not valid(mesh) or mesh:GetAnimationMode() ~= 0 then return end
        local ai = mesh:GetAnimInstance(); if not valid(ai) then return end
        local c = ai:GetClass()
        for _ = 1, 8 do if not valid(c) then return end; if c:GetFName():ToString() == "AnimInstanceHuman" then ok = true; return end; c = c:GetSuperStruct() end
    end)
    return ok
end
function S2L.pupCtl(ST)
    local na, ep = FL.pupnoai or 0, FL.emparkpup or 0
    if (na == 0 and ep == 0) or not M.ownsAvatar(ST) then return end
    local P = ST.pup or {}
    local now = os.clock()
    local function nm(o) local s = "nulo"; pcall(function() if valid(o) then s = o:GetFName():ToString() end end); return s end
    if ep == 1 then
        local E = ST.emp; if not E then E = {}; ST.emp = E end
        if not valid(S2L.abgMed) then pcall(function() S2L.abgMed = S2L.emMed() end) end   -- a CDO: never pooled (shared with abpguard)
        local on = false; pcall(function() on = S2L.abgMed:IsEmissionActive() == true end)
        if on then E.endT = nil; E.seen = true elseif E.seen and not E.endT then E.endT = now end
        local park = on or (E.endT ~= nil and now - E.endT < (FL.emgrace or 15))
        if not park then E.seen = nil; E.endT = nil end
        if park ~= (E.park or false) then
            E.park = park
            log("S2CORE emparkpup: emissao=" .. tostring(on) .. " -> boneco " .. (park and "estacionado (removido; sem adocao/spawn)" or "liberado (corpo novo sera adotado/pedido)"))
        end
        if park and P.pawn then
            local how, ok, name = "ja invalido", true, nm(P.pawn)
            if valid(P.pawn) then
                pcall(function() P.pawn:SetActorHiddenInGame(true) end)
                ok = false
                if type(P.uid) == "number" and P.uid > 0 then
                    how = "XDestroyModel(" .. P.uid .. ")"; ok = pcall(function() FindFirstOf("CustomConsoleManagerAI2"):XDestroyModel(P.uid) end)
                end
                if not ok then how = "K2_DestroyActor"; ok = pcall(function() P.pawn:K2_DestroyActor() end) end
            end
            log("S2CORE emparkpup: boneco " .. name .. " -> escondido + " .. how .. " ok=" .. tostring(ok))
            P.pawn = nil; P.path = nil; P.state = nil; P.hidden = nil; P.abpFor = nil; P.naFor = nil; P.uid = nil; pcall(api.setAvatar, nil); P.retryAt = now + 2
            return
        end
    end
    if na >= 1 and bodyOk(P) and not (FL.bodysafe == 1 and not (ST.wg and ST.wg.open)) and not (ST.far and ST.far.on) and P.naFor ~= P.pawn then
        P.naFor = P.pawn
        local c, r = nil, {}
        pcall(function() c = P.pawn:GetController() end)
        local function st(k, f) local ok, e = pcall(f); r[#r + 1] = k .. "=" .. (ok and "ok" or ("erro " .. tostring(e):sub(1, 60))) end
        if valid(c) then
            st("brain", function() local b = c.BrainComponent; if not valid(b) then error("sem BrainComponent") end; b:StopLogic("s2coop") end)
            st("tickctl", function() c:SetActorTickEnabled(false) end)
        end
        st("path", function() P.pawn:TogglePathFollowingComponent(false) end)
        if na >= 2 and valid(c) then st("unpossess", function() c:UnPossess() end) end
        log("S2CORE pupnoai: boneco " .. nm(P.pawn) .. " controlador=" .. nm(c) .. " -> " .. table.concat(r, " "))
    end
end
-- ===== b134pupai (flag pupai, default 1). User 2026-10-15, both screens, near a dog pack: "the ai of the body is shooting and moving auto like
-- its alive", "its turning the camera, and breaking the animation". Measured live (host, puppet uid 3086; client the same): the puppet has NO
-- controller (pupnoai's StopLogic never applies), IsInThreat 50/50 samples, actor yaw off the remote's yaw by up to 139 deg. The cause is DATA:
-- the puppet was Faction = Player (Mutant<->Player = -800: the dogs are its enemies -> combat, aiming, firing) and ThreatPrototypeSID DefaultNPC
-- (a Shot sound gives 700 threat at EVERY relation level, Explosion 2000, BulletFlyby 200 ... -> TurnHead / MoveToLocation / SearchEnemy even
-- when nobody is an enemy; freeze 30 s + 30/s decay = ~63 s, so the 15 s live tries of XSetRelation / XResetAI could not show an effect).
-- The fix is the avatar pak v4 (offline b134pupai): puppet Faction = S2CoopPuppet (Friend to Player, Neutral to every other faction, generated
-- from the game's faction list), ThreatPrototypeSID = S2CoopPuppetThreat (every stimulus 0), VisionScannerPrototypeSID = NoVision. The hidden
-- target keeps Faction = Player, so enemies still attack it. This block only WATCHES the adopted puppet once a second: every threat on/off is
-- logged, and a body that stays in threat pupais s is a body the new data does not reach (an older body adopted from the A-Life / a save, or
-- pak v3 still installed) -> pupai 1 recycles it (hidden + XDestroyModel(uid), K2_DestroyActor if no uid; refs dropped; the normal code adopts
-- or requests a fresh body), at most pupaimax per 600 s, then log only. pupai 2 = log only.
function S2L.pupAi(ST)
    local md = FL.pupai or 0
    if md == 0 or not M.ownsAvatar(ST) then return end
    local P = ST.pup or {}
    local W = ST.pai; if not W then W = {kill = {}, n = 0}; ST.pai = W end
    local now = os.clock()
    if not bodyOk(P) or (FL.bodysafe == 1 and not (ST.wg and ST.wg.open)) or (ST.far and ST.far.on) then W.body = nil; W.t0 = nil; W.on = nil; return end
    if W.body ~= P.pawn then W.body = P.pawn; W.t0 = nil; W.on = nil end
    local thr = false
    pcall(function() thr = P.pawn:IsInThreat() == true end)
    local name = "?"
    pcall(function() if valid(P.pawn) then name = P.pawn:GetFName():ToString() end end)
    if thr ~= (W.on or false) then
        W.on = thr; W.n = W.n + 1
        if now - (W.logAt or -1e9) >= 5 or W.n <= 20 then
            W.logAt = now
            log(string.format("S2CORE pupai: boneco %s em ameaca=%s (#%d)%s", name, tostring(thr), W.n,
                thr and " - com o pak v4 isto nao deve acontecer" or ""))
        end
    end
    if not thr then W.t0 = nil; return end
    W.t0 = W.t0 or now
    if md ~= 1 or now - W.t0 < (FL.pupais or 90) then return end
    local q = {}
    for _, at in ipairs(W.kill) do if now - at < 600 then q[#q + 1] = at end end
    W.kill = q
    local cap = FL.pupaimax or 2
    if #q >= cap then
        if not W.capAt or now - W.capAt > 600 then
            W.capAt = now
            log(string.format("S2CORE pupai: boneco %s em ameaca ha %.0f s mas ja houve %d reciclagens em 600 s -> so log (pak v4 instalado nos DOIS?)", name, now - W.t0, #q))
        end
        return
    end
    q[#q + 1] = now
    local full
    pcall(function() full = (P.pawn:GetFullName():gsub("^%S+ ", "")) end)
    pcall(function() P.pawn:SetActorHiddenInGame(true) end)
    local how, ok = "K2_DestroyActor", false
    if type(P.uid) == "number" and P.uid > 0 then
        how = "XDestroyModel(" .. tostring(math.tointeger(P.uid) or P.uid) .. ")"
        ok = pcall(function() FindFirstOf("CustomConsoleManagerAI2"):XDestroyModel(P.uid) end)
    end
    if not ok then how = "K2_DestroyActor"; ok = pcall(function() P.pawn:K2_DestroyActor() end) end
    if full then P.bad = P.bad or {}; P.bad[full] = true end                -- never adopted again (pupFind skips P.bad)
    log(string.format("S2CORE pupai: boneco %s em ameaca ha %.0f s -> escondido + %s ok=%s (%d/%d em 600 s; corpo novo = dados novos)",
        name, now - W.t0, how, tostring(ok), #q, cap))
    P.pawn = nil; P.path = nil; P.state = nil; P.hidden = nil; P.abpFor = nil; P.naFor = nil; P.uid = nil
    pcall(api.setAvatar, nil); P.retryAt = now + 2; bodyLost(ST)
    W.body = nil; W.t0 = nil; W.on = nil
end
-- ===== end b134pupai
function M.tick1000(ST, n)
    beat = beat + 1
    pcall(S2L.autoLoad, ST)
    if beat % 5 == 0 then pcall(questRollback, ST) end
    pcall(M.qlogFlush, ST)
    pcall(S2L.iprobeTick, ST)
    pcall(S2L.noInteract, ST)    -- b78fix D4
    pcall(S2L.chUseTick, ST)     -- b91choice (N3)
    pcall(S2L.ldBlkTick, ST)     -- b109dlgblock (ldblk)
    pcall(S2L.qdTick, ST)        -- b129qdiv A (qdiv)
    pcall(S2L.lgTick, ST)        -- b129qdiv B (ldsave)
    pcall(S2L.abpGuard, ST)      -- b85crash
    pcall(S2L.pupCtl, ST)        -- b86pupctl
    pcall(S2L.pupAi, ST)         -- b134pupai (pupai)
    pcall(S2L.orphSweep, ST)     -- b82orph
    if beat % 2 == 0 then S2L.alifeTick(ST) end
    pcall(S2L.animFix, ST)       -- b83anim
    pcall(S2L.offaTick, ST)       -- b153offscreen (offanim / offvb)
    pcall(S2L.offTick, ST)       -- b116motion (offtick)
    S2L.fcTickP(ST)              -- b118fpscap (fpscap)
    pcall(S2L.aiwTick, ST)       -- b121aiwatch (aiwatch)
    pcall(S2L.relMirTick, ST)    -- b177g17 (relmir)
    pcall(S2L.hlTick, ST)        -- b133head (headsafe)
    pcall(S2L.qfTick, ST)        -- b135fixes 3 (qfall)
    if beat % 30 == 0 then
        local P = ST.pup or {}
        log(string.format("S2CORE vivo ver=%s gen=%d loads=%s puppet=%s estado=%s target=%s dano_total=%.1f erros=%d", M.VERSION, S2.gen, tostring(ST.loads), tostring(valid(P.pawn)), tostring(P.state), tostring(valid(ST.tgt and ST.tgt.pawn)), ST.dmg and ST.dmg.total or 0, S2.errs.n))
    end
end

-- ===== b116motion (flag offtick): the visible-body limiter. An Agent's components other than its Mesh tick only every Agent.MinDesiredTickRate s,
-- which the game sets from the Mesh LOD: LOD 2 -> 1/gsc.MidDistanceComponentsMaxTickRate (30), LOD > 2 -> 1/gsc.FarDistanceComponentsMaxTickRate (10)
-- (exe fn 0x24f4ea4). The sum restarts at each tick, so rate N = fps/ceil(fps/N) moves/s: 30 -> 26/s at 78 fps. offtick=K keeps both at ceil(K*4/3).
S2L.OFFT_CV = {"gsc.MidDistanceComponentsMaxTickRate", "gsc.FarDistanceComponentsMaxTickRate"}
S2L.OFFT_DEF = {["gsc.MidDistanceComponentsMaxTickRate"] = 30, ["gsc.FarDistanceComponentsMaxTickRate"] = 10}   -- the game's values (exe .data)
function S2L.offTick(ST)                           -- tick1000, host and client
    local O = ST.offt
    if not O then O = {set = 0, again = 0}; ST.offt = O end
    local k = tonumber(FL.offtick) or 0
    if k <= 0 and not O.saved then return end
    local KS = O.ks
    if not valid(KS) then KS = StaticFindObject("/Script/Engine.Default__KismetSystemLibrary"); O.ks = KS end
    local pw = S2.io and S2.io.pawn
    if not (valid(KS) and valid(pw)) then return end
    local w; pcall(function() w = pw:GetWorld() end)
    if not valid(w) then return end
    local function get(n) local v; pcall(function() v = KS:GetConsoleVariableFloatValue(n) end); return v end
    local function put(n, v) local ok, e = pcall(function() KS:ExecuteConsoleCommand(w, n .. " " .. tostring(v), nil) end); return ok, e end
    if k > 0 then
        local want = math.ceil(k * 4 / 3)
        if not O.saved then
            O.saved = {}
            for _, n in ipairs(S2L.OFFT_CV) do
                local v = get(n)
                if type(v) ~= "number" or v == want then v = S2L.OFFT_DEF[n] end   -- state lost after we set it: keep the game's value
                O.saved[n] = v
            end
            log(string.format("S2CORE offtick: valores do jogo guardados Mid=%s Far=%s; alvo %d/s -> cvar %d", tostring(O.saved[S2L.OFFT_CV[1]]), tostring(O.saved[S2L.OFFT_CV[2]]), k, want))
        end
        O.want = want
        for _, n in ipairs(S2L.OFFT_CV) do
            local cur = get(n)
            if cur ~= want then
                put(n, want)
                local now = get(n)
                if O.set >= #S2L.OFFT_CV then O.again = O.again + 1 end
                O.set = O.set + 1
                if O.set <= 20 or O.set % 100 == 0 then
                    log(string.format("S2CORE offtick: %s %s -> %s (pedido %d; %s; religado=%d)", n, tostring(cur), tostring(now), want, now == want and "ok" or "NAO MUDOU", O.again))
                end
            end
        end
    else
        for _, n in ipairs(S2L.OFFT_CV) do
            local v = O.saved[n]
            if type(v) == "number" then put(n, v); log(string.format("S2CORE offtick: flag 0 -> %s devolvido %s (agora %s)", n, tostring(v), tostring(get(n)))) end
        end
        O.saved = nil; O.want = nil; O.set = 0
    end
end

-- ===== b118fpscap (flag fpscap, default 0; user rule N16 2026-10-15 "the enemies npcs you can make them as 45fps, every other animation put in
-- 30 fps maximum"). ONE mechanism for every animated body on both PCs: UE's own per-component tick interval (SetComponentTickInterval). The
-- engine's tick scheduler applies it BEFORE the GSC gate (exe fn 0x22ad44e runs inside the component's ExecuteTick and only adds, for an
-- Agent's non-mesh components, its own "summed DeltaTime >= Agent.MinDesiredTickRate" test; the Mesh is exempt). One pass per second:
--   every Agent of this side (game NPCs, the puppet, the hidden target; never the local player): Mesh (= animation, bones) and
--   CharacterMovement (= motion) -> 1/fpsenemy while IsInThreat() (alert/combat; our own bodies never), else 1/fpsother;
--   every mover of the mod (b114 nsmooth PMC/RMC of the puppet and of each client NPC mirror) -> 1/fpsother; a mirror whose host NPC is
--   hostile -> 1/fpsenemy (the host sends "#FE:<object names of its nearest hostile NPCs>" in x1; mirror id = that name).
-- Mirror POSES stay main.lua's 10 Hz bone sets (below both caps). Each component's own interval is saved once and given back when the flag
-- becomes 0; an interval changed by someone else is set again (religado). No cvar is touched. On top of offtick: the GSC gate passes a
-- tick when >= 1/cvar s have summed; offtick >= 60 (cvar >= 80, step <= 12.5 ms) never blocks a 1/45 or 1/30 tick at 47-144 fps; offtick 30
-- (cvar 40, 25 ms) would cut a hostile's CharacterMovement at LOD >= 2 to ~30/s at 60-111 fps (its Mesh stays at 45).
function S2L.fcCap(F, c, w, now)                  -- one component; w = interval in s, nil = give its own back
    if not valid(c) then return end
    local k = S2L.pmAddr(c)
    local R = F.r[k]
    if w == nil then
        if R then pcall(function() c:SetComponentTickInterval(R.sv) end); F.r[k] = nil; F.back = F.back + 1 end
        return
    end
    if not R then
        local sv; pcall(function() sv = c:GetComponentTickInterval() end)
        if type(sv) ~= "number" or math.abs(sv - F.ie) < 1e-4 or math.abs(sv - F.io) < 1e-4 then sv = 0 end   -- state lost after we set it
        R = {sv = sv, chk = now}; F.r[k] = R; F.nr = F.nr + 1
    elseif R.w == w and now - R.chk >= (FL.fpsver or 5) then
        R.chk = now
        local cur; pcall(function() cur = c:GetComponentTickInterval() end)
        if type(cur) == "number" and math.abs(cur - w) > 1e-4 then R.w = nil; F.again = F.again + 1 end
    end
    R.seen = now
    if R.w ~= w and pcall(function() c:SetComponentTickInterval(w) end) then R.w = w; F.set = F.set + 1 end
end
function S2L.fcTick(ST)                           -- tick1000, host and client
    local on = (FL.fpscap or 0) == 1
    local F = ST.fc
    if not on and not F then return end
    local now = os.clock(); local t0 = now
    if not F then F = {r = {}, nr = 0, set = 0, again = 0, back = 0, at = now, fe = "", n = 0, ms = 0, msmax = 0}; ST.fc = F end
    F.ie, F.io = 1 / math.max(1, tonumber(FL.fpsenemy) or 45), 1 / math.max(1, tonumber(FL.fpsother) or 30)
    local me = S2.io and S2.io.pawn
    local meA = valid(me) and S2L.pmAddr(me) or -1
    local own = {}
    for _, B in ipairs({ST.pup or {}, ST.tgt or {}}) do if valid(B.pawn) then own[S2L.pmAddr(B.pawn)] = true end end
    local nA, hos, list = 0, {}, nil
    pcall(function() list = FindAllOf("Agent") end)
    for _, a in ipairs(list or {}) do
        if valid(a) then
            local ad = S2L.pmAddr(a)
            if ad ~= meA then
                local h = false
                if on and not own[ad] then pcall(function() h = a:IsInThreat() == true end) end
                local m, cm
                pcall(function() m = a.Mesh end); pcall(function() cm = a.CharacterMovement end)
                local w = on and (h and F.ie or F.io) or nil
                S2L.fcCap(F, m, w, now); S2L.fcCap(F, cm, w, now)
                nA = nA + 1
                if h then hos[#hos + 1] = a end
            end
        end
    end
    F.fe = ""
    if on and api.ROLE == "host" and #hos > 0 then          -- x1 "#FE": names of the nearest hostile NPCs, for the client's mirrors
        local p; pcall(function() p = me:K2_GetActorLocation() end)
        local t = {}
        for _, a in ipairs(hos) do
            local q, n; pcall(function() q = a:K2_GetActorLocation(); n = a:GetFName():ToString() end)
            if q and type(n) == "string" then t[#t + 1] = {n = (n:gsub("[^%w_]", "")), d = p and ((q.X - p.X) ^ 2 + (q.Y - p.Y) ^ 2) or 0} end
        end
        table.sort(t, function(x, y) return x.d < y.d end)
        local s = {}
        for i = 1, math.min(#t, FL.fpsfe or 12) do s[i] = t[i].n end
        F.fe = table.concat(s, ",")
    end
    local N, nM, nMH = ST.ns, 0, 0
    if N and type(N.k) == "table" then                      -- b114 movers (puppet "pup", mirrors "m:<id>")
        local fe = {}
        if on and api.ROLE ~= "host" then for id in (x1part("FE") or ""):gmatch("[^,]+") do fe[id] = true end end
        for key, K in pairs(N.k) do
            if type(K) == "table" then
                local mir = key:sub(1, 2) == "m:"
                local h = mir and fe[key:sub(3)] == true
                local w = on and (h and F.ie or F.io) or nil
                S2L.fcCap(F, K.pmc, w, now); if K.rmc then S2L.fcCap(F, K.rmc, w, now) end
                if mir then nM = nM + 1; if h then nMH = nMH + 1 end end
            end
        end
    end
    if not on then
        local left = 0; for _ in pairs(F.r) do left = left + 1 end
        log(string.format("S2CORE fpscap: flag 0 -> intervalos devolvidos=%d (sem objeto achado=%d)", F.back, left))
        ST.fc = nil
        return
    end
    local nc = 0
    for k, R in pairs(F.r) do if now - (R.seen or 0) > 60 then F.r[k] = nil else nc = nc + 1 end end
    local ms = (os.clock() - t0) * 1000
    F.n = F.n + 1; F.ms = F.ms + ms; if ms > F.msmax then F.msmax = ms end
    if not F.first then
        F.first = true
        log(string.format("S2CORE fpscap: ligado: inimigos (IsInThreat) %d/s, o resto %d/s; agentes=%d hostis=%d espelhos=%d (hostis %d) componentes=%d",
            math.floor(1 / F.ie + 0.5), math.floor(1 / F.io + 0.5), nA, #hos, nM, nMH, nc))
    end
    if now - F.at >= 10 then
        log(string.format("S2CORE fpscap 10 s: agentes=%d hostis=%d espelhos=%d hostis_espelho=%d componentes=%d novos=%d definidos=%d religado=%d #FE=%d custo med=%.2f max=%.2f ms",
            nA, #hos, nM, nMH, nc, F.nr, F.set, F.again, F.fe == "" and 0 or select(2, F.fe:gsub(",", ",")) + 1, F.ms / math.max(1, F.n), F.msmax))
        F.at, F.nr, F.set, F.n, F.ms, F.msmax = now, 0, 0, 0, 0, 0
    end
end
function S2L.fcTickP(ST)
    local ok, e = pcall(S2L.fcTick, ST)
    if not ok then
        S2.errs.n = S2.errs.n + 1; S2.errs.last = "fcTick: " .. tostring(e):sub(1, 160)
        ST.fcErr = (ST.fcErr or 0) + 1
        if ST.fcErr <= 5 then log("S2CORE fpscap erro: " .. tostring(e)) end
    end
end
function S2L.fcOut(ST)                            -- x1 part (host): "#FE:<names>"
    local F = ST.fc
    if (FL.fpscap or 0) ~= 1 or api.ROLE ~= "host" or not (F and F.fe and F.fe ~= "") then return "" end
    return "#FE:" .. F.fe
end
S2L.fcSnap0 = M.snapExtra                          -- #FE rides on x1 through a wrap (like #SL / #HC)
function M.snapExtra(ST, pawn, p)
    local base = S2L.fcSnap0 and S2L.fcSnap0(ST, pawn, p) or ""
    local ok, s = pcall(S2L.fcOut, ST)
    return (base or "") .. ((ok and type(s) == "string") and s or "")
end
-- ===== end b118fpscap
-- ===== b122npcsync (flag npcsync, default 0; user 2026-10-15 20:35 "the npcs is not synced"). The client keeps hub / quest / vendor NPCs
-- (alifeoff=1 + alifekeephubs/alifekeepquest) and, with aimask=0, they run their OWN brains and drift from the host's. Host master (A-Life,
-- NPC AI): the client shows what the host's NPCs do. HOST (npcsync=1): every snapshot carries the x1 part
--   "#NP:<uid>,<pid>,<name#>,<x>,<y>,<z>,<yaw>,<vx>,<vy>;..."  (integers; cm, deg, cm/s)
-- = up to npsbud live NPCs within npsr cm of EITHER player, the nearest to each player first (interleaved). uid = CppMediator:GetGUID (the
-- A-Life UID, key of the orphan sweep), pid = CppMediator:GetPrototypeID (its 2nd key), name# = numeric suffix of the object name (= main.lua's
-- mirror id). Read once per object; the NPC list is rebuilt every npslist s (refs <= npslist s old, valid() before every use, pool 0,0,0 skipped);
-- vendors stay out on the host (isOwnBody / vendmir: each side keeps its own vendor).
-- CLIENT (npcsync=1, needs nsmooth=1): every npslist s its KEPT NPCs (alive, not hidden by alifeTick, not ours) are paired with host entries:
-- same uid (and the same pid when both are known), else (npsmatch=2) the same pid, nearest within npsmr cm. A paired NPC is TAKEN once: per-NPC
-- brain off (npsai=1: controller BrainComponent:StopLogic + controller tick off + TogglePathFollowingComponent(false) = the pupnoai recipe) and its
-- CharacterMovement stopped + tick off (the pupcmoff recipe, re-checked every s); then every tick it is driven from the host's samples through
-- the b114 per-frame mover (S2L.nsMover / nsSample / nsDrive: one PMC, dead reckoning + error pull, host yaw). Animation (npsanim=1): walk/run
-- clips in the AnimBP slot by the host speed (S2L.pmClips, the puppet's live path); idle = the game's own AnimBP (2 = idle clip too).
-- Released (brain + CMC tick back, mover 0 and off, clip stopped) when the host entry is silent npslost s, the NPC leaves the list, a local
-- dialog / trade uses it (the pawn's GetInteractedActor, or the nearest driven NPC within npsdlgr cm while the dialog view is up; + npsdlghold s:
-- dialog, trade and interaction stay local), beyond npsmax, npcsync 0 or nsmooth 0. A paired NPC that alifeTick hides (left the hub radius)
-- stays driven, so it comes back when the host's does. main.lua's mirror of a paired VISIBLE NPC is dropped from the snapshot it applies
-- (no double body). Non-kept NPCs keep the mirrors; unpaired kept NPCs keep their local brain (aimask decides).
M.NP = {}
function M.NP.st(ST)
    local S = ST.nps
    if not S then
        S = {h = {}, ids = {}, pr = {}, how = {}, pa = {}, dr = {}, cl = {}, bz = {}, F = {}, listAt = -100, hn = 0,
             s = {at = os.clock(), snaps = 0, bytes = 0, sent = 0, n = 0, ms = 0, msmax = 0, drv = 0, es = 0, en = 0, emax = 0, take = 0, rel = 0,
                  back = 0, uid = 0, pid = 0, filt = 0, busy = 0}}
        ST.nps = S
    end
    return S
end
function M.NP.ids(S, o, key, med, now)        -- uid, pid of a live NPC pawn: read once per object (address:name)
    local c = S.ids[key]
    if c then c.at = now; return c end
    c = {uid = 0, pid = 0, at = now}
    if med then
        pcall(function() local u = med:GetGUID(o); if type(u) == "number" then c.uid = math.tointeger(u) or 0 end end)
        pcall(function() local p = med:GetPrototypeID(o); if type(p) == "number" then c.pid = math.tointeger(p) or 0 end end)
    end
    S.ids[key] = c
    return c
end
function M.NP.pawns(ST, S, now, skip)         -- live NPC pawns of this side (one FindAllOf); skip(addr) = true leaves one out before more calls
    local me = S2.io and S2.io.pawn
    local meA = valid(me) and S2L.pmAddr(me) or -1
    local med = StaticFindObject("/Script/Stalker2.Default__CppMediator")
    if not valid(med) then med = nil end
    local out = {}
    for _, o in ipairs(FindAllOf("Pawn") or {}) do pcall(function()
        if not valid(o) then return end
        local ad = o:GetAddress()
        if ad == meA or (skip and skip(ad)) or M.isOwnBody(ST, o) then return end
        local alive = true; pcall(function() alive = o:IsAlive() and true or false end)
        if not alive then return end
        local q = o:K2_GetActorLocation()
        if math.abs(q.X) < 100 and math.abs(q.Y) < 100 then return end          -- the A-Life pool at 0,0,0
        local nm = o:GetFName():ToString()
        local key = tostring(ad) .. ":" .. nm
        local c = M.NP.ids(S, o, key, med, now)
        out[#out + 1] = {o = o, ad = ad, key = key, nm = nm:match("_(%d+)$") or "0", uid = c.uid, pid = c.pid, x = q.X, y = q.Y, z = q.Z}
    end) end
    for k, c in pairs(S.ids) do if now - c.at > 120 then S.ids[k] = nil end end
    return out
end
function M.NP.out(ST, pawn, p)                 -- HOST x1 part "#NP:..." (every snapshot)
    if (FL.npcsync or 0) ~= 1 or api.ROLE ~= "host" then return "" end
    local S = M.NP.st(ST)
    local t0 = os.clock(); local now = t0
    if now - S.listAt >= (FL.npslist or 1) then
        S.listAt = now
        local all = M.NP.pawns(ST, S, now)
        local r = api.getRemote and api.getRemote()
        local R2 = (FL.npsr or 10000) ^ 2
        local A, B = {}, {}
        for _, e in ipairs(all) do
            local dh = p and ((e.x - p.X) ^ 2 + (e.y - p.Y) ^ 2) or 1e18
            local dc = (r and type(r.x) == "number" and type(r.y) == "number") and ((e.x - r.x) ^ 2 + (e.y - r.y) ^ 2) or 1e18
            if dh <= R2 then A[#A + 1] = {e = e, d = dh} end
            if dc <= R2 then B[#B + 1] = {e = e, d = dc} end
        end
        table.sort(A, function(a, b) return a.d < b.d end)
        table.sort(B, function(a, b) return a.d < b.d end)
        local sel, seen, bud = {}, {}, FL.npsbud or 24
        for i = 1, math.max(#A, #B) do
            for _, Lq in ipairs({A, B}) do
                local x = Lq[i]
                if x and not seen[x.e.key] and #sel < bud then seen[x.e.key] = true; sel[#sel + 1] = x.e end
            end
        end
        S.sel, S.nall, S.nA, S.nB = sel, #all, #A, #B
    end
    local parts = {}
    for _, e in ipairs(S.sel or {}) do
        if valid(e.o) then pcall(function()
            local q = e.o:K2_GetActorLocation()
            if math.abs(q.X) < 100 and math.abs(q.Y) < 100 then return end      -- pooled since the list
            local yaw = e.o:K2_GetActorRotation().Yaw
            local vx, vy = 0, 0
            pcall(function() local v = e.o:GetVelocity(); vx, vy = v.X, v.Y end)
            parts[#parts + 1] = string.format("%d,%d,%s,%d,%d,%d,%d,%d,%d", e.uid, e.pid, e.nm, math.floor(q.X + 0.5), math.floor(q.Y + 0.5),
                math.floor(q.Z + 0.5), math.floor(yaw + 0.5), math.floor(vx + 0.5), math.floor(vy + 0.5))
        end) end
    end
    local s = "#NP:" .. table.concat(parts, ";")
    local St = S.s
    St.snaps = St.snaps + 1; St.bytes = St.bytes + #s; St.sent = St.sent + #parts
    local ms = (os.clock() - t0) * 1000; St.ms = St.ms + ms; if ms > St.msmax then St.msmax = ms end
    if now - St.at >= 10 then
        if (FL.npslog or 1) == 1 then
            log(string.format("S2CORE npcsync 10 s (host): npcs_vivos=%d perto_do_host=%d perto_do_cliente=%d enviados_med=%.1f (npsbud %d) bytes_med=%.0f snapshots=%d | custo med=%.2f max=%.2f ms/snapshot",
                S.nall or 0, S.nA or 0, S.nB or 0, St.sent / math.max(1, St.snaps), FL.npsbud or 24, St.bytes / math.max(1, St.snaps), St.snaps, St.ms / math.max(1, St.snaps), St.msmax))
        end
        S.s = {at = now, snaps = 0, bytes = 0, sent = 0, n = 0, ms = 0, msmax = 0, drv = 0, es = 0, en = 0, emax = 0, take = 0, rel = 0, back = 0, uid = 0, pid = 0, filt = 0, busy = 0}
    end
    return s
end
function M.NP.parse(S, now)                    -- CLIENT: the host's #NP of the newest snapshot -> S.h[hkey]
    local head = S2.io and S2.io.head
    if head == S.head then return false end
    S.head = head
    local s = x1part("NP")
    if not s then return false end
    local ts = tonumber((head or ""):match('"t"%s*:%s*([%-%d%.eE%+]+)')) or now
    local n = 0
    for u, pd, nm, x, y, z, yw, vx, vy in s:gmatch("(%-?%d+),(%-?%d+),(%d+),(%-?%d+),(%-?%d+),(%-?%d+),(%-?%d+),(%-?%d+),(%-?%d+)") do
        local k = nm ~= "0" and ("n" .. nm) or ("u" .. u)
        local e = S.h[k] or {}
        e.uid, e.pid, e.nm = tonumber(u), tonumber(pd), nm
        e.x, e.y, e.z, e.yaw, e.vx, e.vy = tonumber(x), tonumber(y), tonumber(z), tonumber(yw), tonumber(vx), tonumber(vy)
        e.t, e.recv = ts, now
        S.h[k] = e; n = n + 1
    end
    for k, e in pairs(S.h) do if now - e.recv > 30 then S.h[k] = nil end end
    S.hn = n
    return true
end
function M.NP.pair(ST, S, now)                 -- CLIENT, every npslist s: kept list + pairs (uid, then prototype + nearest)
    local hid = (ST.alife and ST.alife.hid) or {}
    local all = M.NP.pawns(ST, S, now, function(ad) return hid[ad] and not S.pa[ad] end)
    local cl = {}
    for _, c in ipairs(all) do cl[c.key] = c end
    if rawget(_G, "s2n_nat_listed_addr") then for k, c in pairs(cl) do if s2n_nat_listed_addr(c.ad) == true then cl[k] = nil; if S.pr[k] then S.pr[k] = nil; S.how[k] = nil end; if S.dr[k] then M.NP.rel(ST, S, k, "natnpc lista (b162)", true) end end end end   -- b162 (audit 182)
    S.cl = cl
    local lost = FL.npslost or 2
    for ck, hk in pairs(S.pr) do
        local e = S.h[hk]
        local why = (not cl[ck] and "saiu da lista (morto/sumiu)") or ((not e or now - e.recv > lost) and "o host nao manda mais") or nil
        if why then
            S.pr[ck] = nil; S.how[ck] = nil
            if S.dr[ck] then M.NP.rel(ST, S, ck, why, cl[ck] ~= nil or (rawget(_G, "s2n_nat_listed_addr") ~= nil and S.dr[ck].ad ~= nil and s2n_nat_listed_addr(S.dr[ck].ad) == true)) end   -- b162
        end
    end
    local used = {}
    for ck, hk in pairs(S.pr) do used[hk] = ck end
    local byU = {}
    for hk, e in pairs(S.h) do if not used[hk] and now - e.recv <= lost and (e.uid or 0) > 0 then byU[e.uid] = hk end end
    local free = {}
    for ck, c in pairs(cl) do
        if not S.pr[ck] and not hid[c.ad] then
            local hk = c.uid > 0 and byU[c.uid] or nil
            local e = hk and S.h[hk]
            if e and not used[hk] and (e.pid <= 0 or c.pid <= 0 or e.pid == c.pid) then
                S.pr[ck] = hk; used[hk] = ck; S.how[ck] = "uid"; S.s.uid = S.s.uid + 1
            else free[#free + 1] = c end
        end
    end
    if (FL.npsmatch or 2) >= 2 then
        local cand, R2 = {}, (FL.npsmr or 1500) ^ 2
        for _, c in ipairs(free) do
            if c.pid > 0 then
                for hk, e in pairs(S.h) do
                    if not used[hk] and e.pid == c.pid and now - e.recv <= lost then
                        local d = (e.x - c.x) ^ 2 + (e.y - c.y) ^ 2 + (e.z - c.z) ^ 2
                        if d <= R2 then cand[#cand + 1] = {c = c, hk = hk, d = d} end
                    end
                end
            end
        end
        table.sort(cand, function(a, b) return a.d < b.d end)
        for _, x in ipairs(cand) do
            if not S.pr[x.c.key] and not used[x.hk] then S.pr[x.c.key] = x.hk; used[x.hk] = x.c.key; S.how[x.c.key] = "proto"; S.s.pid = S.s.pid + 1 end
        end
    end
    S.pa = {}
    local np, nk = 0, 0
    for ck, c in pairs(cl) do
        if S.pr[ck] then S.pa[c.ad] = ck; np = np + 1 end
        if not hid[c.ad] then nk = nk + 1 end
        local R = S.dr[ck]
        if R then R.a = c.o end                                       -- the ref of this list (<= npslist s old)
    end
    S.np, S.nk = np, nk
    if not S.pairLog or now - S.pairLog >= 60 or (S.npLast ~= np) then
        S.pairLog = now; S.npLast = np
        local nu, npr = 0, 0
        for _, h in pairs(S.how) do if h == "uid" then nu = nu + 1 else npr = npr + 1 end end
        log(string.format("S2CORE npcsync: pares=%d (uid=%d proto=%d) de %d NPCs mantidos aqui; host mandou %d", np, nu, npr, nk, S.hn or 0))
    end
end
function M.NP.take(ST, S, ck, c, hk, now)      -- CLIENT: brain off per NPC + CMC stopped (once per pairing)
    local a = c.o
    if not (valid(a) and S2L.pmAddr(a) == c.ad) then return nil end
    local R = {a = a, ad = c.ad, ck = ck, hk = hk, t = now, Q = {clipN = 20, clipBad = 0}, r = {}}   -- clipN 20: pmClips logs only every 50th clip (no per-NPC spam)
    local function st(k, f) local ok, e = pcall(f); R.r[#R.r + 1] = k .. "=" .. (ok and "ok" or ("erro " .. tostring(e):sub(1, 50))); return ok end
    if (FL.npsai or 1) == 1 then
        R.ai = true
        local ctl; pcall(function() ctl = a:GetController() end)
        if valid(ctl) then
            R.brain = st("brain", function() local b = ctl.BrainComponent; if not valid(b) then error("sem BrainComponent") end; b:StopLogic("s2coop") end)
            R.ctlTick = st("tickctl", function() ctl:SetActorTickEnabled(false) end)
        else R.r[#R.r + 1] = "controlador=nulo" end
        R.path = st("path", function() a:TogglePathFollowingComponent(false) end)
    end
    local cm; pcall(function() cm = a.CharacterMovement end)
    if valid(cm) then
        pcall(function() cm:StopMovementImmediately() end)
        R.cm = st("cmoff", function() cm:SetComponentTickEnabled(false) end)
    end
    R.cmAt = now
    S.dr[ck] = R; S.s.take = S.s.take + 1
    S.takeN = (S.takeN or 0) + 1
    if S.takeN <= 20 or S.takeN % 50 == 0 then
        log(string.format("S2CORE npcsync: tomado %s (par %s com host %s; #%d) -> %s", c.key:match(":(.*)$") or ck, S.how[ck] or "?", hk, S.takeN, table.concat(R.r, " ")))
    end
    return R
end
function M.NP.rel(ST, S, ck, why, touch)       -- CLIENT: give it back (touch=false: the NPC left the list = dead/gone: only the mover is zeroed)
    local R = S.dr[ck]
    if not R then return end
    S.dr[ck] = nil
    local N, K = ST.ns, R.K
    if N and N.k and R.mk and N.k[R.mk] == K then N.k[R.mk] = nil end
    local a = R.a
    local ok = valid(a) and S2L.pmAddr(a) == R.ad
    if ok and K then pcall(S2L.nsStop, K); pcall(function() K.pmc:Deactivate() end) end
    if ok and touch then
        if R.Q and R.Q.clip then local ai; pcall(function() ai = a.Mesh:GetAnimInstance() end); S2L.pmClipStop(R.Q, valid(ai) and ai or nil) end
        if R.cm then pcall(function() a.CharacterMovement:SetComponentTickEnabled(true) end) end
        if R.ai then
            local ctl; pcall(function() ctl = a:GetController() end)
            if valid(ctl) then
                if R.ctlTick then pcall(function() ctl:SetActorTickEnabled(true) end) end
                if R.brain then pcall(function() ctl.BrainComponent:RestartLogic() end) end
            end
            if R.path then pcall(function() a:TogglePathFollowingComponent(true) end) end
        end
    end
    S.s.rel = S.s.rel + 1
    S.relN = (S.relN or 0) + 1
    if S.relN <= 20 or S.relN % 50 == 0 then log(string.format("S2CORE npcsync: solto %s (%s; #%d)%s", ck:match(":(.*)$") or ck, why, S.relN, (ok and touch) and " cerebro/movimento devolvidos" or "")) end
end
function M.NP.relAll(ST, S, why)
    for ck in pairs(S.dr) do M.NP.rel(ST, S, ck, why, true) end
    S.pr = {}; S.how = {}; S.pa = {}
end
function M.NP.drive(ST, S, R, e, now)          -- CLIENT: one tick of one taken NPC from host entry e (the b114 mover)
    local a = R.a
    if not (valid(a) and S2L.pmAddr(a) == R.ad) then return false end
    local N = S2L.nsSt(ST)
    local mk = "n:" .. R.hk
    local K, new = S2L.nsMover(N, a, mk)
    if not K then return false end
    R.K, R.mk = K, mk
    if new or K.rt ~= e.recv then
        K.rt = e.recv
        local vx, vy = e.vx, e.vy
        if vx * vx + vy * vy < 25 and K.x and K.ts and e.t > K.ts and e.t - K.ts <= 1 then   -- host GetVelocity ~0 but it moved: derive
            local dt = e.t - K.ts
            local dx, dy = (e.x - K.x) / dt, (e.y - K.y) / dt
            if dx * dx + dy * dy >= 900 and dx * dx + dy * dy <= (FL.nsvmax or 1500) ^ 2 then vx, vy = dx, dy end
        end
        S2L.nsSample(K, e.x, e.y, e.z, nil, e.t, e.recv, vx, vy, 0)
        R.sp = math.sqrt(vx * vx + vy * vy)
    end
    K.yaw = e.yaw
    local es0, en0 = N.s.es, N.s.en
    local ok = S2L.nsDrive(N, K, a, now)
    local St = S.s
    if ok and N.s.en > en0 then
        local err = N.s.es - es0
        St.es = St.es + err; St.en = St.en + 1; if err > St.emax then St.emax = err end
        R.err = err
    end
    if ok then St.drv = St.drv + 1 end
    if R.cm and now - (R.cmAt or 0) >= 1 then                          -- the game may switch the CMC tick back on
        R.cmAt = now
        local on = false; pcall(function() on = a.CharacterMovement:IsComponentTickEnabled() end)
        if on then pcall(function() local cm = a.CharacterMovement; cm:StopMovementImmediately(); cm:SetComponentTickEnabled(false) end); St.back = St.back + 1 end
    end
    local an = FL.npsanim or 1
    if an >= 1 then
        local sp = (now - e.recv <= 0.5) and (R.sp or 0) or 0
        local band = (sp < 30 and "idle") or (sp < WALK_MAX and "walk") or "run"
        if band ~= R.band or now - (R.anAt or 0) >= 1 then                 -- the AnimBP is asked only on a band change or once a s
            R.band, R.anAt = band, now
            local ai; pcall(function() ai = a.Mesh:GetAnimInstance() end)
            if valid(ai) then
                if band ~= "idle" or an >= 2 then S2L.pmClips(nil, R.Q, ai, sp)
                elseif R.Q.clip then S2L.pmClipStop(R.Q, ai) end
            end
        elseif R.Q.clip and R.Q.mon and band ~= "idle" then
            local rate = PUP_NAT[band] and math.max(0.6, math.min(1.8, sp / PUP_NAT[band])) or 1
            if math.abs(rate - (R.Q.rate or 1)) > 0.15 then R.Q.rate = rate; pcall(function() a.Mesh:GetAnimInstance():Montage_SetPlayRate(R.Q.mon, rate) end) end
        end
    end
    return ok
end
function M.NP.busy(ST, S, me, now)             -- CLIENT: a local dialog / trade keeps its NPC (dialog, trade, interaction stay local)
    local ia
    pcall(function() local x = me:GetInteractedActor(); if valid(x) then ia = S2L.pmAddr(x) end end)
    local lk
    if S2L.ftLock then local okL, r = pcall(S2L.ftLock, ST, S.F, now); if okL then lk = r end end
    local p
    if lk == "dialogo" then pcall(function() p = me:K2_GetActorLocation() end) end
    local hold, best, bd = FL.npsdlghold or 5, nil, (FL.npsdlgr or 400) ^ 2
    for ck, hk in pairs(S.pr) do
        local c = S.cl[ck]
        if c and ia and c.ad == ia then S.bz[ck] = now + hold end
        local e = S.h[hk]
        local R = S.dr[ck]
        if p and e then
            local x, y = e.x, e.y
            if R and valid(R.a) then pcall(function() local q = R.a:K2_GetActorLocation(); x, y = q.X, q.Y end) end
            local d = (x - p.X) ^ 2 + (y - p.Y) ^ 2
            if d <= bd then best, bd = ck, d end
        end
    end
    if best then S.bz[best] = now + hold end
    for ck, t in pairs(S.bz) do if t < now then S.bz[ck] = nil end end
end
function M.NP.tick(ST)                          -- CLIENT tick100, after nsTick
    local S = ST.nps
    local on = (FL.npcsync or 0) == 1 and api.ROLE ~= "host"
    if not on then if S and next(S.dr) then M.NP.relAll(ST, S, "npcsync=0") end return end
    if (FL.nsmooth or 0) ~= 1 then
        if S and next(S.dr) then M.NP.relAll(ST, S, "nsmooth=0") end
        S = M.NP.st(ST)
        if not S.nsLog then S.nsLog = true; log("S2CORE npcsync: precisa de nsmooth=1 (o movedor por quadro do b114); nada dirigido") end
        return
    end
    S = M.NP.st(ST)
    local t0 = os.clock(); local now = t0
    local me = S2L.ioPawn()
    if not valid(me) then return end
    local meA = S2L.pmAddr(me)
    if S.meA and S.meA ~= meA then                                   -- new local pawn (load): the old world is gone, nothing is touched
        local N = ST.ns
        for _, R in pairs(S.dr) do if N and N.k and R.mk then N.k[R.mk] = nil end end
        S.dr, S.pr, S.how, S.pa, S.cl, S.h, S.bz, S.listAt = {}, {}, {}, {}, {}, {}, {}, -100
        log("S2CORE npcsync: pawn local novo (load) -> pares esquecidos")
    end
    S.meA = meA
    M.NP.parse(S, now)
    if now - S.listAt >= (FL.npslist or 1) then S.listAt = now; M.NP.pair(ST, S, now) end
    M.NP.busy(ST, S, me, now)
    local p; pcall(function() p = me:K2_GetActorLocation() end)
    local L = {}
    for ck, hk in pairs(S.pr) do
        local c, e = S.cl[ck], S.h[hk]
        if c and e then L[#L + 1] = {ck = ck, hk = hk, c = c, e = e, d = p and ((e.x - p.X) ^ 2 + (e.y - p.Y) ^ 2) or 0} end
    end
    table.sort(L, function(x, y) return x.d < y.d end)
    local cap, nb = FL.npsmax or 16, 0
    for i, x in ipairs(L) do
        local R = S.dr[x.ck]
        local busy = (S.bz[x.ck] or 0) > now
        if busy then nb = nb + 1 end
        if busy or i > cap then
            if R then M.NP.rel(ST, S, x.ck, busy and "dialogo/troca local" or "alem de npsmax", true) end
        else
            if not R then R = M.NP.take(ST, S, x.ck, x.c, x.hk, now) end
            if R then pcall(M.NP.drive, ST, S, R, x.e, now) end
        end
    end
    local St = S.s
    St.busy = math.max(St.busy, nb)
    local ms = (os.clock() - t0) * 1000; St.n = St.n + 1; St.ms = St.ms + ms; if ms > St.msmax then St.msmax = ms end
    if now - St.at >= 10 then
        local nd = 0; for _ in pairs(S.dr) do nd = nd + 1 end
        if (FL.npslog or 1) == 1 then
            log(string.format("S2CORE npcsync 10 s: host_manda=%d pares=%d de %d mantidos dirigidos=%d (npsmax %d) novos_uid=%d novos_proto=%d tomados=%d soltos=%d ocupados_max=%d erro_med=%.1f erro_max=%.0f cm cmc_religado=%d espelhos_tirados=%d | custo med=%.2f max=%.2f ms/tick",
                S.hn or 0, S.np or 0, S.nk or 0, nd, cap, St.uid, St.pid, St.take, St.rel, St.busy, St.es / math.max(1, St.en), St.emax, St.back, St.filt, St.ms / math.max(1, St.n), St.msmax))
        end
        S.s = {at = now, snaps = 0, bytes = 0, sent = 0, n = 0, ms = 0, msmax = 0, drv = 0, es = 0, en = 0, emax = 0, take = 0, rel = 0, back = 0, uid = 0, pid = 0, filt = 0, busy = 0}
    end
end
function M.NP.tickP(ST)
    local ok, e = pcall(M.NP.tick, ST)
    if not ok then
        S2.errs.n = S2.errs.n + 1; S2.errs.last = "npcsync: " .. tostring(e):sub(1, 160)
        ST.npsErr = (ST.npsErr or 0) + 1
        if ST.npsErr <= 5 or ST.npsErr % 100 == 0 then log("S2CORE npcsync erro #" .. ST.npsErr .. ": " .. tostring(e)) end
    end
end
function M.NP.filt(ST, t)                      -- CLIENT nsApply: main.lua's mirror of a paired, visible NPC is dropped (no double body)
    local S = ST.nps
    if (FL.npcsync or 0) ~= 1 or api.ROLE == "host" or not S or type(t) ~= "string" or not next(S.pr) then return t end
    local hid = (ST.alife and ST.alife.hid) or {}
    local drop, any = {}, false
    for ck, hk in pairs(S.pr) do
        local e, c = S.h[hk], S.cl[ck]
        if e and e.nm ~= "0" and c and not hid[c.ad] then drop[e.nm] = true; any = true end
    end
    if not any then return t end
    local pos = t:find('"npcs"', 1, true)
    if not pos then return t end
    local n = 0
    local tail = t:sub(pos):gsub('{"id":%s*"([^"]*)"[^{}]*}', function(id)
        local sx = id:match("_(%d+)$")
        if sx and drop[sx] then n = n + 1; return "" end
    end)
    S.s.filt = S.s.filt + n
    return t:sub(1, pos - 1) .. tail
end
S2L.npSnap0 = M.snapExtra                          -- #NP rides on x1 through a wrap (like #SL / #HC / #FE)
function M.snapExtra(ST, pawn, p)
    local base = S2L.npSnap0 and S2L.npSnap0(ST, pawn, p) or ""
    local ok, s = pcall(M.NP.out, ST, pawn, p)
    if not ok then
        ST.npsOutErr = (ST.npsOutErr or 0) + 1
        if ST.npsOutErr <= 5 then log("S2CORE npcsync erro (host): " .. tostring(s)) end
    end
    if (FL.natnpc or 0) == 1 and api.ROLE == "host" and rawget(_G, "s2n_nat_host") then local okN, sN = pcall(function() local r = api.getRemote and api.getRemote(); if not (r and type(r.x) == "number" and type(r.y) == "number") then return "" end; return s2n_nat_host(1, valid(pawn) and pawn:GetAddress() or 0, r.x, r.y, tonumber(r.z) or 0, (ST.pup and valid(ST.pup.pawn)) and ST.pup.pawn:GetAddress() or 0, (ST.tgt and valid(ST.tgt.pawn)) and ST.tgt.pawn:GetAddress() or 0) end); if okN and type(sN) == "string" and sN ~= "" then s = ((ok and type(s) == "string") and s or "") .. sN; ok = true elseif not okN then ST.natErr = (ST.natErr or 0) + 1; if ST.natErr <= 5 then log("S2CORE natnpc erro (host): " .. tostring(sN)) end end end   -- b158natnpc
    return (base or "") .. ((ok and type(s) == "string") and s or "")
end
-- ===== end b122npcsync
-- ===== b125npcmove (flag npsmove, default 1; needs npcsync=1 on both; npsmove is a shared flag: the client takes the host's value). User 2026-10-15:
-- "sync everyone at some distance, and ignoring when idle, when idle the ai from the client can handle and maintain they in place without
-- moving"; "if they move a lot, then you sync, if they stay still too long, do not send sync, if they are too often you snap it"; "this wont
-- work for combat". Live (Zalissya, B): 1-3 of 37 hub NPCs move at once; ~0.08 ms per driven NPC per tick; driven walkers had
-- CMC.Velocity = GetVelocity = 0 (CMC tick off, the PMC moved them) under our unarmed slot clip; idle ones keep the game's own montages.
-- HOST: every snapshot each live NPC within npsr cm of either player is read once (K2_GetActorLocation). It is SENT only while ACTIVE = moving
-- (2D speed > npsv cm/s, or > npsd cm from where it was last sent) or in threat (Agent:IsInThreat, read at each npslist rebuild), held npsstill s
-- after its last movement; then ONE "stopped" entry. Plus npskey calm NPCs per snapshot, round robin, as "stopped" keyframes (pairing + the
-- client's idle check). Entry = b122's 9 fields + state: 0 moving, 1 threat, 2 stopped. Up to npsbud active ones per snapshot, nearest first.
-- CLIENT: a paired NPC is TAKEN only while its entry is active (state 0/1, newer than npslost s); on "stopped" or silence it is RELEASED (brain,
-- CMC tick, path back, no clip) and stays where the client's AI keeps it; at that release, and every npslist s while idle, it is snapped
-- (location + yaw) to the host's last spot when farther than npssnap cm. Pairs live npsidle s without any entry (b122: npslost s).
-- Animation while driven (no S2 slot clip): npsmode 2 = b122's PMC mover + the CMC Velocity/Acceleration fields written for the AnimBP (CMC tick
-- stays off); 1 = the CMC tick ON drives it (bRunPhysicsWithNoController, frictions/braking 0, MovementMode None -> Walking; Velocity +
-- RequestedVelocity = host velocity + error pull; snap > npssnap), the PMC is not used; 0 = b122's PMC mover + npsanim clips (fallback).
-- npsmstop=1: the NPC's own montage (smoke, guitar...) is stopped when it is taken (it walks now). npsmove=0 = b122 unchanged.
M.NP.out0, M.NP.parse0, M.NP.take0, M.NP.rel0, M.NP.tick0 = M.NP.out, M.NP.parse, M.NP.take, M.NP.rel, M.NP.tick
function M.NP.mvOn() return (FL.npsmove or 1) == 1 end
function M.NP.out(ST, pawn, p)                 -- HOST x1 "#NP:..." (npsmove=1: active NPCs + stops + keyframes)
    if not M.NP.mvOn() then return M.NP.out0(ST, pawn, p) end
    if (FL.npcsync or 0) ~= 1 or api.ROLE ~= "host" then return "" end
    local S = M.NP.st(ST)
    local t0 = os.clock(); local now = t0
    if now - S.listAt >= (FL.npslist or 1) then
        S.listAt = now
        local all = M.NP.pawns(ST, S, now)
        local r = api.getRemote and api.getRemote()
        local R2 = (FL.npsr or 15000) ^ 2
        local old, W, L = S.mv or {}, {}, {}
        for _, e in ipairs(all) do
            local dh = p and ((e.x - p.X) ^ 2 + (e.y - p.Y) ^ 2) or 1e18
            local dc = (r and type(r.x) == "number" and type(r.y) == "number") and ((e.x - r.x) ^ 2 + (e.y - r.y) ^ 2) or 1e18
            local d = math.min(dh, dc)
            if d <= R2 then
                local m = old[e.key] or {sx = e.x, sy = e.y}
                m.e, m.d, m.thr = e, d, false
                pcall(function() m.thr = e.o:IsInThreat() and true or false end)
                W[e.key] = m; L[#L + 1] = m
            end
        end
        S.mv, S.mvL, S.nall, S.nA = W, L, #all, #L
    end
    local vt, vd, hold = FL.npsv or 30, (FL.npsd or 50) ^ 2, FL.npsstill or 1
    local act, stop, kf, parts = {}, {}, {}, {}
    local L = S.mvL or {}
    for _, m in ipairs(L) do
        local o = m.e.o
        if valid(o) then pcall(function()
            local q = o:K2_GetActorLocation()
            if math.abs(q.X) < 100 and math.abs(q.Y) < 100 then return end      -- pooled since the list
            local sp, dt = 0, now - (m.pt or -1)
            if m.pt and dt >= 0.03 and dt <= 1 then m.vx, m.vy = (q.X - m.px) / dt, (q.Y - m.py) / dt; sp = math.sqrt(m.vx * m.vx + m.vy * m.vy) else m.vx, m.vy = 0, 0 end
            m.px, m.py, m.pz, m.pt = q.X, q.Y, q.Z, now
            if sp > vt or (q.X - m.sx) ^ 2 + (q.Y - m.sy) ^ 2 > vd or m.thr then m.last = now end
            if m.last and now - m.last <= hold then m.act = true; act[#act + 1] = m
            elseif m.act then m.act = false; stop[#stop + 1] = m end
        end) end
    end
    local function put(m, stt)
        if m.sent == now then return end
        local o = m.e.o
        pcall(function()
            local yaw = o:K2_GetActorRotation().Yaw
            local vx, vy = 0, 0
            if stt ~= 2 then
                pcall(function() local v = o:GetVelocity(); vx, vy = v.X, v.Y end)
                if vx * vx + vy * vy < 25 then vx, vy = m.vx or 0, m.vy or 0 end   -- host GetVelocity ~0 while it moves: the measured one
            end
            parts[#parts + 1] = string.format("%d,%d,%s,%d,%d,%d,%d,%d,%d,%d", m.e.uid, m.e.pid, m.e.nm, math.floor(m.px + 0.5), math.floor(m.py + 0.5),
                math.floor(m.pz + 0.5), math.floor(yaw + 0.5), math.floor(vx + 0.5), math.floor(vy + 0.5), stt)
            m.sx, m.sy, m.sent = m.px, m.py, now
        end)
    end
    if #act > (FL.npsbud or 24) then table.sort(act, function(a, b) return a.d < b.d end) end
    for i, m in ipairs(act) do if i <= (FL.npsbud or 24) then put(m, m.thr and 1 or 0) end end
    for _, m in ipairs(stop) do put(m, 2) end
    for _ = 1, math.min(FL.npskey or 1, #L) do                  -- calm keyframes, round robin (only NPCs read in this snapshot)
        S.ki = (S.ki or 0) % #L + 1
        local m = L[S.ki]
        if m and not m.act and m.pt == now and m.sent ~= now then put(m, 2); kf[#kf + 1] = m end
    end
    local s = "#NP:" .. table.concat(parts, ";")
    local St = S.s
    St.snaps = St.snaps + 1; St.bytes = St.bytes + #s; St.sent = St.sent + #parts
    St.act = (St.act or 0) + #act; St.stop = (St.stop or 0) + #stop; St.key = (St.key or 0) + #kf
    local ms = (os.clock() - t0) * 1000; St.ms = St.ms + ms; if ms > St.msmax then St.msmax = ms end
    if now - St.at >= 10 then
        if (FL.npslog or 1) == 1 then
            local nt = 0; for _, m in ipairs(L) do if m.thr then nt = nt + 1 end end
            log(string.format("S2CORE npcsync 10 s (host, npsmove): npcs_vivos=%d no_raio=%d em_ameaca=%d ativos_med=%.1f paradas=%d chaves=%d enviados_med=%.1f bytes_med=%.0f snapshots=%d | custo med=%.2f max=%.2f ms/snapshot",
                S.nall or 0, S.nA or 0, nt, St.act / math.max(1, St.snaps), St.stop, St.key, St.sent / math.max(1, St.snaps), St.bytes / math.max(1, St.snaps), St.snaps, St.ms / math.max(1, St.snaps), St.msmax))
        end
        S.s = {at = now, snaps = 0, bytes = 0, sent = 0, n = 0, ms = 0, msmax = 0, drv = 0, es = 0, en = 0, emax = 0, take = 0, rel = 0, back = 0, uid = 0, pid = 0, filt = 0, busy = 0}
    end
    return s
end
function M.NP.parse(S, now)                    -- CLIENT: 9 fields (b122 host) or 10 (state); no state = moving
    if not M.NP.mvOn() then return M.NP.parse0(S, now) end
    local head = S2.io and S2.io.head
    if head == S.head then return false end
    S.head = head
    local s = x1part("NP")
    if not s then return false end
    local ts = tonumber((head or ""):match('"t"%s*:%s*([%-%d%.eE%+]+)')) or now
    local n = 0
    for it in s:gmatch("[^;]+") do
        local u, pd, nm, x, y, z, yw, vx, vy, st = it:match("^(%-?%d+),(%-?%d+),(%d+),(%-?%d+),(%-?%d+),(%-?%d+),(%-?%d+),(%-?%d+),(%-?%d+),?(%d*)$")
        if u then
            local k = nm ~= "0" and ("n" .. nm) or ("u" .. u)
            local e = S.h[k] or {}
            e.uid, e.pid, e.nm = tonumber(u), tonumber(pd), nm
            e.x, e.y, e.z, e.yaw, e.vx, e.vy = tonumber(x), tonumber(y), tonumber(z), tonumber(yw), tonumber(vx), tonumber(vy)
            e.st = tonumber(st) or 0
            e.t, e.recv = ts, now
            S.h[k] = e; n = n + 1
        end
    end
    local keep = math.max(FL.npsidle or 60, 30)
    for k, e in pairs(S.h) do if now - e.recv > keep then S.h[k] = nil end end
    S.hn = n
    return true
end
function M.NP.take(ST, S, ck, c, hk, now)      -- b122 take + (npsmove) the NPC's own montage stopped, mode 1: CMC tick back ON with physics
    local R = M.NP.take0(ST, S, ck, c, hk, now)
    if not R or not M.NP.mvOn() then return R end
    local md = FL.npsmode or 2
    R.md = md; S.mvTaken = true
    local a, r = R.a, {}
    if md >= 1 and (FL.npsmstop or 1) == 1 then
        pcall(function()
            local ai = a.Mesh:GetAnimInstance()
            if not valid(ai) then return end
            local mo = ai:GetCurrentActiveMontage()
            if valid(mo) then R.mon0 = mo:GetFName():ToString(); ai:StopAllMontages(0.25); r[#r + 1] = "montagem_parada=" .. R.mon0 end
        end)
    end
    if md == 1 then
        local cm; pcall(function() cm = a.CharacterMovement end)
        if valid(cm) then
            R.ph = {}
            for _, kv in ipairs({{"bRunPhysicsWithNoController", true}, {"GroundFriction", 0.0}, {"BrakingDecelerationWalking", 0.0}, {"BrakingFrictionFactor", 0.0}}) do
                pcall(function() R.ph[kv[1]] = cm[kv[1]] end)
                if not pcall(function() cm[kv[1]] = kv[2] end) then r[#r + 1] = kv[1] .. "=erro" end
            end
            pcall(function() R.mm0 = cm.MovementMode; if R.mm0 == 0 then cm:SetMovementMode(1, 0) end end)
            r[#r + 1] = "cmc_tick=" .. tostring(pcall(function() cm:SetComponentTickEnabled(true) end)) .. " modo=" .. tostring(R.mm0)
            R.cm = nil
        end
    end
    S.mvTakeN = (S.mvTakeN or 0) + 1
    if S.mvTakeN <= 20 or S.mvTakeN % 50 == 0 then log(string.format("S2CORE npcsync: npsmode=%d em %s %s", md, ck:match(":(.*)$") or ck, table.concat(r, " "))) end
    return R
end
function M.NP.rel(ST, S, ck, why, touch)       -- npsmove: CMC fields we wrote are zeroed and the mode-1 physics values restored, then b122 rel
    local R = S.dr[ck]
    if R and R.md and touch and valid(R.a) and S2L.pmAddr(R.a) == R.ad then
        pcall(function()
            local cm = R.a.CharacterMovement
            if not valid(cm) then return end
            for k, v in pairs(R.ph or {}) do pcall(function() cm[k] = v end) end
            if R.mm0 == 0 then pcall(function() cm:SetMovementMode(0, 0) end) end
            pcall(function() cm:StopMovementImmediately() end)
            pcall(function() cm.Acceleration = {X = 0, Y = 0, Z = 0} end)
            pcall(function() cm.bHasRequestedVelocity = false end)
        end)
    end
    if R and S.C then S.C.rel = S.C.rel + 1 end
    return M.NP.rel0(ST, S, ck, why, touch)
end
function M.NP.snap(ST, S, ck, c, e, now)       -- CLIENT: an idle NPC farther than npssnap cm from the host's last spot -> placed there (once)
    local a = c.o
    if not (valid(a) and S2L.pmAddr(a) == c.ad) then return nil end
    local q; pcall(function() q = a:K2_GetActorLocation() end)
    if not q then return nil end
    local d = math.sqrt((q.X - e.x) ^ 2 + (q.Y - e.y) ^ 2 + (q.Z - e.z) ^ 2)
    if d <= (FL.npssnap or 300) then return d end
    pcall(function() a:K2_SetActorLocation({X = e.x, Y = e.y, Z = e.z}, false, {}, true) end)
    pcall(function() a:K2_SetActorRotation({Pitch = 0, Yaw = e.yaw, Roll = 0}, false) end)
    S.C.snap = S.C.snap + 1; S.snapN = (S.snapN or 0) + 1
    if S.snapN <= 20 or S.snapN % 50 == 0 then log(string.format("S2CORE npcsync: parado %s a %.0f cm do lugar do host -> posto la (#%d)", ck:match(":(.*)$") or ck, d, S.snapN)) end
    return 0, d
end
function M.NP.cmDrive(S, R, K, a, cm, now)     -- npsmode 1: the CMC (tick on) carries the host velocity + error pull
    if K.x == nil or not valid(cm) then return false end
    local A; pcall(function() A = a:K2_GetActorLocation() end)
    if not A then return false end
    local dtT = math.min(math.max(now - (K.at or (now - 0.1)), 0.05), 0.3); K.at = now
    local cap = (FL.nscap or 250) / 1000
    local age = math.max(0, now - K.st)
    local ext = math.min(age, cap)
    local tx, ty, tz = K.x + K.vx * ext, K.y + K.vy * ext, K.z
    local ex, ey, ez = tx - A.X, ty - A.Y, tz - A.Z
    local err = math.sqrt(ex * ex + ey * ey + ez * ez)
    if err > (FL.npssnap or 300) then
        a:K2_SetActorLocation({X = tx, Y = ty, Z = tz}, false, {}, true); ex, ey = 0, 0; S.C.snap = S.C.snap + 1
    end
    local f = math.max(0, math.min(1, (cap - age) / dtT))
    local k = math.min(FL.nsgain or 7, 0.9 / dtT)
    local vx, vy = K.vx * f + ex * k, K.vy * f + ey * k
    if (FL.nsnoback or 0) == 1 then vx, vy = S2L.nsNoBack(K, vx, vy, K.dx, K.dy, now) end   -- b148pose
    if K.vx * K.vx + K.vy * K.vy < 25 and ex * ex + ey * ey < (FL.nsdead or 3) ^ 2 then vx, vy = 0, 0 end
    local sp, mx = math.sqrt(vx * vx + vy * vy), FL.nsvmax or 1500
    if sp > mx then vx, vy = vx * mx / sp, vy * mx / sp end
    cm.Velocity = {X = vx, Y = vy, Z = 0}
    pcall(function() cm.RequestedVelocity = {X = vx, Y = vy, Z = 0}; cm.bHasRequestedVelocity = (vx ~= 0 or vy ~= 0) end)
    if K.yaw ~= nil then
        local cur; pcall(function() cur = a:K2_GetActorRotation().Yaw end)
        if cur then
            local d = api.angDiff(K.yaw, cur)
            if math.abs(d) > 0.3 then a:K2_SetActorRotation({Pitch = 0, Yaw = cur + d * (1 - math.exp(-dtT * (FL.nsyawgain or 10))), Roll = 0}, false) end
        end
    end
    return true, err
end
function M.NP.mvDrive(ST, S, R, e, now)        -- CLIENT: one tick of one taken NPC by its npsmode (0 = b122 drive with clips)
    local md = R.md or 0
    if md == 0 then return M.NP.drive(ST, S, R, e, now) end
    local a = R.a
    if not (valid(a) and S2L.pmAddr(a) == R.ad) then return false end
    local K
    if md == 2 then
        local N = S2L.nsSt(ST)
        local mk = "n:" .. R.hk
        local K2, new = S2L.nsMover(N, a, mk)
        if not K2 then return false end
        if new then K2.rt = nil end
        R.K, R.mk, K = K2, mk, K2
    else
        K = R.K1 or {}; R.K1 = K
    end
    local fresh = K.rt ~= e.recv
    if fresh then
        K.rt = e.recv
        local vx, vy = e.vx, e.vy
        if vx * vx + vy * vy < 25 and K.x and K.ts and e.t > K.ts and e.t - K.ts <= 1 then   -- host GetVelocity ~0 but it moved: derive
            local dt = e.t - K.ts
            local dx, dy = (e.x - K.x) / dt, (e.y - K.y) / dt
            if dx * dx + dy * dy >= 900 and dx * dx + dy * dy <= (FL.nsvmax or 1500) ^ 2 then vx, vy = dx, dy end
        end
        S2L.nsSample(K, e.x, e.y, e.z, nil, e.t, e.recv, vx, vy, 0)
        R.sp = math.sqrt(vx * vx + vy * vy)
    end
    K.yaw = e.yaw
    local cm; pcall(function() cm = a.CharacterMovement end)
    local St = S.s
    local ok, err = false, nil
    if md == 2 then
        local N = ST.ns
        local es0, en0 = N.s.es, N.s.en
        ok = S2L.nsDrive(N, K, a, now)
        if ok and N.s.en > en0 then err = N.s.es - es0 end
        local re = false
        if R.cm and now - (R.cmAt or 0) >= 1 then                      -- the game may switch the CMC tick back on (b122)
            R.cmAt = now
            local on = false; pcall(function() on = cm:IsComponentTickEnabled() end)
            if on then pcall(function() cm:StopMovementImmediately(); cm:SetComponentTickEnabled(false) end); St.back = St.back + 1; re = true end
        end
        if valid(cm) and (fresh or re) then                             -- what the AnimBP reads (the CMC tick is off: the fields stay)
            local vx, vy = K.vx or 0, K.vy or 0
            local sp = math.sqrt(vx * vx + vy * vy)
            pcall(function() cm.Velocity = {X = vx, Y = vy, Z = 0} end)
            pcall(function() cm.Acceleration = sp > 1 and {X = vx / sp * 1000, Y = vy / sp * 1000, Z = 0} or {X = 0, Y = 0, Z = 0} end)
        end
    else
        ok, err = M.NP.cmDrive(S, R, K, a, cm, now)
    end
    if ok and err then St.es = St.es + err; St.en = St.en + 1; if err > St.emax then St.emax = err end; R.err = err end
    if ok then St.drv = St.drv + 1 end
    return ok
end
function M.NP.tick(ST)                          -- CLIENT tick100 (npsmove=1: only moving / threat NPCs are driven)
    local S = ST.nps
    if not M.NP.mvOn() then
        if S and S.mvTaken then
            local any = false
            for _, R in pairs(S.dr) do if R.md then any = true end end
            if any then M.NP.relAll(ST, S, "npsmove=0") end
            S.mvTaken = nil
        end
        return M.NP.tick0(ST)
    end
    local on = (FL.npcsync or 0) == 1 and api.ROLE ~= "host"
    if not on then if S and next(S.dr) then M.NP.relAll(ST, S, "npcsync=0") end return end
    if (FL.nsmooth or 0) ~= 1 then
        if S and next(S.dr) then M.NP.relAll(ST, S, "nsmooth=0") end
        S = M.NP.st(ST)
        if not S.nsLog then S.nsLog = true; log("S2CORE npcsync: precisa de nsmooth=1 (o movedor por quadro do b114); nada dirigido") end
        return
    end
    S = M.NP.st(ST)
    S.C = S.C or {n = 0, ms = 0, take = 0, rel = 0, snap = 0, idle = 0}
    local t0 = os.clock(); local now = t0
    local me = S2L.ioPawn()
    if not valid(me) then return end
    local meA = S2L.pmAddr(me)
    if S.meA and S.meA ~= meA then                                   -- new local pawn (load): the old world is gone, nothing is touched
        local N = ST.ns
        for _, R in pairs(S.dr) do if N and N.k and R.mk then N.k[R.mk] = nil end end
        S.dr, S.pr, S.how, S.pa, S.cl, S.h, S.bz, S.listAt = {}, {}, {}, {}, {}, {}, {}, -100
        log("S2CORE npcsync: pawn local novo (load) -> pares esquecidos")
    end
    S.meA = meA
    M.NP.parse(S, now)
    local pass = false
    if now - S.listAt >= (FL.npslist or 1) then                      -- b122 pairing; a pair lives npsidle s without entries (calm NPCs are not sent)
        S.listAt = now; pass = true
        local l0 = FL.npslost
        FL.npslost = math.max(FL.npsidle or 60, l0 or 2)
        local okP, eP = pcall(M.NP.pair, ST, S, now)
        FL.npslost = l0
        if not okP then error(eP) end
    end
    M.NP.busy(ST, S, me, now)
    local p; pcall(function() p = me:K2_GetActorLocation() end)
    local lost, md = FL.npslost or 2, FL.npsmode or 2
    local L = {}
    for ck, hk in pairs(S.pr) do
        local c, e, R = S.cl[ck], S.h[hk], S.dr[ck]
        local live = e and (e.st or 0) ~= 2 and now - e.recv <= lost
        if c and live then L[#L + 1] = {ck = ck, hk = hk, c = c, e = e, d = p and ((e.x - p.X) ^ 2 + (e.y - p.Y) ^ 2) or 0}
        elseif R then
            local busy = (S.bz[ck] or 0) > now
            M.NP.rel(ST, S, ck, (e and e.st == 2) and "parou no host" or "host calado (npslost)", true)
            if c and e and not busy then M.NP.snap(ST, S, ck, c, e, now) end
        end
    end
    table.sort(L, function(x, y) return x.d < y.d end)
    local cap, nb = FL.npsmax or 16, 0
    for i, x in ipairs(L) do
        local R = S.dr[x.ck]
        local busy = (S.bz[x.ck] or 0) > now
        if busy then nb = nb + 1 end
        if R and R.md ~= md and not busy and i <= cap then M.NP.rel(ST, S, x.ck, "npsmode mudou", true); R = nil end
        if busy or i > cap then
            if R then M.NP.rel(ST, S, x.ck, busy and "dialogo/troca local" or "alem de npsmax", true) end
        else
            if not R then R = M.NP.take(ST, S, x.ck, x.c, x.hk, now); if R then S.C.take = S.C.take + 1 end end
            if R then pcall(M.NP.mvDrive, ST, S, R, x.e, now) end
        end
    end
    if pass then                                                      -- idle check: released paired NPCs vs the host's last spot
        local hid = (ST.alife and ST.alife.hid) or {}
        local ds = {}
        for ck, hk in pairs(S.pr) do
            local c, e = S.cl[ck], S.h[hk]
            if c and e and not S.dr[ck] and not hid[c.ad] and (S.bz[ck] or 0) <= now and ((e.st or 0) == 2 or now - e.recv > lost) then
                local d = M.NP.snap(ST, S, ck, c, e, now)
                if d then ds[#ds + 1] = d end
            end
        end
        table.sort(ds)
        S.idd = ds; S.C.idle = S.C.idle + 1
    end
    local St = S.s
    St.busy = math.max(St.busy, nb)
    local ms = (os.clock() - t0) * 1000; St.n = St.n + 1; St.ms = St.ms + ms; if ms > St.msmax then St.msmax = ms end
    S.C.n = S.C.n + 1; S.C.ms = S.C.ms + ms
    if now - St.at >= 10 then
        local nd = 0; for _ in pairs(S.dr) do nd = nd + 1 end
        local ds = S.idd or {}
        if (FL.npslog or 1) == 1 then
            log(string.format("S2CORE npcsync 10 s (npsmove, npsmode=%d): host_manda=%d pares=%d de %d mantidos dirigidos=%d ativos=%d (npsmax %d) tomados=%d soltos=%d ocupados_max=%d erro_med=%.1f erro_max=%.0f cm parados=%d desvio_med=%.0f cm postos_total=%d cmc_religado=%d espelhos_tirados=%d | custo med=%.2f max=%.2f ms/tick",
                md, S.hn or 0, S.np or 0, S.nk or 0, nd, #L, cap, St.take, St.rel, St.busy, St.es / math.max(1, St.en), St.emax, #ds, ds[math.max(1, (#ds + 1) // 2)] or -1,
                S.C.snap, St.back, St.filt, St.ms / math.max(1, St.n), St.msmax))
        end
        S.s = {at = now, snaps = 0, bytes = 0, sent = 0, n = 0, ms = 0, msmax = 0, drv = 0, es = 0, en = 0, emax = 0, take = 0, rel = 0, back = 0, uid = 0, pid = 0, filt = 0, busy = 0}
    end
end
-- ===== end b125npcmove
-- ===== b121aiwatch (flag aiwatch, default 0; HOST only: api.ROLE "host" and alifeoff ~= 1). Live 2026-10-15 15:51-15:54: host 46 NPCs,
-- 0 moved in 4 s (x3); CustomConsoleManagerAM:XActivateAIFeature(4194303) -> 16 moved at once. RE (Stalker2-Win64-Shipping.exe, OFFLINE_REPORT
-- §143): the AI feature switches are ONE bitmask at UAIFeatureManager+0x78 (engine-lifetime object in BP_Global, survives loads); in the
-- whole exe only XDeactivateAIFeature (AND NOT), XActivateAIFeature (OR), the ImGui debug toggle (XOR) and the constructor/Initialize write it;
-- no getter is reflected (no read from Lua). XALifeEnable/XALifeDisable are empty stubs in this build (exec 0x5811306 = P_FINISH; ret).
-- So the watchdog re-asserts the OR blindly (idempotent: bits already on stay on): every aiwatchs s while this player is free (no own
-- cinematic / dialog view / host freeze / dead: S2L.ftLock, b100ft's read; not sleeping: IsSleeping, cineTick0's read), and once at each new
-- local pawn (save load, respawn) as soon as it is free. Logs: first call, each new pawn, each change of the call result or of the wait reason.
function S2L.aiwTick(ST)                             -- tick1000
    if (FL.aiwatch or 0) ~= 1 or FL.alifeoff == 1 or api.ROLE ~= "host" then return end
    local W = ST.aiw
    if not W then W = {n = 0}; ST.aiw = W end
    local pw = S2.io and S2.io.pawn
    if not valid(pw) then return end
    local now = os.clock()
    local ad = pw:GetAddress()
    if W.pawnAddr ~= ad then W.pend = W.pawnAddr and "pawn novo" or "primeira vez"; W.pawnAddr = ad end
    if not W.pend and now - (W.at or -1000) < (tonumber(FL.aiwatchs) or 10) then return end
    local why = S2L.ftLock and S2L.ftLock(ST, W, now) or nil
    if not why then local slp = false; pcall(function() slp = pw:IsSleeping() == true end); if slp then why = "dormindo" end end
    if why then
        if W.why ~= why then W.why = why; log("S2CORE aiwatch: espero (" .. why .. ")" .. (W.pend and (" com " .. W.pend .. " pendente") or "")) end
        return
    end
    local m = tonumber(FL.aiwatchm) or 4194303
    local am, ss = FindFirstOf("CustomConsoleManagerAM"), FindFirstOf("CustomConsoleManagerSS")
    local ok1 = valid(am) and pcall(function() am:XActivateAIFeature(m) end) or false
    local ok2 = valid(ss) and pcall(function() ss:XALifeEnable() end) or false
    W.at = now; W.n = W.n + 1
    local st = (ok1 and "ok" or "ERRO") .. "," .. (ok2 and "ok" or "ERRO")
    local reason = W.pend or (W.why and ("livre de novo depois de " .. W.why)) or (st ~= W.st and "resultado mudou") or nil
    if reason then log(string.format("S2CORE aiwatch: XActivateAIFeature(%d) + XALifeEnable -> %s (%s) #%d", m, st, reason, W.n)) end
    W.st = st; W.pend = nil; W.why = nil
end
-- ===== end b121aiwatch
-- ===== b104rep (flag repsync, default 0; user rule N4 "faction reputation: the client's actions (shooting a neutral/friend) update the
-- host's standing"). HOST ONLY (harmless on the client). The host's standing is the master and the only one that counts: every NPC is
-- real on the host, the client's NPCs are mirrors. A client bullet that hits an NPC ALREADY reaches the host (relayed shot ->
-- M.shotResolve -> ReceiveDamageStruct on the real NPC), so no new message is needed. With repsync=1 that hit is dealt in the HOST
-- player's name (AttackerUID = CppMediator:GetGUID(host pawn); the local player read 0 in nodeath's log), so the game's OWN relation
-- rules (RelationPrototypes.cfg: CharacterReactions + FactionReactions for Damage / Wounded / Kill / KillWounded, FactionsInvolvedDistance
-- 100 m, ReputationRollback) change the host's standing exactly as a host bullet would: every faction and NPC of the whole game, no table.
-- With shotattr=1 too, a small echo hit in the client's hidden target's name follows (NPC still alive, once per NPC per repechocd s), so the
-- victim also fights the client's body (b72's lever). The client's standing: story changes (ChangeRelationships 1,285, ReputationLocker 174,
-- ChangeFaction 28 nodes) reach it through the existing kind-N replay (b16 RelObserver_HostOnly pak on the host + b31 RelReplay_ClientOnly
-- pak on the client, qnode=1); combat-only faction deltas are not copied (no reflected relation reader exists; see OFFLINE_REPORT b104rep).
function S2L.repUid(ST, pawn, best)              -- M.shotResolve (host): the uid this client hit is dealt in, or nil = unchanged
    if FL.repsync ~= 1 or not valid(pawn) or not valid(best) then return nil end
    local R = ST.rep or {n = 0, kill = 0, echo = 0, fail = 0, at = {}, nat = 0}; ST.rep = R
    R.pend = nil
    local pa; pcall(function() pa = pawn:GetAddress() end)
    if pa == nil then return nil end
    if R.pa ~= pa then                            -- a new host pawn (load, respawn): read its uid again
        R.pa = pa; R.uid = nil
        local ok, u = pcall(function() return StaticFindObject("/Script/Stalker2.Default__CppMediator"):GetGUID(pawn) end)
        if ok and type(u) == "number" then
            R.uid = u
            log(string.format("S2CORE rep: tiros do cliente em NPCs contam como do host (uid do host=%d)", u))
        else
            log("S2CORE rep: uid do host ilegivel (" .. tostring(u) .. "); tiros do cliente seguem o caminho antigo")
        end
    end
    if R.uid == nil then return nil end
    local ba; pcall(function() ba = best:GetAddress() end)
    R.pend = {uid = R.uid, ba = ba, at = os.clock(), af = ST.attr and ST.attr.fail or 0}
    return R.uid
end
function S2L.repAfter(ST, best, sid, h0, h1, okD)   -- M.shotResolve (host), after the hit: log, count, echo for the client
    local R = ST.rep; local P = R and R.pend
    if not P then return end
    R.pend = nil
    if not valid(best) then return end
    local ba; pcall(function() ba = best:GetAddress() end)
    if ba ~= P.ba or os.clock() - P.at > 1 then return end
    if not okD or (ST.attr and ST.attr.fail or 0) > P.af then
        R.fail = R.fail + 1
        if R.fail <= 3 then log("S2CORE rep: acerto em nome do host recusado; o tiro do cliente nao contou para a reputacao do host") end
        return
    end
    R.n = R.n + 1
    local alive = type(h1) == "number" and h1 > 0
    local dead = type(h0) == "number" and h0 > 0 and type(h1) == "number" and h1 >= 0 and h1 <= 0.001
    if dead then R.kill = R.kill + 1 end
    if R.n <= 5 or R.n % 50 == 0 or dead then
        log(string.format("S2CORE rep: tiro do cliente em %s contado como do host (uid=%d): hp %.1f -> %.1f%s (%d acertos, %d mortes)",
            tostring(sid), P.uid, tonumber(h0) or -1, tonumber(h1) or -1, dead and S2L.repDeadTxt(best) or "", R.n, R.kill))   -- b177g17
    end
    if FL.shotattr ~= 1 or not alive or (FL.repecho or 0) <= 0 then return end
    local T = ST.tgt
    if not (T and valid(T.pawn) and type(T.uid) == "number") or T.uid == P.uid then return end
    local now, key = os.clock(), tostring(sid)
    if now - (R.at[key] or -1e9) < (FL.repechocd or 5) then return end
    R.nat = R.nat + 1
    if R.nat > 200 then R.at = {}; R.nat = 1 end
    R.at[key] = now
    local ok, e = pcall(function() best:ReceiveDamageStruct({Damage = FL.repecho + 0.0, ArmorDamage = 0.0, ArmorPiercing = 0.0, Bleeding = 0.0,
        BleedingChance = 0.0, AttackerUID = {UID = T.uid}, BoneHit = FName("None"), DamageType = 6, DamageSource = 2}) end)
    if ok then
        R.echo = R.echo + 1
        if R.echo <= 3 or R.echo % 50 == 0 then log(string.format("S2CORE rep: eco em nome do alvo do cliente (uid=%d) em %s: %.1f dano (%d ecos)", T.uid, key, FL.repecho, R.echo)) end
    else
        R.fail = R.fail + 1
        if R.fail <= 3 then log("S2CORE rep: eco recusado: " .. tostring(e)) end
    end
end
-- ===== end b104rep
-- ===== b145aggro (flag repfoe, default 1; HOST; user 2026-10-15 21:00 "the host is more threat than the client, even when the client is
-- right next to them"). Cause (shipped EnemyEvaluatorPrototypes.cfg.bin, parsed by the exe loader at 0x143b549be): each NPC scores every
-- known enemy with penalties DistanceCoeff 0.7 x d/150 m + NoAddedDamageCoeff 0.3 (full when that enemy hurt it < 10 in the last 7 s, none
-- at >= 50) + NotPlayerCoeff 0.15 (anything that is not THE player); lowest wins, ChangeEnemyCooldown 3 s. The client's body is an NPC
-- (+0.15) and with repsync=1 every client bullet is dealt in the HOST's name, so the host also collects the client's damage (-0.3).
-- repfoe=1: a client hit on a victim that is ALREADY hostile (a mutant = any BP_AI_ pawn that is not BP_AI_Human, or a human whose
-- GetFocusedEnemy is the host, the client's hidden target or the client's puppet) is dealt in the hidden target's name, so the victim
-- counts the client's damage and turns on the client's body. Kept in the HOST's name (N4 + kill credit): calm / neutral / friendly humans
-- (focus elsewhere or none: b104rep unchanged), the killing hit (raw damage >= hp), unreadable hp, no / parked target. 2 = log only.
-- The proximity half (an enemy closer to the client picks it without being shot) is data: avatar pak NotPlayerCoeff 0 (b145 cfg diff).
function S2L.foeUid(ST, pawn, best, dmg, h0)      -- M.shotResolve (host): T.uid for a hostile victim's non-lethal client hit, else nil
    local m = FL.repfoe or 0
    if m <= 0 or not valid(best) then return nil end
    local F = ST.foe; if not F then F = {n = 0, calm = 0, kill = 0, mut = 0, hum = 0, skip = 0}; ST.foe = F end
    local T = ST.tgt
    if not (T and valid(T.pawn) and type(T.uid) == "number") or T.parkOn then F.skip = F.skip + 1; return nil end
    local full = ""; pcall(function() full = best:GetFullName() end)
    local kind = S2L.relKind(ST, pawn, best)      -- b177g17 (relfoe=1): string = Enemy of the host, false = not, nil = old focus rule
    if kind ~= nil then
    elseif full:find("BP_AI_", 1, true) and not full:find("BP_AI_Human", 1, true) then kind = "mutante"
    else
        local fa
        pcall(function()
            local f = StaticFindObject("/Script/Stalker2.Default__CppMediator"):GetFocusedEnemy(best)
            if valid(f) then fa = f:GetAddress() end
        end)
        if fa then
            for _, o in ipairs({pawn, T.pawn, ST.pup and ST.pup.pawn}) do
                local a; if valid(o) then pcall(function() a = o:GetAddress() end) end
                if a and a == fa then kind = "humano hostil"; break end
            end
        end
    end
    if not kind then F.calm = F.calm + 1; return nil end
    if not (type(h0) == "number" and h0 > 0 and type(dmg) == "number") or dmg >= h0 then
        F.kill = F.kill + 1
        if F.kill <= 3 or F.kill % 50 == 0 then log(string.format("S2CORE repfoe: golpe final/hp ilegivel em %s fica em nome do host (dano %.1f hp %.1f, %d)", tostring(full:match("[^%.:]+$")), tonumber(dmg) or -1, tonumber(h0) or -1, F.kill)) end
        return nil
    end
    F.n = F.n + 1
    if kind == "mutante" then F.mut = F.mut + 1 else F.hum = F.hum + 1 end
    if F.n <= 5 or F.n % 50 == 0 then
        log(string.format("S2CORE repfoe: acerto do cliente em %s (%s) em nome do alvo uid=%d%s (%d: mutantes=%d humanos=%d calmos=%d finais=%d)",
            tostring(full:match("[^%.:]+$")), kind, T.uid, m == 2 and " [so log]" or "", F.n, F.mut, F.hum, F.calm, F.kill))
    end
    if m ~= 1 then return nil end
    if ST.rep then ST.rep.pend = nil end           -- b104rep's repAfter must not count this hit as the host's
    return T.uid
end
-- ===== end b145aggro
-- ===== b177g17 (flags relfoe / relmir, default 0; HOST; V1 gate G17, rule N4). Measured live 2026-10-16 18:20-18:42 (pair 1 dev):
-- the REAL client shot path already changes the host's standing: client bullet -> 'SHOT real' -> host 'S2CORE rep: tiro do cliente em
-- BP_AI_Human_C_2147443522 contado como do host (uid=0): hp 100.0 -> 87.0' -> the game's relation value of that fresh neutral to the host
-- went 0 -> -300 (Disaffection), read with the exe's own relation function (CustomConsoleManagerAM:GetRelationBetween internals).
-- The blocker was b145's "hostile" test (victim focused on the host / the client's hidden target / puppet): the hidden target (uid 3005,
-- Faction=Player, invulnerable, standing inside every fight) collects its OWN relations, apart from the host's: the local stalkers read
-- -7370 -> -8470 to the target while -300 to the host, so they fight the client's body, b145 calls them hostile and every client bullet
-- on them went in the target's name = the host's standing never moved (the supervisor's neutral: -2720 to the target after one such hit).
-- relfoe=1: "hostile" = the game's own relation victim -> host is Enemy (S2CoopNative s2n_rel; RelationLevelRanges level 0); everything else
--   is dealt in the host's name (b104rep). Without s2n_rel the old focus rule stays.
-- relmir=1: every relmirs s the target's relation to every live BP_AI_ pawn within relmirr cm of it is set to the host's (s2n_rel_copy,
--   only when they differ by more than relmirtol): the client's body is on the host's team, so neutrals stop fighting it.
-- shortcut: the pawn scan is Lua (one FindAllOf per relmirs s, at most 60 uids), upgrade: move it into S2CoopNative's natnpc list.
function S2L.relHost(ST, pawn)                       -- the host player's uid (b104rep's cached one, else GetGUID)
    local hu = ST.rep and ST.rep.uid
    if type(hu) == "number" then return hu end
    local ok, u = pcall(function() return StaticFindObject("/Script/Stalker2.Default__CppMediator"):GetGUID(pawn) end)
    if ok and type(u) == "number" then return u end
    return nil
end
function S2L.relKind(ST, pawn, best)                 -- S2L.foeUid: string = hostile to the host, false = not, nil = no reader (old rule)
    if (FL.relfoe or 0) ~= 1 or not valid(best) then return nil end
    local f = rawget(_G, "s2n_rel")
    if type(f) ~= "function" then return nil end
    local hu = S2L.relHost(ST, pawn)
    local ok, bu = pcall(function() return StaticFindObject("/Script/Stalker2.Default__CppMediator"):GetGUID(best) end)
    if not (hu and ok and type(bu) == "number") then return nil end
    local okR, v, lev = pcall(f, bu, hu)
    if not okR or type(v) ~= "number" then return nil end
    local K = ST.relk or {n = 0, foe = 0}; ST.relk = K
    K.n = K.n + 1
    if lev == 0 then K.foe = K.foe + 1 end
    if K.n <= 5 or K.n % 50 == 0 then
        log(string.format("S2CORE relfoe: vitima uid=%d -> host uid=%d relacao %d nivel %s -> %s (%d lidos, %d inimigos)", bu, hu, v, tostring(lev),
            lev == 0 and "nome do alvo do cliente" or "nome do host", K.n, K.foe))
    end
    if lev == 0 then return "inimigo do host rel=" .. v end
    return false
end
function S2L.repDeadTxt(o)                           -- S2L.repAfter: hp 0 is death OR the game's wounded state (measured: a wounded NPC reads
    local w, fw = false, false                       -- IsAlive=false hp=0 wnd=true, then its squad heals it: 1486 'MORTO' at 18:14, alive hp 7-11 later)
    pcall(function() w = o:IsWounded() == true end)
    pcall(function() fw = o:IsFallingWounded() == true end)
    if w or fw then return " FERIDO (estado ferido do jogo; morre se levar outro tiro)" end
    return " MORTO"
end
function S2L.relMirTick(ST)                          -- tick1000 (host): the client's hidden target takes the host's relations
    if (FL.relmir or 0) ~= 1 or api.ROLE ~= "host" then return end
    local f = rawget(_G, "s2n_rel_copy")
    local R = ST.relm; if not R then R = {n = 0, set = 0, at = -1e9}; ST.relm = R end
    if type(f) ~= "function" then
        if not R.noFn then R.noFn = true; log("S2CORE relmir: s2n_rel_copy ausente (S2CoopNative sem b177); desligado") end
        return
    end
    local now = os.clock()
    if now - R.at < (FL.relmirs or 2) then return end
    R.at = now
    local T = ST.tgt
    if not (T and valid(T.pawn) and type(T.uid) == "number") or T.parkOn then return end
    local pw = S2.io and S2.io.pawn
    if not valid(pw) then return end
    local hu = S2L.relHost(ST, pw)
    if not hu or hu == T.uid then return end
    local med = StaticFindObject("/Script/Stalker2.Default__CppMediator")
    local tp = T.pawn:K2_GetActorLocation()
    local r2 = (FL.relmirr or 6000) ^ 2
    local P = ST.pup and ST.pup.pawn
    local list = {}
    for _, o in ipairs(FindAllOf("Pawn") or {}) do
        if #list >= 60 then break end
        pcall(function()
            if o == pw or o == T.pawn or o == P or not o:IsValid() or not o:IsAlive() then return end
            if not o:GetFullName():find("BP_AI_", 1, true) then return end
            local q = o:K2_GetActorLocation()
            if (q.X - tp.X) ^ 2 + (q.Y - tp.Y) ^ 2 > r2 then return end
            local u = med:GetGUID(o)
            if type(u) == "number" and u ~= hu and u ~= T.uid then list[#list + 1] = u end
        end)
    end
    if #list == 0 then return end
    local ok, n, txt = pcall(f, hu, T.uid, FL.relmirtol or 50, table.unpack(list))
    R.n = R.n + 1
    if not ok then
        R.err = (R.err or 0) + 1
        if R.err <= 3 then log("S2CORE relmir: erro " .. tostring(n)) end
        return
    end
    if type(n) == "number" and n > 0 then
        R.set = R.set + n
        if R.set <= 20 or R.n % 30 == 0 then log(string.format("S2CORE relmir: alvo do cliente uid=%d recebeu a relacao do host uid=%d com %d NPCs (%s; total %d)", T.uid, hu, n, tostring(txt), R.set)) end
    end
end
-- ===== end b177g17
-- ===== b133head (flag headsafe, default 1; HOST, main.lua look=1). User 2026-10-15: "some npcs with headgear get their head missing on
-- client". The client's NPC mirror (main.lua applyMirrors: Actor + PoseableMeshComponent) gets exactly the skeletal parts the host's
-- appearance scan sends. main.lua scanLook (host) walks the NPC's SkeletalMeshComponents in component order and STOPS AT 12 parts
-- (`#parts >= 12`, batch 53). Armoured stalkers have 13-18 parts (object dump: 8 of 22 live BP_AI_Human, all 5 with headgear among them),
-- and a head_with_face look (helmet / gas mask / balaclava: SK_fac_XX_mas_01_a + SK_fac_20_00_mas_01_b + SK_hea_* parts under the body
-- mesh, no own face component) puts its head pieces among the body parts, so the cap can drop the head while keeping the rest.
-- Whole family, one rule: every look is sent complete (up to headmax parts) and HEAD FIRST: rank 0 = mesh path has "/_fac/" (every face /
-- head_with_face mesh of the game), rank 1 = headgear prefix S2L.HL_GEAR (gen_head_table.py from the 930 MeshGenerators in use), rank 2 =
-- the rest, component order kept inside a rank. How: main.lua's scanLook is a local; P2.readOne holds it as an upvalue (P2 reached by
-- debug.getupvalue like b131 mslot, measured live 18:05 "P2 do main.lua achado"). hlTick swaps that upvalue for a wrapper: main's own
-- scanLook runs first (its cache, materials, statics, weapon key, log), then the parts string of a NEW cache entry is rebuilt here with
-- main's own per-part format (path@mats@name@socket) and the entry's hash key is dropped so readOne re-hashes it (new look = new app,
-- the client recreates the mirror by itself). Flag 0 = main's scanLook put back and the fixed cache entries dropped (old 12-part lists).
S2L.HL_GEAR = {"sk_fol_", "sk_gla_", "sk_hea_", "sk_mas_"}   -- gen_head_table.py: prefixes used more by the head slots (Cap Gla Hea Head Mas + face companions) than by all others
function S2L.hlRank(path)                            -- 0 head, 1 headgear, 2 other
    local p = string.lower(tostring(path))
    if p:find("/_fac/", 1, true) then return 0 end
    local nm = p:match("([^/]+)$") or p
    nm = nm:match("^([^%.]+)") or nm
    for _, g in ipairs(S2L.HL_GEAR) do if nm:sub(1, #g) == g then return 1 end end
    return 2
end
function S2L.hlParts(o, mc, asset)                   -- every skeletal part of the pawn in main.lua's format, head first; + the list
    local list, mca = {}, mc:GetAddress()
    for _, pc in ipairs(api.compList(o, "/Script/Engine.SkeletalMeshComponent") or {}) do pcall(function()
        if not valid(pc) or pc:GetAddress() == mca then return end
        local a = pc:GetSkeletalMeshAsset()
        if not valid(a) then return end
        local path = (a:GetFullName():gsub("^%S+ ", ""))
        if path == asset then return end
        local nm = pc:GetFName():ToString()
        if nm == "ShadowMesh" then return end
        local sock, lead = "", false                 -- main.lua v25: a hand weapon (socket on the body, no leader) keeps its socket
        pcall(function() local l = pc.LeaderPoseComponent; local g = l; pcall(function() g = l:get() end); lead = valid(g) end)
        if not lead then pcall(function()
            local par = pc:GetAttachParent()
            local sn = pc:GetAttachSocketName():ToString()
            if valid(par) and par:GetAddress() == mca and sn ~= "None" and sn ~= "" then sock = sn end
        end) end
        list[#list + 1] = {r = S2L.hlRank(path), i = #list + 1, s = path .. "@" .. table.concat(api.matsOf(pc), ",") .. "@" .. nm .. "@" .. sock}
    end) end
    table.sort(list, function(x, y) if x.r ~= y.r then return x.r < y.r end return x.i < y.i end)
    local out = {}
    for k, e in ipairs(list) do if k > (FL.headmax or 32) then break end; out[k] = e.s end
    return table.concat(out, "|"), list
end
function S2L.hlLook(H, o, mc, asset, sid)            -- the wrapper body (main.lua calls it as scanLook(o, mc, asset, sid))
    local p, m, s = H.orig(o, mc, asset, sid)
    if (FL.headsafe or 0) ~= 1 or not H.lc then return p, m, s end
    local c = H.lc[sid]
    if type(c) ~= "table" or c.parts ~= p or c.hl then return p, m, s end   -- not main's entry, or already rebuilt (cache hit)
    c.hl = true
    local ok, np, list = pcall(S2L.hlParts, o, mc, asset)
    if not (ok and type(np) == "string") then H.err = (H.err or 0) + 1; if H.err <= 5 then log("S2CORE headsafe erro: " .. tostring(np)) end return p, m, s end
    if np == p then return p, m, s end
    local old = (p == "") and 0 or select(2, p:gsub("|", "")) + 1
    if #list < old then                              -- never fewer parts than main (a part failed here only): keep main's list
        H.err = (H.err or 0) + 1; if H.err <= 5 then log("S2CORE headsafe erro: " .. tostring(sid) .. " " .. #list .. " partes < " .. old .. " do main; lista do main mantida") end
        return p, m, s
    end
    c.parts = np; c.h = nil                          -- readOne re-hashes the entry: new app for the client
    H.n = H.n + 1
    if H.n <= 30 or H.n % 50 == 0 then
        local hp, gp, cut = {}, {}, 0
        for _, e in ipairs(list) do
            if e.r == 0 then hp[#hp + 1] = e.i elseif e.r == 1 then gp[#gp + 1] = e.i end
            if e.r < 2 and e.i > 12 then cut = cut + 1 end
        end
        log(string.format("S2CORE headsafe: %s partes %d -> %d (cabeca na pos %s, equipamento de cabeca na pos %s de %d na ordem do main; %d delas alem da 12a = cortadas antes) #%d",
            tostring(sid), old, math.min(#list, FL.headmax or 32), #hp > 0 and table.concat(hp, ",") or "-", #gp > 0 and table.concat(gp, ",") or "-", #list, cut, H.n))
    end
    return np, m, s
end
function S2L.hlTick(ST)                              -- tick1000: install / remove the wrapper on the host
    local on = (FL.headsafe or 0) == 1 and api.ROLE == "host"
    local H = ST.hl
    if not H then
        if not on then return end
        H = {n = 0}; ST.hl = H
    end
    if H.P2 == nil then
        local p2 = S2L.nsUp(api.lookPut, "P2")
        H.P2 = (type(p2) == "table" and type(p2.readOne) == "function") and p2 or false
    end
    local dg = rawget(_G, "debug")
    local P = H.P2
    if not (P and dg and dg.getupvalue and dg.setupvalue) then
        if not H.miss then H.miss = true; log("S2CORE headsafe: P2.readOne / debug.setupvalue do main.lua NAO achados (" .. tostring(P and true) .. "," .. type(dg) .. "); lista antiga de 12 partes") end
        return
    end
    local idx, cur
    for i = 1, 255 do
        local n, v = dg.getupvalue(P.readOne, i)
        if n == nil then break end
        if n == "scanLook" then idx, cur = i, v; break end
    end
    if not idx or type(cur) ~= "function" then
        if not H.miss then H.miss = true; log("S2CORE headsafe: scanLook NAO achado em P2.readOne; lista antiga de 12 partes") end
        return
    end
    if cur ~= H.w and cur ~= H.orig then H.orig = cur; H.lc = S2L.nsUp(cur, "lookCache") end   -- main's own scanLook (first sight)
    if on then
        if type(H.lc) ~= "table" then
            if not H.miss then H.miss = true; log("S2CORE headsafe: lookCache do main.lua NAO achado; lista antiga de 12 partes") end
            return
        end
        if cur ~= H.w or H.wm ~= M then              -- first install, or a core reload (the old wrapper calls the old module)
            local w = function(o, mc, asset, sid) return S2L.hlLook(H, o, mc, asset, sid) end
            dg.setupvalue(P.readOne, idx, w)
            H.w, H.wm, H.miss = w, M, nil
            log("S2CORE headsafe: scanLook do main.lua trocado (partes completas ate headmax=" .. tostring(FL.headmax or 32) .. ", cabeca primeiro)")
        end
    elseif H.orig and cur ~= H.orig then
        dg.setupvalue(P.readOne, idx, H.orig)
        local d = 0
        for sid, c in pairs(H.lc or {}) do if type(c) == "table" and c.hl then H.lc[sid] = nil; d = d + 1 end end
        H.w, H.wm = nil, nil
        log("S2CORE headsafe: flag 0 -> scanLook original do main.lua devolvido (" .. d .. " aparencias refeitas pelo main)")
    end
end
-- ===== end b133head
-- ===== b153offscreen (flags offanim / offvb, default 1; HOST only). User 2026-10-15: NPCs the host does not look at freeze (the Mesh
-- stays fixed in the world while the capsule walks, no animation) -> the client's mirrors slide. RE (exe): the agent manager (fn 0x24f4ea4)
-- marks an Agent "render irrelevant" off screen and then holds its Mesh off the capsule's child transform updates; an Agent is relevant
-- anyway once its irrelevant time reaches Agent.GuaranteedRelevancyUpdateTime (game value 20 s, measured).
-- offanim: every MIRRORED NPC (main.lua P2.near, first nsmax; humans and mutants alike, any pawn that has the field) gets
--   GuaranteedRelevancyUpdateTime 0 = always relevant (the mesh follows the capsule).
-- offvb: its Mesh.VisibilityBasedAnimTickOption 0 = the pose ticks off screen (live S1 23:4x: transform with L1, pose only with VBATO 0).
-- Original values kept by actor path in ST.offa (survives reloads) and put back when the NPC leaves the set, dies, offanim = 0 or the
-- role is not host; offvb = 0 puts back only VBATO. One FindAllOf("Pawn") per second (far pawns only when a kept path was not seen
-- near); no UObject kept across ticks. Called from M.tick1000 by one line. 10 s line "S2CORE offanim 10 s".
M.FLAGS.offanim = M.FLAGS.offanim or 1
M.FLAGS.offvb = M.FLAGS.offvb or 1
M.FLAGS.natnpc = M.FLAGS.natnpc or 0   -- b158natnpc: 1 = the host AI drives native client NPCs (S2CoopNative s2n_nat.cpp) instead of mirrors
M.FLAGS.b168off = M.FLAGS.b168off or 0   -- b168: 1 = natnpc combat look off (S2CoopNative natcb 0: no weapon / stance / montage mirror)
function S2L.offaTick(ST)
    local ok, e = pcall(S2L.offaRun, ST)
    if not ok then
        ST.offaErr = (ST.offaErr or 0) + 1
        if ST.offaErr <= 5 then log("S2CORE offanim erro: " .. tostring(e)) end
    end
end
function S2L.offaPut(o, A)                              -- the NPC's own values back (live pawn of this tick)
    if A.g ~= nil then pcall(function() o.GuaranteedRelevancyUpdateTime = A.g end) end
    if A.vbset and A.vb ~= nil then pcall(function() o.Mesh.VisibilityBasedAnimTickOption = A.vb end) end
    A.vbset = false
end
function S2L.offaRun(ST)
    local now = os.clock()
    local O = ST.offa
    if not O then O = {ap = {}, nog = {}, logAt = now, want = 0, n = {on = 0, rs = 0, nog = 0, gone = 0, runs = 0, ms = 0, mx = 0}}; ST.offa = O end
    local want, nw = {}, 0
    if FL.offanim == 1 and api.ROLE == "host" and FL.scan == 1 then
        local P = ST.msl and ST.msl.P2
        if not P then P = S2L.nsUp(api.lookPut, "P2") end
        if type(P) == "table" and type(P.near) == "table" then
            for _, e in ipairs(P.near) do
                if nw >= (FL.nsmax or 8) then break end
                if type(e) == "table" and type(e.path) == "string" and not want[e.path] then want[e.path] = true; nw = nw + 1 end
            end
        end
    end
    O.want = nw
    if nw == 0 and next(O.ap) == nil then return end
    local t0 = os.clock()
    local vb = FL.offvb == 1
    local function one(o, path)
        local A = O.ap[path]
        if want[path] then
            local alive = true
            pcall(function() alive = o:IsAlive() == true end)
            if alive then
                if not A then
                    if O.nog[path] then return end
                    local g
                    pcall(function() g = o.GuaranteedRelevancyUpdateTime end)
                    if type(g) ~= "number" then O.nog[path] = now; O.n.nog = O.n.nog + 1; return end   -- no field: left alone
                    A = {g = g, cls = "?"}
                    pcall(function() A.cls = o:GetClass():GetFName():ToString() end)
                    O.ap[path] = A; O.n.on = O.n.on + 1
                end
                A.seen = now
                pcall(function() if o.GuaranteedRelevancyUpdateTime ~= 0 then o.GuaranteedRelevancyUpdateTime = 0 end end)
                if vb then
                    pcall(function()
                        local m = o.Mesh
                        if not valid(m) then return end
                        local cur = m.VisibilityBasedAnimTickOption
                        if not A.vbset then A.vb = cur; A.vbset = true end
                        if cur ~= 0 then m.VisibilityBasedAnimTickOption = 0 end
                    end)
                elseif A.vbset then
                    pcall(function() o.Mesh.VisibilityBasedAnimTickOption = A.vb end)
                    A.vbset = false
                end
                return
            end
        end
        if A then S2L.offaPut(o, A); O.ap[path] = nil; O.n.rs = O.n.rs + 1 end
    end
    local list = FindAllOf("Pawn") or {}
    local hp = api.getLocal and api.getLocal()
    local R2 = ((FL.scanr or 2000) + 1500) ^ 2
    local found, far = {}, {}
    for _, o in ipairs(list) do
        if valid(o) then
            pcall(function()
                if hp then
                    local q = o:K2_GetActorLocation()
                    if (q.X - hp.X) ^ 2 + (q.Y - hp.Y) ^ 2 > R2 then far[#far + 1] = o; return end
                end
                local path = (o:GetFullName():gsub("^%S+ ", ""))
                found[path] = true
                one(o, path)
            end)
        end
    end
    local miss = false
    for path in pairs(O.ap) do if not found[path] then miss = true; break end end
    if miss then
        for _, o in ipairs(far) do
            if valid(o) then
                pcall(function()
                    local path = (o:GetFullName():gsub("^%S+ ", ""))
                    if O.ap[path] then found[path] = true; one(o, path) end
                end)
            end
        end
    end
    for path, A in pairs(O.ap) do
        if not found[path] and now - (A.seen or now) >= 30 then O.ap[path] = nil; O.n.gone = O.n.gone + 1 end   -- actor gone (its values with it)
    end
    for path, at in pairs(O.nog) do if now - at >= 60 then O.nog[path] = nil end end
    local ms = (os.clock() - t0) * 1000
    local N = O.n
    N.runs, N.ms = N.runs + 1, N.ms + ms
    if ms > N.mx then N.mx = ms end
    if now - O.logAt >= 10 then
        local k, cl = 0, {}
        for _, A in pairs(O.ap) do k = k + 1; cl[A.cls] = (cl[A.cls] or 0) + 1 end
        local cs = {}
        for c, x in pairs(cl) do cs[#cs + 1] = c .. "x" .. x end
        table.sort(cs)
        log(string.format("S2CORE offanim 10 s: espelhos=%d com_alavanca=%d novos=%d devolvidos=%d sem_campo=%d sumidos=%d offvb=%s classes=%s custo med=%.2f max=%.2f ms",
            O.want, k, N.on, N.rs, N.nog, N.gone, tostring(FL.offvb), table.concat(cs, ",", 1, math.min(#cs, 6)), N.ms / math.max(1, N.runs), N.mx))
        O.logAt, O.n = now, {on = 0, rs = 0, nog = 0, gone = 0, runs = 0, ms = 0, mx = 0}
    end
end
-- ===== end b153offscreen
-- ===== b131combat (F4 combat sync; flags mslot / shotlag / tgtpark, default 1). Host is master of NPC combat; the client sees the
-- host's NPCs as mirrors (main.lua scan, maxnpcs slots) and its shots are rays the host resolves (M.shotResolve).
-- F1 mslot (host, scan=1): main.lua filled the maxnpcs mirror slots with the NPCs nearest the HOST, corpses and calm hub NPCs included,
--    so a live enemy fighting the CLIENT 30 m away from the host had no mirror (it hit the client through the hidden target, the client
--    could not see or shoot it). msTick rebuilds main.lua's P2.near every mslotsec s with main's own filters and ranks: alive in threat
--    within mslr cm of the client, alive within mslr, corpses within mslr, the rest; nearest to the client first. main.lua's own 2 s rebuild
--    is held off by P2.listAt (it comes back 2 s after msTick stops); scanCache entries outside the set are aged out (t = -100).
-- F2 shotlag: a client shot is tested against the NPC capsule (radius / half height + shotpad, never smaller than the old 45 / 95 cm)
--    at its position now and 1/3, 2/3, 3/3 of shotlag ms back along its velocity (the client aimed at a mirror that lags the host);
--    head x6 / legs x0.7 only for BP_AI_Human (a mutant centre is not a human pelvis). shotlag 0 = the old test.
-- F3 tgtpark: while the hidden target is parked (remote far, core-21 / out of the world, kzsafe) its HP reference is dropped, so the hits
--    it took at the old spot are not sent as one phantom hit when the remote comes back. ST.tgt.cyc = ring of the last 10 lifecycle events.
function S2L.msRank(e)                                 -- sort key of one candidate (class, then 2D distance to the client, then id)
    return (e.hc or e.cls) * 1e12 + math.min(e.hd or e.dc, 9.99e11)   -- b149hitch: hc/hd = incumbent keys (mshys)
end
function S2L.msTick(ST)
    if (FL.mslot or 0) ~= 1 or api.ROLE ~= "host" or FL.scan ~= 1 then return end
    local S = ST.msl
    if not S then S = {at = -10, logAt = os.clock(), runs = 0, aged = 0, last = {}, cnt = {0, 0, 0, 0}}; ST.msl = S end
    if S.P2 == nil then
        local p2 = S2L.nsUp(api.lookPut, "P2")
        S.P2 = (type(p2) == "table" and type(p2.near) == "table" and type(p2.readOne) == "function") and p2 or false
        S.sc = S.P2 and S2L.nsUp(S.P2.readOne, "scanCache") or nil
        log("S2CORE mslot: P2 do main.lua " .. (S.P2 and "achado" or "NAO achado (escolha antiga)") .. "; scanCache " .. (type(S.sc) == "table" and "achado" or "NAO achado"))
    end
    local P = S.P2
    if not P then return end
    local now = os.clock()
    P.listAt = now                                     -- main.lua's 2 s rebuild stays off while this runs
    if now - S.at < (FL.mslotsec or 2) then return end
    S.at = now
    local hp = api.getLocal and api.getLocal()
    if not hp then return end
    local me, av = S2L.ioPawn(), api.getAvatar and api.getAvatar()
    local r = api.getRemote and api.getRemote()
    local cx, cy = hp.X, hp.Y                          -- reference: the client (fresh snapshot), else the host
    if r and type(r.x) == "number" and now - (r.recv or -1e9) < 5 then cx, cy = r.x, r.y end
    local R2, L2 = (FL.scanr or 2000) ^ 2, (FL.mslr or 7000) ^ 2
    local near, list = {}, FindAllOf("Pawn") or {}
    P.seen = P.seen or {}
    for _, o in ipairs(list) do
        pcall(function()
            if not valid(o) or o == me or o == av or M.isOwnBody(ST, o) then return end
            local q = o:K2_GetActorLocation()
            local dh = (q.X - hp.X) ^ 2 + (q.Y - hp.Y) ^ 2
            if dh > R2 then return end
            local full = o:GetFullName()
            local path = (full:gsub("^%S+ ", ""))
            if P.bad and P.bad[path] and now - P.bad[path] < 10 then return end
            P.seen[path] = P.seen[path] or now
            local e = {o = o, d2 = dh, path = path, sid = full:match("[^%.]+$"), dc = (q.X - cx) ^ 2 + (q.Y - cy) ^ 2, cls = 3}
            if e.dc <= L2 then
                local alive = true
                pcall(function() alive = o:IsAlive() == true end)
                if not alive then e.cls = 2
                else
                    e.cls = 1
                    pcall(function() if o:IsInThreat() == true then e.cls = 0 end end)
                end
            end
            near[#near + 1] = e
        end)
    end
    if (FL.mshys or 0) > 0 then S2L.msHys(S, near, now) end   -- b149hitch
    table.sort(near, function(a, b) local ka, kb = S2L.msRank(a), S2L.msRank(b); if ka ~= kb then return ka < kb end; return a.sid < b.sid end)
    for k = #near, (FL.maxnpcs or 4) + 1, -1 do near[k] = nil end
    P.near = near
    S2L.msInc(S, near, now)
    for path, at in pairs(P.bad or {}) do if now - at >= 10 then P.bad[path] = nil end end      -- main.lua's own prunes (its rebuild is held off)
    for path, at in pairs(P.seen) do if now - at >= 600 then P.seen[path] = nil end end
    S.runs = S.runs + 1
    local keep, last = {}, {}
    for _, e in ipairs(near) do keep[e.sid] = true; last[#last + 1] = {sid = e.sid, cls = e.cls, dc = math.sqrt(e.dc) / 100}; S.cnt[e.cls + 1] = S.cnt[e.cls + 1] + 1 end
    S.last = last
    if type(S.sc) == "table" then
        for id, c in pairs(S.sc) do if not keep[id] and type(c) == "table" and (c.t or 0) > -100 then c.t = -100; S.aged = S.aged + 1 end end   -- main.lua drops them (and their look cache) at its next send
    end
    if now - S.logAt >= 10 then
        local C = S.cnt
        log(string.format("S2CORE mslot 10 s: escolhas=%d vagas=%d (ameaca %d, vivos %d, corpos %d, longe %d no total) envelhecidos=%d ref=%s",
            S.runs, #near, C[1], C[2], C[3], C[4], S.aged, (cx == hp.X and cy == hp.Y) and "host" or "cliente"))
        S.logAt, S.runs, S.aged, S.cnt = now, 0, 0, {0, 0, 0, 0}
    end
end
function S2L.shHit(o, ox, oy, oz, dx, dy, dz)          -- shotlag: distance along the ray and height above the centre, or nil
    local q = o:K2_GetActorLocation()
    local t0 = (q.X - ox) * dx + (q.Y - oy) * dy + (q.Z - oz) * dz
    if t0 <= -1000 or t0 > 31000 then return nil end
    if (ox + dx * t0 - q.X) ^ 2 + (oy + dy * t0 - q.Y) ^ 2 + (oz + dz * t0 - q.Z) ^ 2 > 800 * 800 then return nil end   -- far from the ray: no extra reads
    local rr, hh = 0, 0
    pcall(function() local c = o.CapsuleComponent; rr = c:GetScaledCapsuleRadius(); hh = c:GetScaledCapsuleHalfHeight() end)
    local pad = FL.shotpad or 10
    rr = math.max(45, (tonumber(rr) or 0) + pad); hh = math.max(95, (tonumber(hh) or 0) + pad)
    local vx, vy, vz = 0, 0, 0
    pcall(function() local v = o:GetVelocity(); vx, vy, vz = v.X or 0, v.Y or 0, v.Z or 0 end)
    local L = (FL.shotlag or 0) / 1000
    local bt, bh, bd = nil, 0, nil
    for k = 0, 3 do
        local px, py, pz = q.X - vx * L * k / 3, q.Y - vy * L * k / 3, q.Z - vz * L * k / 3
        local t = (px - ox) * dx + (py - oy) * dy + (pz - oz) * dz
        if t > 0 and t <= 30000 then
            local hx, hy, hz = ox + dx * t, oy + dy * t, oz + dz * t
            local d = (hx - px) ^ 2 + (hy - py) ^ 2
            if d <= rr * rr and math.abs(hz - pz) <= hh and (not bd or d < bd) then bt, bh, bd = t, hz - pz, d end
        end
    end
    if bt and not tostring(o:GetFullName()):find("BP_AI_Human", 1, true) then bh = 0 end   -- mutants: body coefficient 1, bone None
    return bt, bh
end
function S2L.tgNote(ST, why, hp)                        -- tgtpark: lifecycle ring of the hidden target (read by the R0 probe)
    local T = ST.tgt; if not T then return end
    T.cyc = T.cyc or {}
    T.cyc[#T.cyc + 1] = {t = os.clock(), why = why, hp = hp or -1}
    if #T.cyc > 10 then table.remove(T.cyc, 1) end
    T.cycN = (T.cycN or 0) + 1
    if T.cycN <= 20 or T.cycN % 50 == 0 then log(string.format("S2CORE tgtcyc: %s hp=%.1f (#%d)", tostring(why), tonumber(hp) or -1, T.cycN)) end
end
function S2L.tgPark(ST, T, why)                         -- tgtpark: the parked target keeps no HP reference
    if (FL.tgtpark or 0) ~= 1 or not T then return end
    if not T.parkOn then T.parkOn = why; pcall(S2L.tgNote, ST, "parado:" .. why, -1) end
    if T.ref then T.parkDrop = (T.parkDrop or 0) + 1 end
    T.ref = nil
end
function S2L.tgBack(ST, T)
    if T and T.parkOn then T.parkOn = nil; pcall(S2L.tgNote, ST, "volta", -1) end
end
-- ===== end b131combat
-- ===== host: resolve a relayed shot (copy of main.lua v27 logic; the remote player's own body is never a target) =====
function M.shotResolve(ST, str, pawn)
    local v = {}
    for num in str:gmatch("[^;]+") do v[#v + 1] = tonumber(num) end
    if #v < 8 then return end
    -- fixA: ST.shotSeen[seq] = hora em que o seq foi visto pela ultima vez; repetido so se visto ha menos de 5 s (igual ao fix_a do main.lua).
    -- Antes: true para sempre, e os tiros de quem relancava o jogo (seq volta a 1) eram ignorados.
    local nowS, seen = os.clock(), ST.shotSeen
    if nowS - (seen.pruneAt or -10) >= 1 then
        seen.pruneAt = nowS
        for k, at in pairs(seen) do if type(k) == "number" and (at == true or nowS - at > 5) then seen[k] = nil end end
    end
    local seenAt = seen[v[1]]
    seen[v[1]] = nowS
    if seenAt then return end
    if FL.cine == 1 and ST.cv and ST.cv.on then ST.cv.shots = (ST.cv.shots or 0) + 1; return end   -- batch 12: host in a cinematic: the shot is dropped
    local ox, oy, oz, dx, dy, dz, dmg = v[2], v[3], v[4], v[5], v[6], v[7], v[8]
    local body = ST.pup and ST.pup.pawn
    local body2 = ST.tgt and ST.tgt.pawn
    local best, bt, bh, n = nil, 1e18, 0, 0
    for _, o in ipairs(FindAllOf("Pawn") or {}) do
        pcall(function()
            if not o:IsValid() or o == pawn or o == body or o == body2 then return end
            if not o:GetFullName():find("BP_AI_", 1, true) or not o:IsAlive() then return end   -- batch 53: humans and mutants
            n = n + 1
            if (FL.shotlag or 0) > 0 then                 -- b131combat F2 (shotlag): capsule + recent path
                local tt, th = S2L.shHit(o, ox, oy, oz, dx, dy, dz)
                if tt and tt < bt then best, bt, bh = o, tt, th end
                return
            end
            local q = o:K2_GetActorLocation()
            local t = (q.X - ox) * dx + (q.Y - oy) * dy + (q.Z - oz) * dz
            if t <= 0 or t > 30000 then return end
            local hx, hy, hz = ox + dx * t, oy + dy * t, oz + dz * t
            local dh = math.sqrt((hx - q.X)^2 + (hy - q.Y)^2)
            if dh <= 45 and math.abs(hz - q.Z) <= 95 and t < bt then best, bt, bh = o, t, hz - q.Z end
        end)
    end
    if not best then log(string.format("SHOT seq=%d hit=nenhum (testados=%d)", v[1], n)); return end
    do local okCh, denyCh = pcall(S2L.chShotDeny, ST, best); if okCh and denyCh then return end end   -- b91choice (N3)
    local sid = best:GetFullName():match("[^%.]+$")
    local h0, h1 = -1, -1
    pcall(function() h0 = best:GetHP() end)
    local bone = bh > 60 and "jnt_head" or "None"
    local okD, eD
    if #v >= 12 then
        local ap, eff, drop, minmod = v[9], v[10], v[11], v[12]
        local fall = 1.0
        if bt >= eff + drop then fall = minmod elseif bt > eff and drop > 0 then fall = 1.0 - (1.0 - minmod) * (bt - eff) / drop end
        local coef = 1.0
        if bh > 60 then coef = 6.0 elseif bh < -10 then coef = 0.7; bone = "jnt_l_leg" end
        dmg = dmg * fall * coef * (v[13] or 1.0)
        local uid = FL.shotattr == 1 and ST.tgt and ST.tgt.pawn and ST.tgt.uid or nil   -- b72: the NPC learns who hit it
        do local okR, rU = pcall(S2L.repUid, ST, pawn, best); if okR and rU then uid = rU end end   -- b104rep (repsync): dealt in the host's name
        do local okF, fU = pcall(S2L.foeUid, ST, pawn, best, dmg, h0); if okF and fU then uid = fU end end   -- b145aggro (repfoe): hostile victim -> client's body
        if uid then
            okD, eD = pcall(function() best:ReceiveDamageStruct({Damage = dmg, ArmorDamage = 0.0, ArmorPiercing = ap, Bleeding = 0.0, BleedingChance = 0.0, AttackerUID = {UID = uid}, BoneHit = FName(bone), DamageType = 6, DamageSource = 2}) end)
            local A = ST.attr or {ok = 0, fail = 0}; ST.attr = A
            if okD then A.ok = A.ok + 1; if A.ok <= 5 or A.ok % 50 == 0 then log(string.format("S2CORE shotattr: acerto do cliente em nome do alvo uid=%s (%d)", tostring(uid), A.ok)) end
            else A.fail = A.fail + 1; if A.fail <= 3 then log("S2CORE shotattr: AttackerUID recusado, chamada antiga: " .. tostring(eD)) end end
        end
        if not uid or not okD then
            okD, eD = pcall(function() best:ReceiveDamageStruct({Damage = dmg, ArmorDamage = 0.0, ArmorPiercing = ap, Bleeding = 0.0, BleedingChance = 0.0, BoneHit = FName(bone), DamageType = 6, DamageSource = 2}) end)
        end
    else
        okD, eD = pcall(function() best:ReceiveDamage(dmg + 0.0, 6, 0.0, 0.0, 0.0, 0.0, FName(bone)) end)
    end
    pcall(function() h1 = best:GetHP() end)
    pcall(S2L.repAfter, ST, best, sid, h0, h1, okD)   -- b104rep (repsync): count, log, echo for the client
    log(string.format("SHOT(core) seq=%d hit=%s dist=%.1fm altura=%.0f osso=%s dano=%.1f hp %.1f -> %.1f ok=%s %s", v[1], sid, bt / 100, bh, bone, dmg, h0, h1, tostring(okD), okD and "" or tostring(eD)))
end

-- ===== b88nopause (flag nopause, default 0). User rule N8 (2026-10-15): neither player can pause the game. Measured: the Esc menu / P
-- sets IsGamePaused=true; SetGamePaused(world, false) un-pauses it with the menu still open and usable. The game pauses from C++ (no
-- hookable UFunction), so tick100 polls IsGamePaused (one call per 100 ms) and un-pauses, leaving loading, the first npwait s of a new
-- pawn, death (screen + our nodeath flow) and game cinematics alone.
function S2L.noPause(ST)
    if (FL.nopause or 0) ~= 1 then return end
    local N = ST.np; if not N then N = {n = 0, sup = 0, logAt = -1e9, hits = {}}; ST.np = N end
    local now = os.clock()
    local pw = cvPawn()
    if not valid(pw) then N.addr = nil; return end                     -- loading screen / main menu: never touched
    local addr = pw:GetAddress()
    if N.addr ~= addr then N.addr = addr; N.at = now end               -- new pawn (load / respawn): let it settle first
    if now - N.at < (FL.npwait or 3) or now < (N.holdUntil or 0) then return end
    if not valid(S2L.npGs) then pcall(function() S2L.npGs = StaticFindObject("/Script/Engine.Default__GameplayStatics") end) end   -- a CDO: never pooled
    local gs, w = S2L.npGs, nil
    if not valid(gs) then return end
    pcall(function() w = pw:GetWorld() end)
    if not valid(w) or gs:IsGamePaused(w) ~= true then return end
    if ST.nd and ST.nd.dead then return end                            -- our nodeath death flow
    local alive, hp = true, 1
    pcall(function() alive = pw:IsAlive() and true or false end); pcall(function() hp = pw:GetHP() end)
    if not alive or (type(hp) == "number" and hp <= 0) then return end -- the death screen: the game's own flow
    local cin = false
    pcall(function() local pc = api.UEHelpers.GetPlayerController(); cin = pc.bInCinematicSequence == true and pc:IsMoveInputIgnored() == true end)
    if cin then return end                                             -- a game cinematic: its pause stays
    local h = {}
    for _, x in ipairs(N.hits) do if now - x < 5 then h[#h + 1] = x end end
    h[#h + 1] = now; N.hits = h
    if #h > (FL.npmax or 10) then
        N.holdUntil = now + 30; N.hits = {}
        log("S2CORE nopause: o jogo pausou " .. #h .. " vezes em 5 s -> nao despauso por 30 s")
        return
    end
    local ok, r = pcall(function() return gs:SetGamePaused(w, false) end)
    local still = true; pcall(function() still = gs:IsGamePaused(w) == true end)
    N.n = N.n + 1
    if now - N.logAt >= 5 then
        log(string.format("S2CORE nopause: jogo pausado -> despausado #%d ok=%s ret=%s ainda_pausado=%s (+%d sem log)", N.n, tostring(ok), tostring(r), tostring(still), N.sup))
        N.logAt = now; N.sup = 0
    else N.sup = N.sup + 1 end
end

M.keys = {}
M.hooks = {
    ["/Script/Stalker2.ItemAppearanceComponent:OnShotSoundStart"] = function(ST, self)
        if FL.tgtsafe == 1 or FL.tgtdiag == 1 then ST.anyShotAt = os.clock(); ST.anyShots = (ST.anyShots or 0) + 1 end   -- b72: a shot was heard
        if self == nil then return end                 -- b139 hookgt: deferred call from the ticker (the hook fired off the game thread)
        local lp = S2.io and S2.io.pawn
        if not valid(lp) then return end
        local ow = self:get():GetOwner()
        if not (ow and ow:GetAddress() == lp:GetAddress()) then return end
        local cm = api.UEHelpers.GetPlayerController().PlayerCameraManager
        local l, r = cm:GetCameraLocation(), cm:GetCameraRotation()
        local cp, yw = math.cos(math.rad(r.Pitch)), math.rad(r.Yaw)
        ST.lshot = {at = os.clock(), ox = l.X, oy = l.Y, oz = l.Z, dx = cp * math.cos(yw), dy = cp * math.sin(yw), dz = math.sin(math.rad(r.Pitch))}
        ST.lshots = (ST.lshots or 0) + 1
    end,
}
-- ===== b138loot (flag cloot, default 1, BOTH roles; user 2026-10-15 19:25 "theres no option to loot on the client side"). A corpse of an NPC
-- (human or mutant) the HOST killed is only a main.lua PoseableMesh mirror on the client: nothing there to open. One rule for the whole game:
-- the client gets a LOOT BODY there = our avatar prototype clsid (S2Coop_RemoteTarget: empty item generator, max HP 5000) spawned at the
-- corpse, adopted, put on the corpse spot, killed (DC:XKillNpcByUID), filled with the host's list by its UID (RK:XCreateItemInInventoryByID)
-- and counted; its mesh is hidden while main.lua mirrors that corpse (clvis=0), so F there opens the game's own loot window.
-- HOST every 1 s: dead pawns within clsend cm of the client -> "#CL:<hid>,<x>,<y>,<z>,<uid>,<ver>,<o>,<sid*n.sid*n|->;..." (hid = object name =
--   main.lua mirror id; LOOT_SIDS counts; quest items (s2u_qitems) stay with the host; o = 1 while the host has it open). One corpse is
--   recounted every clrc s, and at once when the host closes it -> ver+1.
-- CLIENT: one loot body per listed corpse within clr cm (non-empty list, at most clmax, one spawn at a time); on window close the body is
--   counted -> changed = "#CLT:<id>,<hid>,<ver>,<list>" (resent until the host's ver passes it). HOST: ver matches -> canon = client list (+ the
--   host's quest items), ver+1, the real corpse takes no interaction any more (switched) and the host gets its own hidden loot body with canon.
--   A changed canon rebuilds the other side's body (host list wins; a take on an old ver is refused).
-- LOCK (N15 block up front): a body / corpse takes no interaction while the other side has it open ("#CO:<hid>" / o=1), while its list is stale
--   or the link is silent; opened anyway in the same instant -> the second one (the client on a tie) gets CloseLootMenu.
-- clcopy=1 (client): its own live copy of that NPC (same A-Life UID, within 50 m) is killed (story NPCs, S2L.chProt: not) and made inert
--   (hidden, no collision, no interaction): no second live NPC, no second roll. The core-16 G path (#C / LOOT_REQ) is off while cloot=1.
-- Loot bodies are own bodies (isOwnBody: no mirror, no alife hide, no npcsync, no lock / taken keys); orphSweep and new puppet / target
-- requests wait while one is being made; a leftover from a save (dead, max HP 5000, not ours) is made inert twice per world (+5 s, +35 s).
function S2L.clN(t) local n = 0; for _ in pairs(t or {}) do n = n + 1 end return n end
function S2L.clSt(ST)                                        -- state; a new world (ST.wg.key) forgets everything without touching it
    local C = ST.cl
    local wk = ST.wg and ST.wg.key
    if C and C.wk ~= wk then
        if S2L.clN(C.B) + S2L.clN(C.K) > 0 then log(string.format("S2CORE cloot: mundo novo -> %d corpos de saque e %d corpos do host esquecidos sem tocar", S2L.clN(C.B), S2L.clN(C.K))) end
        C = nil
    end
    if not C then
        C = {wk = wk, K = {}, R = {}, B = {}, own = {}, T = {}, done = {}, cp = {}, fail = {}, lsb = {}, seq = 0, lt = 0, ht = 0, gap = 0,
             nonce = tostring(math.floor(os.clock() * 1000) % 1000000),
             s = {at = os.clock(), spawn = 0, live = 0, drop = 0, fail = 0, take = 0, tin = 0, trej = 0, rc = 0, deny = 0, copy = 0, inert = 0, ms = 0}}
        ST.cl = C
    end
    return C
end
function S2L.clEnc(t)                                        -- {sid = n} -> "sid*n.sid*n" (sorted) or "-"
    local r = {}
    for s, n in pairs(t or {}) do if n > 0 then r[#r + 1] = s .. "*" .. math.floor(n) end end
    table.sort(r)
    return #r > 0 and table.concat(r, ".") or "-"
end
function S2L.clDec(s)
    local t = {}
    for sid, n in (s or ""):gmatch("([%w_]+)%*(%d+)") do t[sid] = (t[sid] or 0) + tonumber(n) end
    return t
end
function S2L.clAdd(a, b) local t = {}; for s, n in pairs(a or {}) do t[s] = n end; for s, n in pairs(b or {}) do t[s] = (t[s] or 0) + n end return t end
function S2L.clMed() if not valid(S2L.clM) then S2L.clM = StaticFindObject("/Script/Stalker2.Default__CppMediator") end return S2L.clM end
function S2L.clCount(o, quest)                               -- corpse PAWN items: LOOT_SIDS -> t; quest items (s2u_qitems) -> q (quest = host)
    local med, t, q = S2L.clMed(), {}, {}
    local QT = quest and S2L.qiTab() or nil
    local QS = QT and QT.S or {}
    for _, s in ipairs(LOOT_SIDS) do
        local ok, n = pcall(function() return med:GetItemsCountInInventory(o, s) end)
        if ok and type(n) == "number" and n > 0 then if QS[s] then q[s] = n else t[s] = n end end
    end
    if QT then for _, s in ipairs(QT.list) do if t[s] == nil and q[s] == nil then
        local ok, n = pcall(function() return med:GetItemsCountInInventory(o, s) end)
        if ok and type(n) == "number" and n > 0 then q[s] = n end
    end end end
    return t, q
end
function S2L.clMap(C, now)                                   -- address -> pawn, one FindAllOf per tick (C.map is dropped at the tick end)
    if C.map and C.mapAt == now then return C.map end
    local m = {}
    for _, o in ipairs(FindAllOf("Pawn") or {}) do pcall(function() if o:IsValid() then m[o:GetAddress()] = o end end) end
    C.map, C.mapAt = m, now
    return m
end
function S2L.clGet(C, now, ad, nm)                           -- the pawn at that address if it still has that name
    local o = S2L.clMap(C, now)[ad]
    if not valid(o) then return nil end
    local ok, n = pcall(function() return o:GetFName():ToString() end)
    if ok and n == nm then return o end
    return nil
end
function S2L.clInt(o, on)                                    -- every InteractionComponent of the actor on/off; returns how many changed
    local list, n = {}, 0
    pcall(function() list = api.compList(o, "/Script/Stalker2.InteractionComponent") or {} end)
    if #list == 0 then pcall(function() local c = o.NPCComponent; if valid(c) then list = {c} end end) end
    for _, c in ipairs(list) do pcall(function()
        if valid(c) and (c.bIsInteractionActive == true) ~= on then c:SetInteractionActive(on); n = n + 1 end
    end) end
    return n
end
function S2L.clInert(o)                                      -- hidden, no collision, no interaction
    pcall(function() o:SetActorHiddenInGame(true) end)
    pcall(function() o:SetActorEnableCollision(false) end)
    S2L.clInt(o, false)
end
function S2L.clOwn(ST, o)                                    -- isOwnBody: one of our loot bodies (address + name)
    local C = ST.cl
    if not C or next(C.own) == nil then return false end
    local hit = false
    pcall(function() if o:IsValid() then local nm = C.own[o:GetAddress()]; hit = nm ~= nil and nm == o:GetFName():ToString() end end)
    return hit
end
function S2L.clIs(ST, pw)                                    -- lockTick: the open loot window is on one of our loot bodies
    if (FL.cloot or 0) ~= 1 or not ST.cl then return false end
    local a; pcall(function() a = pw:GetInteractedActor() end)
    return valid(a) and S2L.clOwn(ST, a) or false
end
function S2L.clMirs(ST)                                      -- main.lua's mirrors table (by host object name), or nil if unknown
    local mt = ST.ns and ST.ns.mt
    if type(mt) == "table" then return mt end
    if S2L.clMT == nil then
        local am = S2L.nsUp and S2L.nsUp(api.mirrorsClear, "applyMirrors")
        local m = S2L.nsUp and S2L.nsUp(am, "mirrors")
        S2L.clMT = type(m) == "table" and m or false
    end
    return S2L.clMT or nil
end
function S2L.clMeshHide(ST, hid)                             -- clvis=0: host bodies always, client bodies while a mirror shows that corpse
    if (FL.clvis or 0) ~= 0 then return false end
    if api.ROLE == "host" then return true end
    if rawget(_G, "s2n_nat_dead") and s2n_nat_dead(hid) == true then return true end   -- b158natnpc: a native corpse shows this host corpse
    if (FL.clmir or 1) == 1 then return false end   -- b141c: the client shows the loot body and hides its mirror instead (user: hide the mirror, not the lootable body)
    local mt = S2L.clMirs(ST)
    if not mt then return true end
    return mt[hid] ~= nil
end
function S2L.clClose(lp, C, hid, why)                        -- block up front: the loot window that should not be open is closed
    local ok = pcall(function() lp:CloseLootMenu() end)
    if not ok then pcall(function() FindFirstOf("PlayerController"):CloseLootMenu() end) end
    C.s.deny = C.s.deny + 1
    local now = os.clock()
    if now - (C.denyLog or -1e9) > 2 then C.denyLog = now; log("S2CORE cloot: janela de saque de " .. hid .. " fechada aqui (" .. why .. ")") end
end
function S2L.clOpenAddr(lp)                                  -- address of the actor whose loot window is open, else nil
    local vis = false
    pcall(function() local w = FindFirstOf("InventoryNew"); vis = w ~= nil and w:IsValid() and w:IsVisible() and true or false end)
    if not vis then return nil end
    local ad; pcall(function() local a = lp:GetInteractedActor(); if a and a:IsValid() then ad = a:GetAddress() end end)
    return ad
end
function S2L.clMirHide(ST, hid, B, on)                      -- b141c: one body per corpse on the client: a ready loot body hides its mirror; dropped -> mirror back
    if not on then
        if B.mirA then pcall(function() if B.mirA:IsValid() then B.mirA:SetActorHiddenInGame(false) end end); B.mirA = nil end
        return
    end
    local mt = S2L.clMirs(ST); local m = mt and mt[hid]
    local a = type(m) == "table" and m.actor or nil
    if a and valid(a) then
        if B.mirA and B.mirA ~= a then pcall(function() if B.mirA:IsValid() then B.mirA:SetActorHiddenInGame(false) end end) end
        B.mirA = a
        local h; pcall(function() h = a.bHidden end)
        if not h then pcall(function() a:SetActorHiddenInGame(true) end) end
    end
end
function S2L.clDrop(ST, C, hid, B, now, why)                 -- a loot body goes: inert at once + AI2:XDestroyModel(uid); address stays own
    local o = S2L.clGet(C, now, B.ad, B.nm)
    if o then
        S2L.clInert(o)
        if B.uid then pcall(function() FindFirstOf("CustomConsoleManagerAI2"):XDestroyModel(B.uid) end) end
    end
    S2L.clMirHide(ST, hid, B, false)
    if C.B[hid] == B then C.B[hid] = nil end
    C.s.drop = C.s.drop + 1
    if FL.cllog == 1 then log("S2CORE cloot: corpo de saque de " .. hid .. " largado (" .. why .. ")") end
end
function S2L.clSpawn(ST, C, hid, W, now)                     -- one request: clsid at the corpse (+50 cm), adopted by clStep
    do local okC, cx, cy, cz = pcall(rawget(_G, "s2n_nat_corpse") or function() end, hid); if okC and type(cx) == "number" and type(cz) == "number" then W = {x = cx, y = cy, z = cz, str = W.str, ver = W.ver, block = W.block} end end   -- b162: on the native corpse
    local before = {}
    for _, o in ipairs(FindAllOf("BP_AI_Human_C") or {}) do pcall(function() if o:IsValid() then before[tostring(o:GetAddress()) .. "|" .. o:GetFName():ToString()] = true end end) end
    C.gap = now + 1.5
    C.s.spawn = C.s.spawn + 1
    local ok, e = pcall(function() FindFirstOf("CustomConsoleManagerRK"):XSpawnObjBySID(FL.clsid, 1, false, W.x, W.y, W.z + 50, 0.0, 0.0, 0.0) end)
    if not ok then C.fail[hid] = now + 10; C.s.fail = C.s.fail + 1; log("S2CORE cloot: XSpawnObjBySID " .. tostring(FL.clsid) .. " falhou: " .. tostring(e)); return end
    C.pend = {hid = hid, st = "ask", at = now, x = W.x, y = W.y, z = W.z, str = W.str, ver = W.ver, before = before}
end
function S2L.clStep(ST, C, lp, now)                          -- the body being made: adopt -> kill -> fill -> count (light tick, 0.2 s)
    local P = C.pend
    local hid = P.hid
    if P.st == "ask" then
        if now - P.at > 6 then
            C.pend = nil; C.fail[hid] = now + 10; C.s.fail = C.s.fail + 1; log("S2CORE cloot: nenhum corpo novo em 6 s para " .. hid)
            if (FL.cllate or 1) == 1 and P.before then       -- b181: the body may still spawn later: watch this spot for 60 s
                C.late = C.late or {}; C.late[#C.late + 1] = {hid = hid, before = P.before, x = P.x, y = P.y, to = now + 60}
                if #C.late > 4 then table.remove(C.late, 1) end
            end
            return
        end
        if now < (P.nx or 0) then return end
        P.nx = now + 0.5
        local best, bd
        local T, U = ST.tgt or {}, ST.pup or {}
        for _, o in ipairs(FindAllOf("BP_AI_Human_C") or {}) do pcall(function()
            if not o:IsValid() or o == T.pawn or o == U.pawn then return end
            local k = tostring(o:GetAddress()) .. "|" .. o:GetFName():ToString()
            if P.before[k] then return end
            local mh = o:GetMaxHP(); if mh < 4000 or mh > 6000 then return end
            local q = o:K2_GetActorLocation(); local d = (q.X - P.x)^2 + (q.Y - P.y)^2
            if d < 800^2 and (not bd or d < bd) then best, bd = o, d end
        end) end
        if not best then return end
        local B = {ad = best:GetAddress(), nm = best:GetFName():ToString(), st = "kill", wstr = P.str, ver = P.ver}
        pcall(function() local u = S2L.clMed():GetGUID(best); if type(u) == "number" then B.uid = math.tointeger(u) or u end end)
        C.own[B.ad] = B.nm; C.B[hid] = B
        if ST.alife and ST.alife.hid then ST.alife.hid[B.ad] = nil end
        pcall(function() best:SetActorHiddenInGame(false) end)
        pcall(function() best:SetActorEnableCollision(true) end)
        S2L.clInt(best, false)
        B.mh = S2L.clMeshHide(ST, hid) or ((FL.b164off or 0) == 0 and C.tw ~= nil and C.tw[hid] ~= nil)   -- b164: hidden while built when a twin will be swapped
        if B.mh then pcall(function() best.Mesh:SetVisibility(false, true) end) end
        pcall(function() best:K2_SetActorLocation({X = P.x, Y = P.y, Z = P.z}, false, {}, true) end)
        if not B.uid then S2L.clDrop(ST, C, hid, B, now, "sem uid"); C.pend = nil; C.fail[hid] = now + 30; C.s.fail = C.s.fail + 1; return end
        pcall(function() FindFirstOf("CustomConsoleManagerDC"):XKillNpcByUID(B.uid) end)
        P.st, P.at, P.kills = "kill", now, 1
        return
    end
    local B = C.B[hid]
    local o = B and S2L.clGet(C, now, B.ad, B.nm)
    if not o then C.pend = nil; if B then C.B[hid] = nil end; C.fail[hid] = now + 10; C.s.fail = C.s.fail + 1; log("S2CORE cloot: corpo de saque de " .. hid .. " sumiu enquanto era feito"); return end
    if P.st == "kill" then
        local alive = true; pcall(function() alive = o:IsAlive() and true or false end)
        if not alive then
            if (FL.clammo or 0) >= 1 then S2L.clFill1(ST, C, P, B, now); return end   -- b140lootfix: two-phase fill
            local mgr = FindFirstOf("CustomConsoleManagerRK")
            for sid, n in pairs(S2L.clDec(P.str)) do pcall(function() mgr:XCreateItemInInventoryByID(sid, B.uid, n, 1.0) end) end
            P.st, P.at = "fill", now
        elseif now - P.at > 5 then
            S2L.clDrop(ST, C, hid, B, now, "nao morreu em 5 s"); C.pend = nil; C.fail[hid] = now + 30; C.s.fail = C.s.fail + 1
        elseif now - P.at > 1.5 and P.kills == 1 then
            P.kills = 2
            pcall(function() FindFirstOf("CustomConsoleManagerBuild"):XSetEntityUnkillableByUID(B.uid, false) end)
            pcall(function() FindFirstOf("CustomConsoleManagerDC"):XKillNpcByUID(B.uid) end)
        end
    elseif P.st == "fill1" then S2L.clFill2(ST, C, P, B, o, now)   -- b140lootfix: rounds after the magazines
    elseif P.st == "fill" then
        if now - P.at < 0.5 then return end
        local t, q = S2L.clCount(o, api.ROLE == "host" or (FL.b164off or 0) == 0)
        B.hstr = S2L.clEnc(S2L.clAdd(t, q)); B.wstr = P.str; B.st = "live"; C.pend = nil
        B.gv = S2L.clAdd(t, q); if (FL.clammo or 0) >= 1 then S2L.clFillLog(ST, C, hid, B, P) end   -- b140lootfix: counts given (takes = deltas)
        C.s.live = C.s.live + 1
        if api.ROLE ~= "host" and (FL.b164off or 0) == 0 and rawget(_G, "s2n_cl_swap") then local okS, mS = pcall(s2n_cl_swap, hid, B.ad, (C.tw and C.tw[hid]) or 0); if okS and type(mS) == "number" then B.sw, B.mh = true, false end end   -- b164 one body
        if FL.cllog == 1 then log(string.format("S2CORE cloot: corpo de saque de %s pronto uid=%s (%s) lista=%s%s", hid, tostring(B.uid), B.nm, B.hstr,
            B.hstr ~= P.str and (" PARCIAL (pedido " .. P.str .. ")") or "")) end
    end
end
-- b140lootfix (flag clammo, default 1, BOTH roles; live 2026-10-15 b138 L1: client "lista=A918D*10... PARCIAL (pedido A918D*2...)").
-- Cause (measured b103 + b138): XCreateItemInInventoryByID of a GUN gives it a FULL magazine and GetItemsCountInInventory counts those
-- rounds (A918D 2 asked + GunPM_HG MaxAmmo 8 = 10). The ammo Count itself is exact. And a take was sent as the body's whole count, so the
-- magazine rounds also entered the host list (dupe that grows). One rule for every gun of the game (s2u_equip.tsv, gen_equip.py: gun ->
-- default ammo + MaxAmmo; all other items are exact):
--   fill in two phases: 1) every listed item except the rounds of the listed guns' calibres; 2) +0.5 s count, then those rounds =
--   list - what the magazines already hold (never below 0). clammo=1 (never MORE than the host has): a gun whose full magazine would be
--   more rounds of its calibre than the list has is not made (held back; it stays in the shared list). clammo=2: every gun is made
--   (surplus = at most one magazine per gun, logged).
--   takes are DELTAS: list after a close = list + (count now - count given); a held gun or a magazine surplus never enters the list.
--   a list whose items are all held back gets no loot body ("vazio").
function S2L.clEqT()                                         -- s2u_equip.tsv through the persist reader (cpTables), loaded once
    local E = S2L.clEq
    if not E then
        E = {}; S2L.clEq = E
        local ok, e = pcall(S2L.cpTables, E)
        if not ok or type(E.eqTab) ~= "table" or next(E.eqTab) == nil then
            E.eqTab = {}
            log("S2CORE cloot: s2u_equip.tsv ausente ou vazio: pentes das armas nao previstos (clammo so acerta a municao solta)" .. (ok and "" or (" (" .. tostring(e) .. ")")))
        end
    end
    return E.eqTab
end
function S2L.clHold(want)                                    -- clammo=1: sid -> guns held back (full magazine > rounds of its calibre left)
    local held = {}
    if (FL.clammo or 0) ~= 1 then return held end
    local eq, bud, ws = S2L.clEqT(), {}, {}
    for sid, n in pairs(want) do local e = eq[sid]; if e and e.ammo and (e.max or 0) > 0 and n > 0 then ws[#ws + 1] = sid end end
    table.sort(ws, function(a, b) if eq[a].max ~= eq[b].max then return eq[a].max < eq[b].max end return a < b end)
    for _, sid in ipairs(ws) do
        local e = eq[sid]
        bud[e.ammo] = bud[e.ammo] or (want[e.ammo] or 0)
        local k = math.min(want[sid], math.floor(bud[e.ammo] / e.max))
        bud[e.ammo] = bud[e.ammo] - k * e.max
        if k < want[sid] then held[sid] = want[sid] - k end
    end
    return held
end
function S2L.clVis(str)                                      -- the list has at least one item a loot body would show
    if str == "-" then return false end
    if (FL.clammo or 0) ~= 1 then return true end
    local want = S2L.clDec(str)
    local held = S2L.clHold(want)
    for sid, n in pairs(want) do if n > (held[sid] or 0) then return true end end
    return false
end
function S2L.clFill1(ST, C, P, B, now)                       -- phase 1 (body dead): all but the listed guns' rounds; held guns not made
    local want = S2L.clDec(P.str)
    local held, eq, am = S2L.clHold(want), S2L.clEqT(), {}
    for sid, n in pairs(want) do local e = eq[sid]; if e and e.ammo and (e.max or 0) > 0 and n > (held[sid] or 0) then am[e.ammo] = true end end
    local mgr = FindFirstOf("CustomConsoleManagerRK")
    for sid, n in pairs(want) do
        local k = n - (held[sid] or 0)
        if k > 0 and not am[sid] then pcall(function() mgr:XCreateItemInInventoryByID(sid, B.uid, k, 1.0) end) end
    end
    P.want, P.am, B.held = want, am, held
    P.st, P.at = next(am) and "fill1" or "fill", now
end
function S2L.clFill2(ST, C, P, B, o, now)                    -- phase 2 (+0.5 s): rounds = list - what the magazines already hold
    if now - P.at < 0.5 then return end
    local t, q = S2L.clCount(o, api.ROLE == "host" or (FL.b164off or 0) == 0)
    local have = S2L.clAdd(t, q)
    local mgr = FindFirstOf("CustomConsoleManagerRK")
    for sid in pairs(P.am) do
        local d = (P.want[sid] or 0) - (have[sid] or 0)
        if d > 0 then pcall(function() mgr:XCreateItemInInventoryByID(sid, B.uid, d, 1.0) end) end
    end
    P.st, P.at = "fill", now
end
function S2L.clFillLog(ST, C, hid, B, P)                     -- held guns and any surplus over the list, once per body
    local want, over = S2L.clDec(P.str), {}
    for sid, n in pairs(B.gv) do if n > (want[sid] or 0) then over[sid] = n - (want[sid] or 0) end end
    if next(B.held or {}) == nil and next(over) == nil then return end
    C.s.hold = (C.s.hold or 0) + 1
    if FL.cllog == 1 then log(string.format("S2CORE cloot: corpo de saque de %s: armas retidas (pente cheio > municao da lista)=%s sobra sobre a lista=%s",
        hid, S2L.clEnc(B.held), S2L.clEnc(over))) end
end
function S2L.clRest(B, want, t, q, Q)                        -- list after a close = list + (count now - count given); host quest items apart
    local now, t2, q2 = S2L.clAdd(t, q), {}, {}
    for sid in pairs(S2L.clAdd(want, now)) do
        local n = math.max(0, (want[sid] or 0) + (now[sid] or 0) - (B.gv[sid] or 0))
        if n > 0 then if (Q and Q[sid]) or (q and q[sid]) then q2[sid] = n else t2[sid] = n end end
    end
    B.gv = now
    return t2, q2
end
-- ===== end b140lootfix helpers
function S2L.clRecount(ST, C, hid, now, why)                 -- host: the real corpse counted again; changed -> ver+1
    local E = C.K[hid]
    local o = E and S2L.clGet(C, now, E.ad, hid)
    if not o then return end
    E.rc = now; C.s.rc = C.s.rc + 1
    local t, q = S2L.clCount(o, true)
    local s = S2L.clEnc(t)
    if s ~= E.str or S2L.clEnc(q) ~= S2L.clEnc(E.q) then
        E.canon, E.q, E.str, E.ver = t, q, s, E.ver + 1
        if FL.cllog == 1 then log(string.format("S2CORE cloot: corpo %s recontado no host (%s): ver=%d lista=%s", hid, why, E.ver, s)) end
    end
end
function S2L.clTake(ST, C, now, hid, ver, list)              -- host: a client take "#CLT"
    local E = C.K[hid]
    if not E or ver ~= E.ver then
        C.s.trej = C.s.trej + 1
        log(string.format("S2CORE cloot: retirada do cliente em %s RECUSADA (ver %s, host %s): vale a lista do host", hid, tostring(ver), E and tostring(E.ver) or "sem corpo"))
        return
    end
    E.canon = S2L.clDec(list); E.str = S2L.clEnc(E.canon); E.ver = E.ver + 1
    if (FL.b164off or 0) == 0 then local QS = (S2L.qiTab() or {}).S or {}; E.q = {}; for s, n in pairs(E.canon) do if QS[s] then E.q[s] = n; E.canon[s] = nil end end; E.str = S2L.clEnc(E.canon) end   -- b164: the take list carries quest items: split again
    C.s.tin = C.s.tin + 1
    if not E.sw then E.sw = true; log("S2CORE cloot: corpo " .. hid .. " trocado: o real fica sem interacao, o host usa um corpo de saque com a lista comum") end
    if FL.cllog == 1 then log(string.format("S2CORE cloot: retirada do cliente em %s aceita: ver=%d lista=%s", hid, E.ver, E.str)) end
end
function S2L.clClosed(ST, C, now, hid, kind)                 -- my loot window on hid was closed: count once
    if kind == "c" then S2L.clRecount(ST, C, hid, now, "janela fechada"); return end
    local B = C.B[hid]
    local o = B and S2L.clGet(C, now, B.ad, B.nm)
    if not o then return end
    local host = api.ROLE == "host"
    local t, q = S2L.clCount(o, host or (FL.b164off or 0) == 0)   -- b164: quest items are in the shared list
    local s = S2L.clEnc(S2L.clAdd(t, q))
    if s == B.hstr then return end
    B.hstr = s
    if host then
        local E = C.K[hid]
        if E and (FL.clammo or 0) >= 1 and B.gv then t, q = S2L.clRest(B, S2L.clAdd(E.canon, E.q), t, q, E.q); s = S2L.clEnc(S2L.clAdd(t, q)) end   -- b140lootfix: delta
        if E then E.canon, E.q, E.str, E.ver = t, q, S2L.clEnc(t), E.ver + 1; B.wstr = s; B.ver = E.ver end
        if FL.cllog == 1 then log(string.format("S2CORE cloot: host tirou do corpo de saque %s: ver=%s lista=%s", hid, E and tostring(E.ver) or "?", s)) end
    else
        if (FL.clammo or 0) >= 1 and B.gv then local t2, q2 = S2L.clRest(B, S2L.clDec(B.wstr), t, q, nil); s = S2L.clEnc(S2L.clAdd(t2, q2)); B.wstr = s end   -- b140lootfix: #CLT = list - taken
        C.seq = C.seq + 1
        local tk = {id = C.nonce .. "." .. C.seq, hid = hid, ver = B.ver, str = s, at = now}
        C.T[#C.T + 1] = tk
        B.ver = B.ver + 1
        C.s.take = C.s.take + 1
        if FL.cllog == 1 then log(string.format("S2CORE cloot: cliente tirou de %s: #CLT %s ver=%d lista=%s", hid, tk.id, tk.ver, s)) end
    end
end
function S2L.clLight(ST, C, lp, now)                         -- 0.2 s: messages, window open / close, lock, the body being made
    local host = api.ROLE == "host"
    if host then
        C.co = x1part("CO")
        if (FL.b164off or 0) == 0 and rawget(_G, "s2n_lk_hold") then local r = api.getRemote and api.getRemote(); local co = s2n_lk_hold("rco", C.co or "", (r and r.recv) and (now - r.recv) or 1e9, FL.cllease or 3, FL.clholdmax or 120); C.co = co ~= "" and co or nil end   -- b164: lease + cap
        local raw = x1part("CLT")
        if raw and raw ~= "" then for ent in raw:gmatch("[^;]+") do
            local id, hid, ver, list = ent:match("^([%d%.]+),([%w_]+),(%d+),([%w_%*%.%-]*)$")
            if id and not C.done[id] then C.done[id] = now; S2L.clTake(ST, C, now, hid, tonumber(ver), list) end
        end end
    else
        local raw = x1part("CL")
        C.silent = raw == nil
        if raw ~= nil and raw ~= C.raw then
            C.raw = raw
            local seen = {}
            for ent in raw:gmatch("[^;]+") do
                local hid, x, y, z, uid, ver, o, list = ent:match("^([%w_]+),([%-%d%.]+),([%-%d%.]+),([%-%d%.]+),(%-?%d+),(%d+),([01]),([%w_%*%.%-]*)$")
                if hid then
                    local R = C.R[hid] or {}; C.R[hid] = R
                    R.x, R.y, R.z, R.uid, R.ver, R.o, R.str, R.gone = tonumber(x), tonumber(y), tonumber(z), tonumber(uid), tonumber(ver), o == "1", (list ~= "" and list or "-"), nil
                    seen[hid] = true
                end
            end
            for h, R in pairs(C.R) do if not R.gone and not seen[h] then R.gone = now end end
        end
        for hid, R in pairs(C.R) do if R.gone and now - R.gone > (FL.clgone or 4) then C.R[hid] = nil end end
        local keep = {}
        for _, tk in ipairs(C.T) do
            local R = C.R[tk.hid]
            if R and R.ver > tk.ver then                                      -- the host's ver passed it: done
            elseif not R or now - tk.at > 20 then log("S2CORE cloot: retirada " .. tk.id .. " em " .. tk.hid .. " sem confirmacao do host (largada)")
            else keep[#keep + 1] = tk end
        end
        C.T = keep
    end
    local ad = S2L.clOpenAddr(lp)
    local hid, kind = nil, nil
    if ad then
        for h, B in pairs(C.B) do if B.ad == ad and B.st == "live" then hid, kind = h, "b" end end
        if not hid and host then for h, E in pairs(C.K) do if E.ad == ad then hid, kind = h, "c" end end end
    end
    if C.open and C.open ~= hid then                                     -- closed (or another one opened): count it shortly
        local B = C.B[C.open]; if B then B.open = nil end
        C.cnt = {hid = C.open, kind = C.openK, at = now + 0.3}
        C.open, C.openK = nil, nil
    end
    if hid and C.open ~= hid then
        local other = (host and C.co == hid) or (not host and C.R[hid] and C.R[hid].o)
        if other then S2L.clClose(lp, C, hid, "o outro jogador ja esta com ele aberto")
        else C.open, C.openK = hid, kind; if kind == "b" then C.B[hid].open = true end end
    elseif hid and not host and C.R[hid] and C.R[hid].o then             -- tie: the client gives way
        S2L.clClose(lp, C, hid, "aberto ao mesmo tempo no host; o cliente cede")
        local B = C.B[hid]; if B then B.open = nil end
        C.cnt = {hid = hid, kind = kind, at = now + 0.3}
        C.open, C.openK = nil, nil
    end
    if C.cnt and now >= C.cnt.at then local c = C.cnt; C.cnt = nil; S2L.clClosed(ST, C, now, c.hid, c.kind) end
    if C.pend then S2L.clStep(ST, C, lp, now) end
end
function S2L.clHostScan(ST, C, lp, now)                      -- host 1 s: dead pawns near the client -> C.K; real corpse interaction
    local r = api.getRemote and api.getRemote()
    local rok = r and r.x and (now - (r.recv or -1e9)) < 15
    local send2 = (FL.clsend or 4000)^2
    local seen, nNew = {}, 0
    for _, o in ipairs(FindAllOf("Pawn") or {}) do pcall(function()
        if not o:IsValid() or o == lp then return end
        local alive = true; pcall(function() alive = o:IsAlive() and true or false end)
        if alive or M.isOwnBody(ST, o) then return end
        local full = o:GetFullName()
        if full:find("Stalker2Character", 1, true) then return end
        local q = o:K2_GetActorLocation()
        if math.abs(q.X) < 100 and math.abs(q.Y) < 100 then return end   -- the A-Life pool at 0,0,0
        local hid, ad = o:GetFName():ToString(), o:GetAddress()
        local E = C.K[hid]
        if E and E.ad ~= ad then C.K[hid] = nil; E = nil end
        local d2 = rok and ((q.X - r.x)^2 + (q.Y - r.y)^2) or 1e18
        if not E then
            if d2 > send2 or nNew >= 2 then return end
            local lk = tostring(ad) .. ":" .. hid
            if C.lsb[lk] == nil then
                C.lsb[lk] = full:find("BP_AI_Human_C", 1, true) ~= nil and M.LS.body(ST, string.format("BP_AI_Human_C@%.0f,%.0f,%.0f", q.X / 10, q.Y / 10, q.Z / 10)) ~= nil
            end
            if C.lsb[lk] then return end                                 -- a DeadBody placeholder body: b90 lootshare has it
            local c = ST.nps and ST.nps.ids and ST.nps.ids[lk]
            E = {ad = ad, uid = c and c.uid or 0, ver = 1}
            E.canon, E.q = S2L.clCount(o, true); E.str = S2L.clEnc(E.canon); E.rc = now
            C.K[hid] = E; nNew = nNew + 1; C.s.rc = C.s.rc + 1
            if FL.cllog == 1 and E.str ~= "-" then log(string.format("S2CORE cloot: corpo %s listado para o cliente (uid=%s) lista=%s", hid, tostring(E.uid), E.str)) end
        end
        E.x, E.y, E.z, E.d2 = q.X, q.Y, q.Z, d2
        seen[hid] = true
        if E.sw or C.co == hid then S2L.clInt(o, false); E.off = true; if (FL.b164off or 0) == 0 and rawget(_G, "s2n_lk_off") then s2n_lk_off(ad, 1, FL.cllease or 3) end        -- switched / the client has it open: no interaction here
        elseif E.off then S2L.clInt(o, true); E.off = nil end
    end) end
    for hid, E in pairs(C.K) do
        if not seen[hid] or (not E.sw and E.d2 > (2 * (FL.clsend or 4000))^2) then C.K[hid] = nil end
    end
    local best, bt
    for hid, E in pairs(C.K) do
        if not E.sw and E.d2 <= send2 and C.open ~= hid and now - E.rc >= (FL.clrc or 5) and (not bt or E.rc < bt) then best, bt = hid, E.rc end
    end
    if (FL.b164off or 0) == 0 and rawget(_G, "s2n_lk_off") then s2n_lk_off(0, 0, FL.cllease or 3) end   -- b164: a real corpse nobody claims gets its interaction back
    if best then S2L.clRecount(ST, C, best, now, "periodico") end
end
function S2L.clWant(ST, C)                                   -- hid -> {x, y, z, str, ver, block} of the loot bodies this side should have
    local w = {}
    if api.ROLE == "host" then
        for hid, E in pairs(C.K) do if E.sw then
            w[hid] = {x = E.x, y = E.y, z = E.z, str = S2L.clEnc(S2L.clAdd(E.canon, E.q)), ver = E.ver, block = (C.co == hid)}
        end end
    else
        local tk = {}; for _, t in ipairs(C.T) do tk[t.hid] = true end
        for hid, R in pairs(C.R) do
            w[hid] = {x = R.x, y = R.y, z = R.z, str = R.str, ver = R.ver, block = (R.o or C.silent or R.gone ~= nil or tk[hid]) and true or false}
        end
    end
    return w
end
function S2L.clBodies(ST, C, lp, now)                        -- 1 s: drop / rebuild / interaction / mesh of my loot bodies; one new request
    local w = S2L.clWant(ST, C)
    local me = lp:K2_GetActorLocation()
    local function d2(W) return (W.x - me.X)^2 + (W.y - me.Y)^2 end
    for hid, B in pairs(C.B) do if B.st == "live" then
        local W = w[hid]
        local why
        if not W then why = "fora da lista"
        elseif W.str == "-" or not S2L.clVis(W.str) then why = "vazio"   -- b140lootfix: only held-back guns left = empty
        elseif d2(W) > ((FL.clr or 2500) + 1500)^2 then why = "longe"
        elseif W.str ~= B.wstr and W.str ~= B.hstr and not W.block then why = "lista mudou; refeito" end
        if why and C.open ~= hid then S2L.clDrop(ST, C, hid, B, now, why)
        else
            if W and W.str == B.hstr then B.wstr = W.str; B.ver = math.max(B.ver or 0, W.ver) end
            local o = S2L.clGet(C, now, B.ad, B.nm)
            if not o then S2L.clMirHide(ST, hid, B, false); C.B[hid] = nil
            else
                local on = W and not W.block and W.str ~= "-" and (W.str == B.wstr or W.str == B.hstr) and true or false
                S2L.clInt(o, on)
                local hide = S2L.clMeshHide(ST, hid)
                local vis; pcall(function() vis = o.Mesh:IsVisible() end); B.mh = hide   -- b141b: real state each tick (something showed the mesh again -> two bodies)
                if vis == hide then pcall(function() o.Mesh:SetVisibility(not hide, true) end) end
                if (FL.clmir or 1) == 1 and api.ROLE ~= "host" then S2L.clMirHide(ST, hid, B, true) end
            end
        end
    end end
    local gate = (FL.bodysafe ~= 1 or not ST.wg or ST.wg.open) and not (ST.tgt and ST.tgt.spawnAt) and not (ST.pup and ST.pup.spawnAt)
    if C.pend or now < C.gap or not gate or S2L.clN(C.B) >= (FL.clmax or 6) then return end
    local best, bd
    for hid, W in pairs(w) do
        if not C.B[hid] and W.str ~= "-" and not W.block and S2L.clVis(W.str) and now >= (C.fail[hid] or 0) then
            local d = d2(W)
            if d < (FL.clr or 2500)^2 and (not bd or d < bd) then best, bd = hid, d end
        end
    end
    if best then S2L.clSpawn(ST, C, best, w[best], now) end
end
function S2L.clCopy(ST, C, lp, now)                          -- client clcopy=1: my own live copy of a host-killed NPC -> killed / inert
    local todo, tw0 = {}, {}
    for hid, R in pairs(C.R) do
        if (R.uid or 0) > 0 and not C.cp[hid] and not R.gone and #todo < 2 then todo[#todo + 1] = hid end
        if (R.uid or 0) == 0 and not C.cp[hid] and not R.gone and #tw0 < 2 and (FL.b164off or 0) == 0 then tw0[#tw0 + 1] = hid end   -- b164: twin of a uid-0 corpse
    end
    if #todo == 0 and #tw0 == 0 then return end
    local med, calls = S2L.clMed(), 0
    local ids = ST.nps and ST.nps.ids or {}
    local live, dead = {}, {}
    for _, o in ipairs(FindAllOf("Pawn") or {}) do pcall(function()
        if not o:IsValid() or o == lp or M.isOwnBody(ST, o) then return end
        if not o:IsAlive() then if #tw0 > 0 and not o:GetFullName():find("Stalker2Character", 1, true) then dead[#dead + 1] = o end return end
        live[#live + 1] = o
    end) end
    C.tw = C.tw or {}
    for _, hid in ipairs(tw0) do   -- b164: the client's OWN dead body at a uid-0 host corpse: no interaction (own roll), the loot body wears it
        local R, hit = C.R[hid], nil
        for _, o in ipairs(dead) do if not hit then pcall(function() local q = o:K2_GetActorLocation(); if (q.X - R.x)^2 + (q.Y - R.y)^2 <= 150^2 and math.abs(q.Z - R.z) <= 250 then hit = o end end) end end
        R.twTry = (R.twTry or 0) + 1
        if hit then C.tw[hid] = hit:GetAddress(); S2L.clInt(hit, false); C.cp[hid] = "twin"; C.s.copy = C.s.copy + 1; log("S2CORE cloot: corpo gemeo do cliente em " .. hid .. " (" .. hit:GetFName():ToString() .. "): sem interacao; o corpo de saque veste ele (b164)")
        elseif R.twTry >= 3 then C.cp[hid] = "none" end
    end
    for _, hid in ipairs(todo) do
        local R = C.R[hid]
        local hit
        for _, o in ipairs(live) do
            if hit then break end
            pcall(function()
                local q = o:K2_GetActorLocation()
                if (q.X - R.x)^2 + (q.Y - R.y)^2 > 5000^2 then return end
                local k = tostring(o:GetAddress()) .. ":" .. o:GetFName():ToString()
                local u = ids[k] and ids[k].uid
                if (u == nil or u == 0) and calls < 12 then
                    calls = calls + 1
                    local g = med:GetGUID(o); if type(g) == "number" then u = math.tointeger(g) or g end
                end
                if u == R.uid then hit = o end
            end)
        end
        R.cpTry = (R.cpTry or 0) + 1
        if hit then
            local story = false; pcall(function() story = S2L.chProt(ST, hit) and true or false end)
            if not story then pcall(function() FindFirstOf("CustomConsoleManagerDC"):XKillNpcByUID(R.uid) end) end
            if story or not ((FL.b164off or 0) == 0) then S2L.clInert(hit) else C.tw = C.tw or {}; C.tw[hid] = hit:GetAddress(); S2L.clInt(hit, false) end   -- b164: the killed copy is the twin the loot body wears
            C.cp[hid] = story and "story" or "killed"; C.s.copy = C.s.copy + 1
            log(string.format("S2CORE cloot: copia local de %s (uid %s) %s e inerte", hid, tostring(R.uid), story and "da historia: viva" or "morta"))
        elseif R.cpTry >= 3 then C.cp[hid] = "none" end
    end
end
function S2L.clSweep(ST, C, now)                             -- leftover loot bodies (a save): dead, max HP 5000, not ours -> inert
    local n = 0
    local T = ST.tgt or {}
    for _, o in ipairs(FindAllOf("BP_AI_Human_C") or {}) do pcall(function()
        if not o:IsValid() or o == T.pawn or o:IsAlive() then return end
        local mh = o:GetMaxHP(); if math.abs(mh - 5000) > 0.5 then return end
        local ad, nm = o:GetAddress(), o:GetFName():ToString()
        if C.own[ad] == nm then return end
        S2L.clInert(o); C.own[ad] = nm; n = n + 1
    end) end
    C.s.inert = C.s.inert + n
    if n > 0 then log("S2CORE cloot: " .. n .. " corpo(s) de saque de antes (save) deixados inertes") end
end
function S2L.clLate(ST, C, now)                              -- b181: a loot body that spawned AFTER the 6 s ask timeout: alive, max HP 4000-6000, new since the ask, at the ask spot -> inert + destroyed
    local T, U = ST.tgt or {}, ST.pup or {}
    local L = C.late
    for i = #L, 1, -1 do if now > L[i].to then table.remove(L, i) end end
    if #L == 0 then C.late = nil; return end
    if T.spawnAt or U.spawnAt then return end                -- a target / puppet body is being requested: it could be that one
    local n = 0
    for _, o in ipairs(FindAllOf("BP_AI_Human_C") or {}) do pcall(function()
        if not o:IsValid() or o == T.pawn or o == U.pawn then return end
        local ad, nm = o:GetAddress(), o:GetFName():ToString()
        if C.own[ad] == nm then return end
        local mh = o:GetMaxHP(); if mh < 4000 or mh > 6000 then return end
        local k = tostring(ad) .. "|" .. nm
        local q = o:K2_GetActorLocation()
        for _, E in ipairs(L) do
            if not E.before[k] and (q.X - E.x) ^ 2 + (q.Y - E.y) ^ 2 < 800 ^ 2 then
                S2L.clInert(o); C.own[ad] = nm; n = n + 1
                local u; pcall(function() local g = S2L.clMed():GetGUID(o); if type(g) == "number" then u = math.tointeger(g) or g end end)
                if u then pcall(function() FindFirstOf("CustomConsoleManagerAI2"):XDestroyModel(u) end) end
                log("S2CORE cloot: corpo de saque tardio de " .. E.hid .. " (" .. nm .. " maxhp=" .. tostring(mh) .. ") deixado inerte e destruido")
                break
            end
        end
    end) end
    C.s.inert = C.s.inert + n
end
function S2L.clHeavy(ST, C, lp, now)
    if api.ROLE == "host" then S2L.clHostScan(ST, C, lp, now) elseif (FL.clcopy or 0) == 1 then S2L.clCopy(ST, C, lp, now) end
    S2L.clBodies(ST, C, lp, now)
    if api.ROLE ~= "host" and (FL.b164off or 0) == 0 and rawget(_G, "s2n_cl_swap_tick") then s2n_cl_swap_tick() end   -- b164 one body: a loot body gone -> its native corpse back
    for id, at in pairs(C.done) do if now - at > 120 then C.done[id] = nil end end
    if C.late and not C.pend then S2L.clLate(ST, C, now) end   -- b181
    if not C.pend and (FL.bodysafe ~= 1 or not ST.wg or ST.wg.open) and (C.swN or 0) < 2 then
        C.swAt = C.swAt or (now + 5)
        if now >= C.swAt then S2L.clSweep(ST, C, now); C.swN = (C.swN or 0) + 1; C.swAt = now + 30 end
    end
end
function S2L.clOff(ST, C)                                    -- cloot 0 midway: bodies dropped, real corpses get their interaction back
    local now = os.clock()
    for hid, B in pairs(C.B) do S2L.clDrop(ST, C, hid, B, now, "cloot=0") end
    for hid, E in pairs(C.K) do if E.off then local o = S2L.clGet(C, now, E.ad, hid); if o then S2L.clInt(o, true) end end end
    C.map = nil
    ST.cl = nil
    log("S2CORE cloot: desligado; corpos de saque largados")
end
function S2L.clStats(C, now)
    local S = C.s
    if FL.cllog == 1 and (S.spawn + S.drop + S.take + S.tin + S.trej + S.deny + S.copy + S.inert + S.fail > 0 or next(C.B) ~= nil) then
        log(string.format("S2CORE cloot 10 s (%s): entradas=%d corpos=%d pedidos=%d prontos=%d largados=%d falhas=%d retiradas=%d aceitas=%d recusadas=%d recontagens=%d negados=%d copias=%d inertes=%d ms=%.1f",
            api.ROLE, S2L.clN(api.ROLE == "host" and C.K or C.R), S2L.clN(C.B), S.spawn, S.live, S.drop, S.fail, S.take, S.tin, S.trej, S.rc, S.deny, S.copy, S.inert, S.ms))
    end
    C.s = {at = now, spawn = 0, live = 0, drop = 0, fail = 0, take = 0, tin = 0, trej = 0, rc = 0, deny = 0, copy = 0, inert = 0, ms = 0}
end
function S2L.clTick(ST)                                      -- tick100: light part every 0.2 s, heavy part every 1 s
    if (FL.cloot or 0) ~= 1 then if ST.cl then pcall(S2L.clOff, ST, ST.cl); ST.cl = nil end return end
    local lp = S2.io and S2.io.pawn
    if not valid(lp) then return end
    local C = S2L.clSt(ST)
    local now = os.clock()
    if now < C.lt then return end
    C.lt = now + 0.2
    local ok, e = pcall(S2L.clLight, ST, C, lp, now)
    if ok and now >= C.ht then C.ht = now + 1; ok, e = pcall(S2L.clHeavy, ST, C, lp, now) end
    C.map = nil
    if not ok then C.err = (C.err or 0) + 1; if C.err <= 5 then log("S2CORE cloot erro: " .. tostring(e)) end end
    C.s.ms = C.s.ms + (os.clock() - now) * 1000
    if now - C.s.at >= 10 then S2L.clStats(C, now) end
end
function S2L.clOut(ST)                                       -- x1: host "#CL:..."; client "#CO:<hid>" + "#CLT:..."
    if (FL.cloot or 0) ~= 1 then return "" end
    local C = ST.cl
    if not C then return "" end
    if api.ROLE == "host" then
        local l = {}
        for hid, E in pairs(C.K) do if E.d2 and E.d2 <= (FL.clsend or 4000)^2 then l[#l + 1] = {hid, E} end end
        table.sort(l, function(a, b) return a[2].d2 < b[2].d2 end)
        local t = {}
        local hk = not ((FL.b164off or 0) == 0) or not rawget(_G, "s2n_lk_hold") or s2n_lk_hold("lopen", C.open or "", 0, FL.cllease or 3, FL.clholdmax or 120) ~= ""   -- b164: the host's own hold ends after clholdmax s
        for i = 1, math.min(#l, 12) do
            local hid, E = l[i][1], l[i][2]
            t[#t + 1] = string.format("%s,%.0f,%.0f,%.0f,%s,%d,%d,%s", hid, E.x, E.y, E.z, tostring(math.tointeger(E.uid) or 0), E.ver, (C.open == hid and hk) and 1 or 0, (FL.b164off or 0) == 0 and S2L.clEnc(S2L.clAdd(E.canon, E.q)) or E.str)
        end
        return "#CL:" .. table.concat(t, ";")
    end
    local s = ""
    if C.open and C.openK == "b" then s = "#CO:" .. C.open end
    if #C.T > 0 then
        local t = {}
        for _, tk in ipairs(C.T) do t[#t + 1] = string.format("%s,%s,%d,%s", tk.id, tk.hid, tk.ver, tk.str) end
        s = s .. "#CLT:" .. table.concat(t, ";")
    end
    return s
end
S2L.clSnap0 = M.snapExtra                                    -- #CL / #CO / #CLT ride on x1 through a wrap (like #SL / #HC / #FE / #NP)
function M.snapExtra(ST, pawn, p)
    local base = S2L.clSnap0 and S2L.clSnap0(ST, pawn, p) or ""
    local ok, s = pcall(S2L.clOut, ST)
    if not ok then ST.clOutErr = (ST.clOutErr or 0) + 1; if ST.clOutErr <= 5 then log("S2CORE cloot erro (x1): " .. tostring(s)) end; s = "" end
    return (base or "") .. (type(s) == "string" and s or "")
end
-- ===== end b138loot
-- ===== b144combat (flags wshot / wsfx / wvoice, default 1; user 2026-10-15 "the enemies is without weapon, and the bullets wont get fired",
-- 21:15 "the npcs doesnt talk too, i cant hear the bandits as client"). Host master (NPC combat): the client hears and sees what the host's
-- NPCs do, through ONE relay for every NPC kind:
-- HOST  shots: ItemAppearanceComponent:OnShotSoundStart (fires once per bullet; owner = the shooter; measured for the player) with an owner
--   that is not the local player and not our puppet, within wsr cm of the client -> counted per NPC + its weapon mesh (WeaponInHandsMesh).
--   voices (wvoice): every 1 s the live pawns within wsr cm of the client get their VoiceoverSpeechComponent (refs <= 1 s old); every 0.2 s its
--   AkVoiceoverSpeakerComponent.AkAudioEvent is read: a CHANGED event = a new voice line (the first sighting of an NPC only records it).
--   Every 0.2 s the pending ones become x1 "#WS:<seq>,<npc name>,<s|v>,<weapon SK name | event path under Events/>,<n>;..." kept 0.6 s.
-- CLIENT: each new seq plays at main.lua's mirror of that NPC (S2L.clMirs: weapon part muzzle socket, else weapon / hand / body) or at its
--   paired kept NPC (npcsync): shot = the weapon's own FireEventOneShot (S2L.WSHOT, generated from the game data by gen_wshot_table.py:
--   38 weapon meshes = all 82 Gun* SIDs) + the TP muzzle flash of its family; n shots spaced by its FireInterval (ExecuteInGameThreadWithDelay,
--   no loop); wsfx=2 also spawns the NPC tracer. Voice = PostEventAtLocation of the same event at the head (at most 1 per NPC per 1.5 s).
--   Caps (N16): wsmax shot effects per second in total, wsnmax shots per entry. Unknown weapon mesh = AK74 row.
-- shortcut: voice identity = the speaker's AkAudioEvent property only (probe R3 measures it; a line posted natively without it is not seen)
-- shortcut: a sound plays at a point (it does not follow the NPC); one Wwise voice (no VoiceA/VoiceB switch); upgrade when users notice.
S2L.WSHOT = {   -- b144 generated by gen_wshot_table.py: 38 weapon meshes (SK name = {fire event under /Game/_STALKER2/Audio/WwiseAudio/Events/, muzzle family, fire interval s})
    ["SK_AK74"] = {"Weapons/AR/AK74/AK74_Fire/SFX_AK74_Oneshot.SFX_AK74_Oneshot", "A", 0.100},   -- GunAK74_ST
    ["SK_Fora230"] = {"S2_Live/Weapons/SMG/Fora230/Fora230_Fire/SFX_Fora230_Oneshot.SFX_Fora230_Oneshot", "P", 0.075},   -- GunFora230_PP
    ["SK_Gp3a"] = {"S2_Live/Weapons/SR/GP3A/GP3A_Fire/SFX_GP3A_Oneshot.SFX_GP3A_Oneshot", "A", 0.120},   -- GunGP3A_DMR
    ["SK_Skp"] = {"S2_Live/Weapons/SR/SKP/SKP_Fire/SFX_SKP_Oneshot.SFX_SKP_Oneshot", "A", 0.100},   -- GunSKP_DMR
    ["SK_ThreeLine"] = {"Weapons/SR/ThreeLine/ThreeLine_Fire/SFX_ThreeLine_Oneshot.SFX_ThreeLine_Oneshot", "A", 1.150},   -- GunThreeLine_SP
    ["SK_Toz34"] = {"Weapons/SG/TOZ34/TOZ34_Fire/SFX_TOZ34_Oneshot.SFX_TOZ34_Oneshot", "S", 0.100},   -- GunTOZ_SG
    ["SK_aku"] = {"Weapons/SMG/AKU/AKU_Fire/SFX_AKU_Oneshot.SFX_AKU_Oneshot", "P", 0.090},   -- GunAKU_PP
    ["SK_apb"] = {"Weapons/HG/APB/APB_Fire/SFX_APB_Oneshot.SFX_APB_Oneshot", "H", 0.085},   -- GunAPB_HG
    ["SK_bucket0"] = {"Weapons/SMG/Bucket/Bucket_Fire/SFX_Bucket_Oneshot.SFX_Bucket_Oneshot", "P", 0.086},   -- GunBucket_PP
    ["SK_d1200"] = {"Weapons/SG/D12/D12_Fire/SFX_D12_Oneshot.SFX_D12_Oneshot", "S", 0.100},   -- GunD12_SG
    ["SK_dnipro"] = {"Weapons/AR/Dnipro/Dnipro_FIre/SFX_Dnipro_Oneshot.SFX_Dnipro_Oneshot", "A", 0.095},   -- GunDnipro_ST
    ["SK_fora0"] = {"Weapons/AR/Fora/Fora_Fire/SFX_Fora_Oneshot.SFX_Fora_Oneshot", "A", 0.075},   -- GunFora_ST
    ["SK_gauss"] = {"Weapons/Unique/Gauss/Gauss_Fire/SFX_Gauss_Oneshot.SFX_Gauss_Oneshot", "A", 2.400},   -- GunGauss_SP
    ["SK_gp37"] = {"Weapons/AR/GP37/GP37_Fire/SFX_GP37_Oneshot.SFX_GP37_Oneshot", "A", 0.080},   -- GunG37_ST
    ["SK_grim0"] = {"Weapons/AR/Grim/Grim_Fire/SFX_Grim_Oneshot.SFX_Grim_Oneshot", "A", 0.085},   -- GunGrim_ST
    ["SK_gvi"] = {"Weapons/AR/Gvintar/Gvintar_Fire/SFX_Gvintar_Oneshot.SFX_Gvintar_Oneshot", "A", 0.090},   -- GunGvintar_ST
    ["SK_integ"] = {"Weapons/SMG/Integral/Integral_Fire/SFX_Integral_Oneshot.SFX_Integral_Oneshot", "P", 0.060},   -- GunIntegral_PP
    ["SK_kharod000"] = {"Weapons/AR/Kharod/Kharod_Fire/SFX_Kharod_Oneshot.SFX_Kharod_Oneshot", "A", 0.085},   -- GunKharod_ST
    ["SK_kora"] = {"S2_Live/Weapons/HG/Kora/Kora_Fire/SFX_Kora_Oneshot.SFX_Kora_Oneshot", "H", 0.100},   -- GunKora_HG
    ["SK_lav"] = {"Weapons/AR/Gvintar/Gvintar_Fire/SFX_Gvintar_Oneshot.SFX_Gvintar_Oneshot", "A", 0.090},   -- GunLavina_ST
    ["SK_m1000"] = {"Weapons/HG/M10/M10_Fire/SFX_M10_Oneshot.SFX_M10_Oneshot", "P", 0.045},   -- GunM10_HG
    ["SK_m160"] = {"Weapons/AR/M16/M16_Fire/SFX_M16_Oneshot.SFX_M16_Oneshot", "A", 0.080},   -- GunM16_ST
    ["SK_m701"] = {"Weapons/SR/M701/M701_Fire/SFX_M701_Oneshot.SFX_M701_Oneshot", "A", 2.100},   -- GunM701_SP
    ["SK_m86000"] = {"Weapons/SG/M860/M860_Fire/SFX_M860_Oneshot.SFX_M860_Oneshot", "S", 0.960},   -- GunM860_SG
    ["SK_mar"] = {"Weapons/SR/Mark/Mark_Fire/SFX_Mark_Oneshot.SFX_Mark_Oneshot", "A", 0.140},   -- GunMark_SP
    ["SK_obrez"] = {"Weapons/SG/Obrez/Obrez_Fire/SFX_Obrez_Oneshot.SFX_Obrez_Oneshot", "S", 0.100},   -- GunObrez_SG
    ["SK_pkp00000"] = {"Weapons/MG/PKP/PKP_Fire/SFX_PKP_Oneshot.SFX_PKP_Oneshot", "M", 0.095},   -- GunPKP_MG
    ["SK_pm"] = {"Weapons/HG/PM/PM_Fire/SFX_PM_Oneshot.SFX_PM_Oneshot", "H", 0.090},   -- GunPM_HG
    ["SK_ram2"] = {"Weapons/SG/Ram2/Ram2_Fire/SFX_Ram2_Oneshot.SFX_Ram2_Oneshot", "S", 0.100},   -- GunRam2_SG
    ["SK_rhino00000"] = {"Weapons/HG/Rhino/Rhino_Fire/SFX_Rhino_Oneshot.SFX_Rhino_Oneshot", "H", 0.700},   -- GunRhino_HG
    ["SK_rpg7"] = {"Weapons/Unique/RPG7/RPG7_Fire/SFX_RPG_Oneshot.SFX_RPG_Oneshot", "A", 0.970},   -- GunRpg7_GL
    ["SK_spsa00"] = {"Weapons/SG/SPSA/SPSA_Fire/SFX_SPSA_Oneshot.SFX_SPSA_Oneshot", "S", 0.100},   -- GunSPSA_SG
    ["SK_svm"] = {"Weapons/SR/SVDM/SVDM_Fire/SFX_SVDM_Oneshot.SFX_SVDM_Oneshot", "A", 0.100},   -- GunSVDM_SP
    ["SK_svu"] = {"Weapons/SR/SVU/SVU_Fire/SFX_SVU_Oneshot.SFX_SVU_Oneshot", "A", 0.100},   -- GunSVU_SP
    ["SK_udp"] = {"Weapons/HG/UDP/UDP_Fire/SFX_UDP_Oneshot.SFX_UDP_Oneshot", "H", 0.080},   -- GunUDP_HG
    ["SK_vip"] = {"Weapons/SMG/Viper/Viper_Fire/SFX_Viper_Oneshot.SFX_Viper_Oneshot", "P", 0.082},   -- GunViper_PP
    ["SK_zubr0"] = {"Weapons/SMG/Zubr/Zubr_Fire/SFX_Zubr_Oneshot.SFX_Zubr_Oneshot", "P", 0.090},   -- GunZubr_PP
    ["Sk_arev"] = {"S2_Live/Weapons/AR/Arev/Arev_Fire/SFX_Arev_Oneshot.SFX_Arev_Oneshot", "A", 0.082},   -- GunArev_ST
}
S2L.WSFAM = {H = {"Pistolet", "small"}, P = {"PP", "small"}, A = {"AssaultR", "medium"}, M = {"MachineGun", "huge"}, S = {"Shotgun", "shotgun"}}
S2L.WSMZ = {"Muzzle", "muzzle", "jnt_muzzle", "MuzzleFlash", "FX_Muzzle", "socket_muzzle", "Fire", "jnt_fire"}
S2L.WSPFX = "/Game/_STALKER2/Audio/WwiseAudio/Events/"
function S2L.wsSt(ST)
    local W = ST.ws
    if not W then
        W = {pend = {}, q = {}, seq = 0, ow = {}, owN = 0, vl = {}, vlN = 0, vlist = {}, vAt = -100, vPoll = -100, flushAt = 0, last = -1, rxAt = -100,
             budAt = 0, bud = 0, mz = {}, ev = {}, vgap = {}, errT = 0,
             s = {at = os.clock(), hook = 0, npc = 0, mine = 0, noself = 0, far = 0, sent = 0, voice = 0, vnpc = 0, rx = 0, play = 0, vplay = 0,
                  notgt = 0, noev = 0, cap = 0, err = 0}}
        ST.ws = W
    end
    return W
end
function S2L.wsErr(ST, where, e)
    local W = S2L.wsSt(ST)
    W.s.err = W.s.err + 1; W.errT = W.errT + 1
    if W.errT <= 5 or W.errT % 100 == 0 then log("S2CORE wshot erro (" .. tostring(where) .. ") #" .. W.errT .. ": " .. tostring(e):sub(1, 200)) end
end
function S2L.wsSafe(s) return type(s) == "string" and #s > 0 and #s <= 160 and s:find("^[%w_/%.%-]+$") ~= nil end   -- x1: no , ; # " `
function S2L.wsShot(ST, self, k)                              -- HOST hook body (game thread): which NPC fired, with which weapon
    local W = S2L.wsSt(ST)
    W.s.hook = W.s.hook + 1
    if self == nil then W.s.noself = W.s.noself + (tonumber(k) or 1); return end   -- deferred (fired off the game thread): no owner
    local ow = self:get():GetOwner()
    if not valid(ow) then return end
    local ad = ow:GetAddress()
    local lp, P = S2.io and S2.io.pawn, ST.pup
    if (valid(lp) and lp:GetAddress() == ad) or (P and valid(P.pawn) and P.pawn:GetAddress() == ad) then W.s.mine = W.s.mine + 1; return end
    local now = os.clock()
    local c = W.ow[ad]
    if not c or now - c.at > 2 then                           -- name / weapon / distance read again every 2 s (address reuse after pooling)
        c = {at = now, nm = ow:GetFName():ToString(), sk = "-", far = false}
        pcall(function() local w = ow:GetWeaponInHandsMeshComponent(); if valid(w) then c.sk = w:GetSkeletalMeshAsset():GetFName():ToString() end end)
        local r = api.getRemote and api.getRemote()
        if r and r.x then pcall(function() local q = ow:K2_GetActorLocation(); c.far = (q.X - r.x) ^ 2 + (q.Y - r.y) ^ 2 > (FL.wsr or 6000) ^ 2 end) end
        if not W.ow[ad] then W.owN = W.owN + 1; if W.owN > 300 then W.ow = {}; W.owN = 1 end end
        W.ow[ad] = c
    end
    if c.far then W.s.far = W.s.far + 1; return end
    if not (S2L.wsSafe(c.nm) and S2L.wsSafe(c.sk)) then return end
    W.s.npc = W.s.npc + 1
    local key = c.nm .. ",s," .. c.sk
    W.pend[key] = (W.pend[key] or 0) + 1
end
S2L.wsHook0 = M.hooks["/Script/Stalker2.ItemAppearanceComponent:OnShotSoundStart"]
M.hooks["/Script/Stalker2.ItemAppearanceComponent:OnShotSoundStart"] = function(ST, self, k, ...)
    local ok0, e0 = true, nil
    if S2L.wsHook0 then ok0, e0 = pcall(S2L.wsHook0, ST, self, k, ...) end
    if (FL.wshot or 0) == 1 and api.ROLE == "host" then
        local ok, e = pcall(S2L.wsShot, ST, self, k)
        if not ok then S2L.wsErr(ST, "gancho", e) end
    end
    if not ok0 then error(e0, 0) end                          -- the old body's error stays visible in main.lua's log
end
-- ===== b146voice (flags wvface / wvspk / wvsw, default 1; user 2026-10-15 21:15 "the npcs doesnt talk too, i cant hear the bandits as client").
-- Live 21:26: b144's voice source was empty (npcs_lidos=0): VoiceoverSpeechComponent is a UObject (SpeechComponent <- Object), not an
-- ActorComponent, so GetComponentByClass never found it. Every spoken line of a human NPC (bark, comment, dialog) has a lip-sync face
-- anim AS_FAC_<LANG>_[Voice<X>_]<SID> under Animations/CharacterHead/<folder>; its Wwise event is VO_<SID> under Events/Voiceover/<same
-- folder> (offline over the pak lists: 36153 of 37487 face anims map exactly; the rest are typos in boss lines / no event).
-- HOST  wvface: every 1 s the pawns within wsr cm (humans first) get their body + face AnimInstance (o.Mesh, FaceData.FaceMeshComponent);
--   every 0.2 s GetCurrentActiveMontage of each: a NEW montage whose anim is AS_FAC_* = a new line -> "v" entry "Voiceover/<folder>/VO_<SID>"
--   (checked loaded on the host, else looked up once by name), n = byte of the voice letter (Voice<X> = the NPC's actor voice), 1 = none.
--   The first sighting of an NPC only records. wvspk: every 1 s FindAllOf VoiceoverSpeechComponent -> speaker AkComponent -> owner in the
--   list -> its AkAudioEvent polled 0.2 s (a changed event = a new line, b144). One NPC + event within 3 s = sent once.
-- CLIENT wvsw: a letter -> the event plays on a one-shot AkComponent at the mirror's head with the Voice-<X> switch (SpawnAkComponentAtLocation
--   auto-destroy, SetSwitch, PostAkEventAndWaitForEnd without callback); no letter / failure -> b144 PostEventAtLocation.
-- shortcut: a line without a face montage on the body / face AnimInstance is heard only via wvspk (probe R0 measures both sources).
-- shortcut: the voice letter is the host's VO language's (VoiceEN / VoiceUA letters can differ); the sound stays where it started.
S2L.WVFA = "/Game/_STALKER2/Animations/CharacterHead/"
S2L.WVSW = "/Game/_STALKER2/Audio/WwiseAudio/Switches/Voiceover/Voice/Voice-"
function S2L.wvRule(path)                                    -- face anim object path -> event path under Events/ (no ".Name"), voice letter or "", SID
    local p = (tostring(path or ""):gsub("%.[^/]*$", ""))
    local folder, nm = p:match("^(.*)/([^/]+)$")
    if not nm then return nil end
    local lang, vx, sid = nm:match("^AS_FAC_(%u+)_Voice([%u%d])_(.+)$")
    if not lang then lang, sid = nm:match("^AS_FAC_(%u+)_(.+)$"); vx = "" end
    if not lang or folder:sub(1, #S2L.WVFA) ~= S2L.WVFA then return nil end
    local segs = {}
    for s in folder:sub(#S2L.WVFA + 1):gmatch("[^/]+") do segs[#segs + 1] = s end
    if segs[#segs] == lang then segs[#segs] = nil end
    if segs[#segs] and segs[#segs]:match("^Voice[%u%d]$") then segs[#segs] = nil end
    if #segs == 0 then return nil end
    if segs[1] ~= "General" and #segs >= 2 and segs[2]:sub(1, 3) ~= "VO_" then segs[2] = "VO_" .. segs[2] end
    return "Voiceover/" .. table.concat(segs, "/") .. "/VO_" .. sid, vx, sid
end
function S2L.wvSafe(s) return type(s) == "string" and #s > 0 and #s <= 200 and s:find("^[%w_/%.%-]+$") ~= nil end   -- voice paths reach 165 chars
function S2L.wvAnim(mo)                                      -- montage -> its face anim object path (AS_FAC_*), or nil
    local p = (mo:GetFullName():gsub("^%S+ ", ""))
    if p:find("/AS_FAC_", 1, true) then return p end
    local r
    pcall(function() r = mo.SlotAnimTracks[1].AnimTrack.AnimSegments[1].AnimReference end)
    if valid(r) then
        p = (r:GetFullName():gsub("^%S+ ", ""))
        if p:find("/AS_FAC_", 1, true) then return p end
    end
end
function S2L.wvFix(W, p, sid, now)                           -- HOST: the rule's event is loaded here (the host plays it)? else one lookup by name
    local nm = p:match("([^/]+)$")
    if valid(StaticFindObject(S2L.WSPFX .. p .. "." .. nm)) then W.s.vok = (W.s.vok or 0) + 1; return p end
    local c = W.vfix[sid]
    if c ~= nil then return c or p end
    local fo = rawget(_G, "FindObject")
    if not fo or now - (W.vfixAt or -100) < 2 then return p end   -- at most one full object search per 2 s
    W.vfixAt = now
    local o; pcall(function() o = fo("AkAudioEvent", nm, 0, 0) end)
    local q = valid(o) and (o:GetFullName():gsub("^%S+ ", "")) or ""
    if q:sub(1, #S2L.WSPFX) == S2L.WSPFX then
        q = (q:sub(#S2L.WSPFX + 1):gsub("%.[^/]*$", ""))
        W.vfix[sid] = q; W.s.vfind = (W.s.vfind or 0) + 1
        return q
    end
    W.vfix[sid] = false; W.s.vmiss = (W.s.vmiss or 0) + 1
    return p
end
function S2L.wvSend(W, nm, p, vx, src, now)                  -- HOST: one voice line -> pending "v" entry (one NPC + event within 3 s = once)
    if not (S2L.wvSafe(p) and S2L.wsSafe(nm)) then W.s.vlong = (W.s.vlong or 0) + 1; return end
    local r = W.vrec[nm]
    if r and r.p == p and now - r.t < 3 then W.s.vdup = (W.s.vdup or 0) + 1; return end
    if not r then W.vrecN = (W.vrecN or 0) + 1; if W.vrecN > 300 then W.vrec = {}; W.vrecN = 1 end end
    W.vrec[nm] = {p = p, t = now}
    W.pend[nm .. ",v," .. p] = (vx and vx ~= "") and string.byte(vx) or 1
    W.s.voice = W.s.voice + 1
    W.s[src] = (W.s[src] or 0) + 1
end
function S2L.wvScan(ST, W)                                   -- HOST, every 1 s: the pawns near the client, humans (face) first
    local hum, oth, byAd, r = {}, {}, {}, api.getRemote and api.getRemote()
    if not (r and r.x) then W.vlist = {}; return end
    local lp, P, skip, R2 = S2.io and S2.io.pawn, ST.pup, {}, (FL.wsr or 6000) ^ 2
    if valid(lp) then skip[lp:GetAddress()] = true end
    if P and valid(P.pawn) then skip[P.pawn:GetAddress()] = true end
    for _, o in ipairs(FindAllOf("Pawn") or {}) do
        if #hum >= (FL.wvmaxn or 12) or #hum + #oth >= 40 then break end
        if valid(o) and not skip[o:GetAddress()] then
            local q = o:K2_GetActorLocation()
            if not (q.X == 0 and q.Y == 0) and (q.X - r.x) ^ 2 + (q.Y - r.y) ^ 2 <= R2 then
                local e = {o = o, ad = o:GetAddress(), nm = o:GetFName():ToString()}
                if (FL.wvface or 0) == 1 then
                    pcall(function()
                        local bi = o.Mesh:GetAnimInstance()
                        if valid(bi) then
                            e.bi = bi
                            local fm = bi.FaceData.FaceMeshComponent
                            if valid(fm) then local fa = fm:GetAnimInstance(); if valid(fa) then e.fa = fa end end
                        end
                    end)
                end
                if e.fa then hum[#hum + 1] = e else oth[#oth + 1] = e end
            end
        end
    end
    for _, e in ipairs(oth) do if #hum >= (FL.wvmaxn or 12) then break end; hum[#hum + 1] = e end
    for _, e in ipairs(hum) do byAd[e.ad] = e end
    if (FL.wvspk or 0) == 1 then
        local ns = 0
        for _, vc in ipairs(FindAllOf("VoiceoverSpeechComponent") or {}) do
            ns = ns + 1
            if ns > 64 then break end
            if valid(vc) then pcall(function()
                local spk = vc.AkVoiceoverSpeakerComponent
                if valid(spk) then
                    local ow = spk:GetOwner()
                    local e = valid(ow) and byAd[ow:GetAddress()] or nil
                    if e then e.spk = spk end
                end
            end) end
        end
        W.vsc = ns
    end
    W.vlist = hum
end
function S2L.wvMont(W, e)                                    -- HOST: the face line now playing on e (montage key, anim path) or 0
    for _, ai in ipairs({e.fa or false, e.bi or false}) do
        if ai and valid(ai) then
            local mk, an
            pcall(function()
                local mo = ai:GetCurrentActiveMontage()
                if valid(mo) then
                    mk = mo:GetAddress() .. ":" .. mo:GetFName():ToString()
                    an = W.vma[mk]
                    if an == nil then
                        an = S2L.wvAnim(mo) or false
                        W.vmaN = (W.vmaN or 0) + 1; if W.vmaN > 500 then W.vma = {}; W.vmaN = 1 end
                        W.vma[mk] = an
                    end
                end
            end)
            if mk and an then return mk, an end
        end
    end
    return 0
end
function S2L.wvHost(ST, W, now)                              -- HOST: voice lines of the NPCs near the client (b146voice: face montage + speaker)
    if not W.vm then W.vm, W.vmN, W.vrec, W.vfix, W.vx, W.vma = {}, 0, {}, {}, {}, {} end
    if now - W.vAt >= 1 then W.vAt = now; S2L.wvScan(ST, W) end
    if now - W.vPoll < 0.2 then return end
    W.vPoll = now
    for _, e in ipairs(W.vlist) do
        if valid(e.o) then
            W.s.vnpc = W.s.vnpc + 1
            local key = e.ad .. ":" .. e.nm
            if e.fa or e.bi then
                local cur, ap = S2L.wvMont(W, e)
                local was = W.vm[key]
                if was == nil then W.vmN = W.vmN + 1; if W.vmN > 300 then W.vm = {}; W.vmN = 1 end end
                W.vm[key] = cur
                if was ~= nil and cur ~= 0 and cur ~= was then
                    local p, vx, sid = S2L.wvRule(ap)
                    if p then
                        if vx ~= "" then W.vx[key] = vx end
                        S2L.wvSend(W, e.nm, S2L.wvFix(W, p, sid, now), vx, "vface", now)
                    else W.s.vnorule = (W.s.vnorule or 0) + 1 end
                end
            end
            if e.spk and valid(e.spk) then
                local p
                pcall(function() local ev = e.spk.AkAudioEvent; if valid(ev) then p = (ev:GetFullName():gsub("^%S+ ", "")) end end)
                if p then
                    local was = W.vl[key]
                    if was == nil then W.vlN = W.vlN + 1; if W.vlN > 300 then W.vl = {}; W.vlN = 1 end end
                    W.vl[key] = p
                    if was ~= nil and was ~= p and not p:find("/Weapons/", 1, true) and p:sub(1, #S2L.WSPFX) == S2L.WSPFX then
                        S2L.wvSend(W, e.nm, (p:sub(#S2L.WSPFX + 1):gsub("%.[^/]*$", "")), W.vx[key], "vspk", now)
                    end
                end
            end
        end
    end
end
-- ===== end b146voice (host)
function S2L.wsFlush(ST, W, now)                             -- HOST: pending -> numbered entries, kept 0.6 s in every snapshot
    if now - W.flushAt < 0.2 then return end
    W.flushAt = now
    for key, n in pairs(W.pend) do
        W.seq = W.seq + 1
        W.q[#W.q + 1] = {t = now, s = W.seq .. "," .. key .. "," .. math.floor(n)}
        W.s.sent = W.s.sent + 1
    end
    W.pend = {}
    local keep = {}
    for _, x in ipairs(W.q) do if now - x.t < 0.6 then keep[#keep + 1] = x end end
    while #keep > 30 do table.remove(keep, 1) end
    W.q = keep
end
function S2L.wsOut(ST)                                       -- x1 part (host)
    local W = ST.ws
    if not W or #W.q == 0 or api.ROLE ~= "host" then return "" end
    local t = {}
    for _, x in ipairs(W.q) do t[#t + 1] = x.s end
    return "#WS:" .. table.concat(t, ";")
end
function S2L.wsAsset(W, p)                                   -- loaded once; a missing one is not asked again
    local a = W.ev[p]
    if a and valid(a) then return a end
    if a == false then return nil end
    a = asset(p)
    W.ev[p] = a or false
    return a
end
function S2L.wsTarget(ST, nm)                                -- CLIENT: where host NPC <nm> is shown -> actor, weapon comp, body comp
    do local okA, a, w, b = pcall(rawget(_G, "s2n_nat_actor") or function() end, nm); if okA and a and valid(a) then return a, w, b end end   -- b158natnpc
    local mt = S2L.clMirs and S2L.clMirs(ST)
    local m = type(mt) == "table" and mt[nm] or nil
    if type(m) == "table" and valid(m.actor) then
        local w = m.socks and m.socks[1]
        return m.actor, (valid(w) and w or nil), (valid(m.comp) and m.comp or nil)
    end
    local S, sx = ST.nps, nm:match("_(%d+)$")
    if S and sx and S.pr then
        for ck, hk in pairs(S.pr) do
            local e, c = S.h[hk], S.cl[ck]
            if e and e.nm == sx and c and valid(c.o) then
                local w; pcall(function() w = c.o:GetWeaponInHandsMeshComponent() end)
                return c.o, (valid(w) and w or nil), nil
            end
        end
    end
end
function S2L.wsMuzzle(W, a, w, body, sk)                     -- muzzle socket of the weapon part (cached per mesh), else weapon / hand / body
    if w then
        local mz = W.mz[sk]
        if mz == nil then
            mz = false
            for _, s in ipairs(S2L.WSMZ) do local f = FName(s); if w:DoesSocketExist(f) then mz = s; break end end
            W.mz[sk] = mz
        end
        if mz then local f = FName(mz); return w:GetSocketLocation(f), w:GetSocketRotation(f) end
        return w:K2_GetComponentLocation(), w:K2_GetComponentRotation()
    end
    if body then local f = FName("jnt_r_hand"); if body:DoesSocketExist(f) then return body:GetSocketLocation(f), a:K2_GetActorRotation() end end
    local q = a:K2_GetActorLocation()
    return {X = q.X, Y = q.Y, Z = q.Z + (body and 140 or 50)}, a:K2_GetActorRotation()
end
function S2L.wsFx(ST, W, nm, sk, tr)                         -- CLIENT: one shot of <nm> where it is NOW (called again after each delay)
    local a, w, body = S2L.wsTarget(ST, nm)
    if not a then W.s.notgt = W.s.notgt + 1; return end
    local now = os.clock()
    if now - W.budAt >= 1 then W.budAt = now; W.bud = 0 end
    if W.bud >= (FL.wsmax or 30) then W.s.cap = W.s.cap + 1; return end
    W.bud = W.bud + 1
    local row = S2L.WSHOT[sk] or S2L.WSHOT.SK_AK74
    local fam = S2L.WSFAM[row[2]] or S2L.WSFAM.A
    local loc, rot = S2L.wsMuzzle(W, a, w, body, sk)
    local ev = S2L.wsAsset(W, S2L.WSPFX .. row[1])
    if ev then M.dg.cdoOf("/Script/AkAudio.Default__AkGameplayStatics"):PostEventAtLocation(ev, loc, rot, a) else W.s.noev = W.s.noev + 1 end
    local nf = M.dg.cdoOf("/Script/Niagara.Default__NiagaraFunctionLibrary")
    local ns = S2L.wsAsset(W, "/Game/_Stalker_2/VFX/Weapons/MuzzleTP/NS_MuzzleTP_" .. fam[1] .. ".NS_MuzzleTP_" .. fam[1])
    if ns then nf:SpawnSystemAtLocation(a, ns, loc, rot, {X = 1, Y = 1, Z = 1}, true, true, 0, true) end
    if tr then
        local ts = S2L.wsAsset(W, "/Game/_Stalker_2/VFX/Weapons/a_Trace/NS_Projectile_NPC_" .. fam[2] .. ".NS_Projectile_NPC_" .. fam[2])
        if ts then nf:SpawnSystemAtLocation(a, ts, loc, rot, {X = 1, Y = 1, Z = 1}, true, true, 0, true) end
    end
    W.s.play = W.s.play + 1
    do local f = rawget(_G, "s2n_nat_fire"); if f then pcall(f, nm) end end   -- b168: the copy's own shoot montages with the shot
end
function S2L.wvSwitch(W, G, ev, vx, loc, a)                 -- b146voice CLIENT: one-shot AkComponent with the Voice-<X> switch; true = posted
    local sw = S2L.wsAsset(W, S2L.WVSW .. vx .. ".Voice-" .. vx)
    if not sw then return false end
    local c = G:SpawnAkComponentAtLocation(a, ev, loc, a:K2_GetActorRotation(), false, true)
    if not valid(c) then return false end
    c:SetSwitch(sw)
    W.vuid = (W.vuid or 0) % 1000000 + 1
    local id = c:PostAkEventAndWaitForEnd(ev, {Linkage = -1, UUID = 146000000 + W.vuid, ExecutionFunction = FName("None"), CallbackTarget = c})
    return (tonumber(id) or 0) ~= 0
end
function S2L.wvPlay(ST, W, e, now)                           -- CLIENT: one voice line at the NPC's head
    if (FL.wvoice or 0) < 1 then return end
    if now - (W.vgap[e.nm] or -100) < 1.5 then W.s.cap = W.s.cap + 1; return end
    local a, _, body = S2L.wsTarget(ST, e.nm)
    if not a then W.s.notgt = W.s.notgt + 1; return end
    local pl = e.pl:sub(1, 1) == "/" and e.pl or (S2L.WSPFX .. e.pl)
    if not pl:find("/[^/]*%.[^/]*$") then pl = pl .. "." .. pl:match("([^/]+)$") end   -- b146: "Voiceover/<folder>/VO_<SID>" (no ".Name")
    local ev = S2L.wsAsset(W, pl)
    if not ev then W.s.noev = W.s.noev + 1; return end
    W.vgap[e.nm] = now
    local q = a:K2_GetActorLocation()
    local loc = {X = q.X, Y = q.Y, Z = q.Z + (body and 160 or 70)}
    if body then pcall(function() local f = FName("jnt_head"); if body:DoesSocketExist(f) then local h = body:GetSocketLocation(f); loc = {X = h.X, Y = h.Y, Z = h.Z} end end) end
    local G = M.dg.cdoOf("/Script/AkAudio.Default__AkGameplayStatics")
    local vx = (e.n >= 48 and e.n <= 90) and string.char(e.n) or nil
    if (FL.wvsw or 0) == 1 and vx then
        local ok, r = pcall(S2L.wvSwitch, W, G, ev, vx, loc, a)
        if ok and r then W.s.vsw = (W.s.vsw or 0) + 1; W.s.vplay = W.s.vplay + 1; return end
        if not ok then S2L.wsErr(ST, "voz_troca", r) end
        W.s.vswf = (W.s.vswf or 0) + 1
    end
    G:PostEventAtLocation(ev, loc, a:K2_GetActorRotation(), a)
    W.s.vplay = W.s.vplay + 1
end
function S2L.wsPlay(ST, W, e, now)
    if (FL.wsfx or 0) < 1 then return end
    if e.k == "v" then return S2L.wvPlay(ST, W, e, now) end
    local row = S2L.WSHOT[e.pl] or S2L.WSHOT.SK_AK74
    local n, fi, tr = math.min(e.n, FL.wsnmax or 4), math.max(row[3], 0.05), (FL.wsfx or 0) >= 2
    S2L.wsFx(ST, W, e.nm, e.pl, tr)
    local dl = rawget(_G, "ExecuteInGameThreadWithDelay")
    if n > 1 and dl then
        for i = 2, n do
            local ms = math.floor(fi * 1000 * (i - 1))
            if ms > 190 then break end                        -- the next 0.2 s entry carries the rest
            pcall(dl, ms, function() local okD, eD = pcall(S2L.wsFx, ST, W, e.nm, e.pl, tr); if not okD then S2L.wsErr(ST, "atraso", eD) end end)
        end
    end
end
function S2L.wsParse(part)                                   -- "#WS" text -> entries sorted by seq
    local ents = {}
    for sq, nm, k, pl, n in tostring(part or ""):gmatch("(%d+),([^,;]+),([sv]),([^,;]+),(%d+)") do
        ents[#ents + 1] = {sq = tonumber(sq), nm = nm, k = k, pl = pl, n = tonumber(n)}
    end
    table.sort(ents, function(a, b) return a.sq < b.sq end)
    return ents
end
function S2L.wsClient(ST, W, now)
    local part = x1part("WS")
    if not part or part == "" then return end
    for _, e in ipairs(S2L.wsParse(part)) do
        if e.sq <= W.last and now - W.rxAt > 2 then W.last = e.sq - 1 end   -- host restarted (seq from 1 again after a silence)
        if e.sq > W.last then
            W.last = e.sq; W.rxAt = now; W.s.rx = W.s.rx + 1
            local ok, er = pcall(S2L.wsPlay, ST, W, e, now)
            if not ok then S2L.wsErr(ST, "tocar", er) end
        end
    end
end
function S2L.wsTick(ST)                                      -- tick100, both roles
    if (FL.wshot or 0) ~= 1 and (FL.wsfx or 0) < 1 and (FL.wvoice or 0) < 1 then return end
    local W, now = S2L.wsSt(ST), os.clock()
    if api.ROLE == "host" then
        if (FL.wvoice or 0) == 1 then local ok, e = pcall(S2L.wvHost, ST, W, now); if not ok then S2L.wsErr(ST, "voz", e) end end
        S2L.wsFlush(ST, W, now)
    else
        local ok, e = pcall(S2L.wsClient, ST, W, now)
        if not ok then S2L.wsErr(ST, "cliente", e) end
    end
    local s = W.s
    if now - s.at >= 10 then
        if api.ROLE == "host" then
            if s.hook + s.voice + s.sent + s.err + s.vnpc > 0 then
                log(string.format("S2CORE wshot 10 s (host): gancho=%d npc=%d meu=%d sem_self=%d longe=%d | voz: npcs_lidos=%d falas=%d (rosto=%d falante=%d comps=%d) evento ok=%d achado=%d falta=%d sem_regra=%d dup=%d longo=%d | enviados=%d erros=%d",
                    s.hook, s.npc, s.mine, s.noself, s.far, s.vnpc, s.voice, s.vface or 0, s.vspk or 0, W.vsc or -1, s.vok or 0, s.vfind or 0, s.vmiss or 0,
                    s.vnorule or 0, s.vdup or 0, s.vlong or 0, s.sent, s.err))
            end
        elseif s.rx + s.err > 0 then
            log(string.format("S2CORE wshot 10 s (cliente): recebidos=%d tiros=%d vozes=%d (troca=%d falhou=%d) sem_alvo=%d sem_evento=%d corte=%d erros=%d",
                s.rx, s.play, s.vplay, s.vsw or 0, s.vswf or 0, s.notgt, s.noev, s.cap, s.err))
        end
        W.s = {at = now, hook = 0, npc = 0, mine = 0, noself = 0, far = 0, sent = 0, voice = 0, vnpc = 0, rx = 0, play = 0, vplay = 0, notgt = 0, noev = 0, cap = 0, err = 0}
    end
end
S2L.wsSnap0 = M.snapExtra                                    -- #WS rides on x1 through a wrap (inside b128net's outermost wrap)
function M.snapExtra(ST, pawn, p)
    local base = S2L.wsSnap0 and S2L.wsSnap0(ST, pawn, p) or ""
    local ok, s = pcall(S2L.wsOut, ST)
    if not ok then S2L.wsErr(ST, "x1", s); s = "" end
    return (base or "") .. (type(s) == "string" and s or "")
end
-- ===== end b144combat
-- ===== b128net (flags netrel / netlockkeep, default ON; P5 network tolerance: Radmin VPN + python UDP relay, 5-10 s gaps seen).
-- Audit (OFFLINE_REPORT 150): every one-shot event rides in x1 for a fixed window and is then dropped, with no ack (#J 2 s, #QN #QP 3 s,
-- #G 4 s, #T #W #U #DR 6 s, #Y 2 s, #QI resend s). A link gap longer than the window loses it for good: quest step / choice / pickup never
-- reaches the other side (quest divergence). The quest catch-up runs only on a new session or a client load, never after a gap.
-- netrel=1: ONE generic ack + resend layer under x1 (no module changed; each module keeps its own apply code and dedupe):
--   sender   every snapshot carries "#AS:<my nonce>.<n>"; the peer echoes the newest one it PROCESSED as "#AK:<nonce>.<n>". Each entry of an
--            S2L.NRTAG part keeps its current RUN (carried in every snapshot since n0, by its module or by #RS): ack >= n0 = the peer saw it.
--            Dropped by its module before that, it joins a FIFO and rides in "#RS:<tag>=<entry>`<tag>=<entry>..." (queue prefix: at most
--            netrelper entries / 3000 chars a snapshot) until acked or netrelmax s. Only once the peer has ever acked us (no netrel there: no #RS).
--            ORDER: the receiver puts #RS entries IN FRONT of the same snapshot's live entries (old before new: #G Set, #QI absolute counts).
--            While a tag has entries in the queue its NEW live entries are taken out of their part and queued behind them (per-tag order
--            kept even when the backlog is longer than one snapshot can carry).
--   receiver x1part(): every NRTAG entry seen is remembered netrelseen s; an #RS entry never seen is merged in front of its own part for
--            netrelhold s, so the module applies it once with its own code. A peer session change (P.id) empties the layer.
-- netlockkeep=N: while the peer is silent (FL.peersilent) its story lock #LK and container lock #K keep their last value up to N s (N15:
-- block up front, nobody leads alone in a gap) instead of being dropped at once; 0 = old behaviour.
S2L.NRTAG = {J = true, QN = true, QP = true, QI = true, T = true, G = true, W = true, U = true, Y = true, DR = true}
setmetatable(S2L.NRTAG, {__index = function(_, t) if t == "O" and FL and (FL.nrdoor or 0) == 1 then return true end end})   -- b187 nrdoor (live switch)
function S2L.nrSt(ST)
    local R = ST.nr
    if not R then R = {n = 0, ack = 0, ins = 0, p = {}, q = {}, seen = {}, hold = {}, s = {rs = 0, rin = 0, dup = 0, gone = 0, at = os.clock()}}; ST.nr = R end
    return R
end
function S2L.nrSess(ST, R)                                 -- true = a peer session is known; a NEW one empties the layer
    local pid = ST.peer and ST.peer.id
    if pid ~= R.pid then
        if R.pid ~= nil then log(string.format("S2CORE netrel: nova sessao do outro lado -> %d pendentes e %d vistos esquecidos", S2L.nrCount(R.p), S2L.nrCount(R.seen))) end
        R.pid = pid; R.p = {}; R.q = {}; R.seen = {}; R.hold = {}; R.ack = 0; R.acked = nil; R.ackOut = nil; R.raw = nil; R.merged = nil
    end
    return pid ~= nil
end
-- ===== b143perf (both roles): cheaper bridge ticks. ffc=1 / x1c=1 (default); 0 = the old path, unchanged.
-- ffc: the globals FindFirstOf / FindAllOf (UE4SS 3.0.1, bUseUObjectArrayCache=false: every call walks GUObjectArray) go through
--   S2L.ffFirst / S2L.ffAll via persistent trampolines (S2.ffW; originals kept in S2.ffOrig). Inside ONE loop body (S2.ffBody, set by
--   main.lua b143perf around each loop body = one game frame) a repeated call returns the first call's result (ffcwin s at most);
--   FindFirstOf of a one-per-game class (S2L.FF_ONE, CustomConsoleManager*) is kept ffcttl s while it IsValid (a CustomConsoleManager*
--   miss too: those exist from the start or never). Installed from tick100 only over the untouched original (a probe's own wrapper is
--   never kicked out); ffc=0 or unload puts the originals back.
-- x1c: x1part cut "x1" out of the remote head and re-matched it on EVERY call (55 call sites per tick); now S2L.x1Raw cuts it once
--   per head text and S2L.x1Get splits the (netrel-merged) text into tags once; the first "#TAG:" wins, as with x1:match.
S2L.FF_ONE = {BP_Stalker2Character_C = true, PlayerController = true, PC = true, WeatherController = true, TimeManager = true,
    EnhancedInputLocalPlayerSubsystem = true}
S2L.FFC = {fh = 0, fm = 0, ah = 0, am = 0, fms = 0, ams = 0}   -- probes: first hit/miss, all hit/miss, ms spent in the originals
S2L.X1C = {cut = 0, cutHit = 0, split = 0, get = 0}
S2L.X1TAG = {}
function S2L.ffFirst(name)                                   -- FindFirstOf(name) with ffc=1
    local O, C, now, F = S2.ffOrig, S2.ffMem, os.clock(), S2L.FFC
    local e = C.f[name]
    if e then
        local age, ttl = now - e.at, FL.ffcttl or 1
        if (e.keep and age < ttl and valid(e.o)) or (e.gone and age < ttl)
            or (e.tok ~= nil and e.tok == S2.ffBody and age < (FL.ffcwin or 0.05)) then F.fh = F.fh + 1; return e.o end
    end
    local o = O.first(name)
    F.fm = F.fm + 1; F.fms = F.fms + (os.clock() - now) * 1000
    local ok, cm = valid(o), name:sub(1, 20) == "CustomConsoleManager"
    C.f[name] = {o = o, at = now, tok = S2.ffBody, keep = (ok and (S2L.FF_ONE[name] or cm)) or nil, gone = ((not ok) and cm) or nil}
    return o
end
function S2L.ffAll(name)                                     -- FindAllOf(name) with ffc=1: shared inside one loop body only
    local O, C, now, tok, F = S2.ffOrig, S2.ffMem, os.clock(), S2.ffBody, S2L.FFC
    if tok ~= nil and C.atok == tok then
        local e = C.a[name]
        if e and now - e.at < (FL.ffcwin or 0.05) then F.ah = F.ah + 1; return e.r end
    else C.a = {}; C.atok = tok end                          -- a new body (or none): the lists of the last one are dropped
    local r = O.all(name)
    F.am = F.am + 1; F.ams = F.ams + (os.clock() - now) * 1000
    if tok ~= nil then C.a[name] = {r = r, at = now} end
    return r
end
function S2L.ffInstall()                                     -- tick100: ffc=1 -> trampolines in the globals; ffc=0 -> originals back
    if not S2.ffOrig then
        local f, a = rawget(_G, "FindFirstOf"), rawget(_G, "FindAllOf")
        if type(f) ~= "function" or type(a) ~= "function" then return end
        S2.ffOrig = {first = f, all = a}
    end
    S2.ffMem = S2.ffMem or {f = {}, a = {}}
    local O, W = S2.ffOrig, S2.ffW
    if not W then                                            -- once per Lua state; they call whatever core is loaded now (S2.ffImpl)
        W = {}
        W.first = function(name, ...)
            local I = S2.ffImpl
            if I and type(name) == "string" and select("#", ...) == 0 then
                local ok, r = pcall(I.first, name)
                if ok then return r end
                S2.ffImpl = nil; S2.ffErr = tostring(r)          -- a bug here must never break FindFirstOf: back to the original
            end
            return S2.ffOrig.first(name, ...)
        end
        W.all = function(name, ...)
            local I = S2.ffImpl
            if I and type(name) == "string" and select("#", ...) == 0 then
                local ok, r = pcall(I.all, name)
                if ok then return r end
                S2.ffImpl = nil; S2.ffErr = tostring(r)
            end
            return S2.ffOrig.all(name, ...)
        end
        S2.ffW = W
    end
    if (FL.ffc or 0) ~= 1 then S2L.ffUninstall(); return end
    if S2.ffErr and not S2L.ffErrLog then S2L.ffErrLog = true; log("S2CORE ffc: erro no cache -> FindFirstOf/FindAllOf originais: " .. S2.ffErr) end
    if S2.ffErr then return end
    if not (S2.ffImpl and S2.ffImpl.first == S2L.ffFirst) then
        S2.ffImpl = {first = S2L.ffFirst, all = S2L.ffAll}; S2.ffMem = {f = {}, a = {}}
        log("S2CORE ffc: FindFirstOf/FindAllOf com cache (por corpo de laco; classes unicas " .. tostring(FL.ffcttl or 1) .. " s)")
    end
    if rawget(_G, "FindFirstOf") == O.first then rawset(_G, "FindFirstOf", W.first) end
    if rawget(_G, "FindAllOf") == O.all then rawset(_G, "FindAllOf", W.all) end
end
function S2L.ffUninstall()                                   -- ffc=0 / unload: originals back (only over our own trampolines)
    S2.ffImpl = nil
    local O, W = S2.ffOrig, S2.ffW
    if not (O and W) then return end
    if rawget(_G, "FindFirstOf") == W.first then rawset(_G, "FindFirstOf", O.first) end
    if rawget(_G, "FindAllOf") == W.all then rawset(_G, "FindAllOf", O.all) end
end
-- ===== b149hitch (both roles; flags acache / acneg, mshys / msstay, default ON). User 2026-10-15 22:05 "the client sometimes get stuters
-- in fps": b143 profiler 22:06 (client) StaticFindObject of /Game assets 9-39 ms per call (one 546 ms), Default__CppMediator 2.4 ms per
-- call (29 call sites; tick1000 4.1 ms per run). UE4SS 3.0.1 looks every path up again on every call.
-- acache: the globals StaticFindObject(path) / LoadAsset(path) go through persistent trampolines (S2.acW; originals in S2.acOrig; the
--   current core's S2L.acGet / S2L.acPut through S2.acImpl). Only paths of an asset, class or default object are kept: "/Script/..." or
--   "/Game/...", no ":" (no level actor, no sub-object), no PersistentLevel, exactly one "." in the last segment; World / Level objects
--   are never kept. A kept object is returned only while IsValid AND its class is the same as when it was found; a /Game miss of
--   StaticFindObject is kept acneg s (the caller's LoadAsset fills the entry). Any other call form goes to the original. Installed from
--   tick100 only over the untouched original (a probe's own wrapper is never kicked out); acache=0 or unload puts the originals back.
--   A bug in the cache (never the original's own error) turns it off for the session. Stats in S2.acSt (probes), log every 30 s.
-- mshys (host, mslot=1): an NPC already in the client's mirror set keeps its place msstay s, keeps its best class (threat / alive) for
--   msstay s, and after that is only pushed out by a newcomer mshys cm nearer (the set no longer churns on a 1 m difference or a threat
--   flicker; every push-out was a mirror destroyed and rebuilt on the client).
function S2L.acKey(p)                                          -- true = a path whose object may be kept for the session
    if type(p) ~= "string" or #p > 400 or p:find(":", 1, true) or p:find("PersistentLevel", 1, true) then return false end
    if p:sub(1, 8) ~= "/Script/" and p:sub(1, 6) ~= "/Game/" then return false end
    local last = p:match("[^/]+$") or ""
    local _, dots = last:gsub("%.", "")
    return dots == 1
end
function S2L.acGet(p, kind)                                    -- "hit", obj | "skip" | "miss" (ask the original, then acPut)
    local A = S2.acSt
    if not S2L.acKey(p) then A.skip = A.skip + 1; return "skip" end
    local C = S2.acMem
    local e = C.t[p]
    if e then
        if e.o then
            local ok = false
            pcall(function() ok = e.o:IsValid() and e.o:GetClass():GetAddress() == e.ca end)
            if ok then A.hit = A.hit + 1; return "hit", e.o end
        elseif kind == "sfo" and os.clock() - e.at < (FL.acneg or 5) then A.neg = A.neg + 1; return "hit", e.miss end
        C.t[p] = nil; C.n = C.n - 1
        if e.o then A.stale = A.stale + 1 end
    end
    return "miss"
end
function S2L.acPut(p, o, t0, kind)                             -- after the original: keep a found object (or a /Game StaticFindObject miss)
    local A, C = S2.acSt, S2.acMem
    local ms = (os.clock() - t0) * 1000
    A.miss = A.miss + 1; A.ms = A.ms + ms
    if ms > A.max then A.max, A.maxp = ms, tostring(p):match("[^/]+$") or "?" end
    if kind == "load" then A.lmiss = A.lmiss + 1 end
    local e
    if valid(o) then
        local ca, cn
        pcall(function() local c = o:GetClass(); ca = c:GetAddress(); cn = c:GetFName():ToString() end)
        if not ca or cn == "World" or cn == "Level" then return end
        e = {o = o, ca = ca}
    elseif kind == "sfo" and p:sub(1, 6) == "/Game/" then e = {miss = o, at = os.clock()}
    else return end
    if C.n >= 4000 then C.t = {}; C.n = 0; A.clr = A.clr + 1 end
    if not C.t[p] then C.n = C.n + 1 end
    C.t[p] = e
end
function S2L.acInstall()                                       -- tick100: acache=1 -> trampolines in the globals; acache=0 -> originals back
    if not S2.acOrig then
        local f, l = rawget(_G, "StaticFindObject"), rawget(_G, "LoadAsset")
        if type(f) ~= "function" then return end
        S2.acOrig = {sfo = f, load = (type(l) == "function") and l or nil}
    end
    S2.acMem = S2.acMem or {t = {}, n = 0}
    S2.acSt = S2.acSt or {hit = 0, neg = 0, miss = 0, lmiss = 0, skip = 0, stale = 0, ms = 0, max = 0, maxp = "-", clr = 0, logAt = os.clock()}
    local O, W = S2.acOrig, S2.acW
    if not W then                                              -- once per Lua state; they call whatever core is loaded now (S2.acImpl)
        W = {}
        local function tramp(kind)
            return function(p, ...)
                local I, orig = S2.acImpl, S2.acOrig[kind]
                if I and type(p) == "string" and select("#", ...) == 0 then
                    local ok, r, o = pcall(I.get, p, kind)
                    if not ok then S2.acImpl = nil; S2.acErr = tostring(r)
                    elseif r == "hit" then return o
                    elseif r == "miss" then
                        local t0 = os.clock()
                        local res = orig(p)                    -- the original's own errors reach the caller as before
                        local okP, eP = pcall(I.put, p, res, t0, kind)
                        if not okP then S2.acImpl = nil; S2.acErr = tostring(eP) end
                        return res
                    end
                end
                return orig(p, ...)
            end
        end
        W.sfo = tramp("sfo")
        if O.load then W.load = tramp("load") end
        S2.acW = W
    end
    if (FL.acache or 0) ~= 1 then S2L.acUninstall(); return end
    if S2.acErr and not S2L.acErrLog then S2L.acErrLog = true; log("S2CORE acache: erro no cache -> StaticFindObject/LoadAsset originais: " .. S2.acErr) end
    if S2.acErr then return end
    if not (S2.acImpl and S2.acImpl.get == S2L.acGet) then
        S2.acImpl = {get = S2L.acGet, put = S2L.acPut}
        log("S2CORE acache: StaticFindObject/LoadAsset com cache por caminho (assets, classes, objetos padrao; falta /Game guardada " .. tostring(FL.acneg or 5) .. " s)")
    end
    if rawget(_G, "StaticFindObject") == O.sfo then rawset(_G, "StaticFindObject", W.sfo) end
    if O.load and W.load and rawget(_G, "LoadAsset") == O.load then rawset(_G, "LoadAsset", W.load) end
    local A, now = S2.acSt, os.clock()
    if now - A.logAt >= 30 then
        if A.miss + A.hit > 0 then
            log(string.format("S2CORE acache 30 s: acertos=%d (falta guardada %d) buscas=%d (LoadAsset %d) ms_buscas=%.0f max=%.1f ms (%s) fora=%d invalidos=%d entradas=%d limpezas=%d",
                A.hit, A.neg, A.miss, A.lmiss, A.ms, A.max, A.maxp, A.skip, A.stale, S2.acMem.n, A.clr))
        end
        S2.acSt = {hit = 0, neg = 0, miss = 0, lmiss = 0, skip = 0, stale = 0, ms = 0, max = 0, maxp = "-", clr = 0, logAt = now}
    end
end
function S2L.acUninstall()                                     -- acache=0 / unload: originals back (only over our own trampolines)
    S2.acImpl = nil
    local O, W = S2.acOrig, S2.acW
    if not (O and W) then return end
    if rawget(_G, "StaticFindObject") == W.sfo then rawset(_G, "StaticFindObject", O.sfo) end
    if O.load and W.load and rawget(_G, "LoadAsset") == W.load then rawset(_G, "LoadAsset", O.load) end
end
function S2L.msHys(S, near, now)                               -- mshys: incumbents' rank (hc class, hd squared distance to the client)
    local inc, h, stay = S.inc or {}, FL.mshys or 500, FL.msstay or 4
    for _, e in ipairs(near) do
        local t = inc[e.path]
        if t then
            if now - t.at < stay then e.hc = -1
            else
                e.hc = (now - t.clsAt < stay and t.cls < e.cls) and t.cls or e.cls
                local d = math.max(0, math.sqrt(e.dc) - h); e.hd = d * d
            end
        end
    end
end
function S2L.msInc(S, near, now)                               -- the set just chosen = the incumbents of the next choice
    local old, new, stay = S.inc or {}, {}, FL.msstay or 4
    for _, e in ipairs(near) do
        local o = old[e.path]
        local t = {at = o and o.at or now, cls = e.cls, clsAt = now}
        if o and o.cls < e.cls and now - o.clsAt < stay then t.cls, t.clsAt = o.cls, o.clsAt end
        new[e.path] = t
    end
    S.inc = new
end
-- ===== end b149hitch (core)
function S2L.x1Raw(head)                                     -- the "x1" text of the remote head, cut once per head text
    if type(head) ~= "string" then return "" end
    local X = S2L.X1C
    if head ~= S2L.x1h then S2L.x1h = head; S2L.x1v = head:match('"x1"%s*:%s*"(.-)"') or ""; X.cut = X.cut + 1 else X.cutHit = X.cutHit + 1 end
    return S2L.x1v
end
function S2L.x1Get(head, tag)                                -- x1part with x1c=1: same answer as nrX1(cut):match("#"..tag..":([^#]*)")
    local x1 = S2L.x1Raw(head)
    if S2L.nrX1 then x1 = S2L.nrX1(x1) end
    if type(x1) ~= "string" then return nil end
    local X = S2L.X1C
    X.get = X.get + 1
    if x1 ~= S2L.x1s then
        local t = {}
        for k, v in x1:gmatch("#([A-Z][A-Z0-9]*):([^#]*)") do if t[k] == nil then t[k] = v end end
        S2L.x1s, S2L.x1t = x1, t; X.split = X.split + 1
    end
    local ok = S2L.X1TAG[tag]
    if ok == nil then ok = type(tag) == "string" and tag:find("^[A-Z][A-Z0-9]*$") ~= nil; S2L.X1TAG[tag] = ok end
    if ok then return S2L.x1t[tag] end
    return x1:match("#" .. tag .. ":([^#]*)")
end
-- ===== end b143perf
function S2L.nrCount(t) local n = 0; for _ in pairs(t) do n = n + 1 end return n end
function S2L.nrKeep(tag, P)                                -- x1part while the peer is silent: true = still give this tag (its last value)
    local k = FL.netlockkeep or 0
    return k > 0 and (tag == "LK" or tag == "K") and os.clock() - (P.silentAt or 0) < k
end
function S2L.nrX1(x1)                                      -- x1part: the remote x1 with held #RS entries merged in (cached per raw text)
    if (FL.netrel or 0) ~= 1 or type(x1) ~= "string" then return x1 end
    local ST = S2.state; if not ST then return x1 end
    local P = ST.peer
    if P and (P.state == "silent" or P.live == false) then return x1 end   -- x1part gives nothing then: nothing processed, nothing acked
    local R, now = S2L.nrSt(ST), os.clock()
    if not S2L.nrSess(ST, R) then return x1 end
    if R.raw == x1 and now < (R.rawT or 0) then return R.merged end
    if R.raw ~= x1 then                                    -- a new remote snapshot
        for tag, pay in x1:gmatch("#([A-Z][A-Z0-9]*):([^#]*)") do
            if S2L.NRTAG[tag] then for e in pay:gmatch("[^|]+") do R.seen[tag .. "=" .. e] = now end end
        end
        for k in (x1:match("#RS:([^#]*)") or ""):gmatch("[^`]+") do
            if R.seen[k] then R.s.dup = R.s.dup + 1
            else
                R.seen[k] = now; R.hold[#R.hold + 1] = {k = k, at = now}; R.s.rin = R.s.rin + 1
                if R.s.rin <= 20 then log("S2CORE netrel: recebido de novo (perdido na falha da rede): " .. k:sub(1, 160)) end
            end
        end
        local nc, n = x1:match("#AS:(%x+)%.(%d+)")
        if nc then R.ackOut = "#AK:" .. nc .. "." .. n end
        local mc, mn = x1:match("#AK:(%x+)%.(%d+)")
        if mc and mc == ST.me.nonce then R.acked = true; R.ack = math.max(R.ack, tonumber(mn) or 0) end
        if now - (R.pruneAt or -1e9) >= 5 then
            R.pruneAt = now
            local keep = FL.netrelseen or 300
            for k, at in pairs(R.seen) do if now - at > keep then R.seen[k] = nil end end
        end
    end
    R.raw = x1; R.rawT = now + 0.5
    local add, order, keep = {}, {}, {}
    for _, h in ipairs(R.hold) do
        if now - h.at < (FL.netrelhold or 6) then
            keep[#keep + 1] = h
            local tag, e = h.k:match("^([A-Z][A-Z0-9]*)=(.+)$")
            if tag then if not add[tag] then add[tag] = {}; order[#order + 1] = tag end; add[tag][#add[tag] + 1] = e end
        end
    end
    R.hold = keep
    local out = x1
    for _, tag in ipairs(order) do
        local list = table.concat(add[tag], "|")
        local _, b = out:find("#" .. tag .. ":", 1, true)
        if b then out = out:sub(1, b) .. list .. "|" .. out:sub(b + 1) else out = out .. "#" .. tag .. ":" .. list end
    end
    R.merged = out
    return out
end
function S2L.nrOut(ST, s)                                  -- my x1 -> + #RS (unacked entries the modules dropped) + #AS/#AK
    local R, now = S2L.nrSt(ST), os.clock()
    local head = S2.io and S2.io.head
    pcall(S2L.nrX1, (FL.x1c or 0) == 1 and S2L.x1Raw(head) or head and head:match('"x1"%s*:%s*"(.-)"') or "")   -- process the current remote snapshot first (ack = what x1part gives)
    local known = S2L.nrSess(ST, R)
    R.n = R.n + 1
    local cur, mx, per = {}, FL.netrelmax or 120, FL.netrelper or 40
    for _, q in pairs(R.p) do                              -- (1) ack: an entry carried in EVERY snapshot since n0 (q.run) is seen once ack >= n0
        if q.run and not q.done and R.ack >= q.n0 then q.done = true; q.run = false end
    end
    if known then
        for tag, pay in s:gmatch("#([A-Z][A-Z0-9]*):([^#]*)") do
            if S2L.NRTAG[tag] then
                for e in pay:gmatch("[^|]+") do
                    if not e:find("`", 1, true) then
                        local k = tag .. "=" .. e
                        cur[k] = true
                        if not R.p[k] then R.ins = R.ins + 1; R.p[k] = {k = k, at = now, o = R.ins} end
                    end
                end
            end
        end
    end
    local join = {}                                        -- (2) dropped by its module, not acked: joins the tail of the resend queue
    for k, q in pairs(R.p) do
        if q.done then if not cur[k] then R.p[k] = nil end  -- acked; forgotten once its module drops it
        elseif now - q.at > mx then R.p[k] = nil; R.s.gone = R.s.gone + 1; log("S2CORE netrel: desisti (sem ack em " .. tostring(mx) .. " s): " .. k:sub(1, 160))
        elseif not cur[k] and not q.inq and R.acked then join[#join + 1] = q end
    end
    table.sort(join, function(a, b) return a.o < b.o end)
    for _, q in ipairs(join) do q.inq = true; R.q[#R.q + 1] = q end
    local keepq, t, len, full = {}, {}, 0, false            -- FIFO: the carried entries are a PREFIX of the queue (only ever leave from it)
    for _, q in ipairs(R.q) do
        if R.p[q.k] == q and not q.done then
            keepq[#keepq + 1] = q
            if #t >= per or (#t > 0 and len + #q.k > 3000) then full = true end
            if not full then
                t[#t + 1] = q.k; len = len + #q.k + 1; q.inc = true
                if not q.rs then q.rs = true; R.s.rs = R.s.rs + 1; if R.s.rs <= 20 then log("S2CORE netrel: sem ack, reenviado: " .. q.k:sub(1, 160)) end end
            end
        end
    end
    R.q = keepq
    local blocked, out = nil, s                            -- per-tag ORDER: a tag with queued entries keeps its live ones back (queued instead)
    for _, q in ipairs(keepq) do blocked = blocked or {}; blocked[q.k:match("^([A-Z][A-Z0-9]*)=") or "?"] = true end
    if blocked then
        local late = {}
        out = (s:gsub("#([A-Z][A-Z0-9]*):([^#]*)", function(tag, pay)
            if not (blocked[tag] and S2L.NRTAG[tag]) then return nil end
            local kept = {}
            for e in pay:gmatch("[^|]+") do
                local k = tag .. "=" .. e; local q = R.p[k]
                if q and not q.done then cur[k] = nil; if not q.inq then q.inq = true; late[#late + 1] = q end else kept[#kept + 1] = e end
            end
            return "#" .. tag .. ":" .. table.concat(kept, "|")
        end))
        table.sort(late, function(a, b) return a.o < b.o end)
        for _, q in ipairs(late) do R.q[#R.q + 1] = q end
    end
    for k, q in pairs(R.p) do                              -- (3) runs: carried now (module or #RS) -> the run goes on / starts at n; else it breaks
        if not q.done then
            if cur[k] or q.inc then if not q.run then q.run = true; q.n0 = R.n end else q.run = false end
            q.inc = nil
        end
    end
    if #t > 0 then out = out .. "#RS:" .. table.concat(t, "`") end
    if now - R.s.at >= 10 then
        local S = R.s
        if S.rs + S.rin + S.dup + S.gone > 0 then
            local np = 0; for _, q in pairs(R.p) do if not q.done then np = np + 1 end end
            log(string.format("S2CORE netrel 10 s: pendentes=%d reenviados=%d recebidos_de_novo=%d duplicados_ignorados=%d desistidos=%d ack=%d/%d", np, S.rs, S.rin, S.dup, S.gone, R.ack, R.n))
        end
        R.s = {rs = 0, rin = 0, dup = 0, gone = 0, at = now}
    end
    return out .. "#AS:" .. ST.me.nonce .. "." .. R.n .. (R.ackOut or "")
end
S2L.nrSnap0 = M.snapExtra                                  -- outermost wrap: sees every x1 part (like #SL / #HC / #FE / #NP)
function M.snapExtra(ST, pawn, p)
    local base = S2L.nrSnap0 and S2L.nrSnap0(ST, pawn, p) or ""
    if (FL.netrel or 0) ~= 1 then return base end
    local ok, s = pcall(S2L.nrOut, ST, base)
    if ok and type(s) == "string" then return s end
    ST.nrErr = (ST.nrErr or 0) + 1; if ST.nrErr <= 5 then log("S2CORE netrel erro: " .. tostring(s)) end
    return base
end
-- ===== end b128net
return M
