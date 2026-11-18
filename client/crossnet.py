"""Game-to-game sync with no agent relay: each DLL sends its player over UDP and drives the other's puppet.
The test only teleports A's player and observes B's puppet (slot 192, owned by B's DLL).
Usage: python3 crossnet.py [--mutate-pause]   (mutation: je.netPause on B, so B stops driving its puppet)"""
import json, math, sys, time
import jeb, bot, ncs
MUT = "--mutate-pause" in sys.argv
res = []
def chk(n, ok, info): res.append(ok); print(("PASS " if ok else "FAIL ") + n + "  " + json.dumps(info)[:260])
def A(): jeb.use("test_install")
def B(): jeb.use("test_install_b")
NET_SLOT = 192
def slot_pos(slot):
    for i in range(3): bot.load("jx_s%d" % i, ncs.slot_pos_component(slot, i))
    return tuple(bot.ev("jx_s%d" % i) / 1000 for i in range(3))
START = (-68.979, 31.075, 23.284)    # open ground at the save's start point (prologue)
A(); bot.setup()
bot.load("jb_act", ncs.jump_self(*START)); bot.run("jb_go"); time.sleep(0.5); a0 = bot.pos()
B(); bot.setup(); b_player0 = bot.pos()
for inst in (A, B): inst(); jeb.call("je.netStart")
time.sleep(4.0)   # a few send ticks + spawn on both sides
for inst in (A, B):
    inst(); st = jeb.call("je.netStatus")["result"]; chk("%s net on, sending, receiving" % jeb.D.split("/")[-2], st["on"] and st["sent"] > 0 and st["recv"] > 0, st)
B(); st = jeb.call("je.netStatus")["result"]; chk("B spawned A's puppet by itself", st["puppet"] == "alive" and st["spawns"] >= 1, st)
A(); st = jeb.call("je.netStatus")["result"]; chk("A spawned B's puppet by itself", st["puppet"] == "alive" and st["spawns"] >= 1, st)
# isolate runs: let B's puppet settle next to A's start before anything is measured or paused
B(); jeb.call("je.netPause", {"value": 0})
t0 = time.time(); settle = None
while time.time() - t0 < 15:
    q = slot_pos(NET_SLOT); settle = math.dist(q[:2], a0[:2])
    if settle <= 1.6: break
    time.sleep(0.5)
chk("puppet settled next to A's start (setup)", settle is not None and settle <= 1.6, {"gap_m": round(settle, 2), "seconds": round(time.time() - t0, 1)})
B(); jeb.call("je.netPause", {"value": 1 if MUT else 0})
B(); q_start = slot_pos(NET_SLOT)
prev_a = a0
for i, (dx, dy) in enumerate([(6, 0), (6, 6), (0, 6), (0, 0)]):
    B(); qb = slot_pos(NET_SLOT)          # puppet position BEFORE A moves (the DLL reacts within ~100 ms)
    A(); bot.load("jb_act", ncs.jump_self(a0[0] + dx, a0[1] + dy, a0[2])); bot.run("jb_go"); time.sleep(0.2); pa = bot.pos()
    a_moved = round(math.dist(prev_a[:2], pa[:2]), 2); prev_a = pa
    B(); t0 = time.time(); gap = None
    while time.time() - t0 < 8.0:
        q = slot_pos(NET_SLOT); gap = math.dist(q[:2], pa[:2])
        if gap <= 1.6: break
        time.sleep(0.25)
    moved = round(math.dist(qb[:2], slot_pos(NET_SLOT)[:2]), 2)
    chk("leg %d: B's puppet follows A (no agent relay)" % (i + 1), gap <= 1.6 and moved > 2.0, {"A_moved_m": a_moved, "A_at": [round(v, 2) for v in pa[:2]], "moved_m": moved, "seconds": round(time.time() - t0, 2), "gap_m": round(gap, 2)})
B(); bp = bot.pos(); chk("B's own player untouched", math.dist(bp[:2], b_player0[:2]) < 0.2, {"B player": [round(v, 2) for v in bp]})
B(); print("B net:", jeb.call("je.netStatus")["result"]); jeb.call("je.netPause", {"value": 0})
A(); bot.load("jb_act", ncs.jump_self(*a0)); bot.run("jb_go")
print("SUMMARY: %d/%d passed%s" % (sum(bool(x) for x in res), len(res), " (MUTATION: B paused)" if MUT else ""))
