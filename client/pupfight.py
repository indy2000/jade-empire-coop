"""Probe (host = copy A, during a fight): can the enemies fight the guest's puppet?
read : factions/teams/enemy relations of player, puppet (slot 192), enemies (220..), area (218)
apply: ChangeFaction(puppet, player), puppet AI off, min health 1, AddCombatAreaParticipant(area, puppet)
watch: puppet health and each enemy's last hostile target for N seconds"""
import sys, time, json, jeb, bot, ncs
from ncs import CONSTI, ACTION, int_main, void_main, slot_push, op
G = lambda: ACTION(548, 0); P = lambda: slot_push(192); A_ = lambda: slot_push(218); E = lambda j: slot_push(220 + j)
EQO = op(0x0B, 0x22)
def ev(name, data):
    bot.load(name, data); r = jeb.call("je.evalScript", {"resref": name})
    return r["result"]["value"] if "result" in r else None
def run(name, data): bot.load(name, data); bot.run(name)
def i1(push, r): return ev("jx_p1", int_main(push + ACTION(r, 1), 4))
jeb.use("test_install"); bot.setup()
def read():
    o = {}
    for nm, push in (("player", G()), ("puppet", P())):
        o[nm] = {"valid": i1(push, 42), "stdFaction": i1(push, 713), "faction": i1(push, 712), "team": i1(push, 767), "hp": i1(push, 49),
                 "inArea": ev("jx_p2", int_main(A_() + push + ACTION(884, 2), 4))}
    for j in range(4):
        if not i1(E(j), 42) and not i1(E(j), 49): continue
        o["enemy%d" % j] = {"hp": i1(E(j), 49), "team": i1(E(j), 767), "stdFaction": i1(E(j), 713),
            "enemyOfPuppet": ev("jx_p3", int_main(E(j) + P() + ACTION(235, 2), 4)),      # GetIsEnemy(puppet, enemy)
            "enemyOfPlayer": ev("jx_p3", int_main(E(j) + G() + ACTION(235, 2), 4)),
            "targetIsPuppet": ev("jx_p4", int_main(E(j) + ACTION(721, 1) + P() + EQO, 4)),
            "targetIsPlayer": ev("jx_p4", int_main(E(j) + ACTION(721, 1) + G() + EQO, 4))}
    return o
mode = sys.argv[1]
if mode == "read": print(json.dumps(read(), indent=1))
elif mode == "apply":
    run("jx_a1", void_main(G() + P() + ACTION(173, 2)))                  # ChangeFaction(puppet, player)
    run("jx_a2", void_main(CONSTI(1) + P() + ACTION(935, 2)))            # SetDisableCreatureCombatAI(puppet, TRUE)
    run("jx_a3", void_main(CONSTI(1) + P() + ACTION(716, 2)))            # SetMinimumHealth(puppet, 1)
    run("jx_a4", void_main(P() + A_() + ACTION(875, 2)))                 # AddCombatAreaParticipant(area, puppet)
    print(json.dumps(read(), indent=1))
elif mode == "watch":
    t0 = time.time()
    while time.time() - t0 < float(sys.argv[2]):
        o = read(); print(round(time.time() - t0, 1), json.dumps({"pupHp": o["puppet"]["hp"], "plHp": o["player"]["hp"],
            "tgt": {k: (v["targetIsPuppet"], v["targetIsPlayer"], v["hp"]) for k, v in o.items() if k.startswith("enemy")}}), flush=True)
        time.sleep(1.0)
