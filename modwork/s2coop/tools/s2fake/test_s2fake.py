import json
import os
import subprocess
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import s2proto as P
import fakehub
import fakepeer

NET_TOOL = os.environ.get("S2_NET_TOOL", "")


def mklog(store):
    def log(m):
        store.append(m)
    return log


def peer_args(**kw):
    argv = ["--peer", "127.0.0.1:1", "--name", kw.pop("name", "t")]
    for k, v in kw.items():
        argv += ["--" + k.replace("_", "-")] if v is True else ["--" + k.replace("_", "-"), str(v)]
    return fakepeer.args(argv)


def hub_args(**kw):
    argv = []
    for k, v in kw.items():
        argv += ["--" + k.replace("_", "-"), str(v)]
    return fakehub.args(argv)


class Codec(unittest.TestCase):
    def test_roundtrip(self):
        o = {"v": 2, "sid": "1-2", "x1": "W1~~#H:client,a,b,Medium,ab12#L:a,100,100", "u": "é"}
        t, back = P.unpack(P.pack(P.T_HDR, o))
        self.assertEqual(t, P.T_HDR)
        self.assertEqual(back, o)

    def test_header_bytes(self):
        d = P.pack(P.T_PEER, {"pid": 2})
        self.assertEqual(d[:4], b"S2\x02\x08")
        self.assertEqual(json.loads(zlib.decompress(d[4:])), {"pid": 2})

    def test_bad(self):
        with self.assertRaises(ValueError):
            P.unpack(b"{}")
        with self.assertRaises(ValueError):
            P.unpack(b"S2\x01\x01xx")

    def test_sxc(self):
        d = b"S2\x02\x06" + (7).to_bytes(8, "little") + (3).to_bytes(4, "little") + b"abc"
        t, o = P.unpack(d)
        self.assertEqual((t, o["wid"], o["i"], o["n"]), (P.T_SXC, 7, 3, 3))


class Tags(unittest.TestCase):
    def test_rules(self):
        x1 = "W1~~#H:host,a,b,c,d#L:a,1,1#D2:x#lower:no#9X:no#AS:ab.3#E:"
        self.assertEqual(P.tags(x1), [("H", "host,a,b,c,d"), ("L", "a,1,1"), ("D2", "x"), ("AS", "ab.3"), ("E", "")])
        self.assertEqual(P.lead(x1), "W1~~")
        self.assertEqual(P.hello(x1)["nonce"], "d")

    def test_filter(self):
        x1 = "W1~~#H:client,a,b,c,n#L:w,15,100#QN:c,1,X#G:1;1,2,3;A;B#MV:1,0,0"
        self.assertEqual(P.filter_x1(x1, {"@W", "H", "L", "MV"}), "W1~~#H:client,a,b,c,n#L:w,15,100#MV:1,0,0")
        self.assertEqual(P.filter_x1(x1, {"L"}), "#L:w,15,100")

    def test_table(self):
        t = P.load_tag_table()
        self.assertIn("MV", P.relay_tags(t))
        self.assertNotIn("QN", P.relay_tags(t))
        self.assertNotIn("D", P.relay_tags(t))
        for k, r in t.items():
            self.assertIn(r["scope"], ("host", "peer", "personal"), k)
            if r["relay"]:
                self.assertEqual(r["scope"], "peer", k)


