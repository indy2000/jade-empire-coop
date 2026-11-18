"""Tiny NWScript bytecode (NCS V1.0) assembler for jebridge probe scripts.

Only what the probes need. Opcodes follow the public NCS documentation used by
xoreos and nwnnsscomp; header/entry stub checked against Jade Empire's own scripts.
Routine numbers come from the game's nwscript.nss (index = declaration order).
"""
import struct, sys

def _op(code, typ, payload=b""):
    return bytes([code, typ]) + payload

def CONSTI(v): return _op(0x04, 0x03, struct.pack(">i", v))
def CONSTF(v): return _op(0x04, 0x04, struct.pack(">f", v))
def ACTION(routine, argc): return _op(0x05, 0x00, struct.pack(">HB", routine, argc))
def CPDOWNSP(off, size): return _op(0x01, 0x01, struct.pack(">ih", off, size))
def CPTOPSP(off, size): return _op(0x03, 0x01, struct.pack(">ih", off, size))
def MOVSP(off): return _op(0x1B, 0x00, struct.pack(">i", off))
def MULFF(): return _op(0x16, 0x21)
RSADDI = _op(0x02, 0x03)
RETN = _op(0x20, 0x00)
def JSR(off): return _op(0x1E, 0x00, struct.pack(">i", off))

def int_main(body, extra_stack):
    """int main(){ <body leaves result on top, extra_stack bytes above the return slot> }"""
    main = body + CPDOWNSP(-(extra_stack + 4), 4) + MOVSP(-extra_stack) + RETN
    code = RSADDI + JSR(8) + RETN + main
    return b"NCS V1.0" + b"B" + struct.pack(">I", 13 + len(code)) + code

R = dict(GetArea=24, GetPosition=27, GetIsObjectValid=42, GetHealth=49, GetMaxHealth=50,
         FloatToInt=231, GetPlayer=548, GetObjectType=106, GetIsInCombat=320)

def pos_component(i):
    # stack after GetPosition(GetPlayer()): x, y, z (12 bytes, z on top)
    # copy component i to top, *1000, FloatToInt -> int; extra = 12 + 4
    off = -12 + 4 * i
    body = (ACTION(R["GetPlayer"], 0) + ACTION(R["GetPosition"], 1) + CPTOPSP(off, 4)
            + CONSTF(1000.0) + MULFF() + ACTION(R["FloatToInt"], 1))
    return int_main(body, 16)

PROBES = {
    "jebr_hp":    int_main(ACTION(R["GetPlayer"], 0) + ACTION(R["GetHealth"], 1), 4),
    "jebr_maxhp": int_main(ACTION(R["GetPlayer"], 0) + ACTION(R["GetMaxHealth"], 1), 4),
    "jebr_pcok":  int_main(ACTION(R["GetPlayer"], 0) + ACTION(R["GetIsObjectValid"], 1), 4),
    "jebr_px":    pos_component(0),
    "jebr_py":    pos_component(1),
    "jebr_pz":    pos_component(2),
    "jebr_const": int_main(CONSTI(4242), 4),
}

if __name__ == "__main__":
    out = sys.argv[1]
    for name, data in PROBES.items():
        open(f"{out}/{name}.ncs", "wb").write(data)
        print(name, len(data), data[13:].hex())
