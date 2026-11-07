#!/usr/bin/env python3
"""MorseOS bundle resolver.

Reads a bundle manifest (bundle.ini) and the module descriptors (*/module.ini), resolves
the dependencies and writes what the build needs:

  <out>/bundle.h    #defines: bundle name/version, MOS_HAVE_<MODULE> for every module in
  <out>/bundle.mk   MOS_SRC (module sources), MOS_SDK_SRC (extra SDK sources), MOS_INC
  <out>/apps.c      the table of apps, from which the menus are built

Manifest (bundle.ini):
  [bundle]  name = ..., version = ...
  [apps]     <app> = y | n         (core modules are always in)
  [features] <lib> = y | n         (libraries used by the system when present, e.g. fitpro)

Module descriptor (<root>/<group>/<name>/module.ini):
  [module]
  type        = core | lib | app
  depends     = a, b               (other modules, any type)
  sources     = x.c y.c            (relative to the module directory)
  sdk_sources = drivers/5316/adc.c (relative to the SDK root)
  menu        = MENU               (apps: which menu, empty = not in a menu)
  label       = LABEL              (apps: box label)
  entry       = app_<name>         (apps: name of the C descriptor, default app_<name>)

A library that no selected module depends on is left out, together with its sources,
RAM and GATT services.

Usage: bundle.py <project dir> <bundle.ini> <out dir>     (resolve and generate)
       bundle.py --sizes <project dir> <build dir>        (flash/RAM per module after a build)
"""

import configparser
import os
import subprocess
import sys

GROUPS = ("core", "lib", "apps")


def die(msg):
    print(f"bundle: {msg}", file=sys.stderr)
    sys.exit(1)


def words(v):
    return [w for w in v.replace(",", " ").split() if w]


def load_modules(project):
    mods = {}
    for group in GROUPS:
        gdir = os.path.join(project, group)
        if not os.path.isdir(gdir):
            continue
        for name in sorted(os.listdir(gdir)):
            ini = os.path.join(gdir, name, "module.ini")
            if not os.path.isfile(ini):
                continue
            cp = configparser.ConfigParser(inline_comment_prefixes=(";", "#"))
            cp.read(ini)
            if "module" not in cp:
                die(f"{ini}: no [module] section")
            m = cp["module"]
            if name in mods:
                die(f"module {name} defined twice")
            mods[name] = {
                "name": name,
                "dir": os.path.join(group, name),
                "type": m.get("type", "lib" if group == "lib" else group.rstrip("s")),
                "depends": words(m.get("depends", "")),
                "sources": words(m.get("sources", "")),
                "sdk_sources": words(m.get("sdk_sources", "")),
                "menu": m.get("menu", "").strip(),
                "label": m.get("label", name.upper()).strip(),
                "entry": m.get("entry", f"app_{name}").strip(),
            }
    return mods


def resolve(mods, wanted):
    """Dependencies first (topological order); dies on unknown modules and cycles."""
    order, state = [], {}

    def visit(n, chain):
        if n not in mods:
            die(f"unknown module '{n}'" + (f" (needed by {chain[-1]})" if chain else ""))
        if state.get(n) == "done":
            return
        if state.get(n) == "visiting":
            die("dependency cycle: " + " -> ".join(chain + [n]))
        state[n] = "visiting"
        for d in mods[n]["depends"]:
            visit(d, chain + [n])
        state[n] = "done"
        order.append(n)

    for n in wanted:
        visit(n, [])
    return order


