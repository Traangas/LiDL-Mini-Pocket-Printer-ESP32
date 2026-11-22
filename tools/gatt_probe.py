#!/usr/bin/env python3
"""Phase 1a: scan for the Mini Pocket Printer, dump its GATT table, subscribe to
notifications, and offer an interactive prompt.

Run:  .venv/bin/python tools/gatt_probe.py
Prompt commands:
  l              list characteristics
  r <n>          read characteristic n
  w <n> <hex>    write (no-response if supported) hex bytes to characteristic n
  q              quit
macOS note: CoreBluetooth hides real MACs; devices are identified by a UUID.
"""
import asyncio
import sys

from bleak import BleakClient, BleakScanner

NAME = "Mini Pocket Printer"


def hexdump(b: bytes) -> str:
    return " ".join(f"{x:02x}" for x in b) + "  | " + "".join(chr(x) if 32 <= x < 127 else "." for x in b)


async def find():
    print("Scanning 8 s (printer on, phone app closed)...")
    found = await BleakScanner.discover(timeout=8, return_adv=True)
    cands = []
    for dev, adv in found.values():
        n = dev.name or adv.local_name or ""
        print(f"  {dev.address}  rssi {adv.rssi:4}  '{n}'  svc={adv.service_uuids}  mfr={ {k: v.hex() for k, v in adv.manufacturer_data.items()} }")
        if NAME.lower() in n.lower() or "pocket" in n.lower():
            cands.append(dev)
    return cands


async def main():
    cands = await find()
    if not cands:
        print("\nPrinter not seen over BLE. It may be Bluetooth Classic only (SPP).")
        return
    dev = cands[0]
    print(f"\nConnecting to {dev.name} {dev.address} ...")
    async with BleakClient(dev) as c:
        chars = []
        print(f"Connected. MTU {c.mtu_size}\n")
        for s in c.services:
            print(f"SERVICE {s.uuid}")
            for ch in s.characteristics:
                i = len(chars)
                chars.append(ch)
                print(f"  ({i}) {ch.uuid}  [{', '.join(ch.properties)}]")
                if "read" in ch.properties:
                    try:
                        print("        value:", hexdump(bytes(await c.read_gatt_char(ch))))
                    except Exception as e:
                        print("        read failed:", e)
                if "notify" in ch.properties or "indicate" in ch.properties:
                    def cb(_, data, u=ch.uuid):
                        print(f"\n<< NOTIFY {u}: {hexdump(bytes(data))}\n> ", end="", flush=True)
                    try:
                        await c.start_notify(ch, cb)
                        print("        subscribed")
                    except Exception as e:
                        print("        subscribe failed:", e)
        print("\nCommands: l | r <n> | w <n> <hex> | q")
        loop = asyncio.get_running_loop()
        while True:
            line = (await loop.run_in_executor(None, lambda: input("> "))).strip()
            if not line:
                continue
            line = line.lstrip("> ").strip()
            p = line.split()
            if not p:
                continue
            try:
                if p[0] == "q":
                    break
                if p[0] == "l":
                    for i, ch in enumerate(chars):
                        print(f"  ({i}) {ch.uuid} [{', '.join(ch.properties)}]")
                elif p[0] == "r":
                    print(hexdump(bytes(await c.read_gatt_char(chars[int(p[1])]))))
                elif p[0] == "w":
                    ch = chars[int(p[1])]
                    hexs = "".join(p[2:]).replace("0x", "")
                    print(f"   parsed hex: {hexs!r}")
                    data = bytes.fromhex(hexs)
                    resp = "write" in ch.properties
                    await c.write_gatt_char(ch, data, response=resp)
                    print(f">> wrote {len(data)} bytes ({'with' if resp else 'no'} response)")
                else:
                    print("?")
            except Exception as e:
                print("error:", e)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except (KeyboardInterrupt, EOFError):
        sys.exit(0)
