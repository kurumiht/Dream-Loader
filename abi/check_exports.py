#!/usr/bin/env python3
"""ABI export guard for dreamkurumiHT (no binaries needed; optional DLL check).

What it enforces
  1. every symbol that dreamkurumiHT_plugin.h binds (BIND_DK) is exported by DreamCExport.hpp
  2. the typedef in the plugin header and the exported definition agree (return type + parameter types)
  3. nothing that was in the golden file disappeared or changed signature (a break needs a major bump)
  4. exports that are NOT in the plugin header ("unlisted") are tracked too: a NEW unlisted export fails
     the check, so nothing becomes public ABI by accident
  5. (optional) --dll: every golden symbol really is in the built DLL's export table (needs `objdump`)

Usage
  python abi/check_exports.py --header include/dreamkurumiHT/dreamkurumiHT_plugin.h \
         --export-src include/dreamkurumiHT/DreamCExport.hpp --golden abi/exports_v1.golden.txt
  add --update to bless the current state (a reviewed, deliberate ABI event) and --dll build/dreamkurumiHT.dll
"""
import argparse
import os
import re
import shutil
import subprocess
import sys


def squash(s):
    return " ".join(s.replace("dreamkurumi::", "").split())


def split_top(params):
    out, depth, cur = [], 0, ""
    for ch in params:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return [squash(p) for p in out]


_KEYWORDS = {"const", "unsigned", "signed", "long", "short", "int", "char", "float", "double", "bool", "void"}


def strip_name(p):
    """'const char* cachePath' -> 'const char*'; 'void (*cb)(void*)' -> FNPTR"""
    if "(*" in p:
        return "FNPTR"
    p = p.replace("*", " * ").replace("&", " & ")
    toks = p.split()
    if len(toks) > 1 and re.fullmatch(r"[A-Za-z_]\w*", toks[-1]) and toks[-1] not in _KEYWORDS:
        toks = toks[:-1]
    if not toks:
        return ""
    return squash(" ".join(toks)).replace(" *", "*").replace("* ", "*")


def norm_params(params, aliases):
    if squash(params) in ("", "void"):
        return "void"
    res = []
    for p in split_top(params):
        t = strip_name(p)
        res.append(aliases.get(t, t))
    return ", ".join(res)


def parse_callback_aliases(*texts):
    al = {}
    for t in texts:
        for m in re.finditer(r"typedef\s+[^;()]*\(\s*\*\s*(\w+)\s*\)\s*\([^;]*\)\s*;", t):
            al[m.group(1)] = "FNPTR"
        for m in re.finditer(r"using\s+(\w+)\s*=\s*[^;()]*\(\s*\*\s*\)\s*\([^;]*\)\s*;", t):
            al[m.group(1)] = "FNPTR"
    return al


def parse_header(text):
    aliases = parse_callback_aliases(text)
    typedefs = {}
    for m in re.finditer(r"typedef\s+([^;()]+?)\(\s*\*\s*(FnDK_\w+)\s*\)\s*\((.*?)\)\s*;", text, re.S):
        ret, name, params = squash(m.group(1)), m.group(2), m.group(3)
        typedefs[name[2:]] = (squash(ret), norm_params(params, aliases))
    binds = re.findall(r"BIND_DK\((DK_\w+)\)", text)
    return typedefs, binds, aliases


def parse_exports(text, extra_aliases):
    aliases = dict(extra_aliases)
    aliases.update(parse_callback_aliases(text))
    exports, i = {}, 0
    while True:
        i = text.find("DK_API", i)
        if i < 0:
            break
        j = i + len("DK_API")
        m = re.match(r"\s+([\w\s\*:]+?)\s+(DK_\w+)\s*\(", text[j:])
        if not m:
            i = j
            continue
        ret, name = squash(m.group(1)), m.group(2)
        k = j + m.end()
        depth, start = 1, k
        while k < len(text) and depth:
            depth += (text[k] == "(") - (text[k] == ")")
            k += 1
        exports[name] = (aliases.get(ret, ret), norm_params(text[start:k - 1], aliases))
        i = k
    return exports


