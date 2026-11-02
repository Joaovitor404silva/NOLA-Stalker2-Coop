"""Segundo jogador simulado em OUTRA maquina (so precisa de Python 3, sem o jogo).
Escuta UDP, ecoa cada pacote ao remetente (mede RTT/perda pelo lado do host) e, se --send for dado,
envia posicoes em circulo (como o fakeplayer.py) para o host.
Uso: python3 peer_sim.py --listen 7778 --host IP_DO_HOST:7777 --send"""
import argparse, json, math, socket, time

ap = argparse.ArgumentParser()
ap.add_argument("--listen", type=int, default=7778)
ap.add_argument("--host", default="")
ap.add_argument("--send", action="store_true")
ap.add_argument("--seconds", type=float, default=60)
a = ap.parse_args()
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("0.0.0.0", a.listen))
s.setblocking(False)
t0, n_in, last_send = time.time(), 0, 0
while time.time() - t0 < a.seconds:
    try:
        data, addr = s.recvfrom(65535)
        n_in += 1
        s.sendto(data, addr)  # eco para medir RTT no host
    except BlockingIOError:
        time.sleep(0.002)
    if a.send and a.host and time.time() - last_send > 0.05:
        last_send = time.time()
        h, p = a.host.rsplit(":", 1)
        ang = (time.time() - t0) * 0.5
        msg = {"x": 400000 + 800 * math.cos(ang), "y": 550000 + 800 * math.sin(ang), "z": 500, "npcs": []}
        s.sendto(json.dumps(msg).encode(), (h, int(p)))
print("recebidos:", n_in)
