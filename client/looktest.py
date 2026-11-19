"""Does the puppet look like the remote player and face where he faces?
A's player is read (appearance, head, facing); B's DLL should give A's puppet (slot 192) the same
appearance (head -1 is sent as 0, the model's own head) and, while A stands, A's facing.
A's player is turned with the test hook je.testSetDir (90, 250, 30 degrees); fails if A did not turn.
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
for deg in (90.0, 250.0, 30.0):
    # A's player is turned by the test hook je.testSetDir (writes its orientation vector; scripts cannot
    # turn the player); A's DLL reads it from memory and sends it; B's DLL turns the standing puppet.
    c, sn = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    A(); jeb.call("je.testSetDir", {"oid": int(round(c * 10000)) + 20000, "value": int(round(sn * 10000)) + 20000}); time.sleep(0.3)
    A(); md = jeb.call("je.netStatus")["result"]["myDir"]; af = math.degrees(math.atan2(md[1], md[0])) % 360
    time.sleep(2.0)
    B(); pf = ival(face_of(ncs.slot_push(NET_SLOT))) / 100; st = jeb.call("je.netStatus")["result"]
    chk("A turned to %.0f: B's puppet faces the same way (within 20 deg)" % deg, angd(af, deg) <= 20 and angd(af, pf) <= 20,
        {"A dir read by A's DLL": round(af, 1), "B remoteDir": st["remoteDir"], "puppet facing": pf, "faceSets": st["faceSets"]})
# after a walk: A walks 3 m at 1.5 m/s (the puppet follows), stops and turns; the puppet must face A's way and keep it
for k, (dx, dy, deg) in enumerate(((3, 0, 200.0), (0, 3, 300.0), (-3, 0, 45.0))):
    # walk there at 1.5 m/s in short teleports (B sees a walking player), then stand
    A(); p = bot.pos(); L = math.hypot(dx, dy); t0 = time.time()
    while True:
        f = min(1.0, 1.5 * (time.time() - t0) / L)
        A(); bot.load("jb_act", ncs.jump_self(p[0] + dx * f, p[1] + dy * f, a0[2])); bot.run("jb_go")
        if f >= 1.0: break
        time.sleep(0.05)
    time.sleep(1.0)
    c, sn = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    A(); jeb.call("je.testSetDir", {"oid": int(round(c * 10000)) + 20000, "value": int(round(sn * 10000)) + 20000})
    time.sleep(2.5); fs = []
    for j in range(4):
        B(); fs.append(ival(face_of(ncs.slot_push(NET_SLOT))) / 100); time.sleep(0.5)
    chk("after a walk, A turned to %.0f: B's puppet faces it and keeps it (4 samples over 2 s)" % deg, all(angd(f, deg) <= 20 for f in fs), {"puppet facings": fs})
A(); bot.load("jb_act", ncs.jump_self(*a0)); bot.run("jb_go")
B(); jeb.call("je.netTune", {"value": 0})
print("SUMMARY: %d/%d passed%s" % (sum(res), len(res), " (MUTATION: no look)" if MUT else ""))
