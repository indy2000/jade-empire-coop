"""Does the puppet copy the remote player's gait (walk vs run)?
A moves 6 m at WALK speed (1.5 m/s), then 6 m at RUN speed (4.5 m/s), by short teleports (the player
ignores scripted move actions; B only sees positions). B's DLL counts the walk/run orders it gives
A's puppet (je.netStatus walkOrders / runOrders).
Orders are counted only while A moves. Pass per phase: walk phase gets more walk orders than run orders, run phase more run orders.
Usage: python3 gaittest.py [--mutate]   (mutation: je.netTune 8 = the 0.7.2 rule, which stays in run)"""
import json, math, sys, time
import jeb, bot, ncs
MUT = "--mutate" in sys.argv
import os
def _start():  # 0.8.0: start where copy A's player stands (any area); JE_START=twin -> the Two Rivers point
    if os.environ.get("JE_START") == "twin": return (-68.979, 31.075, 23.284)
    jeb.use("test_install"); bot.setup(); return tuple(bot.pos())
START = _start()
def A(): jeb.use("test_install")
def B(): jeb.use("test_install_b")
def status(): B(); return jeb.call("je.netStatus")["result"]
res = []
def chk(n, ok, info): res.append(ok); print(("PASS " if ok else "FAIL ") + n + "  " + json.dumps(info), flush=True)

A(); bot.setup()
for inst in (A, B): inst(); jeb.call("je.netStart"); jeb.call("je.netPause", {"value": 0})
B(); jeb.call("je.netTune", {"value": 8 if MUT else 0})

def go(x0, x1, speed, y):
    """Teleport A along x from x0 to x1 at `speed`; returns B's status right when A stops (orders given
    while A was moving), then waits for the puppet to catch up."""
    L = abs(x1 - x0); t0 = time.time()
    while True:
        f = min(1.0, speed * (time.time() - t0) / L)
        A(); bot.load("jb_act", ncs.jump_self(START[0] + x0 + (x1 - x0) * f, START[1] + y, START[2])); bot.run("jb_go")
        if f >= 1.0: break
        time.sleep(0.05)
    st = status(); time.sleep(2.5); return st

for rnd in range(2):
    y = 0.0 if rnd == 0 else 3.0
    A(); bot.load("jb_act", ncs.jump_self(START[0], START[1] + y, START[2])); bot.run("jb_go"); time.sleep(3.0)
    s0 = status(); s1 = go(0.0, 5.0, 1.5, y)
    s1b = status(); s2 = go(5.0, 0.0, 4.5, y)
    w = {"walkOrders": s1["walkOrders"] - s0["walkOrders"], "runOrders": s1["runOrders"] - s0["runOrders"]}
    r = {"walkOrders": s2["walkOrders"] - s1b["walkOrders"], "runOrders": s2["runOrders"] - s1b["runOrders"]}
    chk("round %d: A walks 5 m -> puppet mostly walks" % (rnd + 1), w["walkOrders"] > w["runOrders"], w)
    chk("round %d: A runs 5 m -> puppet mostly runs" % (rnd + 1), r["runOrders"] > r["walkOrders"], r)
B(); jeb.call("je.netTune", {"value": 0})
print("SUMMARY: %d/%d passed%s" % (sum(res), len(res), " (MUTATION: 0.7.2 rule)" if MUT else ""))
