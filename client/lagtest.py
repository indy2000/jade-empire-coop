"""How far behind does the remote player's puppet trail a running player?
A's player "runs" around a 6 m square at 4.5 m/s: the player ignores scripted move actions, so the test
teleports it in short steps along the line (B only sees a stream of positions either way).
B's DLL drives A's puppet (slot 192).
Per leg: A_s = time until A arrives, P_s = time until the puppet stands next to A again
(<= 1.6 m from the point 1 m beside A's end point, where the puppet aims), react_s = time until
the puppet has moved 1 m, delay = P_s - A_s, maxGap = largest A-puppet distance seen.
Runs the 0.7.2 follow (tune 0), 0.7.2 without clearing the puppet's action queue (tune 4) and the
0.7.1 behaviour (tune 3: one script per frame, walk until 4 m behind, queued orders) in turn, set on B with je.netTune, so both are measured in the same session.
Pass: new mean delay <= 0.6 s and below the old mean. Usage: python3 lagtest.py [rounds]"""
import json, math, sys, time
import jeb, bot, ncs
ROUNDS = int(sys.argv[1]) if len(sys.argv) > 1 else 2
NET_SLOT = 192
START = (-68.979, 31.075, 23.284)
def A(): jeb.use("test_install")
def B(): jeb.use("test_install_b")
def many(names):
    r = jeb.call("je.evalMany", {"names": ",".join(names)}); return [v / 1000 for v in r["result"]["values"]]
def a_pos(): A(); return many(["jb_px", "jb_py", "jb_pz"])
def p_pos(): B(); return many(["jx_s0", "jx_s1", "jx_s2"])

A(); bot.setup(); bot.load("jb_act", ncs.jump_self(*START)); bot.run("jb_go")
B(); bot.setup()
for i in range(3): bot.load("jx_s%d" % i, ncs.slot_pos_component(NET_SLOT, i))
for inst in (A, B): inst(); jeb.call("je.netStart"); jeb.call("je.netPause", {"value": 0})
time.sleep(4)
B(); st = jeb.call("je.netStatus")["result"]; assert st["puppet"] == "alive", st

def settle():
    t0 = time.time()
    while time.time() - t0 < 10:
        a = a_pos(); p = p_pos()
        if math.dist(a[:2], p[:2]) <= 1.6: return True
        time.sleep(0.3)
    return False

SPEED = 4.5
cur = list(START)
def leg(dx, dy):
    global cur
    beg = tuple(cur); end = (START[0] + dx, START[1] + dy, START[2]); L = math.dist(beg[:2], end[:2])
    spot = (end[0] - 1.0, end[1]); p0 = p_pos(); tr = None; afin = None
    t0 = time.time(); ta = tp = None; gmax = 0.0; samples = 0
    while time.time() - t0 < 12:
        t = time.time() - t0
        if ta is None:
            f = min(1.0, SPEED * t / L)
            w = (beg[0] + (end[0] - beg[0]) * f, beg[1] + (end[1] - beg[1]) * f, START[2])
            A(); bot.load("jb_act", ncs.jump_self(*w)); bot.run("jb_go")
            if f >= 1.0: ta = time.time() - t0
        if ta is not None and afin is None: afin = a_pos(); spot = (afin[0] - 1.0, afin[1])   # where A really stopped
        p = p_pos(); samples += 1; t = time.time() - t0
        gmax = max(gmax, math.dist(w[:2], p[:2]))
        if tr is None and math.dist(p[:2], p0[:2]) >= 1.0: tr = t
        if ta is not None and math.dist(p[:2], spot) <= 1.6: tp = t; break
        if ta is not None: time.sleep(0.1)
    cur = list(afin) if afin else list(end); res_af = afin
    if tp is None: print("   not caught: puppet at", [round(v, 2) for v in p[:2]], "end", [round(v, 2) for v in end[:2]])
    return {"A_s": ta and round(ta, 2), "P_s": tp and round(tp, 2), "delay_s": (ta is not None and tp is not None) and round(tp - ta, 2),
            "react_s": tr and round(tr, 2), "maxGap_m": round(gmax, 2), "samples": samples}

out = {0: [], 4: [], 3: []}
for r in range(ROUNDS):
    for tune in (0, 4, 3):
        B(); jeb.call("je.netTune", {"value": tune})
        A(); bot.load("jb_act", ncs.jump_self(*START)); bot.run("jb_go"); time.sleep(0.3); cur = list(START)
        settle(); B(); j0 = jeb.call("je.netStatus")["result"]["jumps"]
        for dx, dy in [(6, 0), (6, 6), (0, 6), (0, 0)]:
            res = leg(dx, dy); out[tune].append(res)
            B(); j1 = jeb.call("je.netStatus")["result"]["jumps"]; res["jumps"] = j1 - j0; j0 = j1
            print("round %d tune %d leg to (%d,%d): %s" % (r + 1, tune, dx, dy, json.dumps(res)), flush=True)
B(); jeb.call("je.netTune", {"value": 0})
def mean(xs): xs = [x for x in xs if x is not False and x is not None]; return round(sum(xs) / len(xs), 2) if xs else None
summ = {}
for tune, name in ((0, "new"), (4, "new_noclear"), (3, "old")):
    rs = out[tune]
    summ[name] = {"legs": len(rs), "caught": sum(1 for x in rs if x["P_s"] is not None),
                  "meanDelay_s": mean([x["delay_s"] for x in rs]), "meanReact_s": mean([x["react_s"] for x in rs]),
                  "meanMaxGap_m": mean([x["maxGap_m"] for x in rs]), "jumps": sum(x["jumps"] for x in rs)}
print(json.dumps(summ))
n, o = summ["new"], summ["old"]
ok = n["caught"] == n["legs"] and n["meanDelay_s"] is not None and n["meanDelay_s"] <= 0.6 and (o["meanDelay_s"] is None or n["meanDelay_s"] < o["meanDelay_s"])
print("SUMMARY: %s" % ("PASS" if ok else "FAIL"))
