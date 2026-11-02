#include "s2x_int.h"
#include "s2n_nat_api.h"
#include "s2m_bodies.h"
#include "s2m_damage.h"
#include "s2m_quest.h"
#include "s2r_x0.h"
#include <algorithm>
#include <cstdlib>
#include <map>
#include <vector>

extern "C" bool s2n_nat_is_own(uint64_t a) { return s2nat::own(a); }
extern "C" bool s2n_nat_is_listed_addr(uint64_t a) { return s2nat::listed_addr(a); }
extern "C" bool s2n_nat_is_listed_uid(int32_t uid) { return s2nat::listed(uid); }
extern "C" bool s2n_nat_park_twin(uint64_t a, long long uid) { return s2nat::park_twin(a, (int32_t)uid); }
extern "C" bool s2n_nat_inert(uint64_t a, long long uid, int on) { return s2nat::inert(a, (int32_t)uid, on); }

namespace x0::in
{
    namespace
    {
        int g_err = 0;
        double g_brAt = -1;
        int64_t g_b168 = -1;
        int g_a1l = -1, g_a1p = -1;
        int g_m1[4] = {-1, -1, -1, -1};
        int g_wp[2] = {-1, -1};
        int g_cpd = -1;
        int g_cov = -1;
        int g_rmo = -1;
        int g_nvw = -1;
        int g_wph = -1;
        int g_lsc = -1;
        int g_mlk = -1;
        int g_gmk = -1;
        int g_msc = -1;
        int g_lre = -1;
        int g_msec = -1;
        int g_mre = -1;
        int g_gmr = -1;
        int g_mlp = -1;
        int g_msq = -1;
        int g_mkp = -1;
        int g_mov = -1;
        double g_a1h = -1, g_a1a = -1;
        int g_nd = -1;
        int g_mc = -1;
        int g_gait = -1;
        int g_mab = -1;
        int g_cloak = -1;
        int g_copy = -1;
        int g_sync = -1;
        int g_loco = -1;
        int g_kinc = -1;
        int g_gaitc = -1;
int g_wpnl = -1;
int g_kinv = -1;
int g_lprm = -1;
int g_lfl = -1;
int g_traj = -1;
int g_trajv = -1;
int g_losf = -1;
int g_trajown = -1;
int g_cpvel = -1;
int g_props = -1;
int g_dupfix = -1;
        int g_ci = -1, g_cs = -1, g_nv = -1, g_qg = -1, g_fd = -1, g_da = -1, g_hk = -1, g_dk = -1, g_dp = -1, g_wd = -1, g_rh = -1, g_far = -1;
        double g_farDead = -1;

        uint64_t addr(const Ref& r) { return r.valid() ? r.address() : 0; }
        uint64_t local_pawn() { return addr(first_of(L"BP_Stalker2Character_C")); }

        void err(const char* where)
        {
            if (++g_err <= 5) logf("NOLA natnpc erro (%s): exception", where);
        }

        bool mc_mode() { return hub() && views() > 1 && fi("natmc", 1) == 1; }

        Ref body_of(bool tgt, int v)
        {
            auto& b = bodies_v();
            auto& f = tgt ? b.target : b.puppet;
            if (f) return f(v);
            if (v != 0) return Ref{};
            return tgt ? m01::target() : m01::puppet();
        }

        std::string host_view_out(int v)
        {
            if (!mc_mode() || fi("natnpc", 0) != 1 || !is_host()) return std::string();
            if (v == 0)
            {
                std::vector<s2nat::Player> pl;
                each_view([&](int w, const Snap& s, const Peer& p) {
                    if (!s.ok || !s.has_pos || p.state != Peer::up) return;
                    s2nat::Player q;
                    q.view = w; q.pid = view_pid(w); q.x = s.x; q.y = s.y; q.z = s.z;
                    q.pup = addr(body_of(false, w)); q.tgt = addr(body_of(true, w));
                    pl.push_back(q);
                });
                try { s2nat::host_views(local_pawn(), pl); }
                catch (...) { err("host mc"); return std::string(); }
            }
            return s2nat::host_view(v);
        }

        void client_bodies()
        {
            std::vector<s2nat::Body> o;
            if (views() > 1)
                for (int v = 1; v < views(); ++v)
                {
                    uint64_t a = addr(body_of(false, v));
                    if (a) o.push_back({view_pid(v), a});
                }
            s2nat::bodies(my_pid(), o);
        }

        std::string host_out()
        {
            if (fi("natnpc", 0) != 1 || !is_host() || mc_mode()) return std::string();
            const Snap& r = remote();
            if (!r.ok || !r.has_pos) return std::string();
            try { return s2nat::host(1, local_pawn(), r.x, r.y, r.z, addr(m01::puppet()), addr(m01::target())); }
            catch (...) { err("host"); return std::string(); }
        }

