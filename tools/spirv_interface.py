#!/usr/bin/env python3
"""Interface d'un module SPIR-V (variables d'entrée/sortie, ressources, constantes de spécialisation) : sert à
comparer le SPIR-V de glslang à celui de DXC (traduction des shaders sur la Switch, docs/SHADERS_EN_JEU.md).
  python3 tools/spirv_interface.py <cache .odsh> <glslang .spv>   -> différences, code 1 s'il y en a"""
import struct, sys

CLASSES = {0: "UniformConstant", 1: "Input", 2: "Uniform", 3: "Output", 9: "PushConstant", 12: "StorageBuffer"}


def mots(chemin):
    d = open(chemin, "rb").read()
    if d[:8] == b"ODSHAD01":
        octets, n = struct.unpack_from("<II", d, 8)
        d = d[16 + octets:16 + octets + 4 * n]
    return struct.unpack("<%dI" % (len(d) // 4), d)


def interface(w):
    i, deco, var, specs, builtins = 5, {}, [], set(), set()
    while i < len(w):
        op, n = w[i] & 0xFFFF, w[i] >> 16
        a = w[i + 1:i + n]
        if op == 71:  # OpDecorate
            cible, d = a[0], a[1]
            if d == 1:
                specs.add(a[2])
            elif d in (30, 33, 34, 11):  # Location, Binding, DescriptorSet, BuiltIn
                deco.setdefault(cible, {})[{30: "loc", 33: "bind", 34: "set", 11: "builtin"}[d]] = a[2]
        elif op == 59:  # OpVariable
            var.append((a[1], a[2]))
        i += n
    res = set()
    for v, c in var:
        if c not in CLASSES:
            continue
        d = deco.get(v, {})
        if "builtin" in d:
            res.add((CLASSES[c], "builtin", d["builtin"]))
        elif c in (1, 3):
            if "loc" in d:
                res.add((CLASSES[c], "loc", d["loc"]))
        elif "bind" in d:
            res.add(("ressource", d.get("set"), d["bind"]))
    return res, specs


a, sa = interface(mots(sys.argv[1]))
b, sb = interface(mots(sys.argv[2]))
diff = sorted(a - b), sorted(b - a)
if diff[0] or diff[1] or sa != sb:
    print("DXC seul :", diff[0], "| glslang seul :", diff[1], "| specs", sorted(sa), sorted(sb))
    sys.exit(1)
