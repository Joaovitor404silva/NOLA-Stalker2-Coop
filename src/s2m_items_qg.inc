        void qgLoad()
        {
            Qg& Q = S().qg;
            if (Q.loaded) return;
            Q.loaded = true;
            Q.mine.clear();
            std::string text;
            if (E().readText("qshare_mine.txt", text))
            {
                for (auto& l : lines(text))
                {
                    std::string k = stripWs(l);
                    if (!k.empty()) Q.mine.insert(k);
                }
                Q.fresh = false;
            }
            else
            {
                Q.fresh = true;
                E().writeText("qshare_mine.txt", "");
            }
            LOG(sf("NOLA qshare: %d marcas lidas de qshare_mine.txt%s", (int)Q.mine.size(), Q.fresh ? " (arquivo novo: as chaves que ja estao no livro agora contam como antigas)" : ""));
        }
        void qgMark(const std::string& tag, const std::string& k)
        {
            Qg& Q = S().qg;
            std::string m = tag + "|" + k;
            if (!Q.mine.insert(m).second) return;
            E().appendText("qshare_mine.txt", m + "\n");
        }
        void qgReset(const char* why)
        {
            Qg& Q = S().qg;
            Q.ix = 0;
            Q.led.clear();
            Q.todo.clear();
            Q.seenC.clear();
            Q.win.reset();
            Q.g.reset();
            Q.hold.clear();
            Q.put.clear();
            Q.based = false;
            LOG(std::string("NOLA qshare: livro relido (") + why + ")");
        }
        std::string qgWho() { return HOST() ? std::string("h") : "c" + std::to_string(E().myPid()); }
        std::string qgWhoBase(const std::string& w)
        {
            size_t d = w.find('.');
            return d == std::string::npos ? w : w.substr(0, d);
        }
        std::optional<std::string> qgSrcWin()
        {
            State& st = S();
            if (st.ss.open) return std::nullopt;
            if (st.cl && st.cl->open) return "b" + *st.cl->open;
            if (!st.lock.mine) return std::nullopt;
            const std::string& id = *st.lock.mine;
            if (has(id, "PlayerStash")) return std::nullopt;
            if (auto c = lsCid(id)) return "c" + *c;
            if (auto c = contSidOfId(id)) return "c" + *c;
            return "w" + id;
        }
        bool qgInv(Obj pw, Counts& out)
        {
            out.clear();
            Counts all;
            Qg& Q = S().qg;
            if (E().natItems(pw, 0, true, all))
            {
                for (auto& kv : all)
                    if (kv.second > 0 && qsh(kv.first)) out[kv.first] = kv.second;
                Q.nat = true;
                return true;
            }
            Q.nat = false;
            if (!E().invReadable(pw)) return false;
            for (auto& s : S().qList)
            {
                long long n = 0;
                if (E().count(pw, s, n) && n > 0) out[s] = n;
            }
            return true;
        }
        void qgScan()
        {
            State& st = S();
            Qg& Q = st.qg;
            if (!st.tk.loaded) return;
            if (Q.ix > st.tk.list.size()) qgReset("lista de tiradas encolheu");
            bool old = Q.fresh;
            Q.fresh = false;
            while (Q.ix < st.tk.list.size())
            {
                const std::string& k = st.tk.list[Q.ix++];
                std::string kind, src, sid, who;
                long long n = 0, tot = 0;
                if (!qgParse(k, kind, src, sid, n, tot, who) || kind != "qg") continue;
                Q.led[src + ";" + sid].push_back(k);
                if (old)
                {
                    if (!Q.mine.count("o|" + k)) LOG("NOLA qshare: " + k + ": ja estava no livro quando esta PC comecou sem qshare_mine.txt: conta como antiga, nada criado");
                    qgMark("o", k);
                    continue;
                }
                Q.todo.push_back(k);
            }
        }
        void qgClaim(const std::string& k, int pid)
        {
            if (!qshareOn()) return;
            std::string kind, src, sid, who;
            long long n = 0, tot = 0;
            if (!qgParse(k, kind, src, sid, n, tot, who) || kind != "qc") return;
            Qg& Q = S().qg;
            if (!Q.loaded) qgLoad();
            if (!S().tk.loaded) takenLoad();
            if (Q.seenC.count(k)) return;
            if (Q.seenC.size() > 256) Q.seenC.erase(Q.seenC.begin());
            Q.seenC.insert(k);
            qgScan();
            std::string lk = src + ";" + sid;
            auto f = Q.led.find(lk);
            size_t had = 0;
            if (f != Q.led.end())
            {
                for (auto& e : f->second)
                {
                    std::string k2, s2, i2, w2;
                    long long n2 = 0, t2 = 0;
                    if (qgParse(e, k2, s2, i2, n2, t2, w2) && qgWhoBase(w2) == who) ++had;
                }
                if (had == 0)
                {
                    Q.dups += 1;
                    LOG(sf("NOLA qshare: pedido de %s (pid %d) para %s x%lld de %s recusado: %s ja vale (o primeiro vence; a copia que ele pegou no proprio mundo fica com ele)", who.c_str(), pid, sid.c_str(), n,
                           src.c_str(), f->second.front().c_str()));
                    return;
                }
            }
            std::string g = qgKey("qg", src, sid, n, tot, who + "." + std::to_string(had + 1));
            takenAdd(g);
            LOG(sf("NOLA qshare: livro do host: %s x%lld de %s pego por %s (pid %d): os outros jogadores recebem pela criacao do jogo", sid.c_str(), n, src.c_str(), who.c_str(), pid));
            qgScan();
        }
        void qgClaimMine(const std::string& src, const std::string& sid, long long n, long long tot)
        {
            State& st = S();
            Qg& Q = st.qg;
            qgMark("c", src + ";" + sid);
            Q.claims += 1;
            std::string k = qgKey("qc", src, sid, n, tot, qgWho());
            LOG(sf("NOLA qshare: item de missao pego aqui: %s x%lld de %s (agora %lld); %s", sid.c_str(), n, src.c_str(), tot, HOST() ? "no livro do host" : "pedido ao host"));
            if (HOST()) qgClaim(k, 0);
            else st.tList.push_back(TOut{k, NOW() + F("resend", 6)});
        }
        bool qgRelease(const std::string& src, const std::string& sid, long long n, const std::optional<std::string>& win)
        {
            if (src.empty() || src[0] == 'b' || src[0] == 'g') return true;
            if (!win || *win != src) return false;
            Obj w = lootWin();
            Counts wc;
            if (!VAL(w) || !E().natItems(w, 1, true, wc)) return false;
            long long r = std::min(n, g0(wc, sid));
            long long done = 0;
            if (r > 0 && E().natPut(w, sid, -r, done)) E().natRefresh(w);
            S().qg.rms += done < 0 ? -done : done;
            LOG(sf("NOLA qshare: %s x%lld tirado da minha copia de %s (janela aberta; outro jogador ja pegou)", sid.c_str(), r, src.c_str()));
            return true;
        }
        void qgApply(Counts& inv, const std::optional<std::string>& win)
        {
            State& st = S();
            Qg& Q = st.qg;
            std::vector<std::string> keep;
            for (auto& k : Q.todo)
            {
                std::string kind, src, sid, who;
                long long n = 0, tot = 0;
                if (!qgParse(k, kind, src, sid, n, tot, who)) continue;
                std::string lk = src + ";" + sid;
                if (Q.mine.count("c|" + lk) || Q.mine.count("o|" + k))
                {
                    if (Q.mine.count("c|" + lk) && !Q.mine.count("s|" + k))
                    {
                        qgMark("s", k);
                        LOG("NOLA qshare: " + k + ": eu mesmo peguei este item desta fonte: nada criado, nada tirado");
                    }
                    continue;
                }
                bool gd = Q.mine.count("g|" + k) != 0;
                if (!gd)
                {
                    long long have = g0(inv, sid);
                    long long g = qgGive(n, tot, have);
                    bool ok = g <= 0 || E().createItem(sid, 0, g);
                    if (ok)
                    {
                        qgMark("g", k);
                        gd = true;
                        if (g > 0)
                        {
                            inv[sid] = have + g;
                            Q.base[sid] = have + g;
                            Q.grants += 1;
                        }
                        LOG(sf("NOLA qshare: %s x%lld de %s pego por %s: eu tinha %lld -> %s", sid.c_str(), n, src.c_str(), qgWhoBase(who).c_str(), have,
                               g > 0 ? sf("+%lld pela criacao do jogo", g).c_str() : "ja tenho o bastante: nada criado"));
                    }
                    else LOG(sf("NOLA qshare: %s x%lld: criacao falhou (tento de novo)", sid.c_str(), g));
                }
                bool rd = Q.mine.count("r|" + k) != 0;
                if (!rd && qgRelease(src, sid, n, win))
                {
                    qgMark("r", k);
                    rd = true;
                }
                if (!gd || !rd) keep.push_back(k);
            }
            Q.todo = keep;
        }
        void qgTick()
        {
            if (!qshareOn()) return;
            State& st = S();
            Qg& Q = st.qg;
            if (!Q.loaded) qgLoad();
            if (!st.tk.loaded) takenLoad();
            qgScan();
            double now = NOW();
            Obj pw = E().pawn();
            if (!VAL(pw) || !E().worldOpen()) return;
            auto win = qgSrcWin();
            if (!win && !Q.win && Q.hold.empty() && Q.todo.empty() && now < Q.at) return;
            Q.at = now + (Q.nat ? F("qgsec", 0.5) : F("qgslow", 2));
            uint64_t a = E().addr(pw);
            if (!Q.pAddr || *Q.pAddr != a)
            {
                bool first = !Q.pAddr;
                Q.pAddr = a;
                Q.based = false;
                Q.waitT = first ? now : now + F("qgloadw", 8);
                if (!first) LOG("NOLA qshare: pawn novo (save carregado / morte / respawn): itens de missao relidos em " + numStr(F("qgloadw", 8)) + " s; nada enviado");
            }
            if (now < Q.waitT)
            {
                Q.win.reset();
                return;
            }
            Counts inv;
            if (!qgInv(pw, inv)) return;
            Counts wc;
            bool hasW = false;
            if (win)
            {
                Obj w = lootWin();
                hasW = VAL(w) && E().natItems(w, 1, true, wc);
            }
            if (!Q.based)
            {
                Q.base = inv;
                Q.based = true;
                Q.put.clear();
                Q.hold.clear();
                Q.win.reset();
                LOG(sf("NOLA qshare: inventario base lido (%d itens de missao, leitura %s)", (int)inv.size(), Q.nat ? "nativa" : "por contagem"));
                qgApply(inv, win);
                return;
            }
            if (win && Q.win && Q.win->src != *win) Q.win.reset();
            std::set<std::string> all;
            for (auto& kv : inv) all.insert(kv.first);
            for (auto& kv : Q.base) all.insert(kv.first);
            double qw = F("qgwin", 2);
            for (auto& sid : all)
            {
                long long cur = g0(inv, sid), b = g0(Q.base, sid);
                if (cur == b)
                {
                    Q.hold.erase(sid);
                    continue;
                }
                if (cur < b)
                {
                    if (win && hasW && Q.win && Q.win->hasC)
                    {
                        long long up = g0(wc, sid) - g0(Q.win->c, sid);
                        if (up > 0) Q.put[sid] += std::min(b - cur, up);
                    }
                    Q.hold.erase(sid);
                    if (cur > 0) Q.base[sid] = cur;
                    else Q.base.erase(sid);
                    continue;
                }
                long long d = cur - b;
                long long use = std::min(d, g0(Q.put, sid));
                if (use > 0)
                {
                    Q.put[sid] -= use;
                    d -= use;
                    LOG(sf("NOLA qshare: %s +%lld de volta do lugar onde eu mesmo o pus: nao compartilhado", sid.c_str(), use));
                }
                std::optional<std::string> src;
                long long n = d;
                if (d > 0 && win && Q.win && Q.win->src == *win)
                {
                    if (hasW && Q.win->hasC)
                    {
                        long long dn = g0(Q.win->c, sid) - g0(wc, sid);
                        if (dn > 0)
                        {
                            src = *win;
                            n = std::min(d, dn);
                        }
                    }
                    else src = *win;
                }
                else if (d > 0 && !win && Q.win && now - Q.win->at <= qw && (!Q.win->hasC || g0(Q.win->c, sid) > 0))
                {
                    src = Q.win->src;
                    if (Q.win->hasC) n = std::min(d, g0(Q.win->c, sid));
                }
                else if (d > 0 && Q.g && now - Q.g->second <= qw)
                {
                    src = Q.g->first;
                    Q.g.reset();
                }
                if (d > 0 && !src)
                {
                    auto h = Q.hold.find(sid);
                    if (h == Q.hold.end())
                    {
                        Q.hold[sid] = now;
                        continue;
                    }
                    if (now - h->second <= qw) continue;
                }
                Q.hold.erase(sid);
                Q.base[sid] = cur;
                if (d <= 0) continue;
                if (!src)
                {
                    Q.nosrc += 1;
                    LOG(sf("NOLA qshare: %s +%lld aqui sem fonte de saque (missao, dialogo, troca ou item que alguem largou): nao compartilhado aqui", sid.c_str(), d));
                    continue;
                }
                qgClaimMine(*src, sid, n, cur);
            }
            if (win) Q.win = QgWin{*win, wc, now, hasW};
            else if (Q.win && now - Q.win->at > qw) Q.win.reset();
            qgApply(inv, win);
        }
