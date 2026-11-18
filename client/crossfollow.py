"""Two game instances: A's player is teleported around; B shows A's player as a puppet that must follow.
The relay (this script) reads A and drives B. Usage: python3 crossfollow.py [--mutate-no-relay]"""
import json, math, sys, time
import jeb, bot, ncs
MUT = "--mutate-no-relay" in sys.argv
res = []
def chk(n, ok, info): res.append(ok); print(("PASS " if ok else "FAIL ") + n + "  " + json.dumps(info)[:240])
def A(): jeb.use("test_install")
def B(): jeb.use("test_install_b")
A(); bot.setup(); a0 = bot.pos()
B(); bot.setup(); bot.puppet_setup(); b_player0 = bot.pos()
bot.load("jb_res", ncs.resurrect_slot(190)); bot.load("jb_qact0", ncs.set_slot_active(190, False))
bot.spawn("j00h_abbot0", a0[0] + 1.0, a0[1], a0[2]); time.sleep(0.3); bot.run("jb_res"); time.sleep(0.3)
print("A player", [round(v, 2) for v in a0], "| B puppet", [round(v, 2) for v in bot.puppet_pos()], "| B player", [round(v, 2) for v in b_player0])
def relay_until(limit=8.0, stop_at=1.6):
    t0 = time.time(); last = None; orders = 0; gap = None
    while time.time() - t0 < limit:
        A(); pa = bot.pos()
        B(); q = bot.puppet_pos(); gap = math.dist(q[:2], pa[:2])
        if gap <= stop_at: return round(time.time() - t0, 2), round(gap, 2), orders
        goal = (pa[0] - 1.0, pa[1], pa[2])
        if not MUT and (last is None or math.dist(goal[:2], last[:2]) > 0.5):
            bot.puppet_move(*goal, run_=1 if gap > 4 else 0); last = goal; orders += 1
        time.sleep(0.25)
    return None, round(gap, 2), orders
for i, (dx, dy) in enumerate([(6, 0), (6, 6), (0, 6), (0, 0)]):
    A(); bot.load("jb_act", ncs.jump_self(a0[0] + dx, a0[1] + dy, a0[2])); bot.run("jb_go"); time.sleep(0.3)
    B(); qb = bot.puppet_pos()
    took, gap, orders = relay_until()
    B(); moved = round(math.dist(qb[:2], bot.puppet_pos()[:2]), 2)
    chk("leg %d: B's puppet follows A's player" % (i + 1), took is not None and moved > 2.0, {"moved_m": moved, "seconds": took, "gap_m": gap, "orders": orders})
B(); bp = bot.pos(); chk("B's own player untouched", math.dist(bp[:2], b_player0[:2]) < 0.2, {"B player": [round(v, 2) for v in bp]})
bot.run("jb_qact0")
A(); bot.load("jb_act", ncs.jump_self(*a0)); bot.run("jb_go")
print("SUMMARY: %d/%d passed%s" % (sum(bool(x) for x in res), len(res), " (MUTATION: relay off)" if MUT else ""))