        void client(int via)
        {
            bool on = fi("natnpc", 0) == 1;
            std::string nn, nl;
            if (on) { rpart("NN", nn); rpart("NL", nl); }
            client_bodies();
            auto& lp = m01::ext().loot_pending;
            bool busy = m01::adopt_pending() || m01::world_held() || (lp && lp());
            try { s2nat::client(on ? 1 : 0, nn, nl, on ? local_pawn() : 0, addr(m01::puppet()), addr(m01::target()), busy ? 1 : 0, via); }
            catch (...) { err("cliente"); }
            if (via == 1) g_brAt = now();
        }

        bool br_on() { return fi("natbroff", 0) != 1 && s2nat::natbr() == 1; }

        void tick100()
        {
            int64_t b = fi("b168off", 0);
            if (b != g_b168) { g_b168 = b; s2nat::natcb(b == 1 ? 0 : 1); }
            int a1l = (int)fi("natlookst", 0), a1p = (int)fi("natpark", 0);
            double a1h = fd("nathys", 1.0), a1a = fd("natparkage", 10.0);
            if (a1l != g_a1l || a1p != g_a1p || a1h != g_a1h || a1a != g_a1a)
            {
                g_a1l = a1l; g_a1p = a1p; g_a1h = a1h; g_a1a = a1a;
                s2nat::cfg_a1(a1l, a1p, a1h, a1a);
                logf("NOLA natnpc a1 flags: natlookst=%d natpark=%d nathys=%.2f natparkage=%.1f", a1l, a1p, a1h, a1a);
            }
            int m1[4] = {(int)fi("natmut", 1), (int)fi("natadopt_kept", 1), (int)fi("offanim", 1), (int)fi("offvb", 1)};
            if (!std::equal(m1, m1 + 4, g_m1))
            {
                std::copy(m1, m1 + 4, g_m1);
                s2nat::cfg_m1(m1[0], m1[1], m1[2], m1[3]);
                logf("NOLA natnpc m1 flags: natmut=%d natadopt_kept=%d offanim=%d offvb=%d", m1[0], m1[1], m1[2], m1[3]);
            }
            int wp[2] = {(int)fi("natwpnsid", 0), (int)fi("natwpnarm", 1)};
            if (wp[0] != g_wp[0] || wp[1] != g_wp[1])
            {
                g_wp[0] = wp[0]; g_wp[1] = wp[1];
                s2nat::cfg_wpn(wp[0], wp[1]);
                logf("NOLA natnpc wpn flags: natwpnsid=%d natwpnarm=%d", wp[0], wp[1]);
            }
            int cpd = std::clamp((int)fi("natcpdmg", 2), 0, 2) | (fi("natcphp", 1) == 1 ? 4 : 0);
            if (cpd != g_cpd)
            {
                g_cpd = cpd;
                s2nat::cfg_cpd(cpd & 3, (cpd >> 2) & 1);
                logf("NOLA natcpdmg: natcpdmg=%d natcphp=%d (1 = dano nativo local nas copias vivas descartado no cliente, 2 = so reacao e sangue com dano 0.01; natcphp 1 = HP da copia segue o HP do host)", cpd & 3, (cpd >> 2) & 1);
            }
            int cov = fi("natcover", 1) == 1 ? 1 : 0;
            if (cov != g_cov)
            {
                g_cov = cov;
                s2nat::cfg_cover(cov);
                logf("NOLA natcover: natcover=%d (1 = copia segue a cobertura do host, escrita so em bloco de cobertura valido)", cov);
            }
            int msec = fi("natmsec", 1) == 1 ? 1 : 0;
            if (msec != g_msec)
            {
                g_msec = msec;
                s2nat::cfg_msec(msec);
                logf("NOLA natmsec: natmsec=%d (1 = copia toca a montagem na velocidade e com as secoes seguintes do host)", msec);
            }
            int mre = fi("natmre", 1) == 1 ? 1 : 0;
            if (mre != g_mre)
            {
                g_mre = mre;
                s2nat::cfg_mre(mre);
                logf("NOLA natmre: natmre=%d (1 = host reenvia a montagem ativa para copias que perderam a janela natmwin)", mre);
            }
            int gmr = fi("natgenmre", 1) == 1 ? 1 : 0;
            if (gmr != g_gmr)
            {
                g_gmr = gmr;
                s2nat::cfg_genmre(gmr);
                logf("NOLA natgenmre: natgenmre=%d (1 = copia refeita pelo natgen volta a tocar a montagem do host)", gmr);
            }
            int mlp = fi("natmloop", 1) == 1 ? 1 : 0;
            if (mlp != g_mlp)
            {
                g_mlp = mlp;
                s2nat::cfg_mloop(mlp);
                logf("NOLA natmloop: natmloop=%d (1 = secao que termina no host fica em laco na copia, nao segue o link original)", mlp);
            }
            int msq = fi("natmsecq", 1) == 1 ? 1 : 0;
            if (msq != g_msq)
            {
                g_msq = msq;
                s2nat::cfg_msecq(msq);
                logf("NOLA natmsecq: natmsecq=%d (1 = a secao da copia e escolhida com tolerancia de 1 ms, a precisao com que o host envia a posicao; 0 = 0.1 ms)", msq);
            }
            int mkp = fi("natmkeep", 1) == 1 ? 1 : 0;
            if (mkp != g_mkp)
            {
                g_mkp = mkp;
                s2nat::cfg_mkeep(mkp);
                logf("NOLA natmkeep: natmkeep=%d (1 = montagem de arma da copia nao para a montagem do corpo, como no host)", mkp);
            }
            int mov = fi("natmovl", 1) == 1 ? 1 : 0;
            if (mov != g_mov)
            {
                g_mov = mov;
                s2nat::cfg_movl(mov);
                logf("NOLA natmovl: natmovl=%d (1 = o host envia todas as montagens ativas do corpo e a copia toca o mesmo conjunto, sem perder a cobertura na recarga)", mov);
            }
            int rmo = fi("natrmoff", 1) == 1 ? 1 : 0;
            if (rmo != g_rmo)
            {
                g_rmo = rmo;
                s2nat::cfg_rmoff(rmo);
                logf("NOLA natrmoff: natrmoff=%d (1 = copia sem movimento de raiz nem motion warping local, so a posicao do host move o corpo)", rmo);
            }
            int nvw = fi("natnavw", 1) == 1 ? 1 : 0;
            if (nvw != g_nvw)
            {
                g_nvw = nvw;
                s2nat::cfg_navw(nvw);
                logf("NOLA natnavw: natnavw=%d (1 = the game's async NavWalking capsule placement skips a client copy whose position comes from the host, 0 = old behaviour)", nvw);
            }
            int wph = fi("natwpnhid", 1) == 1 ? 1 : 0;
            if (wph != g_wph)
            {
                g_wph = wph;
                s2nat::cfg_wpnhid(wph);
                logf("NOLA natwpnhid: natwpnhid=%d (1 = the weapon in hand and its parts hide and show together with a hidden client copy, 0 = old behaviour)", wph);
            }
            int lsc = fi("natlooksec", 1) == 1 ? 1 : 0;
            if (lsc != g_lsc)
            {
                g_lsc = lsc;
                s2nat::cfg_looksec(lsc);
                logf("NOLA natlooksec: natlooksec=%d (1 = the look carries hidden material sections per LOD and custom primitive data per part, the copy applies and counts them, and parts the client generator did not build count as differences; 0 = old behaviour)", lsc);
            }
            int mlk = fi("natmutlook", 1) == 1 ? 1 : 0;
            if (mlk != g_mlk)
            {
                g_mlk = mlk;
                s2nat::cfg_mutlook(mlk);
                logf("NOLA natmutlook: natmutlook=%d (1 = the host sends the mutant look and the client copy takes its meshes, materials, sections and custom primitive data, e.g. the coat tone; 0 = old behaviour)", mlk);
            }
            int gmk = fi("natgenmk", 1) == 1 ? 1 : 0;
            if (gmk != g_gmk)
            {
                g_gmk = gmk;
                s2nat::cfg_genmk(gmk);
                logf("NOLA natgenmk: natgenmk=%d (1 = parts the client mesh generator did not build, e.g. attaches above the client's mg.MaxAttaches, are created on the copy from the host part list; 0 = old behaviour, they only count as differences)", gmk);
            }
            int msc = fi("natmidsc", 1) == 1 ? 1 : 0;
            if (msc != g_msc)
            {
                g_msc = msc;
                s2nat::cfg_midsc(msc);
                logf("NOLA natmidsc: natmidsc=%d (1 = the look carries the damage and gore scalar parameters of the dynamic material instances (e.g. Damage_Intensity) and the copy sets them, so a corpse keeps the host's gore look; 0 = old behaviour)", msc);
            }
            int lre = (int)fi("natlookre", 0);
            if (lre != g_lre)
            {
                g_lre = lre;
                if (lre != 0) logf("NOLA natlookre: natlookre=%d copias=%d (dev: every live client copy applies its look once more now; set a new nonzero value to repeat)", lre, s2nat::cfg_lookre());
            }
            int nd = fi("natdmg", 0) == 1 ? 1 : 0;
            if (nd != g_nd)
            {
                g_nd = nd;
                s2nat::cfg_dmg(nd);
                logf("NOLA natdmg: natdmg=%d captura_no_host=%d", nd, s2nat::nd_on());
            }
            int mc = fi("natmc", 1) == 1 ? 1 : 0;
            if (mc != g_mc)
            {
                g_mc = mc;
                s2nat::cfg_mc(mc);
                logf("NOLA natmc: natmc=%d (1 = varios jogadores no host: relevancia por uniao, NN pessoal por view)", mc);
            }
            int ng = (int)std::clamp<int64_t>(fi("natgait", 1), 0, 2);
            if (ng != g_gait)
            {
                g_gait = ng;
                s2nat::cfg_gait(ng);
                logf("NOLA natgait: natgait=%d (0 = limiares humanos, 1 = limiares da classe do mutante, 2 = marcha do host)", ng);
            }
            int na = (int)std::clamp<int64_t>(fi("natmab", 3), 0, 3);
            if (na != g_mab)
            {
                g_mab = na;
                s2nat::cfg_mab(na);
                logf("NOLA natmab: natmab=%d (bit 1 = montagens de ataque/habilidade do mutante + raiz travada no kin, bit 2 = invisibilidade do host)", na);
            }
            int ck = fi("natcloak", 1) == 1 ? 1 : 0;
            if (ck != g_cloak)
            {
                g_cloak = ck;
                s2nat::cfg_cloak(ck);
                logf("NOLA natcloak: natcloak=%d (1 = host envia o valor atual da invisibilidade do sugador; cliente iguala a copia e reaplica o material com a funcao do jogo)", ck);
            }
            int cp = fi("natcopy", 1) == 1 ? 1 : 0;
            if (cp != g_copy)
            {
                g_copy = cp;
                s2nat::cfg_copy(cp);
                logf("NOLA natcopy: natcopy=%d (1 = the client spawns every generic human copy from the pak prototype Nola_NpcCopy: faction NolaCopy neutral to all, zero-stimulus threat, NoVision; combat only from the host bits)", cp);
            }
            int sy = (fi("natlegs", 1) == 1 ? 1 : 0) | (fi("natmbh", 1) == 1 ? 2 : 0) | (fi("natqgsafe", 1) == 1 ? 4 : 0) | (fi("natlostfix", 1) == 1 ? 8 : 0) | (fi("nateqfix", 1) == 1 ? 16 : 0);
            if (sy != g_sync)
            {
                g_sync = sy;
                s2nat::cfg_sync(sy & 1, (sy >> 1) & 1, (sy >> 2) & 1, (sy >> 3) & 1, (sy >> 4) & 1);
                logf("NOLA natsync i16e7: natlegs=%d natmbh=%d natqgsafe=%d natlostfix=%d nateqfix=%d (legs = per-copy leg/slide metric, mbh = host sends human movement behaviour and the copy orders that gait, qgsafe = quest guid read only on live non-pooled pawns and never retried after a throw, lostfix = lost spawned copy destroyed and capped per host uid, eqfix = weapon switch with no effect stops repeating)",
                     sy & 1, (sy >> 1) & 1, (sy >> 2) & 1, (sy >> 3) & 1, (sy >> 4) & 1);
            }
            int lo = (fi("natkinmv", 1) == 1 ? 1 : 0) | (fi("nateqsync", 1) == 1 ? 2 : 0) | (fi("natturn", 1) == 1 ? 4 : 0);
            if (lo != g_loco)
            {
                g_loco = lo;
                s2nat::cfg_loco(lo & 1, (lo >> 1) & 1, (lo >> 2) & 1);
                logf("NOLA natloco i16e7b: natturn=%d natkinmv=%d nateqsync=%d (turn = a stopped copy turns through the stock MoveTo end rotation so the turn-in-place animation plays; kinmv = under natkin a copy keeps one long MoveTo along the host heading, re-ordered only on gait change, turn, near target or host stop; eqsync = a host weapon change runs the copy's own ChangeMainHandWeapon at once and host montages are not mirrored while the copy changes weapon)",
                     (lo >> 2) & 1, lo & 1, (lo >> 1) & 1);
            }
            int kc = (fi("natkin", 1) == 1 ? 1 : 0) | (fi("natkinord", 1) == 1 ? 2 : 0) | (fi("natkinstop", 0) == 1 ? 4 : 0) | (fi("natkinreq", 1) == 1 ? 8 : 0) | (fi("natkinpin", 1) == 1 ? 16 : 0);
            if (kc != g_kinc)
            {
                g_kinc = kc;
                s2nat::cfg_kin(kc & 1, (kc >> 1) & 1, (kc >> 2) & 1, (kc >> 3) & 1, (kc >> 4) & 1);
                logf("NOLA natkincfg: natkin=%d natkinord=%d natkinstop=%d natkinreq=%d natkinpin=%d", kc & 1, (kc >> 1) & 1, (kc >> 2) & 1, (kc >> 3) & 1, (kc >> 4) & 1);
            }
            int gc = fi("natgaitcp", 1) == 1 ? 1 : 0;
            if (gc != g_gaitc)
            {
                g_gaitc = gc;
                s2nat::cfg_gaitcp(gc);
                logf("NOLA natgaitcp: natgaitcp=%d (1 = the stock locomotion gait selector exe+0x239895c returns the host gait for adopted copies, so the copy's state tags [rec+0x250] and motion-matching tag follow the host instead of its own idle AI path)", gc);
            }
            int kv = fi("natloco", 1) == 1 ? 1 : 0;
            if (kv != g_kinv)
            {
                g_kinv = kv;
                s2nat::cfg_loco_host(kv);
                logf("NOLA natloco: natloco=%d (1 = the stock locomotion updater exe+0x2397454 is skipped for kin copies and the motion-matching tag Locomotion.<Relax|Combat>.Default.<Stand|Crouch>.<Idle|Walk|Run|Sprint>, the state tags [rec+0x250] and the speed (PoseSearch block [agent+0xac8]+0x620: +0xb8 tag, +0xc8 speed) come from the host instead of the copy's own AI path, which never exists under natkin)", kv);
            }
            int lp = fi("natlocoprm", 1) == 1 ? 1 : 0;
            if (lp != g_lprm)
            {
                g_lprm = lp;
                s2nat::cfg_loco_prm(lp);
                logf("NOLA natlocoprm: natlocoprm=%d (1 = under natloco a kin copy still runs the stock gait speed read exe+0x8ea172 on [rec+0x668] MovementParams, which recomputes the dirty params (+0x1e8) from the class; 0 = the copy keeps the unrecomputed defaults Walk 600 Run 1200 Sprint 1800)", lp);
            }
            int lf = fi("natlocofl", 1) == 1 ? 1 : 0;
            if (lf != g_lfl)
            {
                g_lfl = lf;
                s2nat::cfg_loco_fl(lf);
                logf("NOLA natlocofl: natlocofl=%d (1 = the stock locomotion updater runs first on a kin copy and natloco then replaces only the gait of the tag it chose (Default or Strafe), the state bits and the speed from the host, and the PoseSearch block is found for mutants as well; 0 = the old path that skips the stock updater and always writes Default)", lf);
            }
            int tj = fi("nattraj", 1) == 1 ? 1 : 0;
            if (tj != g_traj)
            {
                g_traj = tj;
                s2nat::cfg_traj(tj);
                logf("NOLA nattraj: nattraj=%d (1 = after the stock PoseSearch trajectory builder exe+0x237106c runs on a moving kin copy, the motion-matching query trajectory gets 30 future samples over 1.0 s from the host velocity (the copy has no AI path, so stock builds none) and bForcedIdling is cleared while the host moves; 0 = the copy keeps the stock trajectory with history only)", tj);
            }
            int tjv = fi("nattrajv", 1) == 1 ? 1 : 0;
            if (tjv != g_trajv)
            {
                g_trajv = tjv;
                s2nat::cfg_traj_v(tjv);
                logf("NOLA nattrajv: nattrajv=%d (1 = after the stock PoseSearch trajectory builder runs on a kin copy, the sample velocities (+0x40) are written: past samples from their own position deltas, current and future samples from the host velocity; the copy is placed kinematically so its CMC velocity is 0 and stock writes velocity 0 on every sample, which makes motion matching pick idle clips at run; 0 = the copy keeps the stock zero velocities)", tjv);
            }
            int lsf = fi("natlocosf", 1) == 1 ? 1 : 0;
            if (lsf != g_losf)
            {
                g_losf = lsf;
                s2nat::cfg_loco_sf(lsf);
                logf("NOLA natlocosf: natlocosf=%d (1 = under natlocofl a copy whose stock tag is Strafe gets the Default family when the host is not in combat or runs/sprints, because DefaultGameplayTags.ini has Strafe tags only for Combat Idle/Walk; 0 = the copy keeps Strafe and gets the nonexistent Locomotion.*.Strafe.*.Run/Sprint tag, which plays stand_idle at run)", lsf);
            }
            int tjo = fi("nattrajown", 0) == 1 ? 1 : 0;
            if (tjo != g_trajown)
            {
                g_trajown = tjo;
                s2nat::cfg_traj_own(tjo);
                logf("NOLA nattrajown: nattrajown=%d (1 = after the stock PoseSearch trajectory builder runs on a kin copy, the future samples it built from the copy's own AI path (our MoveTo orders, outrun by the kinematic placement) are dropped, and a moving copy gets the host-velocity future instead; 0 = the copy keeps that stock future, which can point away from the host's motion)", tjo);
            }
            int cpv = fi("natcpvel", 1) == 1 ? 1 : 0;
            if (cpv != g_cpvel)
            {
                g_cpvel = cpv;
                s2nat::cfg_cp_vel(cpv);
                logf("NOLA natcpvel: natcpvel=%d (1 = before the stock PoseSearch trajectory builder exe+0x237106c runs on a kin copy, the velocity it reads ([traj+0x150] = CharacterMovement, +0x12d0, written by FUN_14216c84e from CMC.Velocity, which is 0 on a kinematically placed copy) gets the host velocity, so the current, past and predicted samples carry the host motion for every PoseSearch class; 0 = the builder reads the copy's own zero velocity)", cpv);
            }
            int pr = fi("natprops", 1) == 1 ? 1 : 0;
            if (pr != g_props)
            {
                g_props = pr;
                s2nat::cfg_props(pr);
                logf("NOLA natprops: natprops=%d (1 = the host sends whether each NPC holds contextual-action props (unit+0x678 attachment map, cb bits 21/22); a copy that still holds props 0.6 s after the host has none destroys them, so montage attach notifies that never reached their detach no longer leave items on copies)", pr);
            }
            int dfx = fi("natdupfix", 1) == 1 ? 1 : 0;
            if (dfx != g_dupfix)
            {
                g_dupfix = dfx;
                s2nat::cfg_dupfix(dfx);
                logf("NOLA natdup: natdupfix=%d (1 = um NPC do host tem no maximo um corpo visivel no cliente: quando o corpo nativo da uid aparece depois da copia criada, a copia sai e o corpo nativo e adotado; se a adocao e recusada a copia sai e o nativo fica visivel; um nativo morto com o NPC vivo no host e escondido; o censo nao conta um corpo nativo escondido como duplicata da copia)", dfx);
            }
            int wl = fi("natwpnlocal", 1) == 1 ? 1 : 0;
            if (wl != g_wpnl)
            {
                g_wpnl = wl;
                s2nat::cfg_wpnlocal(wl);
                logf("NOLA natwpnlocal: natwpnlocal=%d (1 = adopted copies refuse the game's own ChangeMainHandWeapon calls, only the host-driven weapon swap runs)", wl);
            }
            bool cli = !is_host() && coop();
            int ci = cli && fi("clinert", 1) == 1 ? 1 : 0, cs = cli && fi("cldmgsrc", 1) == 1 ? 1 : 0;
            s2nat::cfg_inert(ci, cs, ci || cs ? local_pawn() : 0, ci || cs ? addr(m01::puppet()) : 0, ci || cs ? addr(m01::target()) : 0);
            if (ci != g_ci || cs != g_cs)
            {
                g_ci = ci; g_cs = cs;
                logf("NOLA clinert: clinert=%d cldmgsrc=%d (1 = no cliente todo NPC local escondido fica inerte: IA presa, ataque cortado na fonte; dmgsrc = atacante de cada golpe no jogador local)", ci, cs);
            }
            int nv = fi("npcvis", 1) == 1 ? 1 : 0;
            if (nv != g_nv)
            {
                g_nv = nv;
                s2nat::cfg_vis(nv);
                logf("NOLA npcvis: npcvis=%d (1 = a client copy follows the host NPC bHidden, a copy whose host NPC died but stays alive is removed, a hidden local NPC that shows up again is hidden again)", nv);
            }
            int qg = fi("natqg", 1) == 1 ? 1 : 0;
            if (qg != g_qg)
            {
                g_qg = qg;
                s2nat::cfg_qg(qg);
                logf("NOLA natqg: natqg=%d (1 = host natnpc rows carry the quest placeholder guid; the client adopts its own quest actor with the same guid when its uid differs from the host uid)", qg);
            }
            int fdv = fi("natfd", 1) == 1 ? 1 : 0;
            if (fdv != g_fd)
            {
                g_fd = fdv;
                s2nat::cfg_fd(fdv);
                logf("NOLA natfd: natfd=%d (1 = a host NPC that dies before it was listed, or after it left the list, is listed dead and every client makes one copy at the host corpse; 0 = such a corpse never reaches the clients)", fdv);
            }
            int dav = fi("natdeadall", 1) == 1 ? 1 : 0;
            if (dav != g_da)
            {
                g_da = dav;
                s2nat::cfg_deadall(dav);
                logf("NOLA natdeadall: natdeadall=%d (1 = every dead non-quest NPC inside the list radius of a player is listed dead, 2 per scan, so every client makes one copy at the host corpse even for corpses older than the list (hot reload, save load, batch return); 0 = old natfd rule, only a corpse seen alive within 10 s or one lone fresh corpse)", dav);
            }
            int hk = fi("nathotkeep", 1) == 1 ? 1 : 0;
            if (hk != g_hk)
            {
                g_hk = hk;
                s2nat::cfg_hotkeep(hk);
                logf("NOLA nathotkeep: nathotkeep=%d (1 = on a DLL hot reload the client hands its dead spawned copies to the next generation, which adopts them from the host dead rows without a spawn, and releases its live copies and twins; 0 = old behaviour, the old generation leaves every copy in the world and the new one spawns a second body)", hk);
            }
            int dk = fi("natdeadkeep", 1) == 1 ? 1 : 0;
            if (dk != g_dk)
            {
                g_dk = dk;
                s2nat::cfg_deadkeep(dk);
                logf("NOLA natdeadkeep: natdeadkeep=%d (1 = every dead copy body is in the clhostgc keep list, so the client's stock corpse test keeps it online and only the host decides when it leaves; 0 = keep list holds loot corpses only)", dk);
            }
            int dp = fi("natdeadpos", 1) == 1 ? 1 : 0;
            if (dp != g_dp)
            {
                g_dp = dp;
                s2nat::cfg_deadpos(dp);
                logf("NOLA natdeadpos: natdeadpos=%d (1 = the host sends the pelvis position of a dead NPC, a client dead copy lying farther than its body length from it is re-made at the host corpse, a local actor flagged Garbage or Unreachable is no local body so a copy lost far away is re-made, and a released dead copy is hidden; 0 = old behaviour)", dp);
            }
            int wd = fi("natwide", 1) == 1 ? 1 : 0;
            if (wd != g_wd)
            {
                g_wd = wd;
                s2nat::cfg_wide(wd);
                logf("NOLA natwide: natwide=%d (1 = the host lists every live NPC within natr of each player with no count cap, so every host NPC near a player has a client copy; 0 = old cap of the 8 nearest)", wd);
            }
            int fr = fi("natfar", 1) == 1 ? 1 : 0;
            double frd = fd("natfardead", 4500.0);
            if (fr != g_far || frd != g_farDead)
            {
                g_far = fr; g_farDead = frd;
                s2nat::cfg_far(fr, frd);
                logf("NOLA natfar: natfar=%d natfardead=%.0f (1 = the host lists a corpse while its actor exists; the client makes a copy only inside the radius where its own game keeps the model, 0.9 x the live A-Life radius for a live NPC and natfardead cm for a corpse (stock corpse ring 50 m, CorpseOfflineTimeConditionSquaredDistance); a copy the game destroys beyond that radius does not count toward natlostmax and is made again when the player comes back; 0 = natdead host timer, every loss counts)", fr, frd);
            }
            int rh = fi("natrhost", 1) == 1 ? 1 : 0;
            if (rh != g_rh)
            {
                g_rh = rh;
                s2nat::cfg_rhost(rh);
                logf("NOLA natrhost: natrhost=%d (1 = host lists every NPC within its own live A-Life offline radius of the host player or of a client, and the client drops a copy as soon as a host snapshot leaves it out; 0 = fixed natr around clients, natlost timer)", rh);
            }
            if (is_host()) return;
            if (!(br_on() && g_brAt >= 0 && now() - g_brAt < 0.35)) client(0);
        }

