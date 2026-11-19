"""Compile the production native damage entry with a fake engine (no game needed)."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "src/s2n_nat.cpp").read_text()
start = source.index("    void dd_detour(void* ent, void* dd, char p3)")
end = source.index("    auto dd_bone_ok", start)
entry = source[start:end]
prelude = r'''
#include "s2m_hitshield.h"
#include <cassert>
#include <cstdint>
using LONG = long;
using DmgFn = void (*)(void*, void*, char);
int hp = 100, gear = 100, hits = 0;
void engine(void*, void*, char) { --hp; --gear; ++hits; }
uintptr_t g_ddTramp = reinterpret_cast<uintptr_t>(&engine);
int t_killBypass = 0;
uintptr_t t_killEnt = 0, g_ddTgt = 0, g_ddPup = 0;
int g_dsOn = 0, g_ffN = 0, g_cpN = 0, g_mdOn = 0, g_cpOn = 0;
uint32_t g_ddTgtId = 0;
uint64_t g_ddUpdAt = 0;
LONG g_killBypassHit = 0, g_dsDropped = 0, g_ddPupN = 0;
LONG g_cpSeen = 0, g_cpFlinch = 0, g_cpDrop = 0;
struct { bool natdmg = false; } C;
void InterlockedIncrement(volatile LONG* n) { *n = *n + 1; }
uint32_t dd_id_seh(uintptr_t e) { return static_cast<uint32_t>(e); }
uint32_t pl_id() { return 42; }
uint64_t GetTickCount64() { return 0; }
void ds_capture(const uint8_t*) {}
bool ds_local_drop(const uint8_t*) { return false; }
int ff_slot(uintptr_t) { return -1; }
bool ff_capture(int, const uint8_t*) { return false; }
void dd_capture(const uint8_t*) {}
int cp_slot(uintptr_t) { return -1; }
void md_capture(int, const uint8_t*) {}
bool cp_shrink_seh(uint8_t*, float&) { return false; }
void cp_restore_seh(uint8_t*, float) {}
void lh_note_seh(uintptr_t, const uint8_t*) {}
'''
tests = r'''
int main() {
    using namespace s2m::hitshield;
    clear();
    dd_detour(reinterpret_cast<void*>(42), nullptr, 0);
    assert(hits == 1 && gear == 99);
    renew(42, clock_ms());
    for (int i = 0; i < 100; ++i)
        dd_detour(reinterpret_cast<void*>(42), nullptr, 0);
    assert(hits == 1 && hp == 99 && gear == 99); // no HP OR gear damage
    dd_detour(reinterpret_cast<void*>(43), nullptr, 0);
    assert(hits == 2); // unrelated actor is unaffected
    t_killBypass = 1; t_killEnt = 42;
    dd_detour(reinterpret_cast<void*>(42), nullptr, 0);
    assert(hits == 3); // explicit native kill bypass remains effective
    t_killBypass = 0;
    clear();
    dd_detour(reinterpret_cast<void*>(42), nullptr, 0);
    assert(hits == 4); // damage resumes after release
    renew(0, 100); // player UID zero is valid
    assert(blocks(0, 100) && blocks(0, 599));
    assert(!blocks(0, 600) && !blocks(1, 100));
    renew(42, UINT32_MAX - 100);
    assert(blocks(42, 0) && !blocks(42, 399)); // clock wrap
    renew(0, UINT32_MAX - 499);
    assert(blocks(0, UINT32_MAX)); // zero deadline and zero UID are not an empty snapshot
    clear();
    assert(!blocks(42, 0));
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "hitshield.cpp"
    exe = Path(tmp) / "hitshield"
    cpp.write_text(prelude + entry + tests)
    subprocess.run(["g++", "-std=c++20", "-Wall", "-Wextra", "-fsanitize=address,undefined",
                    "-I" + str(ROOT / "src"), str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS native damage shield: gear/HP, identity, kill bypass, release, expiry, clock wrap")
