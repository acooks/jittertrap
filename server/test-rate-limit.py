#!/usr/bin/env python3
"""Integration test for netem rate-limit support (issue #24).

Exercises the new 'rate' parameter end-to-end:

  - set_netem with rate=N kbit/s applies the qdisc; tc shows the rate.
  - The server's netem_params reply carries rate=N.
  - clear_netem (rate=0, all params 0) restores no-rate state.
  - Old clients sending set_netem without 'rate' don't break the server.

Requires:
  - jt-server with CAP_NET_ADMIN (setcap)
  - passwordless sudo for `ip link` and `tc`
  - python3 with the websockets module

Exit code is 0 on success, non-zero on any failure.
"""

import argparse
import asyncio
import json
import os
import re
import signal
import subprocess
import sys
import time
from contextlib import asynccontextmanager

try:
    import websockets
except ImportError:
    print("Error: websockets module not found. Install with: pip install websockets")
    sys.exit(2)


def sh(cmd, check=False):
    r = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if check and r.returncode != 0:
        raise RuntimeError(f"{cmd!r} failed: {r.stderr}")
    return r


def ensure_iface(name):
    sh(f"sudo -n ip link del {name} 2>/dev/null")
    sh(f"sudo -n ip link add {name} type dummy", check=True)
    sh(f"sudo -n ip link set {name} up", check=True)


def remove_iface(name):
    sh(f"sudo -n ip link del {name} 2>/dev/null")


def tc_rate_bps(iface):
    """Return the netem qdisc 'rate' on iface as bytes/s, or 0 if none."""
    r = sh(f"sudo -n tc qdisc show dev {iface}")
    if r.returncode != 0:
        return 0
    # tc output examples:
    #  "qdisc netem 8001: root refcnt 2 limit 1000 rate 125Kbit"
    #  "qdisc netem 8001: root refcnt 2 limit 1000 rate 1Mbit"
    m = re.search(r"\brate\s+(\d+)([KMG]?)bit\b", r.stdout)
    if not m:
        return 0
    n = int(m.group(1))
    mult = {"": 1, "K": 1000, "M": 1_000_000, "G": 1_000_000_000}[m.group(2)]
    return n * mult // 8  # bit/s -> byte/s


@asynccontextmanager
async def server(port, allowed, binary):
    log_path = f"/tmp/jts-rate-limit-{os.getpid()}.log"
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


class Client:
    """Tracks netem_params and dev_select messages."""

    def __init__(self, ws):
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
                # netem_params is a short message (<128 bytes) so it
                # arrives uncompressed as text/utf-8 JSON.
                if isinstance(raw, bytes):
                    # could be a compressed frame; skip — we don't need it
                    continue
                try:
                    msg = json.loads(raw)
                except Exception:
                    continue
                if msg.get("msg") in ("netem_params", "dev_select", "iface_list"):
                    self.events.append((time.monotonic(), msg["msg"], msg.get("p")))
        except websockets.ConnectionClosed:
            pass

    def start(self):
        self._task = asyncio.create_task(self._drain())

    async def stop(self):
        self._stop.set()
        if self._task:
            await self._task

    async def send(self, msg):
        await self.ws.send(json.dumps(msg))

    def last_netem(self):
        for ts, kind, p in reversed(self.events):
            if kind == "netem_params":
                return p
        return None

    async def wait_netem_with(self, predicate, timeout=2.0):
        """Wait until a netem_params event satisfying predicate arrives, or timeout."""
        deadline = time.monotonic() + timeout
        seen = len(self.events)
        while time.monotonic() < deadline:
            for ts, kind, p in self.events[seen:]:
                if kind == "netem_params" and predicate(p):
                    return p
            seen = len(self.events)
            await asyncio.sleep(0.05)
        return None


@asynccontextmanager
async def connect(port):
    uri = f"ws://localhost:{port}/"
    async with websockets.connect(uri, subprotocols=["jittertrap"], compression=None) as ws:
        c = Client(ws)
        c.start()
        await asyncio.sleep(0.6)  # let initial messages flow
        try:
            yield c
        finally:
            await c.stop()


# ----------------------------------------------------------------------------
# tests

