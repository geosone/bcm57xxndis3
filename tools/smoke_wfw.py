#!/usr/bin/env python3
r"""Boot an isolated copy of an installed DOS/WfW image with BCM57DBG.

Requires a bootable raw image, FAT partition at --partition-offset, Windows in
C:\WINDOWS, mtools and QEMU. Optional --ndis must be an expanded WfW NDIS.386.
The source image is only read. All changes are made to a fresh temporary copy.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

from prepare_wfw_network import FILES


def set_values(text, section, values):
    """Replace named keys in one INI section, preserving unrelated entries."""
    lines = text.splitlines()
    start = next((i for i, line in enumerate(lines)
                  if line.strip().lower() == f"[{section.lower()}]"), None)
    if start is None:
        lines.extend(["", f"[{section}]"])
        start = len(lines) - 1
    end = next((i for i in range(start + 1, len(lines))
                if lines[i].strip().startswith("[")), len(lines))
    keys = {key.lower() for key in values}
    body = [line for line in lines[start + 1:end]
            if line.split("=", 1)[0].strip().lower() not in keys]
    body.extend(f"{key}={value}" for key, value in values.items())
    lines[start + 1:end] = body
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--ndis", type=Path)
    parser.add_argument("--vnetsup", type=Path)
    parser.add_argument("--ifshlp", type=Path)
    parser.add_argument("--network-dir", type=Path,
                        help="expanded files for the isolated NE2000/NetBEUI fixture")
    parser.add_argument("--partition-offset", type=int, default=32256)
    parser.add_argument("--timeout", type=int, default=45)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    work = Path(tempfile.mkdtemp(prefix="bcm57xx-wfw-"))
    print(f"Isolated test files: {work}", flush=True)
    image = work / "disk.img"
    shutil.copyfile(args.image, image)
    fat = f"{image}@@{args.partition_offset}"

    def mcopy(source, target):
        subprocess.run(["mcopy", "-o", "-i", fat, str(source), target], check=True)

    if args.network_dir:
        for name in FILES:
            mcopy(args.network_dir / name, "::/WINDOWS/" + name)
        if args.ndis:
            mcopy(args.ndis, "::/WINDOWS/NDIS.386")

    ini = subprocess.check_output(["mtype", "-i", fat,
                                   "::/WINDOWS/SYSTEM.INI"])
    text = ini.decode("cp1252").replace("\r\n", "\n")
    if args.network_dir:
        text = set_values(text, "386Enh", {
            "network": "*vnetbios,C:\\WINDOWS\\VNETSUP.386",
            "netcard": "C:\\WINDOWS\\NE2000.386",
            "transport": "C:\\WINDOWS\\NETBEUI.386",
            "netmisc": "C:\\WINDOWS\\NDIS.386"})
        text = set_values(text, "network", {
            "winnet": "wfwnet/00025100", "computername": "BCMTEST",
            "username": "TEST", "workgroup": "WORKGROUP",
            "autologon": "no", "reconnect": "no"})
        text = set_values(text, "network drivers", {
            "LoadRMDrivers": "No", "devdir": "C:\\WINDOWS",
            "netcard": "ne2000.dos", "transport": "ndishlp.sys,*netbeui"})
        protocol = ("[network.setup]\nversion=0x311\n"
                    "netcard=ms$ne2000,1,MS$NE2000,1\n"
                    "transport=ms$netbeui,MS$NETBEUI\n"
                    "lana0=ms$ne2000,1,ms$netbeui\n"
                    "[protman]\nDriverName=PROTMAN$\nPRIORITY=MS$NETBEUI\n"
                    "[MS$NE2000]\nDriverName=NE2000$\nIRQ=3\nIOBASE=0x300\n"
                    "[MS$NETBEUI]\nDriverName=netbeui$\nBindings=MS$NE2000\n"
                    "LANABASE=0\n")
        (work / "PROTOCOL.INI").write_bytes(protocol.replace("\n", "\r\n").encode())
        mcopy(work / "PROTOCOL.INI", "::/WINDOWS/PROTOCOL.INI")
    # Exercises Get_Profile_Decimal_Int; without a BCM5722 nothing is reset.
    text = set_values(text, "BCM57DBG", {"ResetTest": "1"})
    lines = text.splitlines()
    index = next(i for i, line in enumerate(lines)
                 if line.strip().lower() == "[386enh]")
    additions = ["device=C:\\BCM57DBG.386"]
    if args.ndis and not args.network_dir:
        additions.insert(0, "device=C:\\NDIS.386")
        additions.append("AlwaysLoadNdisWrapper=1")
        mcopy(args.ndis, "::/NDIS.386")
    if args.vnetsup and not args.network_dir:
        additions.insert(0, "device=C:\\VNETSUP.386")
        mcopy(args.vnetsup, "::/VNETSUP.386")
    if args.ifshlp or args.network_dir:
        ifshlp = args.ifshlp or args.network_dir / "IFSHLP.SYS"
        mcopy(ifshlp, "::/IFSHLP.SYS")
        config = subprocess.check_output(["mtype", "-i", fat, "::/CONFIG.SYS"])
        (work / "CONFIG.SYS").write_bytes(
            config + b"\r\nDEVICE=C:\\IFSHLP.SYS\r\n")
        mcopy(work / "CONFIG.SYS", "::/CONFIG.SYS")
    lines[index + 1:index + 1] = additions
    (work / "SYSTEM.INI").write_bytes(
        ("\r\n".join(lines) + "\r\n").encode("cp1252"))
    mcopy(work / "SYSTEM.INI", "::/WINDOWS/SYSTEM.INI")
    mcopy(repo / "build/BCM57DBG.386", "::/BCM57DBG.386")
    mcopy(repo / "build/WFWEXIT.EXE", "::/WFWEXIT.EXE")
    # OUT F4h exits only the isolated QEMU with isa-debug-exit configured.
    (work / "QEXIT.COM").write_bytes(bytes.fromhex("baf400b000eecd20"))
    mcopy(work / "QEXIT.COM", "::/QEXIT.COM")
    network_start = (b"SET WINDIR=C:\\WINDOWS\r\nC:\\WINDOWS\\NET START\r\n"
                     if args.network_dir else b"")
    (work / "AUTOEXEC.BAT").write_bytes(
        b"@ECHO OFF\r\nPATH C:\\;C:\\WINDOWS\r\n"
        b"DEL C:\\BCMDONE.TXT >NUL\r\n" + network_start +
        b"C:\\WINDOWS\\WIN /b C:\\WFWEXIT.EXE\r\nC:\\QEXIT.COM\r\n")
    mcopy(work / "AUTOEXEC.BAT", "::/AUTOEXEC.BAT")
    command = ["qemu-system-i386", "-machine", "pc", "-accel", "tcg",
               "-cpu", "486", "-m", "32", "-display", "none",
               "-monitor", "none", "-serial", "none",
               "-drive", f"if=ide,index=0,format=raw,file={image}", "-boot", "c",
               "-debugcon", f"file:{work / 'debug.log'}",
               "-global", "isa-debugcon.iobase=0xe9",
               "-device", "isa-debug-exit,iobase=0xf4,iosize=1", "-no-reboot"]
    if args.network_dir:
        command.extend(["-netdev", "user,id=n0,restrict=on", "-device",
                        "ne2k_isa,netdev=n0,iobase=0x300,irq=3"])
    else:
        command.extend(["-nic", "none"])
    with (work / "qemu.log").open("wb") as output:
        try:
            result = subprocess.run(command, stdout=output, stderr=output,
                                    timeout=args.timeout)
        except subprocess.TimeoutExpired:
            parser.exit(1, f"QEMU timed out; inspect {work}\n")
    log = (work / "debug.log").read_text(errors="replace")
    bootlog = subprocess.run(["mtype", "-i", fat, "::/WINDOWS/BOOTLOG.TXT"],
                             capture_output=True)
    if bootlog.returncode == 0:
        (work / "BOOTLOG.TXT").write_bytes(bootlog.stdout)
    print(log, end="")
    marker = subprocess.run(["mtype", "-i", fat, "::/BCMDONE.TXT"],
                            capture_output=True)
    if (result.returncode != 1 or marker.returncode != 0 or
            "BCM57DBG: BCM5722 14e4:165a not found" not in log or
            "BCM57DBG: ResetTest=00000001" not in log):
        parser.exit(1, f"WfW smoke test failed; inspect {work}\n")
    if (args.ndis or args.network_dir) and "raw=00000000" in log:
        parser.exit(1, "NDIS version query failed\n")
    if args.network_dir and ("AddAdapter: BCM5722 not found" not in log or
                            "NdisRegisterMac status=c0000001" not in log):
        parser.exit(1, "Expected NDIS callback and no-adapter failure were not observed\n")
    print("PASS: VxD initialized, absent hardware handled, Win16 ran, DOS returned")


if __name__ == "__main__":
    main()
