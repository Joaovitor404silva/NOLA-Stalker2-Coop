"""fakeclient.py: scripted fake CLIENT for host-half tests, no second game.
It answers the host's own snapshot: position shifted by --dx cm, role in the #H handshake = client with one launch id for the whole run,
host-only fields removed, client messages appended.
  --script "6:#K:Box@1,2,3||6:#T:key||4:"   = each step: <seconds>:<x1 text sent during those seconds> (empty text = plain snapshots)
Local mode (stop the loopback relay first: relay_ctl.sh stop): reads <bridge>/out.json, rewrites <bridge>/in.json.
  uv run --no-project python -I fakeclient.py --dir <bridge> --script "..." [--dx 300]
Network mode, from ANOTHER machine (plain Python 3; fakehost.py must be next to this file, the transport lives there):
  other machine:  python3 fakeclient.py --net --peer <game PC ip>:7779 --listen 7778 --script "..."
  game PC:        uv run --no-project python -I D:/modding/offline/b3/relay_v2.py --listen 7779 --peer <other machine ip>:7778 --dir <bridge>
Both modes: --loss 5 --delay 80 [--jitter 40], or --profile internet-good|internet|internet-bad; --key <text> with a relay that has one."""
import argparse, json, os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))     # "python -I" leaves the script's folder out of the path
from fakehost import Transport, add_transport_args, check_transport_args
ap = argparse.ArgumentParser(); add_transport_args(ap); ap.add_argument("--script", default="20:")
ap.add_argument("--dx", type=float, default=300); ap.add_argument("--sep", default="||", help="step separator (default ||, so x1 text may contain ;)")
a = ap.parse_args(); check_transport_args(ap, a)
tr = Transport(a)
nonce = "fake-%d" % int(time.time()); n = 0
for step in a.script.split(a.sep):
    if not step: continue
    secs, _, x1add = step.partition(":"); t0 = time.time()
    print("step %ss x1=%r" % (secs, x1add), flush=True)
    while time.time() - t0 < float(secs):
        now = time.time(); body = None
        try:
            m = json.loads(tr.recv(now) or "")
            m["x"] = m.get("x", 0) + a.dx
            x1 = m.get("x1", "")
            x1 = re.sub(r"#H:host,([^,]*),([^,]*),([^,#]*),[^#]*", lambda g: "#H:client,%s,%s,%s,%s" % (g.group(1), g.group(2), g.group(3), nonce), x1)
            x1 = re.sub(r"#(X|C|R|J|S|W|AL|E|V|B|N|K|T|U|Q|L):[^#]*", "", x1)      # drop the host's own fields; the script supplies the client's
            m["x1"] = x1 + ("#L:a,100,100" if "#L:" not in x1add else "") + x1add
            m["sid"] = nonce; m.pop("npcs", None); m.pop("apps", None); m.pop("ids", None)
            body = json.dumps(m, separators=(",", ":"))
        except ValueError: pass                          # no snapshot of the host yet
        if tr.send(now, body): n += 1
        time.sleep(0.1)
print("fake client wrote", n, "snapshots, id", nonce)
