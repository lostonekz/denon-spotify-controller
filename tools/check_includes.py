#!/usr/bin/env python3
"""
GATE 1 — Layer boundary enforcement by #include inspection.

docs/ARCHITECTURE.md §1.4. Fails the build on any include edge that violates
the layer table in tools/arch_rules.json.

CMake REQUIRES is the primary enforcement (an undeclared component physically
cannot compile). This catches what CMake does not: a component that declares a
dependency it should not have, and the ui -> hal symbol-level exception, which
CMake can only express at whole-component granularity.
"""

import argparse
import json
import os
import re
import sys

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)


def load_rules(root):
    with open(os.path.join(root, "tools", "arch_rules.json"), encoding="utf-8") as fh:
        return json.load(fh)


def owning_component(root, path):
    """Map a source file to its component name."""
    rel = os.path.relpath(path, root).replace(os.sep, "/")
    parts = rel.split("/")
    if parts[0] == "main":
        return "main"
    if parts[0] == "components" and len(parts) > 1:
        return parts[1]
    return None


def build_header_index(root, rules):
    """header filename -> component that publishes it."""
    index = {}
    for comp in rules["component_layer"]:
        inc_dir = os.path.join(root, "components", comp, "include")
        if not os.path.isdir(inc_dir):
            continue
        for f in os.listdir(inc_dir):
            if f.endswith(".h"):
                index[f] = comp
    return index


def classify(header, rules, header_index):
    """Return a dependency token for an included header."""
    base = os.path.basename(header)

    if base in rules["stdlib_headers"]:
        return None  # always fine

    if header.startswith("unity"):
        return None

    for pref in rules["idf_prefixes"]:
        if header.startswith(pref) or base.startswith(pref):
            return "idf"

    if header.startswith("lvgl") or base.startswith("lv_"):
        return "lvgl"

    if base in header_index:
        return ("component", header_index[base])

    return None  # local/private header, not a cross-component edge


def check(root, verbose=False):
    rules = load_rules(root)
    header_index = build_header_index(root, rules)
    layers = rules["layers"]
    comp_layer = rules["component_layer"]
    glue = rules["ui_hal_glue"]

    violations = []
    scanned = 0

    roots = [os.path.join(root, "main"), os.path.join(root, "components")]
    for base in roots:
        if not os.path.isdir(base):
            continue
        for dirpath, _dirs, files in os.walk(base):
            for f in files:
                if not f.endswith((".c", ".h")):
                    continue
                path = os.path.join(dirpath, f)
                comp = owning_component(root, path)
                if comp is None:
                    continue
                if comp not in comp_layer:
                    violations.append("%s: component %r is not declared in "
                                      "arch_rules.json component_layer"
                                      % (os.path.relpath(path, root), comp))
                    continue

                scanned += 1
                my_layer = comp_layer[comp]
                allowed = layers.get(my_layer, [])

                with open(path, encoding="utf-8", errors="replace") as fh:
                    text = fh.read()

                for inc in INCLUDE_RE.findall(text):
                    tok = classify(inc, rules, header_index)
                    if tok is None:
                        continue

                    if tok == "idf":
                        if "idf" in allowed or "idf_freertos" in allowed:
                            if my_layer == "core" and not (
                                    inc.startswith("freertos/") or
                                    inc == "sdkconfig.h" or
                                    inc.startswith("esp_err")):
                                violations.append(
                                    "%s: core layer may only use FreeRTOS/esp_err, "
                                    "got %r" % (os.path.relpath(path, root), inc))
                            continue
                        violations.append("%s: layer %r may not include ESP-IDF (%r)"
                                          % (os.path.relpath(path, root), my_layer, inc))
                        continue

                    if tok == "lvgl":
                        if "lvgl" not in allowed:
                            violations.append(
                                "%s: layer %r may not include LVGL (%r) - "
                                "only the ui layer may"
                                % (os.path.relpath(path, root), my_layer, inc))
                        continue

                    kind, dep_comp = tok
                    assert kind == "component"
                    if dep_comp == comp:
                        continue

                    dep_layer = comp_layer.get(dep_comp)
                    if dep_layer is None:
                        violations.append("%s: includes %r from undeclared component %r"
                                          % (os.path.relpath(path, root), inc, dep_comp))
                        continue

                    # The ui -> hal glue exception, at symbol/header granularity.
                    if my_layer == "ui" and dep_layer == "hal":
                        if (dep_comp in glue["allowed_components"] and
                                os.path.basename(inc) in glue["allowed_headers"]):
                            continue
                        violations.append(
                            "%s: ui may include hal only for LVGL glue "
                            "(%s), got %r from %r"
                            % (os.path.relpath(path, root),
                               "/".join(glue["allowed_headers"]), inc, dep_comp))
                        continue

                    if dep_layer not in allowed:
                        violations.append(
                            "%s: %s(%s) -> %s(%s) via %r is not an allowed edge"
                            % (os.path.relpath(path, root), comp, my_layer,
                               dep_comp, dep_layer, inc))

    return scanned, violations


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()
    root = os.path.abspath(args.root)

    scanned, violations = check(root, args.verbose)

    print("check_includes: %d file(s) scanned" % scanned)
    for v in violations:
        print("  VIOLATION %s" % v)

    if violations:
        print("FAILED: %d layering violation(s)" % len(violations))
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
