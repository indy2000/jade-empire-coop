"""One creature, one change at a time: spawn -> walk? -> activate -> walk? -> deactivate -> active flag."""
import json, math, time, bot, ncs
from jeb import call
SLOT = ncs.PUPPET_SLOT
res = []
def chk(n, ok, info): res.append(ok); print(("PASS " if ok else "FAIL ") + n + "  " + json.dumps(info)[:240])
def walk(dx, dy=0.0, wait=4.0):
    q0 = bot.puppet_pos(); t = (q0[0] + dx, q0[1] + dy, q0[2])
    bot.puppet_move(*t); time.sleep(wait); q = bot.puppet_pos()
    return {"moved": round(math.dist(q0[:2], q[:2]), 2), "toTarget": round(math.dist(q[:2], t[:2]), 2), "from": [round(v, 2) for v in q0]}
bot.setup(); bot.puppet_setup()
bot.load("jb_qact1", ncs.set_slot_active(SLOT, True)); bot.load("jb_qact0", ncs.set_slot_active(SLOT, False)); bot.load("jb_qisact", ncs.slot_active(SLOT))
st = call("je.state")["result"]; print("state", st)
p = bot.pos(); print("player", p)
bot.spawn("j00h_abbot0", p[0] + 2.0, p[1] + 1.0, p[2]); time.sleep(0.5)
chk("spawned creature valid", bot.ev("jb_qok") == 1, {})
a0 = bot.ev("jb_qisact"); print("active right after CreateObject:", a0)
w1 = walk(3.0); print("walk before activate:", w1)
bot.run("jb_qact1"); time.sleep(0.5); a1 = bot.ev("jb_qisact")
chk("SetObjectActive(TRUE) -> active", a1 == 1, {"active": a1})
w2 = walk(-3.0); chk("walks after activate", w2["toTarget"] < 0.5 and w2["moved"] > 2.0, w2)
w3 = walk(0.0, 2.5); chk("walks again (second order)", w3["toTarget"] < 0.5, w3)
bot.run("jb_qact0"); time.sleep(1.0); a2 = bot.ev("jb_qisact")
chk("SetObjectActive(FALSE) -> inactive", a2 == 0, {"active": a2})
p1 = call("ping")["result"]["frames"]; time.sleep(1); p2 = call("ping")["result"]["frames"]
chk("game still running", p2 > p1, [p1, p2])
print("SUMMARY: %d/%d passed" % (sum(bool(x) for x in res), len(res)))