class Peer(unittest.TestCase):
    def host_hdr(self, f=1, sid="h1", x1="W1~~#H:host,v37,b205,Medium,abc#L:a,100,100#AS:abc.4"):
        return P.pack(P.T_HDR, P.snapshot(sid, f, 1.0, 1000, 2000, 300, 0, 0, 0, 100.0, 100.0, 3, x1))

    def test_handshake_and_stream(self):
        logs = []
        fp = fakepeer.FakePeer(peer_args(take_at=1, quest_at=2, down_at=3, crouch_every=1), mklog(logs))
        t0, o0 = P.unpack(fp.tick(0.0)[0])
        self.assertEqual((t0, P.hello(o0["x1"])["role"]), (P.T_HDR, "client"))
        fp.feed(self.host_hdr(), 0.0)
        self.assertTrue(fp.host_up)
        snaps = []
        for i in range(1, 40):
            for dg in fp.tick(i * 0.1):
                t, o = P.unpack(dg)
                if t == P.T_HDR:
                    snaps.append(o)
        self.assertTrue(snaps)
        s0 = snaps[0]
        self.assertEqual(s0["v"], 2)
        self.assertEqual(s0["ids"], [])
        h = P.hello(s0["x1"])
        self.assertEqual((h["role"], h["mod"], h["core"], h["diff"]), ("client", "v37", "b205", "Medium"))
        self.assertTrue(all(c in "0123456789abcdef" for c in h["nonce"]))
        self.assertIn("#AK:abc.4", s0["x1"])
        self.assertEqual((s0["gt"], s0["wx"]), (100.0, 3))
        xs = {round(s["x"]) for s in snaps}
        self.assertGreater(len(xs), 5)
        self.assertTrue(any("#G:1;" in s["x1"] for s in snaps))
        self.assertTrue(any("#QN:c,1,S2FakeNode_M1" in s["x1"] for s in snaps))
        self.assertTrue(any("#L:w,15,100" in s["x1"] for s in snaps))
        self.assertTrue(any("#MV:1," in s["x1"] for s in snaps))
        self.assertTrue(len({s["f"] for s in snaps}) == len(snaps))

    def test_sleep_answer(self):
        logs = []
        a = fakepeer.args(["--peer", "127.0.0.1:1", "--name", "sl", "--take-at", "-1", "--quest-at", "-1", "--sl-answer", "Y", "--sl-hold", "1"])
        fp = fakepeer.FakePeer(a, mklog(logs))
        fp.feed(self.host_hdr(x1="W1~~#H:host,v37,b205,Medium,abc#SL:4,S,0.00,1700000000,0,0,-"), 0.0)
        fp.feed(self.host_hdr(f=2, x1="W1~~#H:host,v37,b205,Medium,abc#SL:4,S,0.00,1700000000,0,0,-#AS:abc.5"), 0.1)
        x1s = []
        for i in range(1, 20):
            for dg in fp.tick(i * 0.1):
                t, o = P.unpack(dg)
                if t == P.T_HDR:
                    x1s.append((i * 0.1, o["x1"]))
        on = [t for t, x in x1s if "#SL:0,-,0.00,0,4,1700000000,Y,0" in x]
        self.assertTrue(on)
        self.assertLess(max(on), 1.05)
        self.assertEqual(fp.summary()["sleep"], [{"seq": 4, "ts": "1700000000", "answer": "Y"}])

    def test_item_sends_and_holders(self):
        logs = []
        a = fakepeer.args(["--peer", "127.0.0.1:1", "--name", "it", "--take-at", "-1", "--quest-at", "-1",
                           "--send", "0.2:K:BP_Box_C@520,30,1:1", "--send", "0.5:U:1,P,A012A,2"])
        fp = fakepeer.FakePeer(a, mklog(logs))
        fp.slot = 2
        fp.feed(self.host_hdr(x1="W1~~#H:host,v37,b205,Medium,abc#KH:2:BP_Box_C@520,30,1|3:Box@1,2,3#UR:3.0,1,P,A045A,1#LO:3,4,17,dlg,1,2,3"), 0.0)
        x1s = []
        for i in range(1, 30):
            for dg in fp.tick(i * 0.1):
                t, o = P.unpack(dg)
                if t == P.T_HDR:
                    x1s.append((i * 0.1, o["x1"]))
        self.assertTrue(any("#K:BP_Box_C@520,30,1" in x for t, x in x1s if t < 1.2))
        self.assertFalse(any("#K:" in x for t, x in x1s if t > 1.4))
        self.assertTrue(any("#U:1,P,A012A,2" in x for t, x in x1s))
        self.assertEqual(fp.holders["KH"], {"BP_Box_C@520,30,1": 2, "Box@1,2,3": 3})
        self.assertEqual(fp.relayed, ["3.0,1,P,A045A,1"])
        self.assertEqual(fp.lead_owner, 3)
        self.assertTrue(any("held by pid 2 (me)" in m for m in logs))
        self.assertTrue(any("from pid 3 (other)" in m for m in logs))
        sm = fp.summary()
        self.assertEqual(sm["lead_owner"], 3)

    def test_stale_and_session(self):
        logs = []
        fp = fakepeer.FakePeer(peer_args(), mklog(logs))
        fp.feed(self.host_hdr(f=5), 0.0)
        fp.feed(self.host_hdr(f=3, x1="#L:d,0,1"), 0.1)
        self.assertEqual(fp.host_tags.get("L"), "a,100,100")
        fp.feed(self.host_hdr(f=1, sid="h2"), 0.2)
        self.assertEqual(fp.host_sessions, 2)
        fp.tick(10.0)
        self.assertFalse(fp.host_up)
        self.assertTrue(any("HOST SILENT" in m for m in logs))

    def test_revive_by_third_and_go(self):
        logs = []
        fp = fakepeer.FakePeer(peer_args(down_at=0, take_at=-1, quest_at=-1), mklog(logs))
        fp.feed(P.pack(P.T_ROSTER, {"you": 2, "max": 4, "peers": [{"pid": 1, "st": "up"}, {"pid": 2, "st": "up"}, {"pid": 3, "st": "up"}]}), 0.0)
        fp.feed(self.host_hdr(x1="W1~~#H:host,v37,b205,Medium,abc#L:a,100,100#GO:4,bleed,1"), 0.0)
        x1 = lambda o: [P.unpack(d)[1] for d in o if P.unpack(d)[0] == P.T_HDR][0]["x1"]
        self.assertIn("#L:w,15,100", x1(fp.tick(0.1)))
        self.assertIn("#RV:0,0,0", x1(fp.tick(0.2)))
        for rv in ("0,0,0", "1,0,3"):
            fp.feed(P.pack(P.T_PEER, {"pid": 1, "sid": "s1", "x": 1, "y": 2, "z": 3, "x1": "#L:a,100,100#RV:" + rv}), 0.3)
        self.assertIn("#L:w,15,100", x1(fp.tick(0.4)))
        for rv in ("0,0,0", "1,0,2"):
            fp.feed(P.pack(P.T_PEER, {"pid": 3, "sid": "s3", "x": 1, "y": 2, "z": 3, "x1": "#L:a,100,100#RV:" + rv}), 0.5)
        self.assertIn("#L:a,30,100", x1(fp.tick(0.6)))
        self.assertEqual(fp.revived_by, 3)
        fp.feed(self.host_hdr(f=2, x1="#GO:4,bleed,1"), 0.7)
        self.assertIsNone(fp.go_dead)
        fp.feed(self.host_hdr(f=3, x1="#GO:5,bleed,2"), 0.8)
        self.assertIsNone(fp.go_dead)
        fp.feed(self.host_hdr(f=4, x1="#GO:6,alldown"), 0.9)
        self.assertEqual(fp.go_dead, "6,alldown")
        self.assertIn("#L:d,0,100", x1(fp.tick(1.0)))
        self.assertEqual(fp.summary()["go_events"], ["5,bleed,2", "6,alldown"])

    def test_dead_counts_and_revive_send(self):
        fp = fakepeer.FakePeer(peer_args(dead_at=1, revive="0.5:3", take_at=-1, quest_at=-1), mklog([]))
        fp.feed(P.pack(P.T_ROSTER, {"you": 1, "max": 4, "peers": [{"pid": 1, "st": "up"}]}), 0.0)
        x1 = lambda o: [P.unpack(d)[1] for d in o if P.unpack(d)[0] == P.T_HDR][0]["x1"]
        fp.tick(0.0)
        self.assertIn("#RV:1,0,3", x1(fp.tick(0.6)))
        self.assertIn("#RV:1,1,3", x1(fp.tick(1.2)))
        self.assertIn("#RV:1,1,3", x1(fp.tick(1.4)))

    def test_peer_roster_full(self):
        logs = []
        fp = fakepeer.FakePeer(peer_args(), mklog(logs))
        fp.feed(P.pack(P.T_PEER, {"pid": 2, "sid": "s2", "x": 1, "y": 2, "z": 3, "x1": "#H:client,a,b,c,n2#L:w,15,100"}), 0.0)
        fp.feed(P.pack(P.T_ROSTER, {"you": 1, "max": 4, "peers": [{"pid": 1, "st": "up"}, {"pid": 2, "st": "up"}]}), 0.0)
        fp.feed(P.pack(P.T_FULL, {"max": 4}), 0.0)
        sm = fp.summary()
        self.assertEqual(sm["others"]["2"]["life"], "w,15,100")
        self.assertEqual(sm["slot"], 1)
        self.assertTrue(sm["full"])


