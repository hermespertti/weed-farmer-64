#!/usr/bin/env python3
"""Tiny BlenderMCP socket client: framed JSON (4-byte length header)."""
import socket, struct, json, sys

def bpy_call(cmd, timeout=120):
    import time
    last = None
    for _ in range(20):
        try:
            s = socket.create_connection(("127.0.0.1", 9876), timeout=timeout)
            break
        except OSError as e:
            last = e
            time.sleep(15)
    else:
        raise last
    body = json.dumps(cmd).encode()
    s.sendall(struct.pack(">I", len(body)) + body)
    hdr = b""
    while len(hdr) < 4:
        c = s.recv(4 - len(hdr))
        if not c: raise ConnectionError("closed")
        hdr += c
    (n,) = struct.unpack(">I", hdr)
    buf = b""
    while len(buf) < n:
        c = s.recv(n - len(buf))
        if not c: raise ConnectionError("closed mid-body")
        buf += c
    s.close()
    return json.loads(buf.decode())

def run(code, timeout=120):
    return bpy_call({"type": "execute_code",
                     "params": {"code": code}}, timeout=timeout)

if __name__ == "__main__":
    print(json.dumps(run(sys.stdin.read()), indent=1)[:2000])
