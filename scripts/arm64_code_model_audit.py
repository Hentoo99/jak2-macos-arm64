#!/usr/bin/env python3
"""Audit how ARM64 GOAL v3 objects reach their static data.

For every segment the script decodes the executable prefix and reports:
  - LDR literal / ADR instructions (the 'literal' code model, +-1 MiB);
  - ADRP + LDR/ADD pairs (the 'page' code model, +-4 GiB) and whether each
    pair resolves to an aligned address inside the segment's static data.

With --require-page it fails if any literal-model reference remains, which is
the precondition for linking the code into a signed image.  With --compare it
checks that two builds of the same source reference the same static offsets,
in the same order.

Usage:
  arm64_code_model_audit.py [--require-page] OBJ_OR_DIR...
  arm64_code_model_audit.py --compare LITERAL.o PAGE.o
"""

import argparse
import struct
import sys
from pathlib import Path

N_SEG = 3
SEG_NAMES = ["main", "debug", "top-level"]
EXEC_FLAG = 0x80000000


def sign_extend(value, bits):
    sign = 1 << (bits - 1)
    return (value ^ sign) - sign


def parse_object(data):
    if data[:4] != b"GOAL":
        raise ValueError("not a v3 GOAL object (missing GOAL magic)")
    version, n_seg = struct.unpack_from("<II", data, 8)
    if version != 3 or n_seg != N_SEG:
        raise ValueError(f"unsupported object version {version} / {n_seg} segments")
    table = struct.unpack_from("<12I", data, 16)
    link = [(table[2 * i], table[2 * i + 1]) for i in range(N_SEG)]
    code = [(table[6 + 2 * i], table[6 + 2 * i + 1]) for i in range(N_SEG)]
    (data_start,) = struct.unpack_from("<I", data, 64)
    segments = []
    for seg in range(N_SEG):
        offset, size = code[seg]
        link_size = link[seg][1]
        if not link_size & EXEC_FLAG:
            raise ValueError("segment has no ARM64 executable-size flag (x86 object?)")
        exec_size = link_size & ~EXEC_FLAG
        seg_data = data[data_start + offset:data_start + offset + size]
        segments.append((seg_data, exec_size))
    return segments


def audit_segment(seg_data, exec_size):
    """Return (stats, ordered list of (kind, target - exec_size), errors)."""
    stats = {"ldr_literal": 0, "adr": 0, "adrp_ldr": 0, "adrp_add": 0}
    refs = []
    errors = []
    words = [struct.unpack_from("<I", seg_data, pc)[0] for pc in range(0, exec_size, 4)]
    skip_next = False
    for i, word in enumerate(words):
        pc = 4 * i
        if skip_next:
            skip_next = False
            continue
        if word & 0xFF000000 == 0x58000000:  # LDR (literal), 64-bit
            target = pc + 4 * sign_extend((word >> 5) & 0x7FFFF, 19)
            stats["ldr_literal"] += 1
            refs.append(("load", target - exec_size))
        elif word & 0x9F000000 == 0x10000000:  # ADR
            imm = (((word >> 5) & 0x7FFFF) << 2) | ((word >> 29) & 3)
            target = pc + sign_extend(imm, 21)
            stats["adr"] += 1
            refs.append(("address", target - exec_size))
        elif word & 0x9F000000 == 0x90000000:  # ADRP
            rd = word & 31
            imm = (((word >> 5) & 0x7FFFF) << 2) | ((word >> 29) & 3)
            page = (pc & ~0xFFF) + 0x1000 * sign_extend(imm, 21)
            if i + 1 >= len(words):
                errors.append(f"ADRP at {pc:#x} is the last word of the code")
                continue
            nxt = words[i + 1]
            lo12 = (nxt >> 10) & 0xFFF
            rn = (nxt >> 5) & 31
            if rn != rd:
                errors.append(f"ADRP at {pc:#x} is not paired with an instruction using x{rd}")
                continue
            if nxt & 0xFFC00000 == 0xF9400000:  # LDR (unsigned offset), 64-bit
                target = page + 8 * lo12
                stats["adrp_ldr"] += 1
                refs.append(("load", target - exec_size))
                if target % 8:
                    errors.append(f"ADRP+LDR at {pc:#x} targets unaligned {target:#x}")
            elif nxt & 0xFFC00000 == 0x91000000:  # ADD (immediate), no shift
                target = page + lo12
                stats["adrp_add"] += 1
                refs.append(("address", target - exec_size))
            else:
                errors.append(f"ADRP at {pc:#x} followed by unsupported word {nxt:#010x}")
                continue
            skip_next = True
        else:
            continue
        target = refs[-1][1] + exec_size
        if not exec_size <= target < len(seg_data):
            errors.append(f"reference at {pc:#x} targets {target:#x}, outside static data "
                          f"[{exec_size:#x}, {len(seg_data):#x})")
    return stats, refs, errors


