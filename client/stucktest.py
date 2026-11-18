"""Puppet must reach A's player even where walking is blocked (points where 0.6.1's puppet stayed 8.6-9.4 m away).
Starts from the open start point, puppet settled; then A jumps to each blocked-route point."""
import json, math, time
import jeb, bot, ncs
res = []
def chk(n, ok, info): res.append(ok); print(("PASS " if ok else "FAIL ") + n + "  " + json.dumps(info)[:220])
def A(): jeb.use("test_install")
def B(): jeb.use("test_install_b")
NET_SLOT = 192
def slot_pos(slot):
    for i in range(3): bot.load("jx_s%d" % i, ncs.slot_pos_component(slot, i))
    return tuple(bot.ev("jx_s%d" % i) / 1000 for i in range(3))
START = (-68.979, 31.075, 23.284)
HARD = [(-62.84, 31.61, 23.284), (-61.84, 38.62, 23.284), (-67.84, 38.62, 23.284)]
A(); bot.setup(); B(); bot.setup()
for inst in (A, B): inst(); jeb.call("je.netStart"); jeb.call("je.netPause", {"value": 0})
for k, tgt in enumerate(HARD):
    A(); bot.load("jb_act", ncs.jump_self(*START)); bot.run("jb_go"); time.sleep(4)          # reset next to start
    A(); bot.load("jb_act", ncs.jump_self(*tgt)); bot.run("jb_go"); time.sleep(0.3); pa = bot.pos()
    B(); t0 = time.time(); gap = None
    while time.time() - t0 < 8.0:
        q = slot_pos(NET_SLOT); gap = math.dist(q[:2], pa[:2])
        if gap <= 1.6: break
        time.sleep(0.3)
    st = jeb.call("je.netStatus")["result"]
    chk("hard point %d: puppet reaches A within 8 s" % (k + 1), gap <= 1.6, {"A_at": [round(v, 2) for v in pa[:2]], "gap_m": round(gap, 2), "seconds": round(time.time() - t0, 2), "stuckJumps": st.get("stuckJumps")})
A(); bot.load("jb_act", ncs.jump_self(*START)); bot.run("jb_go")
print("SUMMARY: %d/%d passed" % (sum(bool(x) for x in res), len(res)))
