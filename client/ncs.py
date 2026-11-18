"""Minimal NCS V1.0 assembler + script builders for jebridge bots (Jade Empire build 1510).
Routine numbers = declaration order in the game's nwscript.nss."""
import struct
def op(c, t, p=b""): return bytes([c, t]) + p
CONSTI = lambda v: op(0x04, 0x03, struct.pack(">i", v))
CONSTF = lambda v: op(0x04, 0x04, struct.pack(">f", v))
CONSTS = lambda s: op(0x04, 0x05, struct.pack(">H", len(s)) + s.encode())
CONSTO = lambda v: op(0x04, 0x06, struct.pack(">i", v))
ACTION = lambda r, n: op(0x05, 0x00, struct.pack(">HB", r, n))
CPDOWNSP = lambda o, s: op(0x01, 0x01, struct.pack(">ih", o, s))
CPTOPSP = lambda o, s: op(0x03, 0x01, struct.pack(">ih", o, s))
MOVSP = lambda o: op(0x1B, 0x00, struct.pack(">i", o))
MULFF = op(0x16, 0x21)
RSADDI = op(0x02, 0x03); RETN = op(0x20, 0x00)
JSR = lambda o: op(0x1E, 0x00, struct.pack(">i", o))
OBJECT_SELF = 0
R = dict(AssignCommand=6, ExecuteScript=8, ActionMoveToLocation=21, GetArea=24, GetPosition=27,
         GetIsObjectValid=42, GetHealth=49, GetMaxHealth=50, GetFacing=28, FloatToInt=231,
         Location=215, JumpToLocation=313, GetPlayer=548)
def ncs(code): return b"NCS V1.0" + b"B" + struct.pack(">I", 13 + len(code)) + code
def void_main(body): return ncs(JSR(8) + RETN + body + RETN)
def int_main(body, extra): return ncs(RSADDI + JSR(8) + RETN + body + CPDOWNSP(-(extra + 4), 4) + MOVSP(-extra) + RETN)
def location_here_area(x, y, z, facing):
    # Location(GetArea(OBJECT_SELF), [x,y,z], facing): push args right-to-left
    return (CONSTF(facing) + CONSTF(x) + CONSTF(y) + CONSTF(z)
            + CONSTO(OBJECT_SELF) + ACTION(R["GetArea"], 1) + ACTION(R["Location"], 3))
def move_self(x, y, z, facing=0.0, run=0):
    # ActionMoveToLocation(loc, nRunType, nMoveAnim=-1)
    return void_main(CONSTI(-1) + CONSTI(run) + location_here_area(x, y, z, facing) + ACTION(R["ActionMoveToLocation"], 3))
def jump_self(x, y, z, facing=0.0):
    # JumpToLocation(loc, nWalkStraightLineToPoint=1)
    return void_main(CONSTI(1) + location_here_area(x, y, z, facing) + ACTION(R["JumpToLocation"], 2))
def exec_on_player(script):
    # ExecuteScript(GetPlayer(), script): push sScript, then oTarget (top)
    return void_main(CONSTS(script) + ACTION(R["GetPlayer"], 0) + ACTION(R["ExecuteScript"], 2))
def player_pos_component(i):
    body = (ACTION(R["GetPlayer"], 0) + ACTION(R["GetPosition"], 1) + CPTOPSP(-12 + 4 * i, 4)
            + CONSTF(1000.0) + MULFF + ACTION(R["FloatToInt"], 1))
    return int_main(body, 16)
R.update(CreateObject=243, DestroyObject=241, GetLocalObject=54, SetLocalObject=58, GetObjectType=106)
OBJECT_TYPE_CREATURE = 2
PUPPET_SLOT = 190
def loc_near_player(x, y, z, facing=0.0):
    return (CONSTF(facing) + CONSTF(x) + CONSTF(y) + CONSTF(z)
            + ACTION(R["GetPlayer"], 0) + ACTION(R["GetArea"], 1) + ACTION(R["Location"], 3))
def push_puppet():
    return CONSTI(PUPPET_SLOT) + ACTION(R["GetPlayer"], 0) + ACTION(R["GetLocalObject"], 2)
def spawn_puppet(template, x, y, z):
    body = (loc_near_player(x, y, z) + CONSTS(template) + CONSTI(OBJECT_TYPE_CREATURE) + ACTION(R["CreateObject"], 3)
            + CPTOPSP(-4, 4) + CONSTI(PUPPET_SLOT) + ACTION(R["GetPlayer"], 0) + ACTION(R["SetLocalObject"], 3)
            + MOVSP(-4))
    return void_main(body)
def exec_on_puppet(script):
    return void_main(CONSTS(script) + push_puppet() + ACTION(R["ExecuteScript"], 2))
def puppet_valid():
    return int_main(push_puppet() + ACTION(R["GetIsObjectValid"], 1), 4)
def puppet_pos_component(i):
    body = (push_puppet() + ACTION(R["GetPosition"], 1) + CPTOPSP(-12 + 4 * i, 4)
            + CONSTF(1000.0) + MULFF + ACTION(R["FloatToInt"], 1))
    return int_main(body, 16)