        bool guid_listed(const std::string& hex)
        {
            if (hex.size() != 32) return false;
            Ref cm = find_path(L"/Script/Stalker2.Default__CppMediator");
            if (!cm) return false;
            int32_t g[4];
            for (int i = 0; i < 4; ++i) g[i] = (int32_t)(uint32_t)std::strtoul(hex.substr(i * 8, 8).c_str(), nullptr, 16);
            Call c(cm, L"GetActorByGuid");
            c.raw(L"TargetGUID", g, sizeof(g));
            if (!c.run()) return false;
            Ref a = c.ro();
            return a.valid() && s2nat::listed_addr(a.address());
        }

        void load_classes()
        {
            std::vector<TsvRow> rows;
            std::string e;
            if (!data_tsv("nat_classes.tsv", rows, &e)) { logf("NOLA natnpc: nat_classes.tsv ilegivel (%s): so a classe natcls", e.c_str()); return; }
            std::map<std::string, s2nat::ClsRow> by;
            for (const TsvRow& r : rows)
            {
                if (r.key.size() < 2) continue;
                s2nat::ClsRow& c = by[r.key[0]];
                c.cls = r.key[0];
                if (r.key[1] == "kind") c.kind = r.v;
                else if (r.key[1] == "sid") c.sid = r.v;
                else if (r.key[1] == "hpmax") c.hpmax = std::atof(r.v.c_str());
                else if (r.key[1] == "walk") c.walk = std::atof(r.v.c_str());
                else if (r.key[1] == "run") c.run = std::atof(r.v.c_str());
                else if (r.key[1] == "sprint") c.sprint = std::atof(r.v.c_str());
                else if (r.key[1] == "invis") c.invis = std::atof(r.v.c_str());
            }
            std::vector<s2nat::ClsRow> v;
            int mut = 0;
            int spd = 0, inv = 0;
            for (auto& [k, c] : by) { v.push_back(c); mut += c.kind != "human"; spd += c.kind != "human" && c.walk > 0 && c.run > 0; inv += c.invis > 0; }
            s2nat::classes(v);
            logf("NOLA natnpc: %d classes de nat_classes.tsv (%d mutantes, %d com velocidades, %d com invisibilidade)", (int)v.size(), mut, spd, inv);
        }