async def test_set_and_verify_rate(port, iface):
    """set_netem with rate=1000 kbit/s applies qdisc; server reports same rate."""
    async with connect(port) as c:
        await c.send({"msg": "dev_select", "p": {"iface": iface}})
        await asyncio.sleep(0.4)

        # Set delay=10ms loss=0 rate=1000kbit
        await c.send({
            "msg": "set_netem",
            "p": {"dev": iface, "delay": 10, "jitter": 0, "loss": 0, "rate": 1000}
        })
        got = await c.wait_netem_with(lambda p: p.get("rate", 0) > 0, timeout=2.0)
        assert got is not None, "server never echoed netem_params with rate>0"
        assert got.get("rate") == 1000, f"server reported rate={got.get('rate')}, expected 1000"
        assert got.get("delay") == 10, f"delay round-trip lost: {got}"

        # Cross-check with tc directly
        bps = tc_rate_bps(iface)
        # 1000 kbit/s = 125000 byte/s
        assert abs(bps - 125000) < 250, \
            f"tc qdisc shows rate={bps} byte/s, expected ~125000"
    return f"set rate=1000kbit verified via tc and netem_params"


async def test_clear_rate(port, iface):
    """Setting rate=0 with non-zero delay leaves netem active but unrate-limited."""
    async with connect(port) as c:
        await c.send({"msg": "dev_select", "p": {"iface": iface}})
        await asyncio.sleep(0.4)

        await c.send({
            "msg": "set_netem",
            "p": {"dev": iface, "delay": 5, "jitter": 0, "loss": 0, "rate": 500}
        })
        await c.wait_netem_with(lambda p: p.get("rate") == 500, timeout=2.0)

        # Now reset rate to 0
        await c.send({
            "msg": "set_netem",
            "p": {"dev": iface, "delay": 5, "jitter": 0, "loss": 0, "rate": 0}
        })
        got = await c.wait_netem_with(lambda p: p.get("rate", -2) == 0, timeout=2.0)
        assert got is not None, "server never echoed netem_params with rate=0"

        bps = tc_rate_bps(iface)
        assert bps == 0, f"tc qdisc still reports rate={bps} after clear"
    return "rate cleared via set_netem rate=0"


async def test_old_client_compat(port, iface):
    """A client that omits 'rate' in set_netem must not crash the server.

    The server-side unpacker treats missing 'rate' as 0 (no rate limit)."""
    async with connect(port) as c:
        await c.send({"msg": "dev_select", "p": {"iface": iface}})
        await asyncio.sleep(0.4)

        # First set a rate, then issue an old-style set_netem without 'rate'
        await c.send({
            "msg": "set_netem",
            "p": {"dev": iface, "delay": 5, "jitter": 0, "loss": 0, "rate": 750}
        })
        await c.wait_netem_with(lambda p: p.get("rate") == 750, timeout=2.0)

        # Old-format message: no 'rate' key
        await c.send({
            "msg": "set_netem",
            "p": {"dev": iface, "delay": 8, "jitter": 0, "loss": 0}
        })
        got = await c.wait_netem_with(lambda p: p.get("delay") == 8, timeout=2.0)
        assert got is not None, "server didn't reply to old-style set_netem"
        # Old-style set replaces the qdisc fresh; the missing rate should mean
        # no rate limit (0).
        assert got.get("rate", 0) == 0, \
            f"old-style set_netem unexpectedly kept rate={got.get('rate')}"
    return "old client (no 'rate' field) accepted by server"


# ----------------------------------------------------------------------------
async def run_all(args):
    iface = "jt-rate-test"
    ensure_iface(iface)
    try:
        async with server(args.port, f"lo:{iface}", args.binary) as (log_path, proc):
            results = []
            try:
                results.append(await test_set_and_verify_rate(args.port, iface))
                results.append(await test_clear_rate(args.port, iface))
                results.append(await test_old_client_compat(args.port, iface))
            except AssertionError as e:
                print(f"FAIL: {e}")
                for r in results:
                    print(f"  ok: {r}")
                print("---- last 60 lines of server log ----")
                with open(log_path) as f:
                    lines = f.readlines()
                sys.stdout.write("".join(lines[-60:]))
                return 1
            except Exception as e:
                print(f"ERROR: {e}")
                return 2

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
            print("All rate-limit integration tests passed.")
            return 0
    finally:
        remove_iface(iface)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default="./jt-server",
                        help="path to jt-server (default ./jt-server)")
    parser.add_argument("--port", type=int, default=8766)
    args = parser.parse_args()
    return asyncio.run(run_all(args))


if __name__ == "__main__":
    sys.exit(main())
