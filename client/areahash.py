import struct, jeb, bot, ncs
ALPHA = "abcdefghijklmnopqrstuvwxyz0123456789_"
ADDII = ncs.op(0x14, 0x20); MULII = ncs.op(0x16, 0x20)
def area_hash_ncs(nchars=16):
    b = ncs.CONSTI(0)                                                          # h
    b += ncs.ACTION(ncs.R["GetPlayer"], 0) + ncs.ACTION(24, 1) + ncs.ACTION(168, 1) + ncs.ACTION(61, 1)   # tag (lower case)
    for i in range(nchars):
        b += ncs.CONSTI(1) + ncs.CONSTI(i) + ncs.CPTOPSP(-12, 4) + ncs.ACTION(65, 3)     # GetSubString(tag, i, 1)
        b += ncs.CONSTS(ALPHA) + ncs.ACTION(66, 2) + ncs.CONSTI(2) + ADDII              # c = FindSubString(ALPHA, sub) + 2
        b += ncs.CPTOPSP(-12, 4) + ncs.CONSTI(31) + MULII + ADDII                        # h*31 + c
        b += ncs.CPDOWNSP(-12, 4) + ncs.MOVSP(-4)
    b += ncs.CPTOPSP(-8, 4)
    return ncs.int_main(b, 12)
def taglen_ncs(): return ncs.int_main(ncs.ACTION(ncs.R["GetPlayer"], 0) + ncs.ACTION(24, 1) + ncs.ACTION(168, 1) + ncs.ACTION(59, 1), 4)
if __name__ == "__main__":
    for inst in ("test_install", "test_install_b"):
        jeb.use(inst)
        r = jeb.call("ping", timeout=3)
        if "result" not in r: print(inst, "not running"); continue
        bot.load("jx_ah", area_hash_ncs()); bot.load("jx_tl", taglen_ncs())
        print(inst, "area tag length", bot.ev("jx_tl"), "hash", bot.ev("jx_ah"))
