"""Relay UDP entre o mod Lua (via arquivos) e o outro PC. Uso: python relay.py --listen 7777 --peer 127.0.0.1:7777 [--dx 1000]
out.json: posicao local escrita pelo Lua; in.json: posicao remota que o Lua le. --dx desloca X do remoto (cm) para teste em loopback."""
import argparse, json, os, socket, time
d = os.path.dirname(os.path.abspath(__file__))
ap = argparse.ArgumentParser()
ap.add_argument("--listen", type=int, default=7777)
ap.add_argument("--peer", default="127.0.0.1:7777")
ap.add_argument("--dx", type=float, default=0)
ap.add_argument("--dir", default="", help="pasta com out.json/in.json (padrao: a do script)")
ap.add_argument("--record", default="", help="grava cada snapshot enviado (uma linha JSON) neste arquivo")
a = ap.parse_args()
host, port = a.peer.rsplit(":", 1)
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("127.0.0.1" if host.startswith("127.") else "0.0.0.0", a.listen)); s.setblocking(False)
try: s.ioctl(socket.SIO_UDP_CONNRESET, False)  # Windows: ignora "port unreachable" do peer ausente
except (AttributeError, OSError): pass
if a.dir: d = a.dir
out, inn = os.path.join(d, "out.json"), os.path.join(d, "in.json")
last = None
while True:
    try:
        with open(out) as f: txt = f.read().strip()
        if txt and txt != last:
            last = txt; s.sendto(txt.encode(), (host, int(port)))
            if a.record:
                with open(a.record, "a") as rf: rf.write(txt + "\n")
    except OSError: pass
    try:
        while True:
            data, _ = s.recvfrom(65535)
            m = json.loads(data)
            m["x"] += a.dx
            for n in m.get("npcs", []): n["x"] += a.dx
            with open(inn + ".tmp", "w") as f: json.dump(m, f)
            for _ in range(5):
                try: os.replace(inn + ".tmp", inn); break
                except PermissionError: time.sleep(0.01)  # o Lua esta lendo o arquivo
    except (BlockingIOError, ConnectionResetError): pass
    except (ValueError, KeyError): pass
    time.sleep(0.05)
