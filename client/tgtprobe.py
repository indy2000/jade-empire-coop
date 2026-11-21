"""RE probe (branch re/target, host copy A, during a fight): what do the enemies target?
Creature +0x58c = combat target struct (dword object id, short kind at +0x590; FUN_0046f240/0046ff70).
  ids    : object ids (ObjectToString parsed in NCS) + creature pointers of player, puppet (192), enemies (220..)
  watch N: every second, each enemy's target id (player / puppet / other) and the puppet's health
  pin J  : write the puppet's id into enemy J's target every frame (je.pin); 'unpin' clears"""
import sys, time, json, jeb, bot, ncs
from ncs import CONSTI, CONSTS, ACTION, CPTOPSP, CPDOWNSP, MOVSP, int_main, slot_push, op
ADDII, MULII, ADDSS = op(0x14, 0x20), op(0x16, 0x20), op(0x14, 0x23)
G = lambda: ACTION(548, 0)
def ev(name, data):
    bot.load(name, data); r = jeb.call("je.evalScript", {"resref": name}); return r["result"]["value"] if "result" in r else None
def objid(push):
    # h = 0; s = right 8 of ("00000000" + lower(ObjectToString(o))); for each char: h = h*16 + hexdigit
    b = CONSTI(0) + CONSTI(8) + CONSTS("00000000") + push + ACTION(272, 1) + ACTION(61, 1) + ADDSS + ACTION(62, 2)   # stack: h, s8
    for i in range(8):
        b += CONSTI(1) + CONSTI(i) + CPTOPSP(-12, 4) + ACTION(65, 3) + CONSTS("0123456789abcdef") + ACTION(66, 2)
        b += CPTOPSP(-12, 4) + CONSTI(16) + MULII + ADDII + CPDOWNSP(-12, 4) + MOVSP(-4)
    b += CPTOPSP(-8, 4)
    v = ev("jx_oid", int_main(b, 12)); return None if v is None else v & 0xffffffff
def creature(oid): r = jeb.call("je.creature", {"oid": oid}); return r.get("result", {}).get("creature")
def peek(addr, n): r = jeb.call("je.peek", {"oid": addr, "value": n}); return bytes.fromhex(r["result"]["hex"]) if "result" in r else None
jeb.use("test_install"); bot.setup()
pl = jeb.call("je.playerPtr")["result"]
ids = {"player": objid(G()), "puppet": objid(slot_push(192))}
for j in range(4): ids["enemy%d" % j] = objid(slot_push(220 + j))
cr = {k: (creature(v) if v else None) for k, v in ids.items()}
def name(i): return next((k for k, v in ids.items() if v == i), hex(i))
def tgt(c):
    b = peek(c + 0x588, 0x30)
    if not b: return None
    return {"inCombat588": hex(int.from_bytes(b[0:4], "little")), "target": name(int.from_bytes(b[4:8], "little")),
            "kind": int.from_bytes(b[8:10], "little"), "owner_ok": int.from_bytes(b[0x2c:0x30], "little") == c}
mode = sys.argv[1]
if mode == "ids":
    print(json.dumps({"playerPtr": pl, "ids": {k: (hex(v) if v else v) for k, v in ids.items()}, "creature": cr}, indent=1))
    for k in ids:
        if k.startswith("enemy") and cr[k]: print(k, tgt(cr[k]))
elif mode == "watch":
    t0 = time.time()
    while time.time() - t0 < float(sys.argv[2]):
        row = {k: tgt(cr[k])["target"] for k in ids if k.startswith("enemy") and cr[k] and tgt(cr[k])}
        print(round(time.time() - t0, 1), row, "puppet hp", ev("jx_ph", int_main(slot_push(192) + ACTION(49, 1), 4)), flush=True); time.sleep(1)
elif mode == "pin":
    j = int(sys.argv[2]); print(jeb.call("je.pin", {"oid": cr["enemy%d" % j] + 0x58c, "value": ids["puppet"]}))
elif mode == "unpin": print(jeb.call("je.pin", {"oid": 0}))
if mode == "watch-writes":   # hardware write watch on enemy J's target id; the DLL logs each writer eip once (WATCH2)
    j = int(sys.argv[2]); print(jeb.call("je.watch", {"oid": cr["enemy%d" % j] + 0x58c}))
if mode == "stats": print(jeb.call("je.watchStats"))
if mode == "desired":
    for k in ids:
        if k.startswith("enemy") and cr[k]: print(k, "desired", name(int.from_bytes(peek(cr[k] + 0x5a4, 4), "little")))
    if len(sys.argv) > 2:
        j = int(sys.argv[2]); print(jeb.call("je.pin", {"oid": cr["enemy%d" % j] + 0x5a4, "value": ids["puppet"]}))
if mode == "dump":
    for k in ids:
        if cr.get(k):
            b = peek(cr[k] + 0x588, 0x34); print(k, [hex(int.from_bytes(b[i:i+4], "little")) for i in range(0, 0x34, 4)])
if mode == "gates":   # the checks FUN_004bc760(enemy, target) makes, for target = player and puppet
    def call(fn, ecx, *a): r = jeb.call("je.call", {"fn": fn, "ecx": ecx, "n": len(a), "a": a[0] if a else 0, "b": a[1] if len(a) > 1 else 0}); return r.get("result", {}).get("eax", r.get("error"))
    def vt(x, off): v = int.from_bytes(peek(x, 4), "little"); return int.from_bytes(peek(v + off, 4), "little")
    j = int(sys.argv[2]) if len(sys.argv) > 2 else 1; e = cr["enemy%d" % j]
    for k in ("player", "puppet"):
        t = cr[k]
        print(k, {"vt94(target)": call(vt(t, 0x94), t), "vt28(target)": call(vt(t, 0x28), t), "+588": hex(int.from_bytes(peek(t + 0x588, 4), "little")),
                  "+594": hex(int.from_bytes(peek(t + 0x594, 4), "little")),
                  "relation 46f3a0": call(0x0046f3a0, e + 0x58c, t) & 0xff, "canAttack 4bc760": call(0x004bc760, e, t) & 0xff})
    print("enemy", {"vt94": call(vt(e, 0x94), e), "+588": hex(int.from_bytes(peek(e + 0x588, 4), "little"))})
if mode == "pin588":
    print("puppet +0x588 was", hex(int.from_bytes(peek(cr["puppet"] + 0x588, 4), "little")))
    print(jeb.call("je.pin", {"oid": cr["puppet"] + 0x588, "value": 1}))
if mode == "anim":   # log every animation request of in-combat creatures for N s (re7 hook on FUN_00443d80)
    cname = {v: k for k, v in cr.items() if v}
    print(jeb.call("je.animLog", {"value": 1})); t0 = time.time(); seen = 0; rows = []
    while time.time() - t0 < float(sys.argv[2]):
        time.sleep(0.5); r = jeb.call("je.animLog", {"oid": 12})["result"]; n = r["n"]
        new = r["recs"][-(n - seen):] if n - seen < len(r["recs"]) else r["recs"]
        for x in new:
            rows.append(x); print(x[0] % 100000, cname.get(x[1], hex(x[1])), "flag", x[2], "req", [hex(v) if v > 0xffff else v for v in x[3:]], flush=True)
        seen = n
    print(jeb.call("je.animLog", {"value": 0}), "total", seen)
