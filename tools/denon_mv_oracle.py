#!/usr/bin/env python3
"""
Independent Python implementation of the Denon MV codec, plus a validator for
test/host/denon_mv_vectors.csv.

WHY THIS EXISTS
The C codec and this oracle are written from the protocol, not from each other.
The shared vector table is checked against BOTH. A bug reachable by copying the
implementation into the test -- the classic way a test table gets written to
match whatever the code already does -- requires making the same mistake twice,
independently, in two languages.

It also means the boundary table is verifiable with no compiler present, which
is how it got verified before any C was built.

Usage:
    python tools/denon_mv_oracle.py            # validate the vector table
    python tools/denon_mv_oracle.py --dump     # print the table as C
"""

import argparse
import csv
import os
import sys

HALF_DB_MAX = 197  # 98.5 dB

OK, NOT_MV, MALFORMED, OUT_OF_RANGE, MAX_REPORT = (
    "OK", "NOT_MV", "MALFORMED", "OUT_OF_RANGE", "MAX_REPORT")


def _parse_digits(p):
    """Bare digit payload -> (status, half_db)."""
    if not p or not all(c.isdigit() for c in p):
        return MALFORMED, None
    if len(p) not in (2, 3):
        return MALFORMED, None

    whole = int(p[0]) * 10 + int(p[1])
    half = 0
    if len(p) == 3:
        if p[2] == "5":
            half = 1
        elif p[2] == "0":
            half = 0
        else:
            return MALFORMED, None

    value = whole * 2 + half
    if value > HALF_DB_MAX:
        return OUT_OF_RANGE, None
    return OK, value


def mv_parse(s):
    """Wire string -> (status, half_db or None)."""
    if s is None or len(s) < 2 or s[0] != "M" or s[1] != "V":
        return NOT_MV, None

    if s.startswith("MVMAX"):
        rest = s[len("MVMAX"):].lstrip(" ")
        st, val = _parse_digits(rest)
        if st != OK:
            return MALFORMED, None
        return MAX_REPORT, val

    return _parse_digits(s[2:])


def mv_format(half_db):
    """half_db -> (status, wire string or None)."""
    if half_db < 0 or half_db > HALF_DB_MAX:
        return OUT_OF_RANGE, None
    whole, half = divmod(half_db, 2)
    return OK, "MV%02d%s" % (whole, "5" if half else "")


def load_vectors(path):
    rows = []
    with open(path, newline="", encoding="utf-8") as fh:
        for raw in fh:
            raw = raw.strip()
            if not raw or raw.startswith("#") or raw.startswith("direction,"):
                continue
            parts = next(csv.reader([raw]))
            if len(parts) < 4:
                continue
            rows.append({
                "direction": parts[0].strip(),
                "input": parts[1],
                "status": parts[2].strip(),
                "expect": parts[3].strip(),
                "note": parts[4].strip() if len(parts) > 4 else "",
            })
    return rows


def validate(path):
    rows = load_vectors(path)
    failures = []

    for i, r in enumerate(rows, 1):
        d, inp, want_st, want_ex = r["direction"], r["input"], r["status"], r["expect"]

        if d == "parse":
            st, val = mv_parse(inp)
            if st != want_st:
                failures.append("%s:%d parse(%r) status %s, table says %s"
                                % (path, i, inp, st, want_st))
            elif st in (OK, MAX_REPORT) and val != int(want_ex):
                failures.append("%s:%d parse(%r) = %s, table says %s"
                                % (path, i, inp, val, want_ex))

        elif d == "format":
            st, s = mv_format(int(inp))
            if st != want_st:
                failures.append("%s:%d format(%s) status %s, table says %s"
                                % (path, i, inp, st, want_st))
            elif st == OK and s != want_ex:
                failures.append("%s:%d format(%s) = %r, table says %r"
                                % (path, i, inp, s, want_ex))

        elif d == "roundtrip":
            v = int(inp)
            st, s = mv_format(v)
            if st != OK:
                failures.append("%s:%d roundtrip format(%s) -> %s" % (path, i, inp, st))
                continue
            st2, back = mv_parse(s)
            if st2 != OK or back != v:
                failures.append("%s:%d roundtrip %s -> %r -> (%s,%s)"
                                % (path, i, inp, s, st2, back))
        else:
            failures.append("%s:%d unknown direction %r" % (path, i, d))

    return rows, failures


