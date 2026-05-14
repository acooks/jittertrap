#!/usr/bin/env python3
"""Integration test for dynamic interface list (issue #51).

Exercises the netlink monitor + auto-promote paths:

  - Non-active iface add/remove: every connected client sees the refreshed
    iface_list.
  - Active iface removal: server auto-promotes to another iface; client
    receives iface_list (without the removed iface) FIRST, then dev_select
    naming the new iface.
  - Multi-client coherence: all clients see the same iface_list contents.
  - Stress: many add/remove cycles complete without server crash / hang.

Requires:
  - jt-server with CAP_NET_RAW etc set (setcap)
  - passwordless sudo for `ip link` ops
  - python3 with the websockets module

Exit code is 0 on success, non-zero on any failure.
"""

import argparse
import asyncio
import json
import os
import signal
import subprocess
import sys
import time
import zlib
from contextlib import asynccontextmanager

try:
    import websockets
except ImportError:
    print("Error: websockets module not found. Install with: pip install websockets")
    sys.exit(2)

WS_COMPRESS_HEADER = 0x01

# Subset of the preset dictionary needed to decode the messages we care about.
# Mirrors server/ws_compress.c.
WS_DICTIONARY = (
    b'192.168.10.0.172.16.fe80:::ffff:'
    b'ICMPUDPTCPBULKBECS0CS1'
    b'pcap_readypcap_statuspcap_configpcap_triggersample_period'
    b'netem_paramsdev_selectiface_list'
)


def decompress(data: bytes) -> str | None:
    """Decompress server frame if it carries the WS_COMPRESS_HEADER."""
    if not data:
        return None
    if data[:1] == bytes([WS_COMPRESS_HEADER]):
        d = zlib.decompressobj(zdict=WS_DICTIONARY)
        try:
            return (d.decompress(data[1:]) + d.flush()).decode()
        except Exception:
            return None
    try:
        return data.decode()
    except Exception:
        return None


def parse(raw) -> dict | None:
    if isinstance(raw, bytes):
        s = decompress(raw)
    else:
        s = raw
    if not s:
        return None
    try:
        return json.loads(s)
    except Exception:
        return None


class Client:
    """Wraps a WS connection and accumulates the iface_list / dev_select
    messages it observes, in arrival order."""

    def __init__(self, name, ws):
        self.name = name
        self.ws = ws
        self.events: list[tuple[float, str, dict]] = []
        self._stop = asyncio.Event()
        self._task: asyncio.Task | None = None

    async def _drain(self):
        try:
            while not self._stop.is_set():
                try:
                    raw = await asyncio.wait_for(self.ws.recv(), timeout=0.2)
                except asyncio.TimeoutError:
                    continue
                msg = parse(raw)
                if not msg:
                    continue
                if msg.get("msg") in ("iface_list", "dev_select"):
                    self.events.append((time.monotonic(), msg["msg"], msg.get("p")))
        except websockets.ConnectionClosed:
            pass

    def start(self):
        self._task = asyncio.create_task(self._drain())

    async def stop(self):
        self._stop.set()
        if self._task:
            await self._task

    async def send_dev_select(self, iface):
        await self.ws.send(json.dumps({"msg": "dev_select", "p": {"iface": iface}}))

    def last_iface_list(self):
        for ts, kind, p in reversed(self.events):
            if kind == "iface_list":
                return p["ifaces"]
        return None

    def last_dev_select(self):
        for ts, kind, p in reversed(self.events):
            if kind == "dev_select":
                return p["iface"]
        return None

    def iface_list_appearances_of(self, iface):
        return [(ts, p["ifaces"]) for ts, kind, p in self.events
                if kind == "iface_list" and iface in p["ifaces"]]

    def first_event_after(self, since_ts, kind):
        for ts, k, p in self.events:
            if ts >= since_ts and k == kind:
                return ts, p
        return None


def sh(cmd, check=False):
    """Run a shell command; capture output."""
    r = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if check and r.returncode != 0:
        raise RuntimeError(f"{cmd!r} failed: {r.stderr}")
    return r


def ensure_iface(name):
    sh(f"sudo -n ip link del {name} 2>/dev/null")
    sh(f"sudo -n ip link add {name} type dummy", check=True)
    sh(f"sudo -n ip link set {name} up")


def remove_iface(name):
    sh(f"sudo -n ip link del {name}")


