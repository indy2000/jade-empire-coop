import json, math, time, bot, ncs
from jeb import call
res = []
def chk(n, ok, info): res.append(ok); print(("PASS " if ok else "FAIL ") + n + "  " + json.dumps(info)[:240])
def walk_npc(dx):
    bot.load("jb_n", ncs.set_slot_nearest_creature(191, 1)); bot.run("jb_n")
    for i in range(3): bot.load("jb_n%d" % i, ncs.slot_pos_component(191, i))
    npos = lambda: tuple(bot.ev("jb_n%d" % i) / 1000 for i in range(3))
    q0 = npos(); t = (q0[0] + dx, q0[1], q0[2])
    bot.load("jb_qact", ncs.move_self(*t)); bot.load("jb_ngo", ncs.exec_on_slot(191, "jb_qact")); bot.run("jb_ngo")
    time.sleep(4.0); q = npos()
    return round(math.dist(q0[:2], q[:2]), 2), round(math.dist(q[:2], t[:2]), 2)
st = call("je.state")["result"]; chk("game unfocused (appActive=0)", st["appActive"] == 0, st)
moved, left = walk_npc(3.0); chk("unfocused, keepActive off: NPC does not walk", moved < 0.3, {"moved": moved, "toTarget": left})
r = call("je.keepActive", {"value": 1}); chk("keepActive on", r["result"]["keepActive"] == 1 and r["result"]["appActive"] == 1, r)
moved, left = walk_npc(-3.0); chk("unfocused, keepActive on: NPC walks to target", left < 0.5 and moved > 2.0, {"moved": moved, "toTarget": left})
r = call("je.keepActive", {"value": 0}); time.sleep(0.5)
st = call("je.state")["result"]; chk("keepActive off -> flag no longer forced", st["keepActive"] == 0, st)
p1 = call("ping")["result"]["frames"]; time.sleep(1.5); p2 = call("ping")["result"]["frames"]; chk("game still running", p2 > p1, [p1, p2])
print("SUMMARY: %d/%d passed" % (sum(bool(x) for x in res), len(res)))
