"""Combat sync v1 (0.10.0), two copies on mock Steam, the fight after the door in the owner's save.
Steps:  python3 fighttest.py prep      (net on, players cannot die: SetMinimumHealth 1, companions out)
        -> the owner starts the fight in A, then in B
        python3 fighttest.py check [--mutate]   (mutation: je.netTune 128 on B = no combat sync)
Checks: both in a fight; B matched its enemies to A's; A sets an enemy's health -> B shows it;
B "hits" an enemy (health -4) -> A's enemy loses 4; A kills an enemy -> it dies in B."""
import json, sys, time
import jeb, bot, ncs
MUT = "--mutate" in sys.argv
MUTE = "--mutate-engage" in sys.argv   # 0.11.0: host enemies never go for the puppet (tune 64)
TUNE = (128 if MUT else 64 if MUTE else 0) | 256   # 256: B never lowers its minimum health (the test keeps B alive)
G = lambda: ncs.ACTION(ncs.R["GetPlayer"], 0)
def A(): jeb.use("test_install")
def B(): jeb.use("test_install_b")
res = []
def chk(n, ok, info): res.append(ok); print(("PASS " if ok else "FAIL ") + n + "  " + json.dumps(info), flush=True)
def st():   # retry: one status reply in a run came back without its combat block (seen once, cause unknown)
    for _ in range(5):
        r = jeb.call("je.netStatus").get("result", {})
        if "combat" in r: return r
        time.sleep(0.3)
    return r
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
        # owner's ask: she still killed enemies in A at times: out of the world for the test (DeactivateHenchman, #311)
        bot.load("jx_hd", ncs.void_main(ncs.ACTION(326, 0) + ncs.ACTION(311, 1))); bot.run("jx_hd")
    B(); jeb.call("je.netTune", {"value": TUNE})
    print("prepared: start the fight in A, then in B"); sys.exit()
A(); bot.setup(); B(); bot.setup()
B(); jeb.call("je.netTune", {"value": TUNE})
def matched():
    A(); h = st()["combat"]; B(); g = st()["combat"]
    alive = [e for e in h["host"] if not e[2] and e[1] > 0]
    m = [l for l in g["local"] if l[1] >= 0]
    return (h, g, alive, m) if h["inFight"] and g["inFight"] and alive and len(m) >= len(alive) else None
r = wait(matched, 15)
A(); h = st()["combat"]; B(); g = st()["combat"]
chk("both fight, B matched its enemies to A's", r is not None, {"A host": h["host"], "B local": g["local"], "B snapsIn": g["snapsIn"], "B freezes": g["freezes"]})
# 0.11.0: on A one enemy goes for B's puppet, and what the puppet takes comes off B's player
if r:
    def split():
        A(); h = st()["combat"]; alive = [e for e in h["host"] if not e[2]]
        return h if h["onPuppet"] and len(h["onPuppet"]) < len(alive) else None
    sp = wait(split, 10); A(); h = st()["combat"]
    chk("A: the enemies split between A's player and B's puppet", sp is not None, {"onPuppet": h["onPuppet"], "host": h["host"], "pupOid": h["pupOid"]})
    # GetMaxHealth(player) read 0 on B once, so "refill" sets a fixed 100 (script view of health)
    B(); bot.load("jx_rf", ncs.void_main(ncs.CONSTI(100) + G() + ncs.ACTION(156, 2))); bot.run("jx_rf")
    hb0 = ival(ncs.int_main(G() + ncs.ACTION(49, 1), 4)); A(); dmg0 = st()["combat"]["pupDmg"]
    def hurt():
        B(); g = st()["combat"]; hb = ival(ncs.int_main(G() + ncs.ACTION(49, 1), 4))
        return (g, hb) if g["hitsIn"] > 0 and hb < hb0 else None
    e = wait(hurt, 20); A(); h = st()["combat"]; B(); g = st()["combat"]
    chk("B's player loses health when A's enemy hits B's puppet", e is not None,
        {"A pupHits": h["pupHits"], "A pupDmg": h["pupDmg"], "B hitsIn": g["hitsIn"], "B hp start": hb0, "B hp now": ival(ncs.int_main(G() + ncs.ACTION(49, 1), 4))})
# 0.11.0: B's health is what it was minus the host's hits since then (the heal-back once undid a host hit)
if r:
    def settled():
        A(); d1 = st()["combat"]["pupDmg"]; time.sleep(1.5); B(); hb = ival(ncs.int_main(G() + ncs.ACTION(49, 1), 4)); A(); d2 = st()["combat"]["pupDmg"]
        return (hb, d1) if d1 == d2 else None
    se = wait(settled, 20); want = max(1, hb0 - (se[1] - dmg0)) if se else None
    chk("B's health = start - host damage (no host hit healed back)", bool(se) and se[0] == want, {"B hp": se and se[0], "expected": want, "host damage since start": se and se[1] - dmg0})
# 0.11.0-re9: on B the enemies fight with their own AI (animations), their hits on B are healed back
if r:
    def localhit():
        B(); g = st()["combat"]; return g if g["heals"] > 0 else None
    lh = wait(localhit, 15); B(); g = st()["combat"]
    chk("B: an enemy attacks B's player in B's own game (hit healed back)", lh is not None, {"heals": g["heals"], "healed": g["healed"]})
# 0.10.1: after a reload B killed its new enemies (matched to the previous fight's dead ones)
if r:
    hd = {e[0]: e[2] for e in r[0]["host"]}
    bad = [l for l in r[1]["local"] if l[3] and l[1] >= 0 and not hd.get(l[1], 0)]
    chk("no enemy dead on B while A's is alive", not bad, {"B dead but A alive": bad})
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
    if k != j:   # 0.10.1: the guest's killing blow did not reach the host
        B(); kill(240 + l)
        ok = wait(lambda: (A(), dead(220 + j))[1], 5)
        A(); chk("B kills enemy %d: it dies in A" % j, bool(ok), {"A dead": dead(220 + j), "A hp": hp(220 + j)})
B(); jeb.call("je.netTune", {"value": 0})
print("SUMMARY: %d/%d passed%s" % (sum(res), len(res), " (MUTATION: no combat sync on B)" if MUT else ""))
