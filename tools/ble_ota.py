#!/usr/bin/env python3
"""BLE firmware-OTA client for SmartLada Rev C (device side: src/net/bleota.cpp).

Streams a compiled .bin into the ESP32-C6's inactive OTA slot over BLE GATT, while the
device stays a live Zigbee end device (C6 BLE/802.15.4 coexistence). On success the device
sets the new slot bootable and reboots.

Usage:
    python3 tools/ble_ota.py <firmware.bin> [--name SmartLada] [--token 0x5A5AA5A5]

Needs: bleak  (uv pip install bleak). macOS requires the host app to hold Bluetooth
permission (Privacy & Security -> Bluetooth).
"""
import asyncio, struct, sys, time, argparse
from bleak import BleakScanner, BleakClient

SVC  = "5ada0a70-0000-4a5a-b1ed-5a5aa5a50001"
CTRL = "5ada0a70-0001-4a5a-b1ed-5a5aa5a50001"
DATA = "5ada0a70-0002-4a5a-b1ed-5a5aa5a50001"
STAT = "5ada0a70-0003-4a5a-b1ed-5a5aa5a50001"
STATE = {0: "idle", 1: "receiving", 2: "done", 3: "error"}


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bin")
    ap.add_argument("--name", default="SmartLada")
    ap.add_argument("--token", default="0x5A5AA5A5")
    ap.add_argument("--scan", type=float, default=10.0)
    ap.add_argument("--drop-after-finish", action="store_true",
                    help="disconnect immediately after FINISH instead of waiting for the "
                         "status notify. Tests that a link loss during the commit does NOT "
                         "cancel the update: the device should still boot the new image.")
    a = ap.parse_args()
    token = int(a.token, 0)

    with open(a.bin, "rb") as f:
        fw = f.read()
    print(f"firmware: {a.bin}  {len(fw)} bytes")

    print(f"scanning for '{a.name}' ...")
    dev = await BleakScanner.find_device_by_filter(
        lambda d, adv: (adv.local_name == a.name) or (SVC in (adv.service_uuids or [])),
        timeout=a.scan,
    )
    if not dev:
        print("device not found"); sys.exit(1)
    print(f"found {dev.address}, connecting ...")

    done = asyncio.Event()
    result = {"state": None, "acked": 0}

    def on_status(_h, data: bytearray):
        st = data[0] if data else 255
        recv = struct.unpack("<I", data[1:5])[0] if len(data) >= 5 else 0
        result["state"] = st
        if st == 1:
            result["acked"] = recv          # device confirms it wrote up to `recv` bytes
        else:
            print(f"  status: {STATE.get(st, st)}  recv={recv}")
        if st in (2, 3):
            done.set()

    WINDOW = 16384   # bytes the sender may run ahead of the device's acked count

    async with BleakClient(dev) as cli:
        mtu = cli.mtu_size
        chunk = max(20, mtu - 3)
        print(f"connected, mtu={mtu}, chunk={chunk}")
        await cli.start_notify(STAT, on_status)

        # START
        await cli.write_gatt_char(CTRL, struct.pack("<BII", 0x01, token, len(fw)), response=True)
        await asyncio.sleep(0.3)
        if result["state"] == 3:
            print("device rejected START (token?)"); sys.exit(1)

        # DATA stream: write-without-response, paced by a credit window (device acks via notify).
        t0 = time.time(); sent = 0; last_progress = time.time()
        for i in range(0, len(fw), chunk):
            while sent - result["acked"] >= WINDOW:          # wait for the device to catch up
                await asyncio.sleep(0.003)
                if result["state"] == 3:
                    print("device reported error mid-stream"); sys.exit(1)
                if time.time() - last_progress > 12:
                    print("\nstalled (no ack for 12s)"); sys.exit(1)
            await cli.write_gatt_char(DATA, fw[i:i + chunk], response=False)
            sent += min(chunk, len(fw) - i)
            last_progress = time.time()
            if (i // chunk) % 64 == 0:
                pct = 100 * sent // len(fw)
                rate = sent / max(0.001, time.time() - t0) / 1024
                print(f"  sent {sent}/{len(fw)} ({pct}%)  ack={result['acked']}  {rate:.1f} KB/s", end="\r", flush=True)
        dt = time.time() - t0
        print(f"\nsent {sent} bytes in {dt:.1f}s ({sent/max(0.001,dt)/1024:.1f} KB/s)")

        # FINISH
        await cli.write_gatt_char(CTRL, struct.pack("<B", 0x02), response=True)
        if a.drop_after_finish:
            # The host normally holds the link until "done". Real hosts often just go away once
            # every byte is sent, so the device must treat a disconnect while FINISH is pending
            # as "commit anyway" -- only an explicit ABORT cancels.
            print("dropping the link right after FINISH (device should still commit)")
            await cli.disconnect()
            print("disconnected; watch the board: it should reboot into the new firmware")
            sys.exit(0)
        try:
            await asyncio.wait_for(done.wait(), timeout=15.0)
        except asyncio.TimeoutError:
            print("no final status (device may have rebooted already)")
        ok = result["state"] == 2
        print("OTA", "OK -> device rebooting into new firmware" if ok else "FAILED")
        sys.exit(0 if ok else 1)


if __name__ == "__main__":
    asyncio.run(main())
