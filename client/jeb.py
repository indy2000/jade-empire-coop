"""File-transport client for jebridge (coop-anything bridge protocol v1)."""
import json, os, sys, time
BASE = os.path.expanduser("~/mnt/Downloads/JadeEmpireCoop")
D = os.path.join(BASE, os.environ.get("JEB_INSTALL", "test_install"), "bridge")
def use(install):
    """Point this client at another test install (e.g. test_install_b)."""
    global D; D = os.path.join(BASE, install, "bridge")
_n = [int(time.time() * 1000) % 1000000]
def call(method, params=None, timeout=10.0):
    _n[0] += 1; rid = _n[0]
    name = f"req{rid}.json"
    tmp = os.path.join(D, "in", name + ".part")
    with open(tmp, "w") as f: f.write(json.dumps({"id": rid, "method": method, "params": params or {}}) + "\n")
    os.rename(tmp, os.path.join(D, "in", name))
    out = os.path.join(D, "out", name); t0 = time.time()
    while time.time() - t0 < timeout:
        if os.path.exists(out):
            time.sleep(0.05)
            txt = open(out).read().strip()
            if txt:
                try: return json.loads(txt)
                except ValueError: pass      # the DLL is still writing the reply: read again

        time.sleep(0.05)
    return {"id": rid, "error": {"code": "client-timeout", "message": "no reply file"}}
if __name__ == "__main__":
    m = sys.argv[1]; p = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    print(json.dumps(call(m, p)))