def destroy_puppet():
    return void_main(CONSTF(0.0) + push_puppet() + ACTION(R["DestroyObject"], 2))
R.update(ActionJumpToLocation=214, ActionForceMoveToLocation=382, ClearAllActions=9, SetCommandable=162, GetCommandable=163, GetCurrentAction=522)
def action_jump_self(x, y, z):
    return void_main(CONSTI(1) + location_here_area(x, y, z, 0.0) + ACTION(R["ActionJumpToLocation"], 2))
def force_move_self(x, y, z, run=0, timeout=10.0):
    return void_main(CONSTI(-1) + CONSTF(timeout) + CONSTI(run) + location_here_area(x, y, z, 0.0) + ACTION(R["ActionForceMoveToLocation"], 4))
def puppet_int(routine):  # int f(object puppet)
    return int_main(push_puppet() + ACTION(routine, 1), 4)
R.update(ActionMoveToObject=22)
def move_to_player_self(run=0, rng=1.0):
    return void_main(CONSTI(-1) + CONSTF(rng) + CONSTI(run) + ACTION(R["GetPlayer"], 0) + ACTION(R["ActionMoveToObject"], 4))
R.update(GetNearestObject=227, GetName=None)
def slot_push(slot): return CONSTI(slot) + ACTION(R["GetPlayer"], 0) + ACTION(R["GetLocalObject"], 2)
def set_slot_nearest_creature(slot, nth):
    body = (CONSTI(nth) + CONSTI(OBJECT_TYPE_CREATURE) + ACTION(R["GetPlayer"], 0) + ACTION(R["GetNearestObject"], 3)
            + CONSTI(slot) + ACTION(R["GetPlayer"], 0) + ACTION(R["SetLocalObject"], 3))
    return void_main(body)
def slot_valid(slot): return int_main(slot_push(slot) + ACTION(R["GetIsObjectValid"], 1), 4)
def slot_pos_component(slot, i):
    return int_main(slot_push(slot) + ACTION(R["GetPosition"], 1) + CPTOPSP(-12 + 4 * i, 4) + CONSTF(1000.0) + MULFF + ACTION(R["FloatToInt"], 1), 16)
def exec_on_slot(slot, script): return void_main(CONSTS(script) + slot_push(slot) + ACTION(R["ExecuteScript"], 2))
R.update(GetObjectActive=755, SetObjectActive=756, GetIsDead=140)
def set_slot_active(slot, on):
    # SetObjectActive(o, bActive, spawnAnim=-1, despawnAnim=-1, crust=-1, durationMs=-1): push right-to-left
    return void_main(CONSTI(-1) + CONSTI(-1) + CONSTI(-1) + CONSTI(-1) + CONSTI(1 if on else 0) + slot_push(slot)
                     + ACTION(R["SetObjectActive"], 6))
def slot_active(slot): return int_main(slot_push(slot) + ACTION(R["GetObjectActive"], 1), 4)
R.update(GetLocation=213)
def spawn_puppet_at_player(template):
    body = (ACTION(R["GetPlayer"], 0) + ACTION(R["GetLocation"], 1) + CONSTS(template) + CONSTI(OBJECT_TYPE_CREATURE)
            + ACTION(R["CreateObject"], 3)
            + CPTOPSP(-4, 4) + CONSTI(PUPPET_SLOT) + ACTION(R["GetPlayer"], 0) + ACTION(R["SetLocalObject"], 3) + MOVSP(-4))
    return void_main(body)
R.update(GetAILevel=285, SetAILevel=286)
def slot_int(slot, routine): return int_main(slot_push(slot) + ACTION(routine, 1), 4)
def set_slot_ai(slot, level):  # SetAILevel(object, level): push level, then object
    return void_main(CONSTI(level) + slot_push(slot) + ACTION(R["SetAILevel"], 2))
R.update(SetHealth=156)
def set_slot_health(slot, hp): return void_main(CONSTI(hp) + slot_push(slot) + ACTION(R["SetHealth"], 2))
R.update(EffectResurrection=82, ApplyEffectToObject=220)
DURATION_TYPE_INSTANT = 0
def resurrect_slot(slot):
    # ApplyEffectToObject(oTarget, nDurationType, eEffect, fDuration=0.0): push right-to-left
    return void_main(CONSTF(0.0) + CONSTI(0) + ACTION(R["EffectResurrection"], 1) + CONSTI(DURATION_TYPE_INSTANT)
                     + slot_push(slot) + ACTION(R["ApplyEffectToObject"], 4))
# Templates whose models were seen to load in the prologue area. Spawning a template whose model the
# current area has not loaded triggers an engine assert ("Model not found") and the game closes.
SAFE_TEMPLATES = {"j00h_abbot0"}
def set_slot_commandable(slot, on):  # SetCommandable(bCommandable, oTarget): push oTarget, then bCommandable on top
    return void_main(slot_push(slot) + CONSTI(1 if on else 0) + ACTION(R["SetCommandable"], 2))
