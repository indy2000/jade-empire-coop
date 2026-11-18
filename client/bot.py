import json, math, sys, time
from jeb import call
import ncs
def load(name, data):
    r = call("je.loadScript", {"name": name, "hex": data.hex()}); assert "result" in r, r
def ev(name):
    r = call("je.evalScript", {"resref": name}); assert "result" in r, r; return r["result"]["value"]
def run(name):
    r = call("je.runScript", {"resref": name}); assert "result" in r, r; return r["result"]
def setup():
    for i, n in enumerate(("jb_px", "jb_py", "jb_pz")): load(n, ncs.player_pos_component(i))
    load("jb_go", ncs.exec_on_player("jb_act"))
def pos(): return tuple(ev(n) / 1000 for n in ("jb_px", "jb_py", "jb_pz"))
def move_to(x, y, z, run_=0):
    load("jb_act", ncs.move_self(x, y, z, 0.0, run_)); return run("jb_go")
def jump_to(x, y, z):
    load("jb_act", ncs.jump_self(x, y, z, 0.0)); return run("jb_go")
def puppet_setup():
    for i, n in enumerate(("jb_qx", "jb_qy", "jb_qz")): load(n, ncs.puppet_pos_component(i))
    load("jb_qok", ncs.puppet_valid()); load("jb_qgo", ncs.exec_on_puppet("jb_qact")); load("jb_qdel", ncs.destroy_puppet())
def puppet_pos(): return tuple(ev(n) / 1000 for n in ("jb_qx", "jb_qy", "jb_qz"))
def spawn(template, x, y, z):
    assert template in ncs.SAFE_TEMPLATES, "template %r not verified for this area (engine asserts on missing models)" % template
    load("jb_spawn", ncs.spawn_puppet(template, x, y, z)); return run("jb_spawn")
def puppet_move(x, y, z, run_=0):
    load("jb_qact", ncs.move_self(x, y, z, 0.0, run_)); return run("jb_qgo")
