"""Jogador 2 headless (so Python 3): recebe os snapshots do host por UDP, segue o host a ~10 m (dentro da coleira),
reenvia sua posicao a 20 Hz e registra uma linha por segundo em --log. Roda como servico leve.
Uso: python3 peer_headless.py --listen 7778 --host IP:7777 --seconds 600 --log /tmp/peer.log"""
import argparse, json, math, socket, time
ap = argparse.ArgumentParser()
ap.add_argument("--listen", type=int, default=7778); ap.add_argument("--host", required=True)
ap.add_argument("--seconds", type=float, default=600); ap.add_argument("--log", default="/tmp/peer.log")
ap.add_argument("--replay", default="", help="modo host gravado: reenvia as linhas deste arquivo (snapshots de um host real) a --hz")
ap.add_argument("--hz", type=float, default=5)
ap.add_argument("--offset", type=float, default=1000, help="cm de distancia do host")
a = ap.parse_args()
h, p = a.host.rsplit(":", 1); dst = (h, int(p))
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("0.0.0.0", a.listen)); s.setblocking(False)
log = open(a.log, "a", buffering=1)
replay = [l.strip() for l in open(a.replay)] if a.replay else []
ri = 0
t0 = last_send = last_log = time.time()
n_in = n_out = n_bad = 0; bytes_in = 0; host = None; npcs = 0
while time.time() - t0 < a.seconds:
    try:
        d, _ = s.recvfrom(65535); n_in += 1; bytes_in += len(d)
        try:
            m = json.loads(d); host = (m["x"], m["y"], m["z"]); npcs = len(m.get("npcs", []))
        except (ValueError, KeyError): n_bad += 1
    except (BlockingIOError, ConnectionResetError):
        time.sleep(0.002)
    now = time.time()
    if replay and now - last_send >= 1 / a.hz:
        last_send = now; s.sendto(replay[ri % len(replay)].encode(), dst); ri += 1; n_out += 1
    elif not replay and now - last_send >= 0.05 and host:
        last_send = now; ang = (now - t0) * 0.3
        msg = {"x": host[0] + a.offset * math.cos(ang), "y": host[1] + a.offset * math.sin(ang), "z": host[2], "npcs": [], "t": now}
        s.sendto(json.dumps(msg).encode(), dst); n_out += 1
    if now - last_log >= 1:
        last_log = now
        log.write(f"{time.strftime('%H:%M:%S')} in={n_in} ({bytes_in//1024} KB) out={n_out} bad={n_bad} host={host and tuple(round(v) for v in host)} npcs={npcs}\n")
log.write("fim\n")