@asynccontextmanager
async def server(port, allowed, binary):
    """Start jt-server, yield log path, kill on exit."""
    log_path = f"/tmp/jts-iface-recovery-{os.getpid()}.log"
    if os.path.exists(log_path):
        os.unlink(log_path)
    env = os.environ.copy()
    env.setdefault("ASAN_OPTIONS", "detect_leaks=1:halt_on_error=0:exitcode=23")
    proc = subprocess.Popen(
        [binary, "-p", str(port), "-a", allowed],
        stdout=open(log_path, "w"),
        stderr=subprocess.STDOUT,
        env=env,
        preexec_fn=os.setpgrp,
    )
    try:
        for _ in range(50):
            if proc.poll() is not None:
                with open(log_path) as f:
                    sys.stderr.write(f.read())
                raise RuntimeError("server exited during startup")
            r = sh(f"ss -ltn 'sport = :{port}' 2>/dev/null | grep -q :{port}")
            if r.returncode == 0:
                break
            await asyncio.sleep(0.05)
        else:
            raise RuntimeError(f"server not listening on {port} after 2.5s")
        yield log_path, proc
    finally:
        try:
            os.killpg(proc.pid, signal.SIGTERM)
            proc.wait(timeout=3)
        except Exception:
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except Exception:
                pass


@asynccontextmanager
async def connect_client(port, name):
    uri = f"ws://localhost:{port}/"
    async with websockets.connect(uri, subprotocols=["jittertrap"], compression=None) as ws:
        c = Client(name, ws)
        c.start()
        # let initial messages flow
        await asyncio.sleep(0.6)
        try:
            yield c
        finally:
            await c.stop()


# ----------------------------------------------------------------------------
# tests

async def test_non_active_add_remove(port):
    """Adding/removing a non-selected iface updates iface_list for all clients."""
    iface = "jt-iar-add"
    sh(f"sudo -n ip link del {iface} 2>/dev/null")
    async with connect_client(port, "c1") as c1, connect_client(port, "c2") as c2:
        before1 = c1.last_iface_list()
        assert before1 is not None, "client 1 never received initial iface_list"
        assert iface not in before1

        sh(f"sudo -n ip link add {iface} type dummy", check=True)
        await asyncio.sleep(1.0)  # debounce window + propagation

        after1 = c1.last_iface_list()
        after2 = c2.last_iface_list()
        assert after1 and iface in after1, f"client 1 expected {iface!r} in {after1}"
        assert after2 and iface in after2, f"client 2 expected {iface!r} in {after2}"

        sh(f"sudo -n ip link del {iface}")
        await asyncio.sleep(1.0)

        gone1 = c1.last_iface_list()
        gone2 = c2.last_iface_list()
        assert iface not in gone1, f"client 1 still saw {iface!r} in {gone1}"
        assert iface not in gone2, f"client 2 still saw {iface!r} in {gone2}"
    return "non-active add/remove: ok"


async def test_active_removal_autopromote(port):
    """Removing the active iface auto-promotes and emits iface_list BEFORE dev_select."""
    iface = "jt-iar-act"
    ensure_iface(iface)
    try:
        async with connect_client(port, "c1") as c1:
            await c1.send_dev_select(iface)
            await asyncio.sleep(0.7)
            assert c1.last_dev_select() == iface, \
                f"switch to {iface} not confirmed; saw {c1.last_dev_select()}"

            t_del = time.monotonic()
            sh(f"sudo -n ip link del {iface}")
            await asyncio.sleep(2.0)

            # iface_list without the removed iface must arrive
            il = next((ts for ts, kind, p in c1.events
                       if ts >= t_del and kind == "iface_list" and iface not in p["ifaces"]),
                      None)
            assert il is not None, f"no iface_list without {iface} arrived after delete"

            # dev_select to a new iface must arrive
            ds = next((ts for ts, kind, p in c1.events
                       if ts >= t_del and kind == "dev_select" and p["iface"] != iface),
                      None)
            assert ds is not None, f"no auto-promote dev_select arrived after delete"

            # ordering: iface_list must come before dev_select
            assert il < ds, \
                f"ordering violation: dev_select at {ds:.3f} preceded iface_list at {il:.3f}"
    finally:
        sh(f"sudo -n ip link del {iface} 2>/dev/null")
    return "active removal + auto-promote ordering: ok"


