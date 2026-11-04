import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import gen_lockbehind as g

fail = 0


def check(c, msg):
    global fail
    if not c:
        fail += 1
        print("FAIL " + msg)


SAMPLE = """E03_MQ02_Trigger_ToLockDoor : struct.begin
\tSID = E03_MQ02_Trigger_ToLockDoor
\tQuestSID = E03_MQ02
\tNodeType = EQuestNodeType::Trigger
\tLaunchers : struct.begin
\t\t[0] : struct.begin
\t\t\tConnections : struct.begin
\t\t\t\t[0] : struct.begin
\t\t\t\t\tSID = E03_MQ02_SendSignal_2_18_E03_MQ02_BP_BunkerDoor_Factory_01.ReceiverOn
\t\t\t\tstruct.end
\t\t\tstruct.end
\t\tstruct.end
\tstruct.end
\tTriggerQuestGuid = 6AECDAB041291C3878348A83FCCFCD3C
struct.end
E03_MQ02_SendSignal_CloseLabDoor : struct.begin
\tSID = E03_MQ02_SendSignal_CloseLabDoor
\tQuestSID = E03_MQ02
\tNodeType = EQuestNodeType::SendSignal
\tLaunchers : struct.begin
\t\t[0] : struct.begin
\t\t\tConnections : struct.begin
\t\t\t\t[0] : struct.begin
\t\t\t\t\tSID = E03_MQ02_Trigger_ToLockDoor
\t\t\t\tstruct.end
\t\t\t\t[1] : struct.begin
\t\t\t\t\tSID = E03_MQ02_EnteredFirstCodelock
\t\t\t\tstruct.end
\t\t\tstruct.end
\t\tstruct.end
\tstruct.end
\tSignalReceiverGuid = CF46E2ED4ACF6B69A7A253BAD606DE65
struct.end
X_OnTriggerEnterEvent : struct.begin
\tSID = X_OnTriggerEnterEvent
\tQuestSID = X
\tNodeType = EQuestNodeType::Trigger
\tEventType = EQuestEventType::OnTriggerEnter
\tTriggerQuestGuid = 11111111111111111111111111111111
struct.end
X_OnTriggerExitEvent : struct.begin
\tSID = X_OnTriggerExitEvent
\tQuestSID = X
\tNodeType = EQuestNodeType::Trigger
\tEventType = EQuestEventType::OnTriggerExit
\tTriggerQuestGuid = 22222222222222222222222222222222
struct.end
X_SendSignal_BP_DoorView.UnlockDoorReceiver : struct.begin
\tSID = X_SendSignal_BP_DoorView.UnlockDoorReceiver
\tQuestSID = X
\tNodeType = EQuestNodeType::SendSignal
\tLaunchers : struct.begin
\t\t[0] : struct.begin
\t\t\tConnections : struct.begin
\t\t\t\t[0] : struct.begin
\t\t\t\t\tSID = X_OnTriggerEnterEvent
\t\t\t\tstruct.end
\t\t\tstruct.end
\t\tstruct.end
\tstruct.end
struct.end
X_SendSignal_Door_ForceClose : struct.begin
\tSID = X_SendSignal_Door_ForceClose
\tQuestSID = X
\tNodeType = EQuestNodeType::SendSignal
\tLaunchers : struct.begin
\t\t[0] : struct.begin
\t\t\tConnections : struct.begin
\t\t\t\t[0] : struct.begin
\t\t\t\t\tSID = X_OnTriggerExitEvent
\t\t\t\tstruct.end
\t\t\t\t[1] : struct.begin
\t\t\t\t\tSID = X_OnTriggerEnterEvent
\t\t\t\tstruct.end
\t\t\tstruct.end
\t\tstruct.end
\tstruct.end
struct.end
X_ActivateRestrictor_Go : struct.begin
\tSID = X_ActivateRestrictor_Go
\tQuestSID = X
\tNodeType = EQuestNodeType::ActivateRestrictor
\tLaunchers : struct.begin
\t\t[0] : struct.begin
\t\t\tConnections : struct.begin
\t\t\t\t[0] : struct.begin
\t\t\t\t\tSID = X_OnTriggerEnterEvent
\t\t\t\tstruct.end
\t\t\tstruct.end
\t\tstruct.end
\tstruct.end
struct.end
"""


