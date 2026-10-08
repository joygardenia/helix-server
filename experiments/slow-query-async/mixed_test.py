import socket, threading, time, sys

HOST, PORT = "127.0.0.1", int(sys.argv[1])
LOCKN = int(sys.argv[2]) if len(sys.argv) > 2 else 20
OBSN  = int(sys.argv[3]) if len(sys.argv) > 3 else 40

def req(path, body=b"", method=b"GET"):
    if body:
        return (method + b" " + path.encode() + b" HTTP/1.1\r\nHost: x\r\nContent-Type: application/json\r\n"
                + b"Content-Length: " + str(len(body)).encode() + b"\r\n\r\n" + body)
    return method + b" " + path.encode() + b" HTTP/1.1\r\nHost: x\r\n\r\n"

def one(reqbytes, results, idx, timeout=30):
    t0 = time.time()
    try:
        s = socket.create_connection((HOST, PORT), timeout=timeout)
        s.sendall(reqbytes)
        data = b""
        while b"\r\n\r\n" not in data:
            c = s.recv(4096)
            if not c: break
            data += c
        hdr, _, body = data.partition(b"\r\n\r\n")
        cl = 0
        for line in hdr.split(b"\r\n"):
            if line.lower().startswith(b"content-length:"):
                cl = int(line.split(b":")[1])
        while len(body) < cl:
            body += s.recv(4096)
        s.close()
    except Exception:
        pass
    results[idx] = time.time() - t0

# start lock-waiting flood (all hit /inventory/use, same row)
lock_req = req("/inventory/use", b'{"player_id":1,"item_id":1,"qty":1}', b"POST")
obs_req  = req("/players/1")
lock_res = [None]*LOCKN
obs_res  = [None]*OBSN
ths = []
for i in range(LOCKN):
    t = threading.Thread(target=one, args=(lock_req, lock_res, i)); ths.append(t); t.start(); time.sleep(0.005)
time.sleep(0.05)
for i in range(OBSN):
    t = threading.Thread(target=one, args=(obs_req, obs_res, i)); ths.append(t); t.start(); time.sleep(0.005)
for t in ths: t.join()

def stats(name, res):
    lat = sorted(r for r in res if r is not None)
    if not lat:
        print(name, "no results"); return
    def pct(p): return lat[min(len(lat)-1, int(len(lat)*p))]
    print(f"{name}: count={len(lat)} min={lat[0]*1000:.1f}ms p50={pct(.5)*1000:.1f}ms p90={pct(.9)*1000:.1f}ms max={lat[-1]*1000:.1f}ms")

stats("  observers(/players/1)", obs_res)
stats("  lockers(/inventory/use)", lock_res)
