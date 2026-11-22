#!/usr/bin/env python3
"""Test the LuckPrinter / AiYin "normal" protocol on the Mini Pocket Printer.

Step 1: send the status query 10 FF 40 to every writable characteristic and
        log which one produces a reply.
Step 2: on the first characteristic that replied (or --char UUID-prefix),
        run the full print sequence with a 384-dot test pattern:
          10 FF 40              status
          10 FF F1 03           enable
          00 x 12               wakeup
          1D 76 30 00 wL wH hL hH <rows>   GS v 0 raster, MSB-first, 1 = black
          1B 4A 50              feed 80 dots
          10 FF F1 45           finalize (expects "OK" or 0xAA)

Run:  .venv/bin/python tools/luck_test.py            (probe + print)
      .venv/bin/python tools/luck_test.py --probe-only
      .venv/bin/python tools/luck_test.py --char ff02
"""
import argparse
import asyncio
import time

from bleak import BleakClient, BleakScanner

WIDTH = 384  # dots, 48 bytes per row (assumption to confirm)
CHUNK = 100  # printer announced 02 64 00 -> 100-byte payload limit

STATUS = bytes.fromhex("10ff40")
WAKEUP = bytes(12)
FEED = bytes.fromhex("1b4a50")
# AiYin dialect (Fichero D11s) vs generic Luck/Lujiang dialect
DIALECTS = {
    "aiyin": (bytes.fromhex("10fffe01"), bytes.fromhex("10fffe45")),
    "luck": (bytes.fromhex("10fff103"), bytes.fromhex("10fff145")),
}
INFO_QUERIES = [
    ("model", "10ff20f0"), ("firmware", "10ff20f1"), ("serial", "10ff20f2"),
    ("battery", "10ff50f1"), ("status", "10ff40"), ("density", "10ff11"), ("all info", "10ff70"),
]
DENSITY = bytes.fromhex("10ff100001")      # medium
PAPER_CONTINUOUS = bytes.fromhex("10ff8402")

log_t0 = time.monotonic()
credits = 0
credit_event = asyncio.Event()


def hexs(b: bytes) -> str:
    return " ".join(f"{x:02x}" for x in b)


def log(msg: str) -> None:
    print(f"[{time.monotonic() - log_t0:6.2f}] {msg}", flush=True)