def generate(project, manifest, out):
    mods = load_modules(project)
    cp = configparser.ConfigParser(inline_comment_prefixes=(";", "#"))
    if not cp.read(manifest):
        die(f"cannot read {manifest}")
    bundle = cp["bundle"] if "bundle" in cp else {}
    name = bundle.get("name", "morseos")
    version = bundle.get("version", "0.0")
    apps = cp["apps"] if "apps" in cp else {}
    for a in apps:
        if a not in mods:
            die(f"{manifest}: unknown app '{a}'")
        if mods[a]["type"] != "app":
            die(f"{manifest}: '{a}' is a {mods[a]['type']}, not an app")
    def on(a):
        v = apps[a].strip().lower()
        if v in ("y", "yes", "on", "1", "true"):
            return True
        if v in ("n", "no", "off", "0", "false"):
            return False
        die(f"{manifest}: {a} = {apps[a]!r}: use y or n")

    selected = [a for a in apps if on(a)]
    feats = cp["features"] if "features" in cp else {}
    for f_ in feats:
        if f_ not in mods:
            die(f"{manifest}: unknown feature '{f_}'")
        if mods[f_]["type"] != "lib":
            die(f"{manifest}: feature '{f_}' is a {mods[f_]['type']}, not a lib")
        v = feats[f_].strip().lower()
        if v in ("y", "yes", "on", "1", "true"):
            selected.append(f_)
        elif v not in ("n", "no", "off", "0", "false"):
            die(f"{manifest}: {f_} = {feats[f_]!r}: use y or n")
    cores = [n for n, m in mods.items() if m["type"] == "core"]
    order = resolve(mods, cores + selected)

    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "bundle.h"), "w") as f:
        f.write("/* generated by tools/bundle.py from %s: do not edit */\n" % os.path.basename(manifest))
        f.write("#ifndef MOS_BUNDLE_H\n#define MOS_BUNDLE_H\n")
        f.write(f'#define MOS_BUNDLE_NAME "{name}"\n#define MOS_VERSION "{version}"\n')
        for n in order:
            f.write(f"#define MOS_HAVE_{n.upper()} 1\n")
        f.write("#endif\n")
    with open(os.path.join(out, "bundle.mk"), "w") as f:
        f.write("# generated by tools/bundle.py: do not edit\n")
        srcs = [os.path.join(mods[n]["dir"], s) for n in order for s in mods[n]["sources"]]
        sdk = sorted({s for n in order for s in mods[n]["sdk_sources"]})
        incs = sorted({mods[n]["dir"] for n in order})
        f.write("MOS_SRC = " + " \\\n\t".join(srcs) + "\n")
        f.write("MOS_SDK_SRC = " + " \\\n\t".join(sdk) + "\n")
        f.write("MOS_INC = " + " ".join(incs) + "\n")
        f.write("MOS_MODULES = " + " ".join(order) + "\n")
    with open(os.path.join(out, "apps.c"), "w") as f:
        f.write("/* generated by tools/bundle.py: do not edit */\n#include \"mos.h\"\n\n")
        app_list = [n for n in order if mods[n]["type"] == "app"]
        for n in app_list:
            f.write(f"extern const mos_app_t {mods[n]['entry']};\n")
        f.write("\nconst mos_entry_t mos_entries[] = {\n")
        for n in app_list:
            m = mods[n]
            f.write(f'    {{"{m["menu"]}", "{m["label"]}", &{m["entry"]}}},\n')
        f.write("};\nconst int mos_entry_count = sizeof(mos_entries) / sizeof(mos_entries[0]);\n")

    libs_out = sorted(n for n, m in mods.items() if n not in order)
    print(f"bundle {name} {version}: {len(order)} modules")
    for n in order:
        m = mods[n]
        where = f"  [{m['menu']} > {m['label']}]" if m["type"] == "app" and m["menu"] else ""
        print(f"  {m['type']:4s} {n}{where}")
    if libs_out:
        print("  left out: " + " ".join(libs_out))


def sizes(project, build):
    """text+data (flash) and data+bss (RAM) per module, from the object files."""
    mods = load_modules(project)
    rows = []
    for n, m in mods.items():
        flash = ram = 0
        found = False
        for s in m["sources"]:
            obj = os.path.join(build, "app", m["dir"], os.path.splitext(s)[0] + ".o")
            if not os.path.isfile(obj):
                continue
            found = True
            out = subprocess.run(["readelf", "-SW", obj], capture_output=True, text=True,
                                 env=dict(os.environ, LC_ALL="C")).stdout
            for line in out.splitlines():
                parts = line.replace("[ ", "[").split()
                if len(parts) < 7 or not parts[0].startswith("["):
                    continue
                try:
                    sec, size = parts[1], int(parts[5], 16)
                except ValueError:
                    continue  # header line
                if sec.startswith((".text", ".rodata", ".ram_code")):
                    flash += size
                elif sec.startswith(".data"):
                    flash += size
                    ram += size
                elif sec.startswith(".bss"):
                    ram += size
        if found:
            rows.append((n, m["type"], flash, ram))
    print(f"{'module':16s} {'type':5s} {'flash':>8s} {'RAM':>7s}   (before --gc-sections)")
    for n, t, fl, ra in sorted(rows, key=lambda r: -r[2]):
        print(f"{n:16s} {t:5s} {fl:8d} {ra:7d}")


if __name__ == "__main__":
    a = sys.argv[1:]
    if a[:1] == ["--sizes"] and len(a) == 3:
        sizes(a[1], a[2])
    elif len(a) == 3:
        generate(*a)
    else:
        print(__doc__)
        sys.exit(2)
