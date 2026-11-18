"""Investigate the intermittent y = -1: same scripts, one per call vs several in one main-thread call."""
import json, time, collections
import jeb, bot, ncs
def many(names):
    r = jeb.call("je.evalMany", {"names": ",".join(names)}); return r.get("result", {}).get("values", r)
for inst in ("test_install", "test_install_b"):
    jeb.use(inst); bot.setup()
    single = [bot.ev(n) for n in ("jb_px", "jb_py", "jb_pz")]
    print("==", inst, "one script per call:", single)
    combos = [["jb_py"], ["jb_px", "jb_py"], ["jb_px", "jb_py", "jb_pz"], ["jb_py", "jb_px"], ["jb_pz", "jb_py"],
              ["jb_pcok", "jb_px", "jb_py", "jb_pz"], ["jb_py", "jb_py", "jb_py"]]
    for c in combos:
        seen = collections.Counter(json.dumps(many(c)) for _ in range(10))
        print("  ", ",".join(c).ljust(28), dict(seen))
    st = jeb.call("je.netStatus")["result"]; print("   net:", {k: st.get(k) for k in ("armed", "lastSentMm", "remoteMm")})
