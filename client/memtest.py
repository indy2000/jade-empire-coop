import json, time
from jeb import call
P = json.load(open("probes.json"))
res = []
def chk(name, ok, info): res.append(ok); print(("PASS " if ok else "FAIL ") + name + "  " + json.dumps(info)[:220])
v = call("version"); chk("server 0.3", v.get("result", {}).get("server") == "jebridge-c 0.3", v)
# names that do NOT exist in override: only the in-memory path can find them
for k, h in P.items():
    m = "jm" + k[4:]
    r = call("je.loadScript", {"name": m, "hex": h}); chk("load " + m, r.get("result", {}).get("ok"), r)
def ev(n): return call("je.evalScript", {"resref": n})
r = ev("jm_const"); chk("mem const = 4242", r.get("result", {}).get("value") == 4242, r)
r = ev("jm_hp"); chk("mem hp > 0", r.get("result", {}).get("value", 0) > 0, r)
pos = [ev(n) for n in ("jm_px", "jm_py", "jm_pz")]
chk("mem position", all("result" in p for p in pos), pos)
# replace a script at runtime and see the new behaviour
c2 = P["jebr_const"].replace("00001092", "00000007")
r = call("je.loadScript", {"name": "jm_const", "hex": c2}); r2 = ev("jm_const")
chk("hot replace -> 7", r2.get("result", {}).get("value") == 7, r2)
r = call("je.loadScript", {"name": "jm_bad", "hex": "00112233"}); chk("bad hex rejected", "error" in r, r)
r = ev("jm_unloaded"); chk("unknown mem name -> not run", "error" in r, r)
r = ev("jebr_hp"); chk("override path still works", "result" in r, r)
p1 = call("ping")["result"]["frames"]; time.sleep(1.5); p2 = call("ping")["result"]["frames"]
chk("game still running", p2 > p1, {"f1": p1, "f2": p2})
print("SUMMARY: %d/%d passed" % (sum(bool(x) for x in res), len(res)))
if all("result" in p for p in pos): print("player position (m):", [p["result"]["value"] / 1000 for p in pos])