def t_classify():
    check(g.classify("E03_MQ02_SendSignal_CloseLabDoor") == "close", "CloseLabDoor is close")
    check(g.classify("E16_MQ01_P_SendSignal_BP_BunkerDoor_10.ReceiverOff") == "close", "door ReceiverOff is close")
    check(g.classify("E06_MQ02_SendSignal_7_1_E06_MQ01_nii_Door_02.LockDoorReceiver") == "close", "LockDoorReceiver is close")
    check(g.classify("SQ20_SendSignal_ForceClose_Locker_Final_After") == "close", "ForceClose is close")
    check(g.classify("EQ146_SendSignal_BP_DoorView.UnlockDoorReceiver") == "open", "UnlockDoorReceiver is open, not lock")
    check(g.classify("E04_MQ02_SendSignal_BP_mod_stairs_6.UnblockLadderTopReceiver") == "open", "UnblockLadder is open")
    check(g.classify("E03_MQ02_SendSignal_2_18_E03_MQ02_BP_BunkerDoor_Factory_01.ReceiverOn") == "open", "door ReceiverOn is open")
    check(g.classify("E08_MQ05_SendSignal_BP_Military_Radio_E08_iskra.ReceiverOff") == "off", "radio off is not a door")
    check(g.classify("E08_MQ03_SendSignal_BP_CardlockX5ReceiverOn") == "ambiguous", "lock + ReceiverOn is ambiguous")
    check(g.classify("RookieVillage_DoorOpener_SendSignal_BP_BunkerDoor_06.ReceiverOff") == "ambiguous", "Opener + ReceiverOff is ambiguous")
    check(g.classify("E08_MQ04_SendSignal_6") == "unknown", "bare SendSignal is unknown")


def t_rows():
    nodes = {}
    g.parse_text(SAMPLE, nodes)
    check(len(nodes) == 7, "7 nodes parsed (%d)" % len(nodes))
    check(nodes["E03_MQ02_SendSignal_CloseLabDoor"]["from"] == ["E03_MQ02_Trigger_ToLockDoor", "E03_MQ02_EnteredFirstCodelock"], "launchers read from Connections")
    check(g.is_enter_trigger(nodes["E03_MQ02_Trigger_ToLockDoor"]), "Trigger node without EventType is an enter trigger")
    check(g.is_enter_trigger(nodes["X_OnTriggerEnterEvent"]), "OnTriggerEnter event is an enter trigger")
    check(not g.is_enter_trigger(nodes["X_OnTriggerExitEvent"]), "OnTriggerExit is not")
    rows = g.rows_of(nodes, {"E03_MQ02_Trigger_ToLockDoor"})
    by = {(r[1], r[2]): r for r in rows}
    r = by.get(("E03_MQ02_Trigger_ToLockDoor", "E03_MQ02_SendSignal_CloseLabDoor"))
    check(r is not None and r[0] == "E03_MQ02" and r[3] == "6AECDAB041291C3878348A83FCCFCD3C" and r[4] == "CF46E2ED4ACF6B69A7A253BAD606DE65" and r[5] == "close" and r[6] == "1",
          "E03_MQ02 lock-behind row: quest, trigger guid, receiver, close, geo")
    check(("X_OnTriggerEnterEvent", "X_SendSignal_BP_DoorView.UnlockDoorReceiver") not in by, "open rows are dropped")
    r = by.get(("X_OnTriggerEnterEvent", "X_SendSignal_Door_ForceClose"))
    check(r is not None and r[5] == "close" and r[6] == "0", "close row without trigger geometry keeps geo=0")
    check(("X_OnTriggerExitEvent", "X_SendSignal_Door_ForceClose") not in by, "exit triggers are not lock-behind")
    r = by.get(("X_OnTriggerEnterEvent", "X_ActivateRestrictor_Go"))
    check(r is not None and r[5] == "restrictor", "restrictor rows are listed as ambiguous kind restrictor")


def t_real():
    f = os.path.join(g.GD, "E03_MQ02.cfg")
    if not os.path.isfile(f):
        print("skip real E03_MQ02.cfg (no zonekit data)")
        return
    nodes = {}
    g.parse(f, nodes)
    rows = g.rows_of(nodes, g.trigger_sids())
    hit = [r for r in rows if r[1] == "E03_MQ02_Trigger_ToLockDoor" and r[2] == "E03_MQ02_SendSignal_CloseLabDoor"]
    check(len(hit) == 1 and hit[0][5] == "close" and hit[0][6] == "1", "real E03_MQ02.cfg yields the active CloseLabDoor row")


t_classify()
t_rows()
t_real()
print("test_gen_lockbehind: %s" % ("ok" if fail == 0 else "%d fail" % fail))
sys.exit(1 if fail else 0)
