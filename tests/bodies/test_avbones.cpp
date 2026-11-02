#include "s2m_avbones_logic.h"

#include <cstdio>
#include <random>

using namespace m01::ab;

static int g_fail = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL %s\n", msg); g_fail++; } } while (0)

static double qerr(const double* a, const double* b)
{
    double d = std::fabs(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]);
    return 2 * std::acos(std::min(1.0, d)) * 180.0 / 3.14159265358979323846;
}

int main()
{
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> U(-1, 1);
    double worst = 0;
    for (int i = 0; i < 20000; i++)
    {
        double q[4] = {U(rng), U(rng), U(rng), U(rng)}, r[4];
        qnorm(q);
        uint8_t b[6];
        pack_q(q, b);
        unpack_q(b, r);
        worst = std::max(worst, qerr(q, r));
    }
    std::printf("worst quat error %.4f deg\n", worst);
    CHECK(worst < 0.02, "quat roundtrip");

    std::vector<std::string> names = {"jnt_root", "jnt_hips", "jnt_spine_01", "jnt_r_arm", "jnt_r_hand", "jnt_r_finger", "jnt_weapon", "jnt_ik_foot_root"};
    std::vector<int> par = {-1, 0, 1, 2, 3, 4, 0, 0};
    Sel s = select(names, par, 4, -1);
    CHECK(s.idx.size() == 5, "selection count");
    bool fing = false;
    for (int i : s.idx) if (i == 5 || i == 7) fing = true;
    CHECK(!fing, "finger and foot ik excluded");
    std::printf("selected %zu bytes %zu\n", s.idx.size(), payload_bytes(s));

    std::vector<Tf> buf(8);
    for (auto& t : buf) { std::memset(&t, 0, sizeof t); t.q[3] = 1; t.s[0] = t.s[1] = t.s[2] = 1; }
    buf[1].t[2] = 100;
    buf[2].t[2] = 120;
    buf[3].t[0] = 20; buf[3].t[2] = 140;
    buf[4].t[0] = 45; buf[4].t[2] = 140;
    buf[5].t[0] = 50; buf[5].t[2] = 140;
    buf[6].t[0] = 30; buf[6].t[2] = 130;

    std::string enc = encode(s, 5, buf.data(), 8);
    uint8_t sq; int cnt; uint32_t h;
    CHECK(header(enc, sq, cnt, h) && sq == 5 && cnt == 5 && h == s.hash, "header");
    Pose p;
    CHECK(decode(enc, s, p), "decode");

    std::vector<Tf> same = buf;
    apply(same.data(), 8, par, s, p, 1.0, 0);
    double maxd = 0;
    for (int i = 0; i < 8; i++)
        for (int c = 0; c < 3; c++) maxd = std::max(maxd, std::fabs(same[i].t[c] - buf[i].t[c]));
    std::printf("identity apply max translation drift %.4f cm\n", maxd);
    CHECK(maxd < 0.2, "identity apply keeps translations");

    Pose q2 = p;
    double rz[4] = {0, 0, std::sin(0.5), std::cos(0.5)};
    for (size_t k = 0; k < s.idx.size(); k++)
        if (s.idx[k] == 2) std::memcpy(q2.q[k].data(), rz, sizeof rz);
    std::vector<Tf> mv = buf;
    apply(mv.data(), 8, par, s, q2, 1.0, 0);
    double hand[3] = {mv[4].t[0] - mv[3].t[0], mv[4].t[1] - mv[3].t[1], 0};
    double len = std::sqrt(hand[0] * hand[0] + hand[1] * hand[1]);
    CHECK(std::fabs(len - 25) < 0.3, "hand keeps arm length after spine rotation");
    double fd[3] = {mv[5].t[0] - mv[4].t[0], mv[5].t[1] - mv[4].t[1], mv[5].t[2] - mv[4].t[2]};
    double fl = std::sqrt(fd[0] * fd[0] + fd[1] * fd[1] + fd[2] * fd[2]);
    CHECK(std::fabs(fl - 5) < 0.2, "finger follows hand");
    CHECK(std::fabs(mv[7].t[0]) < 1e-9 && std::fabs(mv[7].t[2]) < 1e-9, "ik foot untouched");

    std::vector<Tf> half = buf;
    apply(half.data(), 8, par, s, q2, 0.0, 0);
    CHECK(std::fabs(half[4].t[0] - 45) < 1e-9, "weight 0 leaves pose");

    std::deque<Pose> ring;
    Pose a = p, b2 = q2;
    a.at = 1.0; b2.at = 1.1;
    ring.push_back(a); ring.push_back(b2);
    Pose mid;
    CHECK(sample(ring, 1.05, mid), "sample");
    CHECK(sample(ring, 2.0, mid) && mid.at == 1.1, "hold newest");
    Pose scratch;
    CHECK(sample(ring, 1.05, scratch), "sample scratch");
    size_t cq = scratch.q.capacity(), ct = scratch.t.capacity();
    const double* dq = scratch.q.data()->data();
    for (int i = 0; i < 100; i++) CHECK(sample(ring, 1.0 + 0.001 * i, scratch), "sample scratch again");
    CHECK(scratch.q.capacity() == cq && scratch.t.capacity() == ct && scratch.q.data()->data() == dq, "scratch reuse keeps capacity");
    CHECK(sample(ring, 0.5, scratch) && scratch.q.capacity() == cq && scratch.q.data()->data() == dq, "scratch reuse on edge hold");
    std::string e1 = encode(s, 9, buf.data(), 8), e2 = encode(s, 9, buf.data(), 8), e3 = encode(s, 10, buf.data(), 8);
    uint8_t s1, s2, s3; int c1, c2, c3; uint32_t h1, h2, h3;
    CHECK(header(e1, s1, c1, h1) && header(e2, s2, c2, h2) && header(e3, s3, c3, h3), "dedupe headers");
    CHECK(s1 == s2 && s1 != s3 && h1 == h3, "dedupe by seq from header alone");
    CHECK(weight(10, 5, 9.95) > 0.99 && weight(10, 9.95, 9.95) < 0.5 && weight(10, 5, 9.0) == 0.0 && weight(10, 5, 9.9) == 1.0, "weight");

    std::printf(g_fail ? "FAILED\n" : "OK\n");
    return g_fail ? 1 : 0;
}
