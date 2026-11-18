import json, threading, time
from jeb import call
res = []
def chk(name, r, cond):
    ok = bool(cond(r)); res.append((name, ok)); print(("PASS " if ok else "FAIL ") + name + "  " + json.dumps(r)[:200])
chk("ping ready", call("ping"), lambda r: r["result"]["ready"] and r["result"]["frames"] > 0)
chk("version build", call("version"), lambda r: r["result"]["build"] == "steam-1.00-1510")
st = call("je.state")
chk("state on main thread", st, lambda r: r["result"]["thread"] == r["result"]["mainThread"])
chk("runScript noop rc=1", call("je.runScript", {"resref": "jebr_noop"}), lambda r: r["result"]["rc"] == 1)
chk("runScript missing rc=0", call("je.runScript", {"resref": "jebr_doesnotexist"}), lambda r: r["result"]["rc"] == 0)
chk("runScript no resref -32602", call("je.runScript", {}), lambda r: r["error"]["code"] == -32602)
chk("unknown method -32601", call("nosuch"), lambda r: r["error"]["code"] == -32601)
chk("debug flag on", call("je.setDebugPanel", {"value": 1}), lambda r: r["result"]["debugPanelFlag"] == 1)
chk("debug flag read back", call("je.state"), lambda r: r["result"]["debugPanelFlag"] == 1)
t0 = time.time(); ok = all(call("je.runScript", {"resref": "jebr_noop"})["result"]["rc"] == 1 for _ in range(20))
chk("20x runScript", {"secs": round(time.time() - t0, 2)}, lambda r: ok)
p1 = call("ping")["result"]["frames"]; time.sleep(2); p2 = call("ping")["result"]["frames"]
chk("frames advance after tests", {"f1": p1, "f2": p2}, lambda r: p2 > p1)
print("SUMMARY: %d/%d passed" % (sum(o for _, o in res), len(res)))
