    struct AgData { volatile uint32_t tid; volatile uint8_t on; uint8_t pad[3]; volatile LONG hits; uint32_t pad2; volatile uint32_t tidx[3]; };
    constexpr int kAgTids = 4;
    struct AgEmit
    {
        uint8_t* p;
        void b(std::initializer_list<uint8_t> v) { for (uint8_t x : v) *p++ = x; }
        void rel(const uint8_t* t, int tail = 0) { int32_t r = (int32_t)(t - (p + 4 + tail)); std::memcpy(p, &r, 4); p += 4; }
        uint8_t* j8() { *p++ = 0; return p - 1; }
        void at(uint8_t* d) { *d = (uint8_t)(p - (d + 1)); }
    };
    struct AgStubs { uint8_t* a; uint8_t* b; uint8_t* c; };
    inline auto ag_tid_at(AgData* d, int k) -> uint8_t*
    {
        return k == 0 ? reinterpret_cast<uint8_t*>(const_cast<uint32_t*>(&d->tid)) : reinterpret_cast<uint8_t*>(const_cast<uint32_t*>(&d->tidx[k - 1]));
    }
    inline auto ag_set(AgData* d, const uint32_t* ids, int n) -> void
    {
        if (n <= 0) { d->on = 0; d->tid = 0xFFFFFFFFu; for (int k = 0; k < kAgTids - 1; ++k) d->tidx[k] = 0xFFFFFFFFu; return; }
        d->on = 0;
        d->tid = ids[0];
        for (int k = 1; k < kAgTids; ++k) d->tidx[k - 1] = ids[k < n ? k : 0];
        d->on = 1;
    }
    inline auto ag_emit(uint8_t* st, uint8_t* base, AgData* d) -> AgStubs
    {
        uint8_t* Pid = base + 0x9eda140;
        uint8_t* on = reinterpret_cast<uint8_t*>(const_cast<uint8_t*>(&d->on));
        uint8_t* hits = reinterpret_cast<uint8_t*>(const_cast<LONG*>(&d->hits));
        AgEmit e{st};
        AgStubs s{};
        s.a = e.p;
        e.b({0x3B, 0x05}); e.rel(Pid);
        e.b({0x0F, 0x84}); e.rel(base + 0xaeefbe);
        e.b({0x80, 0x3D}); e.rel(on, 1); e.b({0x00});
        e.b({0x74}); uint8_t* aNo = e.j8();
        for (int k = 0; k < kAgTids; ++k)
        {
            e.b({0x3B, 0x05}); e.rel(ag_tid_at(d, k));
            e.b({0x0F, 0x84}); e.rel(base + 0xaeefbe);
        }
        e.at(aNo);
        e.b({0xE9}); e.rel(base + 0xaeea4f);
        s.b = e.p;
        e.b({0x44, 0x8B, 0x25}); e.rel(Pid);
        e.b({0x66, 0x45, 0x0F, 0x57, 0xFF});
        e.b({0x44, 0x39, 0xE7});
        e.b({0x0F, 0x84}); e.rel(base + 0xaeec19);
        e.b({0x80, 0x3D}); e.rel(on, 1); e.b({0x00});
        e.b({0x74}); uint8_t* bNp = e.j8();
        uint8_t* bHit[kAgTids - 1];
        for (int k = 0; k < kAgTids - 1; ++k) { e.b({0x3B, 0x3D}); e.rel(ag_tid_at(d, k)); e.b({0x74}); bHit[k] = e.j8(); }
        e.b({0x3B, 0x3D}); e.rel(ag_tid_at(d, kAgTids - 1));
        e.b({0x75}); uint8_t* bNp2 = e.j8();
        for (uint8_t* h : bHit) e.at(h);
        e.b({0xF0, 0xFF, 0x05}); e.rel(hits);
        e.b({0xE9}); e.rel(base + 0xaeec19);
        e.at(bNp); e.at(bNp2);
        e.b({0x66, 0x45, 0x0F, 0x28, 0xF8});
        e.b({0xE9}); e.rel(base + 0xaeec19);
        s.c = e.p;
        e.b({0x31, 0xC0, 0x44, 0x39, 0xE7});
        e.b({0x74}); uint8_t* c1 = e.j8();
        e.b({0x80, 0x3D}); e.rel(on, 1); e.b({0x00});
        e.b({0x74}); uint8_t* c0a = e.j8();
        uint8_t* cHit[kAgTids - 1];
        for (int k = 0; k < kAgTids - 1; ++k) { e.b({0x3B, 0x3D}); e.rel(ag_tid_at(d, k)); e.b({0x74}); cHit[k] = e.j8(); }
        e.b({0x3B, 0x3D}); e.rel(ag_tid_at(d, kAgTids - 1));
        e.b({0x75}); uint8_t* c0b = e.j8();
        e.at(c1); for (uint8_t* h : cHit) e.at(h);
        e.b({0xB0, 0x01});
        e.at(c0a); e.at(c0b);
        e.b({0x31, 0xD2, 0x3B, 0x0D}); e.rel(Pid);
        e.b({0x74}); uint8_t* d1 = e.j8();
        e.b({0x80, 0x3D}); e.rel(on, 1); e.b({0x00});
        e.b({0x74}); uint8_t* d0a = e.j8();
        uint8_t* dHit[kAgTids - 1];
        for (int k = 0; k < kAgTids - 1; ++k) { e.b({0x3B, 0x0D}); e.rel(ag_tid_at(d, k)); e.b({0x74}); dHit[k] = e.j8(); }
        e.b({0x3B, 0x0D}); e.rel(ag_tid_at(d, kAgTids - 1));
        e.b({0x75}); uint8_t* d0b = e.j8();
        e.at(d1); for (uint8_t* h : dHit) e.at(h);
        e.b({0xB2, 0x01});
        e.at(d0a); e.at(d0b);
        e.b({0xE9}); e.rel(base + 0xaeecfc);
        return s;
    }
