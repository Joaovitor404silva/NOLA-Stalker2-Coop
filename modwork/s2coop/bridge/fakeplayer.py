"""Simula o jogador 2: envia por UDP posicoes em circulo ao redor da posicao do host (le out.json). Uso: python fakeplayer.py --to IP:7777 [--radius 800] [--seconds 60]"""
import argparse, json, math, os, socket, time
d = os.path.dirname(os.path.abspath(__file__))
ap = argparse.ArgumentParser()
ap.add_argument("--to", default="127.0.0.1:7777"); ap.add_argument("--radius", type=float, default=800)
ap.add_argument("--seconds", type=float, default=60)
a = ap.parse_args()
host, port = a.to.rsplit(":", 1)
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
t0 = time.time()
while time.time() - t0 < a.seconds:
    try: c = json.load(open(os.path.join(d, "out.json")))
    except (OSError, ValueError): time.sleep(0.2); continue
    ang = (time.time() - t0) * 0.5
    msg = {"x": c["x"] + a.radius * math.cos(ang), "y": c["y"] + a.radius * math.sin(ang), "z": c["z"], "npcs": []}
    s.sendto(json.dumps(msg).encode(), (host, int(port)))
    time.sleep(0.05)
