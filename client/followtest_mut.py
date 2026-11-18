"""Automated follow test: the player is teleported along a square path; the puppet must catch up each time.
Models the remote-player avatar: position updates arrive, the puppet walks/runs to them."""
import json, math, time, bot, ncs
from jeb import call
res = []
def chk(n, ok, info): res.append(ok); print(("PASS " if ok else "FAIL ") + n + "  " + json.dumps(info)[:240])
bot.setup(); bot.puppet_setup()
bot.load("jb_res", ncs.resurrect_slot(190)); bot.load("jb_qact0", ncs.set_slot_active(190, False))
home = bot.pos()
bot.spawn("j00h_abbot0", home[0] + 1.5, home[1], home[2]); time.sleep(0.3)  # MUTATION: no resurrection
def follow_until(target, limit=5.0, stop_at=1.6):
    t0 = time.time(); last_order = None; orders = 0
    while time.time() - t0 < limit:
        q = bot.puppet_pos(); d = math.dist(q[:2], target[:2])
        if d <= stop_at: return round(time.time() - t0, 2), round(d, 2), orders
        goal = (target[0] - 1.0, target[1], target[2])              # stand 1 m beside the player
        if last_order is None or math.dist(goal[:2], last_order[:2]) > 0.5:
            bot.puppet_move(*goal, run_=1 if d > 4 else 0); last_order = goal; orders += 1
        time.sleep(0.25)
    q = bot.puppet_pos(); return None, round(math.dist(q[:2], target[:2]), 2), orders
path = [(6, 0), (6, 6), (0, 6), (0, 0)]
for i, (dx, dy) in enumerate(path):
    tgt = (home[0] + dx, home[1] + dy, home[2])
    call("je.runScript", {"resref": "jb_go"}) if False else None
    bot.load("jb_act", ncs.jump_self(*tgt)); bot.run("jb_go"); time.sleep(0.3)
    q_before = bot.puppet_pos(); pl = bot.pos(); took, gap, orders = follow_until(pl)
    moved = round(math.dist(q_before[:2], bot.puppet_pos()[:2]), 2)
    chk("leg %d: puppet reaches player at %s" % (i + 1, [round(v, 1) for v in pl[:2]]), took is not None and moved > 2.0, {"moved_m": moved, "seconds": took, "gap_m": gap, "orders": orders})
bot.load("jb_act", ncs.jump_self(*home)); bot.run("jb_go")
bot.run("jb_qact0")
print("SUMMARY: %d/%d passed" % (sum(bool(x) for x in res), len(res)))