class HubT(unittest.TestCase):
    def client(self, hub, addr, name, now, life="a,100,100", extra=""):
        x1 = "W1~~#H:client,v,c,Medium,%s#L:%s#MV:0,0,0#QN:c,1,Node_%s%s" % (name, life, name, extra)
        m = P.snapshot("sid-" + name, int(now * 10), now, 10, 20, 30, 0, 0, 0, 1.0, 1.0, 0, x1)
        out = []
        for dg in P.snapshot_datagrams(m):
            out += hub.feed(dg, addr, now)
        return out

    def decode(self, out):
        r = {}
        for dg, dst in out:
            t, o = P.unpack(dg)
            r.setdefault(dst, []).append((t, o))
        return r

    def test_four_clients_fanout(self):
        logs = []
        hub = fakehub.Hub(hub_args(max=4), mklog(logs))
        addrs = [("10.0.0.%d" % i, 7778) for i in range(1, 5)]
        for i, ad in enumerate(addrs):
            self.client(hub, ad, "n%d" % i, 1.0)
        out = self.decode(hub.tick(1.0))
        self.assertEqual(set(out), set(addrs))
        for ad in addrs:
            kinds = [t for t, _ in out[ad]]
            self.assertEqual(kinds.count(P.T_HDR), 1)
            self.assertEqual(kinds.count(P.T_PEER), 3)
            self.assertEqual(kinds.count(P.T_ROSTER), 1)
            peers = [o for t, o in out[ad] if t == P.T_PEER]
            mine = hub.by_addr[ad].pid
            self.assertNotIn(mine, [p["pid"] for p in peers])
            for p in peers:
                self.assertNotIn("#QN:", p["x1"])
                self.assertIn("#L:", p["x1"])
                self.assertIn("#H:client", p["x1"])
            ro = [o for t, o in out[ad] if t == P.T_ROSTER][0]
            self.assertEqual(ro["you"], mine)
            self.assertEqual(len(ro["peers"]), 4)
        self.assertEqual(sorted(s.pid for s in hub.slots.values()), [1, 2, 3, 4])
        self.assertEqual(sum(1 for m in logs if "S2HUB qn: origem c" in m), 4)

    def test_full_and_free(self):
        logs = []
        hub = fakehub.Hub(hub_args(max=2), mklog(logs))
        self.client(hub, ("a", 1), "a", 1.0)
        self.client(hub, ("b", 1), "b", 1.0)
        r = self.client(hub, ("c", 1), "c", 1.0)
        self.assertEqual([P.unpack(d)[0] for d, _ in r], [P.T_FULL])
        hub.feed(P.pack(P.T_BYE, {"sid": "sid-a"}), ("a", 1), 2.0)
        self.client(hub, ("c", 1), "c", 2.5)
        self.assertEqual(hub.by_addr[("c", 1)].pid, 1)

    def test_silent_left(self):
        logs = []
        hub = fakehub.Hub(hub_args(max=4, silent=5, left=10), mklog(logs))
        self.client(hub, ("a", 1), "a", 0.0)
        hub.tick(6.0)
        self.assertEqual(hub.slots[1].state, "silent")
        hub.tick(17.0)
        self.assertEqual(hub.slots, {})
        self.assertTrue(any("PEER LEFT" in m for m in logs))

    def test_moved_address(self):
        logs = []
        hub = fakehub.Hub(hub_args(max=4), mklog(logs))
        self.client(hub, ("a", 1), "a", 0.0)
        self.client(hub, ("a", 2), "a", 0.5)
        self.assertEqual(len(hub.slots), 1)
        self.assertIn(("a", 2), hub.by_addr)

    def test_one_client_sends_no_new_types(self):
        hub = fakehub.Hub(hub_args(max=1), mklog([]))
        self.client(hub, ("a", 1), "a", 0.0)
        kinds = {P.unpack(d)[0] for d, _ in hub.tick(0.0)}
        self.assertEqual(kinds, {P.T_HDR})

    def test_all_down_and_bleed(self):
        logs = []
        hub = fakehub.Hub(hub_args(max=4, host_down_at=0), mklog(logs))
        self.client(hub, ("a", 1), "a", 0.0, life="w,15,100")
        self.client(hub, ("b", 1), "b", 0.0, life="a,100,100")
        hub.tick(0.0)
        self.assertEqual(hub.go_seq, 0)
        self.client(hub, ("b", 1), "b", 0.5, life="w,15,100")
        hub.tick(0.5)
        self.assertEqual(hub.go_seq, 1)
        out = self.decode(hub.tick(0.6))
        h = [o for t, o in out[("a", 1)] if t == P.T_HDR][0]
        self.assertIn("#GO:1,alldown", h["x1"])
        hub2 = fakehub.Hub(hub_args(max=4), mklog(logs))
        self.client(hub2, ("a", 1), "a", 0.0, extra="#RV:0,0,0")
        self.client(hub2, ("b", 1), "b", 0.0)
        hub2.tick(0.0)
        self.assertEqual(hub2.go_seq, 0)
        self.client(hub2, ("a", 1), "a", 0.5, life="d,0,100", extra="#RV:0,1,0")
        hub2.tick(0.5)
        self.assertEqual(hub2.go_seq, 1)
        self.assertTrue(any("bleed,1" in m for m in logs))

    def test_go_single_and_latched(self):
        logs = []
        hub = fakehub.Hub(hub_args(max=4), mklog(logs))
        for n, ad in (("a", 1), ("b", 2), ("c", 3)):
            self.client(hub, (n, ad), n, 0.0, extra="#RV:0,0,0")
        hub.tick(0.0)
        self.client(hub, ("b", 2), "b", 1.0, extra="#RV:0,1,0")
        self.client(hub, ("c", 3), "c", 1.1, extra="#RV:0,1,0")
        out = self.decode(hub.tick(1.2))
        h = [o for t, o in out[("a", 1)] if t == P.T_HDR][0]
        self.assertIn("#GO:1,bleed,2#", h["x1"])
        self.assertEqual(hub.go_seq, 1)
        self.assertIn("#L:d,0,100", h["x1"])
        self.assertTrue(any("already sent; bleed pid=3 ignored" in m for m in logs))
        out = self.decode(hub.tick(11.5))
        h = [o for t, o in out[("a", 1)] if t == P.T_HDR][0]
        self.assertNotIn("#GO:", h["x1"])

    def test_silent_blocks_alldown(self):
        logs = []
        hub = fakehub.Hub(hub_args(max=4, host_down_at=0, silent=1), mklog(logs))
        self.client(hub, ("a", 1), "a", 0.0, life="w,15,100")
        self.client(hub, ("b", 2), "b", 0.0, life="w,15,100")
        self.client(hub, ("c", 3), "c", 0.0, life="a,100,100")
        hub.tick(0.0)
        self.client(hub, ("a", 1), "a", 1.5, life="w,15,100")
        self.client(hub, ("b", 2), "b", 1.5, life="w,15,100")
        hub.tick(1.5)
        self.assertEqual(hub.go_seq, 0)
        self.client(hub, ("c", 3), "c", 1.6, life="w,15,100")
        hub.tick(1.6)
        self.assertEqual(hub.go_seq, 1)
        self.assertEqual(hub.summary()["go_txt"], ["1,alldown"])

    def test_client_revives_host(self):
        logs = []
        hub = fakehub.Hub(hub_args(max=4, host_down_at=0), mklog(logs))
        self.client(hub, ("a", 1), "a", 0.0, extra="#RV:0,0,0")
        self.client(hub, ("b", 2), "b", 0.0, extra="#RV:0,0,0")
        hub.tick(0.0)
        self.client(hub, ("b", 2), "b", 0.5, extra="#RV:1,0,0")
        out = self.decode(hub.tick(0.6))
        h = [o for t, o in out[("a", 1)] if t == P.T_HDR][0]
        self.assertTrue(hub.host_revived)
        self.assertIn("#L:a,100,100", h["x1"])
        self.assertEqual(hub.revives, [(2, 0)])


