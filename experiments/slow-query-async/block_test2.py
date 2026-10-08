import socket, threading, time, sys

HOST, PORT = "127.0.0.1", int(sys.argv[1])
N = int(sys.argv[2]) if len(sys.argv) > 2 else 20
PATH = sys.argv[3] if len(sys.argv) > 3 else "/inventory/use"
BODY = b'{"player_id":1,"item_id":1,"qty":1}'
REQ = (b"POST " + PATH.encode() + b" HTTP/1.1\r\nHost: x\r\nContent-Type: application/json\r\n"
       + b"Content-Length: " + str(len(BODY)).encode() + b"\r\n\r\n" + BODY)

def one(i, results):
    t0 = time.time()
    try:
        s = socket.create_connection((HOST, PORT), timeout=30)
        s.sendall(REQ)
        data = b""
        while b"\r\n\r\n" not in data:
            chunk = s.recv(4096)
            if not chunk: break
            data += chunk
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
    results[i] = time.time() - t0

results = [None]*N
ths = []
for i in range(N):
    t = threading.Thread(target=one, args=(i, results))
    ths.append(t); t.start(); time.sleep(0.01)
for t in ths: t.join()
lat = sorted(r for r in results if r is not None)
if not lat:
    print("no results"); sys.exit(1)
def pct(p): return lat[min(len(lat)-1, int(len(lat)*p))]
print(f"count={len(lat)} min={lat[0]*1000:.1f}ms p50={pct(.5)*1000:.1f}ms p90={pct(.9)*1000:.1f}ms max={lat[-1]*1000:.1f}ms")