def test_pattern() -> tuple[bytes, int]:
    """Rows: 8 solid black, 8 with 1-px markers at x=0 and x=383,
    16 checkerboard (8x8 squares), 8 left-half black."""
    wb = WIDTH // 8
    rows = []
    for _ in range(8):
        rows.append(b"\xff" * wb)
    for _ in range(8):
        r = bytearray(wb)
        r[0] = 0x80        # x = 0 (MSB-first)
        r[-1] = 0x01       # x = 383
        rows.append(bytes(r))
    for y in range(16):
        rows.append(bytes(0xFF if ((x + y // 8) % 2 == 0) else 0x00 for x in range(wb)))
    for _ in range(8):
        rows.append(b"\xff" * (wb // 2) + b"\x00" * (wb - wb // 2))
    return b"".join(rows), len(rows)


async def main():
    global credits
    ap = argparse.ArgumentParser()
    ap.add_argument("--probe-only", action="store_true")
    ap.add_argument("--char", default="2af1", help="UUID prefix of characteristic to print on (default 2af1)")
    ap.add_argument("--dialect", default="aiyin", choices=DIALECTS)
    ap.add_argument("--skip-probe", action="store_true", help="skip the per-characteristic status probe")
    args = ap.parse_args()
    ENABLE, FINALIZE = DIALECTS[args.dialect]

    log("Scanning for the printer...")
    dev = await BleakScanner.find_device_by_filter(
        lambda d, a: "pocket" in ((d.name or a.local_name or "").lower()), timeout=10)
    if not dev:
        log("Printer not found. Is it on, and is the phone app closed?")
        return
    log(f"Connecting to {dev.name} ...")

    async with BleakClient(dev) as c:
        replies: dict[str, list[bytes]] = {}

        def make_cb(uuid):
            def cb(_, data):
                global credits
                data = bytes(data)
                replies.setdefault(uuid, []).append(data)
                tag = uuid[4:8]
                # Phomemo-style credit records on ff03
                if tag == "ff03" and len(data) == 2 and data[0] == 0x01:
                    credits += data[1]
                    credit_event.set()
                    return  # don't spam the log with credits
                log(f"<< {tag} {hexs(data)}  |{''.join(chr(x) if 32 <= x < 127 else '.' for x in data)}|")
            return cb

        writables = []
        for s in c.services:
            for ch in s.characteristics:
                if "notify" in ch.properties or "indicate" in ch.properties:
                    try:
                        await c.start_notify(ch, make_cb(ch.uuid))
                    except Exception as e:
                        log(f"subscribe {ch.uuid} failed: {e}")
                if "write" in ch.properties or "write-without-response" in ch.properties:
                    writables.append(ch)
        await asyncio.sleep(1.0)
        log(f"Initial credits: {credits}")

        async def write(ch, data: bytes, label: str):
            global credits
            use_credits = ch.uuid[4:8] == "ff02"
            for i in range(0, len(data), CHUNK):
                part = data[i:i + CHUNK]
                if use_credits:
                    t = time.monotonic()
                    while credits <= 0 and time.monotonic() - t < 2.0:
                        credit_event.clear()
                        try:
                            await asyncio.wait_for(credit_event.wait(), 0.5)
                        except asyncio.TimeoutError:
                            pass
                    credits -= 1
                nr = "write-without-response" in ch.properties
                await c.write_gatt_char(ch, part, response=not nr)
                await asyncio.sleep(0.02)
            log(f">> {ch.uuid[4:8]} {label} ({len(data)} bytes)")

        # Step 1: status query on each writable characteristic
        responder = None
        for ch in ([] if args.skip_probe else writables):
            before = {k: len(v) for k, v in replies.items()}
            await write(ch, STATUS, "status 10 ff 40")
            await asyncio.sleep(1.5)
            new = {k: v[before.get(k, 0):] for k, v in replies.items()
                   if len(v) > before.get(k, 0)}
            # ignore pure credit records
            real = {k: [d for d in v if not (k[4:8] == "ff03" and len(d) == 2 and d[0] == 1)]
                    for k, v in new.items()}
            real = {k: v for k, v in real.items() if v}
            if real:
                log(f"   reply to status on {ch.uuid[4:8]}: {[(k[4:8], hexs(d)) for k, v in real.items() for d in v]}")
                responder = responder or ch
            else:
                log(f"   no reply on {ch.uuid[4:8]}")

        target = next((ch for ch in writables if ch.uuid[4:8].startswith(args.char.lower())
                       or ch.uuid.startswith(args.char.lower())), None) or responder
        if not target:
            log(f"No characteristic matching {args.char}")
            return

        # Step 1b: AiYin info queries on the target characteristic
        log(f"Info queries on {target.uuid[4:8]}:")
        for name, hx in INFO_QUERIES:
            await write(target, bytes.fromhex(hx), f"{name} {hx}")
            await asyncio.sleep(0.8)
        if args.probe_only:
            return

        # Step 2: full print sequence (Fichero order: density, paper, wakeup, enable, raster, feed, stop)
        rows, h = test_pattern()
        raster = bytes.fromhex("1d763000") + (WIDTH // 8).to_bytes(2, "little") + h.to_bytes(2, "little") + rows
        log(f"Printing test pattern on {target.uuid} with {args.dialect} dialect ...")
        await write(target, DENSITY, "density 10 ff 10 00 01")
        await asyncio.sleep(0.5)
        await write(target, PAPER_CONTINUOUS, "paper continuous 10 ff 84 02")
        await asyncio.sleep(0.5)
        await write(target, WAKEUP, "wakeup 12x00")
        await asyncio.sleep(0.2)
        await write(target, ENABLE, f"enable {hexs(ENABLE)}")
        await asyncio.sleep(0.2)
        await write(target, raster, f"raster {WIDTH}x{h}")
        await write(target, FEED, "feed 1b 4a 50")
        await write(target, FINALIZE, f"finalize {hexs(FINALIZE)}")
        log("Waiting up to 15 s for finalize reply (OK / aa)...")
        await asyncio.sleep(15)
        log("Done.")


if __name__ == "__main__":
    asyncio.run(main())