async def test_multi_client_coherence(port):
    """Three clients see identical iface_list contents after a change."""
    iface = "jt-iar-mc"
    sh(f"sudo -n ip link del {iface} 2>/dev/null")
    async with connect_client(port, "c1") as c1, \
               connect_client(port, "c2") as c2, \
               connect_client(port, "c3") as c3:
        sh(f"sudo -n ip link add {iface} type dummy", check=True)
        await asyncio.sleep(1.0)

        lists = [sorted(c.last_iface_list() or []) for c in (c1, c2, c3)]
        assert lists[0] == lists[1] == lists[2], \
            f"clients disagree: {lists}"
        assert iface in lists[0], f"{iface!r} missing: {lists[0]}"

        sh(f"sudo -n ip link del {iface}")
        await asyncio.sleep(1.0)

        lists = [sorted(c.last_iface_list() or []) for c in (c1, c2, c3)]
        assert lists[0] == lists[1] == lists[2], \
            f"clients disagree after delete: {lists}"
        assert iface not in lists[0], f"{iface!r} still listed: {lists[0]}"
    return "multi-client coherence: ok"


async def test_stress_flap(port, cycles):
    """N rapid add/remove cycles. Asserts server still responsive at the end."""
    iface = "jt-iar-flap"
    sh(f"sudo -n ip link del {iface} 2>/dev/null")
    for _ in range(cycles):
        sh(f"sudo -n ip link add {iface} type dummy")
        sh(f"sudo -n ip link del {iface}")
    await asyncio.sleep(1.0)
    async with connect_client(port, "post-flap") as c:
        il = c.last_iface_list()
        assert il is not None, "server stopped emitting iface_list after stress"
        assert iface not in il, f"{iface!r} leaked into post-flap list: {il}"
    return f"stress flap x{cycles}: ok"


async def test_stress_active_flap(port, cycles):
    """Repeatedly select then remove an active iface. Catches thread-cancel
    leaks in tt_thread_restart / pcap teardown."""
    iface = "jt-iar-aflap"
    async with connect_client(port, "active-flap") as c:
        for i in range(cycles):
            ensure_iface(iface)
            await asyncio.sleep(0.05)
            await c.send_dev_select(iface)
            await asyncio.sleep(0.4)  # let toptalk start its pcap on the iface
            sh(f"sudo -n ip link del {iface}")
            await asyncio.sleep(0.8)  # debounce + auto-promote + tt restart
        ds = c.last_dev_select()
        assert ds and ds != iface, \
            f"after {cycles} cycles, dev_select stuck on {ds!r}"
    return f"active-flap x{cycles}: ok"


# ----------------------------------------------------------------------------

async def run_all(args):
    if sh("sudo -n true").returncode != 0:
        print("FAIL: passwordless sudo required (for `ip link` ops)")
        return 2

    async with server(args.port, args.allowed, args.binary) as (log_path, proc):
        results = []
        try:
            results.append(await test_non_active_add_remove(args.port))
            results.append(await test_active_removal_autopromote(args.port))
            results.append(await test_multi_client_coherence(args.port))
            results.append(await test_stress_flap(args.port, args.cycles))
            results.append(await test_stress_active_flap(args.port, args.active_cycles))
        except AssertionError as e:
            print(f"FAIL: {e}")
            for r in results:
                print(f"  ok: {r}")
            print("---- last 80 lines of server log ----")
            with open(log_path) as f:
                lines = f.readlines()
            sys.stdout.write("".join(lines[-80:]))
            return 1
        except Exception as e:
            print(f"ERROR: {e}")
            return 2

        # Look for ASAN errors / unexpected fatal warnings in the server log.
        with open(log_path) as f:
            log_text = f.read()
        for marker in ("ERROR: AddressSanitizer", "==ERROR==", "SUMMARY: AddressSanitizer"):
            if marker in log_text:
                print(f"FAIL: server log contains {marker!r}")
                for r in results:
                    print(f"  ok: {r}")
                idx = log_text.find(marker)
                print(log_text[max(0, idx - 200):idx + 2000])
                return 1

        for r in results:
            print(f"ok: {r}")
        print("All iface-recovery integration tests passed.")
        return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default="./jt-server",
                        help="path to jt-server (default ./jt-server)")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--allowed", default="lo:jt-iar-add:jt-iar-act:jt-iar-mc:jt-iar-flap:jt-iar-aflap")
    parser.add_argument("--cycles", type=int, default=20,
                        help="stress flap cycles")
    parser.add_argument("--active-cycles", type=int, default=5,
                        help="active-iface flap cycles (slower: tt thread restart)")
    args = parser.parse_args()
    return asyncio.run(run_all(args))


if __name__ == "__main__":
    sys.exit(main())
