import jeb, sys, json
def st(n):
    jeb.use(n); r = jeb.call("je.netStatus", timeout=4).get("result")
    if not r: return None
    return {k: r[k] for k in ("puppet", "spawns", "myArea", "remoteArea", "areaHides", "noPlayerSkips", "recv")}
if "--start" in sys.argv:
    for n in ("test_install", "test_install_b"): jeb.use(n); jeb.call("je.netStart")
for n in ("test_install", "test_install_b"): print(n[-1] if n.endswith("b") else "A", json.dumps(st(n)))
