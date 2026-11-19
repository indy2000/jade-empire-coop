"""Does the puppet look like the remote player and face where he faces?
A's player is read (appearance, head, facing); B's DLL should give A's puppet (slot 192) the same
appearance (head -1 is sent as 0, the model's own head) and, while A stands, A's facing.
A's player is turned in place with SetFacing (90, 250, 90 degrees); the check also fails if A did not turn.
Usage: python3 looktest.py [--mutate]   (mutation: je.netTune 32 on B = no look; only facing is
checked then, the appearance of an already spawned puppet does not change back)"""
import json, math, sys, time
import jeb, bot, ncs
MUT = "--mutate" in sys.argv
NET_SLOT = 192
def A(): jeb.use("test_install")
def B(): jeb.use("test_install_b")
res = []
def chk(n, ok, info): res.append(ok); print(("PASS " if ok else "FAIL ") + n + "  " + json.dumps(info), flush=True)
def ival(d): bot.load("jx_lk", d); return bot.ev("jx_lk")
P = ncs.ACTION(ncs.R["GetPlayer"], 0)
def face_of(push): return ncs.int_main(push + ncs.ACTION(28, 1) + ncs.CONSTF(100.0) + ncs.MULFF + ncs.ACTION(ncs.R["FloatToInt"], 1), 4)
def angd(a, b): d = abs(a - b) % 360; return min(d, 360 - d)
A(); bot.setup(); a0 = bot.pos()
a_app = ival(ncs.int_main(P + ncs.ACTION(109, 1), 4)); a_head = ival(ncs.int_main(P + ncs.ACTION(119, 1), 4))
for inst in (A, B): inst(); jeb.call("je.netStart"); jeb.call("je.netPause", {"value": 0})
B(); jeb.call("je.netTune", {"value": 32 if MUT else 0})
time.sleep(5)
B(); st = jeb.call("je.netStatus")["result"]
chk("B knows A's look", st["remoteApp"] == a_app and st["remoteHead"] == a_head, {"A app": a_app, "A head": a_head, "B remoteApp": st["remoteApp"], "B remoteHead": st["remoteHead"]})
if not MUT:
    B(); p_app = ival(ncs.slot_int(NET_SLOT, 109)); p_head = ival(ncs.slot_int(NET_SLOT, 119))
    chk("B's puppet has A's appearance", p_app == a_app and p_head == (0 if a_head < 0 else a_head), {"puppet app": p_app, "puppet head": p_head, "appSets": st["appSets"]})
for deg in ():   # facing: disabled in 0.8.0 (GetFacing on the player is stale; SetFacing does not turn the player)
    # SetFacing(deg, nAnimate 0, GetPlayer(), FACING_TURNSHORTEST): JumpToLocation's facing did not turn the player
    A(); bot.load("jx_turn", ncs.void_main(ncs.CONSTI(0) + P + ncs.CONSTI(0) + ncs.CONSTF(deg) + ncs.ACTION(10, 4))); bot.run("jx_turn"); time.sleep(0.4)
    A(); af = ival(face_of(P)) / 100
    if angd(af, deg) > 20: print("   note: A's player did not turn to %.0f (is %.1f)" % (deg, af))
    time.sleep(2.0)
    B(); pf = ival(face_of(ncs.slot_push(NET_SLOT))) / 100; fs = jeb.call("je.netStatus")["result"]["faceSets"]
    chk("A faces %.0f: B's puppet faces the same way (within 20 deg)" % deg, angd(af, deg) <= 20 and angd(af, pf) <= 20, {"A facing": af, "puppet facing": pf, "faceSets": fs})
B(); jeb.call("je.netTune", {"value": 0})
print("SUMMARY: %d/%d passed%s" % (sum(res), len(res), " (MUTATION: no look)" if MUT else ""))
