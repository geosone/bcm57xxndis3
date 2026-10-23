#!/usr/bin/env python3
"""Expand the network fixture from locally owned, extracted WfW 3.11 media."""
import argparse
from pathlib import Path
import subprocess


FILES = {
    "NDIS.386": "NDIS.38_", "VNETSUP.386": "VNETSUP.38_",
    "IFSHLP.SYS": "IFSHLP.SY_", "NETBEUI.386": "NETBEUI.38_",
    "NE2000.386": "NE2000.38_", "NE2000.DOS": "NE2000.DO_",
    "NET.EXE": "NET.EX_", "NET.MSG": "NET.MS_",
    "PROTMAN.EXE": "PROTMAN.EX_", "PROTMAN.DOS": "PROTMAN.DO_",
    "NDISHLP.SYS": "NDISHLP.SY_",
}

# NDIS CPU calibration fix from JHRobotics/patcher9x,
# PATCH_CPU_SPEED_NDIS_V4. Copyright (c) 2022 JHRobotics; MIT license,
# see LICENSES/patcher9x-MIT.txt. Applied only to a newly expanded test file.
NDIS_BEFORE = bytes.fromhex(
    "B9 00 00 10 00 90 E2 FE CD 20 3F 00 01 00 "
    "2B C2 B9 E8 03 00 00 F7 E1 A3")
NDIS_AFTER = bytes.fromhex(
    "B9 00 00 40 06 E2 FE CD 20 3F 00 01 00 29 "
    "D0 75 01 40 6A 0A 59 F7 E1 A3")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("media", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--decoder", type=Path, default=Path("build/expand_kwaj"))
    parser.add_argument("--ndis-cpu-fix", action="store_true",
                        help="apply the checked CPU calibration fix to the test copy")
    args = parser.parse_args()
    sources = {}
    for output, name in FILES.items():
        matches = sorted(path for path in args.media.rglob("*")
                         if path.is_file() and path.name.upper() == name)
        if len(matches) != 1:
            parser.error(f"expected exactly one {name}, found {len(matches)}")
        sources[output] = matches[0]
    args.output.mkdir(parents=True, exist_ok=True)
    for output, source in sources.items():
        subprocess.run([str(args.decoder.resolve()), str(source),
                        str(args.output / output)], check=True)
    if args.ndis_cpu_fix:
        path = args.output / "NDIS.386"
        data = path.read_bytes()
        if data.count(NDIS_BEFORE) != 1 or NDIS_AFTER in data:
            parser.error("NDIS CPU patch signature is missing or ambiguous")
        path.write_bytes(data.replace(NDIS_BEFORE, NDIS_AFTER))
        print("Applied checked NDIS CPU calibration fix to test copy")
    print(f"Expanded {len(sources)} network files to {args.output}")


if __name__ == "__main__":
    main()