        void init()
        {
            s2n_nat_install();
            load_classes();
            own_tag(Mod::x0, "NN", [] { return host_out(); });
            own_tag_view(Mod::x0, "NNV", [](int v) { return host_view_out(v); });
            every(Mod::x0, "natCl", Rate::t100, [] { tick100(); }, ord::t100_natCl);
            every(Mod::x0, "natFeed", Rate::frame, [] { nat_feed_tick(); });
            auto& h = s2m_quest::hooks();
            h.qi_npcadd = [](std::string_view sid) { return s2nat::qi_npcadd(std::string(sid)); };
            h.qr_npc = [](std::string_view sid) -> std::optional<std::string> {
                std::string g;
                if (!s2nat::qr_npc(std::string(sid), g)) return std::nullopt;
                return g;
            };
            h.nat_guid_listed = [](const std::string& guid) { return guid_listed(guid); };
            m01::add_own_check(Mod::x0, [](const Ref& r) { return r.valid() && s2nat::own(r.address()); });
            s2m::damage::links().nd_on = [] { return s2nat::nd_on() == 1; };
            s2m::damage::links().last_cause = [](double& age) { return s2nat::last_cause(age); };
            auto& dl = s2m::damage::links();
            dl.ff_bodies = [](bool on, const std::vector<s2m::damage::FfBody>& b) {
                std::vector<s2nat::FfBody> o;
                for (const auto& x : b)
                    if (x.body.valid()) o.push_back({x.view, x.pid, x.body.address(), x.puppet ? 1 : 0});
                s2nat::ff_bodies(on ? 1 : 0, o);
            };
            dl.ff_take = [] {
                std::vector<s2m::damage::FfHit> o;
                for (const s2nat::FfHit& h : s2nat::ff_take())
                {
                    s2m::damage::FfHit x;
                    x.view = h.view;
                    x.pid = h.pid;
                    x.puppet = h.puppet != 0;
                    for (int k = 0; k < 5; ++k) x.f[k] = h.f[k];
                    x.ty = h.ty;
                    x.src = h.src;
                    x.b7a = h.b7a;
                    x.b7b = h.b7b;
                    x.bn = h.bn;
                    for (int k = 0; k < 6; ++k) x.v[k] = h.v[k];
                    x.bone = h.bone;
                    o.push_back(std::move(x));
                }
                return o;
            };
            dl.ff_apply = [](const Ref& pw, const s2m::damage::FfHit& h) {
                if (!pw.valid()) return 0;
                s2nat::FfHit x;
                for (int k = 0; k < 5; ++k) x.f[k] = (float)h.f[k];
                x.ty = (uint8_t)h.ty;
                x.src = (uint8_t)h.src;
                x.b7a = (uint8_t)h.b7a;
                x.b7b = (uint8_t)h.b7b;
                x.bn = (uint32_t)h.bn;
                for (int k = 0; k < 6; ++k) x.v[k] = h.v[k];
                x.bone = h.bone;
                return s2nat::ff_apply(pw.address(), x);
            };
            dl.ff_stat = [](bool& any) {
                return s2nat::ff_status(any);
            };
            dl.md_cfg = [](bool on) { s2nat::md_cfg(on ? 1 : 0); };
            dl.md_take = [] {
                std::vector<s2m::damage::MdHit> o;
                for (const s2nat::MdHit& h : s2nat::md_take())
                {
                    s2m::damage::MdHit x;
                    x.uid = h.uid;
                    for (int k = 0; k < 5; ++k) x.f[k] = h.f[k];
                    x.ty = h.ty;
                    x.src = h.src;
                    x.b7a = h.b7a;
                    x.b7b = h.b7b;
                    x.bn = h.bn;
                    for (int k = 0; k < 6; ++k) x.v[k] = h.v[k];
                    x.bone = h.bone;
                    o.push_back(std::move(x));
                }
                return o;
            };
            dl.md_find = [](int64_t uid) -> Ref {
                if (uid <= 0 || uid > 0x7FFFFFFF) return Ref{};
                uint64_t a = s2nat::md_find((int32_t)uid);
                return a ? Ref::addr(a) : Ref{};
            };
            dl.md_apply = [](const Ref& npc, const s2m::damage::MdHit& h, int64_t atk) {
                if (!npc.valid()) return 0;
                s2nat::MdHit x;
                x.uid = (int32_t)h.uid;
                for (int k = 0; k < 5; ++k) x.f[k] = (float)h.f[k];
                x.ty = (uint8_t)h.ty;
                x.src = (uint8_t)h.src;
                x.b7a = (uint8_t)h.b7a;
                x.b7b = (uint8_t)h.b7b;
                x.bn = (uint32_t)h.bn;
                for (int k = 0; k < 6; ++k) x.v[k] = h.v[k];
                x.bone = h.bone;
                return s2nat::md_apply(npc.address(), x, atk);
            };
            dl.md_stat = [](bool& any) { return s2nat::md_status(any); };
            status(Mod::x0, [] { return s2nat::b164_status(); });
        }
        ::x0::ModReg g_reg(Mod::x0, "nat", &init);
    }

    void nat_after_read()
    {
        if (is_host() || !br_on()) return;
        client(1);
    }

    void nat_feed_tick()
    {
        if (!port(Mod::x0)) return;
        bool mb = s2r::x0_mailbox();
        s2nat::feed_mode(mb);
        if (!mb) return;
        auto& B = bridge();
        std::string t;
        if (!s2r::x0_in(t)) return;
        B.mb_last = t;
        size_t z = t.find('\x1a');
        if (z != std::string::npos) t.resize(z);
        s2nat::feed(t);
    }
}

namespace x0
{
    BodiesV& bodies_v()
    {
        static BodiesV b;
        return b;
    }
}

extern "C" bool s2x_nd_send_ok() { return !x0::port(x0::Mod::damage) || s2m::damage::nd_send_allowed(); }
extern "C" bool s2x_nd_apply_ok() { return !x0::port(x0::Mod::damage) || s2m::damage::nd_apply_allowed(); }
extern "C" void s2x_nd_sent(double amount) { if (x0::port(x0::Mod::damage)) s2m::damage::nd_sent(amount); }
extern "C" void s2x_nd_applied(double amount, double hp0, double hp1) { if (x0::port(x0::Mod::damage)) s2m::damage::nd_applied(amount, hp0, hp1); }
