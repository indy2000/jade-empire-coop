"""Combat sync v1 (0.10.0), two copies on mock Steam, the fight after the door in the owner's save.
Steps:  python3 fighttest.py prep      (net on, players cannot die: SetMinimumHealth 1, companions out)
        -> the owner starts the fight in A, then in B
        python3 fighttest.py check [--mutate]   (mutation: je.netTune 128 on B = no combat sync)
Checks: both in a fight; B matched its enemies to A's; A sets an enemy's health -> B shows it;
B "hits" an enemy (health -4) -> A's enemy loses 4; A kills an enemy -> it dies in B."""
import json, sys, time
import jeb, bot, ncs
MUT = "--mutate" in sys.argv
G = lambda: ncs.ACTION(ncs.R["GetPlayer"], 0)
def A(): jeb.use("test_install")
def B(): jeb.use("test_install_b")
res = []
def chk(n, ok, info): res.append(ok); print(("PASS " if ok else "FAIL ") + n + "  " + json.dumps(info), flush=True)
def st(): return jeb.call("je.netStatus")["result"]
def ival(d): bot.load("jx_ft", d); return bot.ev("jx_ft")
def hp(slot): return ival(ncs.slot_int(slot, 49))
def dead(slot): return ival(ncs.slot_int(slot, 140))
def set_hp(slot, v): bot.load("jx_fs", ncs.void_main(ncs.CONSTI(v) + ncs.slot_push(slot) + ncs.ACTION(156, 2))); bot.run("jx_fs")
def kill(slot): bot.load("jx_fk", ncs.void_main(ncs.CONSTF(0.0) + ncs.CONSTI(0) + ncs.ACTION(927, 1) + ncs.CONSTI(0) + ncs.slot_push(slot) + ncs.ACTION(220, 4))); bot.run("jx_fk")
def wait(pred, secs):
    t0 = time.time()
    while time.time() - t0 < secs:
        v = pred()
        if v: return v
        time.sleep(0.4)
    return None
if sys.argv[1] == "prep":
    for inst in (A, B):
        inst(); bot.setup(); jeb.call("je.netStart"); jeb.call("je.netPause", {"value": 0})
        bot.load("jx_mh", ncs.void_main(ncs.CONSTI(1) + G() + ncs.ACTION(716, 2))); bot.run("jx_mh")
        # test only: the host's companion stays out too (SetHenchmanFollowPlayerInCombat(GetSelectedHenchman(), TRUE)),
        # or Dawn Star kills the enemies before the checks run
        bot.load("jx_hf", ncs.void_main(ncs.CONSTI(1) + ncs.ACTION(326, 0) + ncs.ACTION(1045, 2))); bot.run("jx_hf")
    B(); jeb.call("je.netTune", {"value": 128 if MUT else 0})
    print("prepared: start the fight in A, then in B"); sys.exit()
A(); bot.setup(); B(); bot.setup()
B(); jeb.call("je.netTune", {"value": 128 if MUT else 0})
def matched():
    A(); h = st()["combat"]; B(); g = st()["combat"]
    alive = [e for e in h["host"] if not e[2] and e[1] > 0]
    m = [l for l in g["local"] if l[1] >= 0]
    return (h, g, alive, m) if h["inFight"] and g["inFight"] and alive and len(m) >= len(alive) else None
r = wait(matched, 15)
A(); h = st()["combat"]; B(); g = st()["combat"]
chk("both fight, B matched its enemies to A's", r is not None, {"A host": h["host"], "B local": g["local"], "B snapsIn": g["snapsIn"], "B freezes": g["freezes"]})
if r:
    h, g, alive, m = r
    j = alive[0][0]; l = [x for x in g["local"] if x[1] == j][0][0]
    A(); a0 = hp(220 + j); set_hp(220 + j, max(1, a0 - 3)); a1 = hp(220 + j)
    ok = wait(lambda: (B(), hp(240 + l) == a1)[1], 5)
    B(); chk("A sets enemy %d health %d -> %d: B's enemy shows it" % (j, a0, a1), bool(ok), {"B hp": hp(240 + l)})
    B(); b0 = hp(240 + l); set_hp(240 + l, max(1, b0 - 4))
    ok = wait(lambda: (A(), hp(220 + j) == a1 - 4)[1], 5)
    A(); a2 = hp(220 + j); B(); g2 = st()["combat"]
    chk("B hits enemy %d for 4: A's enemy loses 4" % j, bool(ok), {"A hp before": a1, "A hp now": a2, "B dmgSent": g2["dmgSent"]})
    k = alive[-1][0] if len(alive) > 1 else j; lk = [x for x in g["local"] if x[1] == k][0][0]
    A(); kill(220 + k)
    ok = wait(lambda: (B(), dead(240 + lk))[1], 5)
    chk("A kills enemy %d: it dies in B" % k, bool(ok), {"B dead": dead(240 + lk)})
B(); jeb.call("je.netTune", {"value": 0})
print("SUMMARY: %d/%d passed%s" % (sum(res), len(res), " (MUTATION: no combat sync on B)" if MUT else ""))