def dll_exports(path):
    objdump_cmd = "objdump"
    if shutil.which(objdump_cmd) is None:
        for candidate in [
            r"C:\Temp\w64devkit\w64devkit\bin\objdump.exe",
            r"C:\w64devkit\bin\objdump.exe",
            r"C:\msys64\ucrt64\bin\objdump.exe"
        ]:
            if os.path.exists(candidate):
                objdump_cmd = candidate
                break
    out = subprocess.run([objdump_cmd, "-p", path], capture_output=True, text=True, check=True).stdout
    return set(re.findall(r"\b(DK_\w+)\b", out))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--header", required=True)
    ap.add_argument("--export-src", required=True)
    ap.add_argument("--golden", required=True)
    ap.add_argument("--update", action="store_true")
    ap.add_argument("--dll")
    ap.add_argument("--strict-dll", action="store_true", help="Fail if DLL exports symbols not in golden file")
    a = ap.parse_args()
    htxt = open(a.header, encoding="utf-8-sig", errors="replace").read()
    etxt = open(a.export_src, encoding="utf-8-sig", errors="replace").read()
    typedefs, binds, aliases = parse_header(htxt)
    exports = parse_exports(etxt, aliases)
    errors, warns = [], []

    bound = sorted(set(binds))
    for b in bound:
        if b not in exports:
            errors.append(f"bound in plugin header but NOT exported: {b}")
            continue
        td = typedefs.get(b)
        if td is None:
            errors.append(f"no FnDK typedef for bound symbol {b}")
            continue
        if td != exports[b]:
            errors.append(f"signature drift {b}: header {td[0]} ({td[1]})  vs export {exports[b][0]} ({exports[b][1]})")
    unlisted = sorted(set(exports) - set(bound))

    current = [
        "# dreamkurumiHT ABI golden file. Edit only through `check_exports.py --update` in a reviewed commit.",
        "# api: symbol | return | params",
    ]
    for b in bound:
        if b in exports:
            current.append(f"api: {b} | {exports[b][0]} | {exports[b][1]}")
    current.append("# unlisted: exported but not part of the documented plugin header (public by accident until decided)")
    for u in unlisted:
        current.append(f"unlisted: {u} | {exports[u][0]} | {exports[u][1]}")

    if a.update:
        with open(a.golden, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(current) + "\n")
        print(f"golden updated: {len(bound)} api, {len(unlisted)} unlisted")
        for e in errors:
            print("ERROR:", e)
        sys.exit(1 if errors else 0)

    try:
        old = [l.rstrip("\n") for l in open(a.golden, encoding="utf-8") if l.strip() and not l.startswith("#")]
    except FileNotFoundError:
        sys.exit(f"golden file missing: {a.golden} (run with --update once)")
    cur = [l for l in current if not l.startswith("#")]
    oldset, curset = set(old), set(cur)
    for l in sorted(oldset - curset):
        kind, rest = l.split(": ", 1)
        name = rest.split(" | ")[0]
        still = [c for c in cur if c.split(": ", 1)[1].startswith(name + " |")]
        label = "BREAKING change/removal" if kind == "api" else "unlisted export changed/removed"
        errors.append(f"{label} -> {l}" + (f"   now: {still[0]}" if still else ""))
    for l in sorted(curset - oldset):
        if l.startswith("unlisted:"):
            errors.append(f"NEW unlisted export (document it in the plugin header or un-export it): {l}")
        else:
            warns.append(f"new api symbol (ok, bless with --update): {l}")

    if a.dll:
        have = dll_exports(a.dll)
        golden_names = {l.split(": ", 1)[1].split(" | ")[0] for l in old}
        for l in old:
            n = l.split(": ", 1)[1].split(" | ")[0]
            if n not in have:
                errors.append(f"golden symbol missing from DLL export table: {n}")
        if a.strict_dll:
            for h in sorted(have):
                if h not in golden_names:
                    errors.append(f"DLL exports extra symbol not in golden file: {h}")

    for w in warns:
        print("warn:", w)
    for e in errors:
        print("ERROR:", e)
    print(f"checked {len(bound)} api + {len(unlisted)} unlisted exports: {'FAILED' if errors else 'OK'}")
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