def exhaustive_roundtrip():
    """Every legal value must survive format->parse unchanged."""
    bad = []
    for v in range(0, HALF_DB_MAX + 1):
        st, s = mv_format(v)
        if st != OK:
            bad.append("format(%d) -> %s" % (v, st))
            continue
        st2, back = mv_parse(s)
        if st2 != OK or back != v:
            bad.append("%d -> %r -> (%s, %s)" % (v, s, st2, back))
    return bad


C_STATUS = {
    OK: "DENON_CODEC_OK",
    NOT_MV: "DENON_CODEC_NOT_MV",
    MALFORMED: "DENON_CODEC_MALFORMED",
    OUT_OF_RANGE: "DENON_CODEC_OUT_OF_RANGE",
    MAX_REPORT: "DENON_CODEC_MAX_REPORT",
}


def c_escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def generate_c(rows, out_path, src_name):
    """Emit the vector table as a C include. Single source of truth stays the
    CSV; CI regenerates and diffs so the two cannot drift."""
    lines = [
        "/* GENERATED by tools/denon_mv_oracle.py from %s -- DO NOT EDIT." % src_name,
        " * Regenerate: python tools/denon_mv_oracle.py --gen-c",
        " * CI fails if this file differs from a fresh generation. */",
        "",
        "typedef struct {",
        "    const char *input_s;   /* parse: wire string, else NULL */",
        "    int         input_v;   /* format/roundtrip: half-dB */",
        "    int         status;",
        "    int         expect_v;  /* half-dB when status OK/MAX_REPORT */",
        "    const char *expect_s;  /* format: wire string, else NULL */",
        "    const char *note;",
        "} mv_vector_t;",
        "",
    ]

    for kind in ("parse", "format", "roundtrip"):
        sel = [r for r in rows if r["direction"] == kind]
        lines.append("static const mv_vector_t MV_%s[] = {" % kind.upper())
        for r in sel:
            st = C_STATUS[r["status"]]
            note = c_escape(r["note"])
            if kind == "parse":
                ev = r["expect"] if r["expect"] != "-" else "0"
                lines.append('    { "%s", 0, %s, %s, NULL, "%s" },'
                             % (c_escape(r["input"]), st, ev, note))
            elif kind == "format":
                es = ('"%s"' % c_escape(r["expect"])) if r["expect"] != "-" else "NULL"
                lines.append('    { NULL, %s, %s, 0, %s, "%s" },'
                             % (r["input"], st, es, note))
            else:
                lines.append('    { NULL, %s, %s, %s, NULL, "%s" },'
                             % (r["input"], st, r["input"], note))
        lines.append("};")
        lines.append("#define MV_%s_COUNT %d" % (kind.upper(), len(sel)))
        lines.append("")

    text = "\n".join(lines)
    with open(out_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--vectors", default=None)
    ap.add_argument("--dump", action="store_true")
    ap.add_argument("--gen-c", action="store_true",
                    help="regenerate the C vector include")
    ap.add_argument("--check-c", action="store_true",
                    help="fail if the C include is stale (CI gate)")
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    path = args.vectors or os.path.join(root, "test", "host", "denon_mv_vectors.csv")

    if not os.path.exists(path):
        print("MISSING vector table: %s" % path)
        return 1

    rows, failures = validate(path)

    if args.dump:
        for r in rows:
            print(r)
        return 0

    inc_path = os.path.join(root, "test", "host", "denon_mv_vectors.inc")

    if args.gen_c:
        generate_c(rows, inc_path, "denon_mv_vectors.csv")
        print("wrote %s (%d vectors)" % (inc_path, len(rows)))
        return 0

    if args.check_c:
        if not os.path.exists(inc_path):
            print("MISSING generated include: %s" % inc_path)
            print("  run: python tools/denon_mv_oracle.py --gen-c")
            return 1
        with open(inc_path, encoding="utf-8") as fh:
            on_disk = fh.read()
        tmp = inc_path + ".check"
        fresh = generate_c(rows, tmp, "denon_mv_vectors.csv")
        os.remove(tmp)
        if on_disk.replace("\r\n", "\n") != fresh.replace("\r\n", "\n"):
            print("STALE: %s does not match denon_mv_vectors.csv" % inc_path)
            print("  run: python tools/denon_mv_oracle.py --gen-c")
            return 1
        print("denon_mv vectors: C include is in sync with the CSV")
        return 0

    rt = exhaustive_roundtrip()

    print("denon_mv_oracle: %d vectors, %d exhaustive roundtrips (0..%d)"
          % (len(rows), HALF_DB_MAX + 1, HALF_DB_MAX))

    for f in failures + rt:
        print("  FAIL %s" % f)

    if failures or rt:
        print("FAILED: %d vector, %d roundtrip" % (len(failures), len(rt)))
        return 1

    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
