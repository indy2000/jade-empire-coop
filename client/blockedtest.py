"""Synthetic 'cannot walk' case: B's puppet is made non-commandable (walk orders are refused), A moves 6 m.
Without the stuck rule the puppet stays behind; with it the DLL should jump it next to A."""
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
A(); bot.setup(); B(); bot.setup()
for inst in (A, B): inst(); jeb.call("je.netStart"); jeb.call("je.netPause", {"value": 0})
A(); bot.load("jb_act", ncs.jump_self(*START)); bot.run("jb_go"); time.sleep(4)
B(); bot.load("jx_cmd0", ncs.set_slot_commandable(NET_SLOT, False)); bot.load("jx_cmd1", ncs.set_slot_commandable(NET_SLOT, True))
bot.load("jx_iscmd", ncs.slot_int(NET_SLOT, ncs.R["GetCommandable"])); bot.run("jx_cmd0"); time.sleep(0.3)
print("puppet commandable:", bot.ev("jx_iscmd"))
for k, (dx, dy) in enumerate([(6, 0), (0, 6)]):
    A(); bot.load("jb_act", ncs.jump_self(START[0] + dx, START[1] + dy, START[2])); bot.run("jb_go"); time.sleep(0.3); pa = bot.pos()
    B(); t0 = time.time(); gap = None
    while time.time() - t0 < 8.0:
        q = slot_pos(NET_SLOT); gap = math.dist(q[:2], pa[:2])
        if gap <= 1.6: break
        time.sleep(0.3)
    st = jeb.call("je.netStatus")["result"]
    chk("blocked puppet %d reaches A within 8 s" % (k + 1), gap <= 1.6, {"gap_m": round(gap, 2), "seconds": round(time.time() - t0, 2), "stuckJumps": st.get("stuckJumps"), "server": jeb.call("version")["result"]["server"]})
    A(); bot.load("jb_act", ncs.jump_self(*START)); bot.run("jb_go"); time.sleep(0.3)
B(); bot.run("jx_cmd1")
print("SUMMARY: %d/%d passed" % (sum(bool(x) for x in res), len(res)))
