import jeb, bot, ncs, time, math, json
S = 192
def B(): jeb.use("test_install_b")
B(); jeb.call("je.netPause", {"value": 1}); bot.setup()
for i in range(3): bot.load("jx_s%d" % i, ncs.slot_pos_component(S, i))
def pp(): return [v / 1000 for v in jeb.call("je.evalMany", {"names": "jx_s0,jx_s1,jx_s2"})["result"]["values"]]
bot.load("jx_go", ncs.exec_on_slot(S, "jx_act"))
START = (-68.979, 31.075, 23.284)
def clear_move(x, y, z, rt): return ncs.void_main(ncs.ACTION(ncs.R["ClearAllActions"], 0) + ncs.CONSTI(-1) + ncs.CONSTI(rt) + ncs.location_here_area(x, y, z, 0.0) + ncs.ACTION(ncs.R["ActionMoveToLocation"], 3))
def reset():
    bot.load("jx_act", ncs.void_main(ncs.ACTION(ncs.R["ClearAllActions"], 0) + ncs.CONSTI(1) + ncs.location_here_area(*START, 0.0) + ncs.ACTION(ncs.R["JumpToLocation"], 2))); bot.run("jx_go"); time.sleep(0.8)
import sys
BOOST = int(sys.argv[1]) if len(sys.argv) > 1 else 0
EFF = int(sys.argv[2]) if len(sys.argv) > 2 else 165
DUR = int(sys.argv[3]) if len(sys.argv) > 3 else 2
if BOOST:
    bot.load("jx_eff", ncs.void_main(ncs.CONSTF(30.0 if DUR == 1 else 0.0) + ncs.CONSTI(BOOST) + ncs.ACTION(EFF, 1) + ncs.CONSTI(DUR) + ncs.slot_push(S) + ncs.ACTION(220, 4)))
    print("apply effect", EFF, BOOST, "dur", DUR, bot.run("jx_eff"))
p0 = pp(); print("puppet at", p0)
for rt, name in ((0, "walk"), (1, "run"), (3, "jog")):
    reset(); start = pp(); dest = (start[0] + 6, start[1], start[2])
    bot.load("jx_act", clear_move(*dest, rt)); bot.run("jx_go"); t0 = time.time(); tr = []
    while time.time() - t0 < 6:
        q = pp(); tr.append((time.time() - t0, q))
        if math.dist(q[:2], dest[:2]) < 0.3: break
    moving = [(t, q) for t, q in tr if math.dist(q[:2], start[:2]) > 0.5 and math.dist(q[:2], dest[:2]) > 0.5]
    sp = None
    if len(moving) >= 2: (ta, qa), (tb, qb) = moving[0], moving[-1]; sp = math.dist(qa[:2], qb[:2]) / (tb - ta) if tb > ta else None
    print(name, "steady speed m/s", sp and round(sp, 2), "total s", round(tr[-1][0], 2), "end", [round(v, 2) for v in tr[-1][1][:2]])
    if rt == 0:  # walk back to start for the next try
        pass
B(); jeb.call("je.netPause", {"value": 0})
