import json, math, time, bot, ncs
from jeb import call
res = []
def chk(n, ok, info): res.append(ok); print(("PASS " if ok else "FAIL ") + n + "  " + json.dumps(info)[:240])
def props():
    d = {}
    for name, r in (("dead", ncs.R["GetIsDead"]), ("hp", ncs.R["GetHealth"]), ("active", ncs.R["GetObjectActive"])):
        bot.load("jb_pr", ncs.slot_int(190, r)); d[name] = bot.ev("jb_pr")
    return d
def walk(dx, dy=0.0, wait=4.0):
    q0 = bot.puppet_pos(); t = (q0[0] + dx, q0[1] + dy, q0[2]); bot.puppet_move(*t); time.sleep(wait); q = bot.puppet_pos()
    return {"moved": round(math.dist(q0[:2], q[:2]), 2), "toTarget": round(math.dist(q[:2], t[:2]), 2)}
bot.setup(); bot.puppet_setup()
bot.load("jb_res", ncs.resurrect_slot(190)); bot.load("jb_hp", ncs.set_slot_health(190, 100)); bot.load("jb_qact0", ncs.set_slot_active(190, False))
p = bot.pos(); bot.spawn("j00h_abbot0", p[0] + 2.0, p[1] + 1.0, p[2]); time.sleep(0.4)
d = props(); chk("fresh spawn is dead (known)", d["dead"] == 1, d)
w = walk(3.0); chk("dead spawn does not walk (control)", w["moved"] < 0.3, w)
bot.run("jb_res"); time.sleep(0.5); d = props(); print("after EffectResurrection:", d)
if d["hp"] <= 0: bot.run("jb_hp"); time.sleep(0.3); d = props(); print("after SetHealth(100):", d)
chk("alive after resurrection", d["dead"] == 0, d)
w = walk(-3.0); chk("walks after resurrection", w["toTarget"] < 0.5 and w["moved"] > 2.0, w)
w = walk(0.0, 2.5); chk("walks again", w["toTarget"] < 0.5, w)
bot.run("jb_qact0"); time.sleep(0.8); d = props(); chk("deactivated", d["active"] == 0, d)
p1 = call("ping")["result"]["frames"]; time.sleep(1); p2 = call("ping")["result"]["frames"]; chk("game still running", p2 > p1, [p1, p2])
print("SUMMARY: %d/%d passed" % (sum(bool(x) for x in res), len(res)))