def audit_file(path):
    segments = parse_object(path.read_bytes())
    total = {"ldr_literal": 0, "adr": 0, "adrp_ldr": 0, "adrp_add": 0}
    all_refs = []
    all_errors = []
    for seg, (seg_data, exec_size) in enumerate(segments):
        stats, refs, errors = audit_segment(seg_data, exec_size)
        for k, v in stats.items():
            total[k] += v
        all_refs.append(refs)
        all_errors += [f"{SEG_NAMES[seg]}: {e}" for e in errors]
    return total, all_refs, all_errors


def collect(paths):
    for p in paths:
        p = Path(p)
        if p.is_dir():
            yield from sorted(p.glob("*.o"))
        else:
            yield p


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="+")
    parser.add_argument("--require-page", action="store_true",
                        help="fail if any LDR literal / ADR reference remains")
    parser.add_argument("--compare", action="store_true",
                        help="compare the static references of exactly two objects")
    args = parser.parse_args()

    if args.compare:
        if len(args.paths) != 2:
            parser.error("--compare takes exactly two objects")
        (_, refs_a, err_a), (_, refs_b, err_b) = (audit_file(Path(p)) for p in args.paths)
        ok = not err_a and not err_b
        for e in err_a + err_b:
            print(f"error: {e}")
        for seg in range(N_SEG):
            if refs_a[seg] != refs_b[seg]:
                ok = False
                print(f"{SEG_NAMES[seg]}: static references differ "
                      f"({len(refs_a[seg])} vs {len(refs_b[seg])})")
                for i, (a, b) in enumerate(zip(refs_a[seg], refs_b[seg])):
                    if a != b:
                        print(f"  first difference at reference {i}: {a} vs {b}")
                        break
            else:
                print(f"{SEG_NAMES[seg]}: {len(refs_a[seg])} static references match")
        return 0 if ok else 1

    failed = False
    grand = {"ldr_literal": 0, "adr": 0, "adrp_ldr": 0, "adrp_add": 0}
    n_files = 0
    for path in collect(args.paths):
        n_files += 1
        try:
            stats, _, errors = audit_file(path)
        except ValueError as e:
            print(f"{path.name}: skipped ({e})")
            continue
        for k, v in stats.items():
            grand[k] += v
        literal_refs = stats["ldr_literal"] + stats["adr"]
        bad = errors or (args.require_page and literal_refs)
        if bad or len(args.paths) == 1 and not Path(args.paths[0]).is_dir():
            print(f"{path.name}: ldr-literal={stats['ldr_literal']} adr={stats['adr']} "
                  f"adrp+ldr={stats['adrp_ldr']} adrp+add={stats['adrp_add']}")
        for e in errors:
            print(f"  error: {e}")
        if bad:
            failed = True
    print(f"{n_files} objects: ldr-literal={grand['ldr_literal']} adr={grand['adr']} "
          f"adrp+ldr={grand['adrp_ldr']} adrp+add={grand['adrp_add']}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