@unittest.skipUnless(NET_TOOL and os.path.exists(NET_TOOL), "set S2_NET_TOOL to a net_tool.exe built from src/s2r_net.cpp")
class Cpp(unittest.TestCase):
    def script(self, lines):
        d = tempfile.mkdtemp()
        i, o = os.path.join(d, "in.txt"), os.path.join(d, "out.txt")
        with open(i, "w") as fh:
            fh.write("\n".join(lines) + "\n")
        subprocess.run([NET_TOOL, "script", i, o], check=True)
        with open(o) as fh:
            return fh.read().splitlines()

    def test_cpp_decodes_python(self):
        fp = fakepeer.FakePeer(peer_args(anchor="0,0,0"), mklog([]))
        dg = fp.tick(1.0)[0]
        r = self.script(["F\t1.0\t" + dg.hex(), "R\t1.0"])
        self.assertTrue(r[0].startswith("F\t0\t"))
        rend = json.loads(bytes.fromhex(r[1].split("\t")[1]).decode())
        self.assertEqual(rend["sid"], fp.sid)
        self.assertEqual(P.hello(rend["x1"])["role"], "client")

    def test_cpp_two_clients_flap(self):
        a = fakepeer.FakePeer(peer_args(anchor="0,0,0", name="a"), mklog([]))
        b = fakepeer.FakePeer(peer_args(anchor="0,0,0", name="b"), mklog([]))
        lines = []
        for k in range(6):
            t = 1.0 + k * 0.1
            lines += ["F\t%.1f\t%s" % (t, a.tick(t)[0].hex()), "R\t%.1f" % t, "F\t%.1f\t%s" % (t, b.tick(t)[0].hex()), "R\t%.1f" % t]
        rs = [json.loads(bytes.fromhex(l.split("\t")[1]).decode())["sid"] for l in self.script(lines) if l.startswith("R\t")]
        self.assertEqual(rs, [a.sid, b.sid] * 6)

    def test_python_decodes_cpp(self):
        m = P.snapshot("s", 1, 1.0, 1, 2, 3, 0, 0, 0, -1.0, -1.0, -1, "W1~~#H:host,a,b,c,ab#L:a,1,1")
        r = self.script(["S\t1.0\t" + P.dumps(m).encode().hex()])
        dgs = [bytes.fromhex(l.split("\t")[2]) for l in r if l.startswith("D\t")]
        t, o = P.unpack(dgs[0])
        self.assertEqual(t, P.T_HDR)
        self.assertEqual(o["x1"], m["x1"])
        self.assertEqual(o["ids"], [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
