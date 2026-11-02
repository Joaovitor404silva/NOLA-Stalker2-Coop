"""Mede RTT e perda UDP ate um peer que ecoa (peer_sim.py). Uso: python rtt.py --to IP:7778 --size 10000 --hz 10 --seconds 20"""
import argparse, socket, struct, time, statistics
ap = argparse.ArgumentParser()
ap.add_argument("--to", required=True); ap.add_argument("--size", type=int, default=10000)
ap.add_argument("--hz", type=float, default=10); ap.add_argument("--seconds", type=float, default=20)
a = ap.parse_args()
h, p = a.to.rsplit(":", 1)
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("0.0.0.0", 7799)); s.setblocking(False)
sent, rtts, t0, seq = {}, [], time.time(), 0
pad = b"x" * max(0, a.size - 12)
while time.time() - t0 < a.seconds + 1:
    if time.time() - t0 < a.seconds and time.time() - t0 >= seq / a.hz:
        sent[seq] = time.perf_counter(); s.sendto(struct.pack("!Id", seq, 0.0) + pad, (h, int(p))); seq += 1
    try:
        d, _ = s.recvfrom(65535); i = struct.unpack("!I", d[:4])[0]
        if i in sent: rtts.append((time.perf_counter() - sent.pop(i)) * 1000)
    except (BlockingIOError, ConnectionResetError): time.sleep(0.0005)
lost = len(sent)
print(f"tamanho={a.size}B hz={a.hz} enviados={seq} recebidos={len(rtts)} perdidos={lost} ({100*lost/max(seq,1):.1f}%)")
if rtts: print(f"RTT ms: min={min(rtts):.2f} med={statistics.median(rtts):.2f} media={statistics.mean(rtts):.2f} p95={sorted(rtts)[int(len(rtts)*0.95)-1]:.2f} max={max(rtts):.2f}")
