# Lidl TRONIC Mini Pocket Printer: BLE protocol

Implemented in `lib/MiniPocketPrinter` (see README.md).

Status: work in progress. Items are marked **confirmed** (seen on real hardware) or *hypothesis*.

## Device

| Item | Value |
|---|---|
| BLE name | `Mini Pocket Printer_BLE` (**confirmed**) |
| Advertised service | `e7810a71-73ae-499d-8c15-faa9aef0c3f2` |
| Manufacturer data | company ID `0x5941` ("AY", AiYin), payload `5e 55 xx xx xx xx` (MAC-like) |
| Firmware | `V1.06LY` (self-test page) |
| Negotiated MTU (macOS) | 240 |

## GATT layout (**confirmed**, identical to the Fichero D11s / AiYin module)

| Service | Write | Notify |
|---|---|---|
| `000018f0-…` | `2af1` (write, write-NR) | `2af0` |
| `0000ff00-…` | `ff02` (write, write-NR) | `ff01`, `ff03` |
| `e7810a71-…` | `bef8d6c9-…` (write, write-NR, notify) | same |
| `49535343-fe7d-…` (ISSC) | `…8841…9bb3` | `…1e4d…9616`, `…aca3…` (write+notify) |

## Flow control on `ff03` (**confirmed**)

- On connect: `01 04` (4 write credits), `02 64 00` (payload limit 100 bytes).
- Each write to `ff02` is answered with `01 01` (one credit returned).

## Raster (**confirmed** by printout)

- Print line 384 dots = 48 bytes.
- `1D 76 30 00 wL wH hL hH <data>` (GS v 0), width in bytes, height in rows, little-endian.
- Rows are MSB-first (bit 7 = leftmost dot), 1 = black.
- 1-px markers at x=0 and x=383 both printed.

## Command channel (**confirmed**)

Write to `2af1` (write-without-response), replies arrive as notifications on `2af0`.
Protocol: LuckPrinter "normal" family (same as TiMini-Print `luck_a2`). Model reports itself as `A2Y`.

| Bytes | Command | Reply seen |
|---|---|---|
| `10 FF 20 F0` | model | `A2Y` |
| `10 FF 20 F1` | firmware | `V1.06LY` |
| `10 FF 20 F2` | serial | `P2541XXXXXXX` |
| `10 FF 50 F1` | battery | `00 54` → [status, 84 %] |
| `10 FF 40` | status bitmask | `00` = ready (01 printing, 02 cover open, 04 no paper, 08 low batt, 10/40 overheat, 20 charging) |
| `10 FF 11` | get density | `01 0C 10` |
| `10 FF 70` | all info | `Mini Pocket Printer|55:55:XX:XX:XX:XX|5E:55:XX:XX:XX:XX|V1.06LY|P2541XXXXXXX|82` |
| `10 FF 10 00 nn` | set density | `OK` |
| `10 FF 84 02` | set paper (AiYin) | no reply, probably unsupported |

Note: the BLE MAC is `5E:55:XX:XX:XX:XX`; `55:55:…` (from the QR code) is the Classic MAC.

## Print sequence (**confirmed**, printed immediately)

```
10 FF 10 00 01           set density (optional) -> "OK"
10 FF 40                 status, expect 00
10 FF F1 03              enable (Luck dialect)
00 x 12                  wakeup
1D 76 30 00 30 00 hL hH  GS v 0 raster, 48 bytes/row, then rows
1B 4A 50                 feed 80 dots
10 FF F1 45              finalize (expect "OK" or AA)
```

The AiYin enable/stop pair `10 FF FE 01` / `10 FF FE 45` does **not** print on this model.

If the printer is in an error state (e.g. latch not fully closed) it ignores queries and buffers print data;
it printed the buffered job when the latch was reopened and closed. Check `10 FF 40` before printing.

## Not working / no reply

- Plain ESC/POS text (`1B 40 "Hello" 0A`) on `2af1`, `bef8d6c9`, ISSC write.
- Cat-printer status `51 78 A3 …` on `ff02`.
- NIIMBOT frames `55 55 …` on `bef8d6c9`.
- `1B 64 n` feed on `ff02` (no paper movement, possibly due to the error state).
