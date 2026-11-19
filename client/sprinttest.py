"""Remote player faster than the puppet (the friend runs 5.8 m/s, the puppet 5.3): A laps the 6 m square
at 7 m/s for ~10 s by short teleports. A puppet that is moving must not be teleported by the stuck rule.
Counts B's stuckJumps per run: new rule (tune 0) vs the 0.7.3 rule (tune 3, old follow + old stuck rule).
Pass: tune 0 has 0 stuck jumps, and the old rule shows some (the test can see the problem)."""
import json, math, time
import jeb, bot, ncs
import os
def _start():  # 0.8.0: start where copy A's player stands (any area); JE_START=twin -> the Two Rivers point
    if os.environ.get("JE_START") == "twin": return (-68.979, 31.075, 23.284)
    jeb.use("test_install"); bot.setup(); return tuple(bot.pos())
START = _start()
def A(): jeb.use("test_install")
def B(): jeb.use("test_install_b")
A(); bot.setup()
for inst in (A, B): inst(); jeb.call("je.netStart"); jeb.call("je.netPause", {"value": 0})
pts = [(0, 0), (6, 0), (6, 6), (0, 6)]
def lap_run(seconds):
    t0 = time.time(); per = 6.0 / SPEED
    while time.time() - t0 < seconds:
        t = time.time() - t0; seg = int(t / per) % 4; f = (t % per) / per
        (x0, y0), (x1, y1) = pts[seg], pts[(seg + 1) % 4]
        A(); bot.load("jb_act", ncs.jump_self(START[0] + x0 + (x1 - x0) * f, START[1] + y0 + (y1 - y0) * f, START[2])); bot.run("jb_go")
res = {}
for tune in (0, 3, 0, 3):
    B(); jeb.call("je.netTune", {"value": tune})
    A(); bot.load("jb_act", ncs.jump_self(*START)); bot.run("jb_go"); time.sleep(3)
    B(); s0 = jeb.call("je.netStatus")["result"]["stuckJumps"]
    lap_run(10.0)
    B(); s1 = jeb.call("je.netStatus")["result"]["stuckJumps"]
    res.setdefault(tune, []).append(s1 - s0); print("tune", tune, "stuck jumps in 10 s:", s1 - s0, flush=True)
B(); jeb.call("je.netTune", {"value": 0})
ok = sum(res[0]) == 0 and sum(res[3]) > 0
print(json.dumps(res)); print("SUMMARY: %s" % ("PASS" if ok else "FAIL"))
