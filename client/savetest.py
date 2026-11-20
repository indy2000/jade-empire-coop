"""Join from the host's save: B (net.txt 'getsave 1') asks A (host) for its save over Steam (mock).
A makes an autosave and sends save.sac + SaveImage.tga; B must write them to Save\\Co-op - host
byte for byte (SHA-256 against A's newest Save\\Auto* folder).
Usage: python3 savetest.py [--mutate]   (mutation: A's net is never started, so nothing may arrive)
Run on the device VM next to jeb.py; installs under $JEB_BASE (default: Downloads/JadeEmpireCoop)."""
import glob, hashlib, json, os, sys, time
import jeb
MUT = "--mutate" in sys.argv
BASE = jeb.BASE
A_DIR, B_DIR = os.path.join(BASE, "test_install"), os.path.join(BASE, "test_install_b")
def A(): jeb.use("test_install")
def B(): jeb.use("test_install_b")
res = []
def chk(n, ok, info): res.append(ok); print(("PASS " if ok else "FAIL ") + n + "  " + json.dumps(info), flush=True)
def sha(p): return hashlib.sha256(open(p, "rb").read()).hexdigest() if os.path.exists(p) else None
coop = os.path.join(B_DIR, "Save", "Co-op - host")
t_start = time.time()
if not MUT: A(); jeb.call("je.netStart")
B(); jeb.call("je.netStart")
st = None
for i in range(40):
    time.sleep(1)
    B(); st = jeb.call("je.netStatus")["result"]
    if st.get("save", {}).get("done"): break
B(); st = jeb.call("je.netStatus")["result"]
chk("B received a save (status done)", bool(st.get("save", {}).get("done")), st.get("save"))
autos = sorted(glob.glob(os.path.join(A_DIR, "Save", "Auto*")), key=os.path.getmtime)
newest = autos[-1] if autos else None
fresh = newest is not None and os.path.getmtime(newest) >= t_start - 2
for f in ("save.sac", "SaveImage.tga"):
    a, b = (sha(os.path.join(newest, f)) if newest else None), sha(os.path.join(coop, f))
    chk("%s in B's 'Co-op - host' equals A's fresh autosave" % f, fresh and a is not None and a == b,
        {"A autosave": os.path.basename(newest) if newest else None, "fresh": fresh, "A": a and a[:16], "B": b and b[:16]})
print("SUMMARY: %d/%d passed%s" % (sum(res), len(res), " (MUTATION: host net not started)" if MUT else ""))
