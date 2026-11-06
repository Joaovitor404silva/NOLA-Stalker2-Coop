"""Compile actual receiver and menu code with platform IO replaced, plus lock/compass tests."""
from pathlib import Path
import subprocess, tempfile
R = Path(__file__).resolve().parents[2]
def run(args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)
with tempfile.TemporaryDirectory() as td:
    t = Path(td)
    s = (R/'src/s2r_net.cpp').read_text()
    inc = s[s.index('#include <algorithm>'):s.index('#pragma comment')]
    fmt = s[s.index('    static std::string fmt'):s.index('    double clock_s()')]
    codec = s[s.index('    namespace js'):s.index('    namespace fs')]
    helpers = s[s.index('    static bool py_space'):s.index('    static bool read_text_strip')] + s[s.index('    static bool ascii_digits'):s.index('    namespace proto')]
    protocol = s[s.index('    namespace proto'):s.index('        struct SxError')] + '\n}\n'
    num = s[s.index('        V num_or('):s.index('        std::string roster_json')]
    receiver = s[s.index('        struct CView'):s.index('    struct Net::Impl')]
    source = '#include "s2r_net.h"\n#include <cassert>\n#include <iostream>\n' + inc
    source += '\nnamespace s2r {\n' + fmt + codec + helpers + protocol
    source += '\nnamespace { using js::V;\n' + num + receiver + '\n}\n'
    source += (R/'tests/hardening/test_client_rx.inc').read_text()
    (t/'receiver.cpp').write_text(source)
    run(['g++', '-std=c++20', '-O1', '-g', '-fsanitize=address,undefined', '-I'+str(R/'src'), t/'receiver.cpp', '-o', t/'receiver'])
    run([t/'receiver'])
    run(['g++', '-std=c++20', '-O1', '-I'+str(R/'src'), R/'tests/pcompass/test_pcompass.cpp', '-o', t/'compass'])
    run([t/'compass'])
    world = (R/'src/s2m_world.cpp').read_text()
    body = world[world.index('void World::lock_receive('):world.index('void World::frame()')]
    (t/'door.cpp').write_text((R/'tests/hardening/test_door_lock.inc').read_text().replace('// PRODUCTION_LOCK_RECEIVE', body))
    run(['g++', '-std=c++20', '-O1', t/'door.cpp', '-o', t/'door'])
    run([t/'door'])
    menu = (R/'src/s2m_menu.cpp').read_text()
    body = menu[menu.index('    void settle_done('):menu.index('    std::wstring last_file()')]
    (t/'menu.cpp').write_text((R/'tests/hardening/test_menu_settle.inc').read_text().replace('// PRODUCTION_SETTLE', body))
    run(['g++', '-std=c++20', '-O1', '-I'+str(R/'src'), t/'menu.cpp', '-o', t/'menu'])
    run([t/'menu'])
    run(['g++', '-std=c++20', '-O1', '-DS2M_LOCK_TEST', '-I'+str(R/'src'), R/'tests/lock/test_lock.cpp', R/'src/s2m_lock.cpp', R/'src/s2m_pop.cpp', '-o', t/'lock'])
    for name in ['restore_retry', 'vendor_recognised_late', 'vendor_dialog_late']:
        log = run([t/'lock', R/'data', R/'tests/lock'/f'{name}.txt'], capture_output=True, text=True).stdout
        if name == 'restore_retry':
            assert log.index('A npc n8 active 1') > log.index('A npc n8 restore failed')
        elif name == 'vendor_recognised_late':
            assert log.index('A npc trader active 1') > log.index('A npc trader active 0')
            assert log.count('A npc trader active 0') == 1
            assert 'A npc native_off active 1' not in log
        else:
            assert 'vendor recognised after dialogue opened' in log
            assert 'UIDialogClose' not in log
        print('PASS:', name)

    for name in ['guest_trader', 'guest_technician', 'guest_story', 'guest_scene', 'guest_vendor_late', 'guest_trade_failure', 'host_trade_unchanged']:
        log = run([t/'lock', R/'data', R/'tests/lock'/f'{name}.txt'], capture_output=True, text=True).stdout
        if name in ['guest_story', 'guest_scene', 'host_trade_unchanged']:
            assert 'A open ' not in log, log
        else:
            expected = 'Upgrade' if name == 'guest_technician' else 'Trade'
            assert log.count('A open '+expected) == 1, log
        if name == 'guest_trade_failure':
            assert 'trade request failed; dialogue answers restored' in log, log
        print('PASS:', name)
    assert 'svendtrade\ti\t1' in (R/'data/flags_role_client.tsv').read_text()
    run(['g++', '-std=c++20', '-O1', R/'tests/pop/test_pop.cpp', R/'src/s2m_pop.cpp', '-o', t/'pop'])
    run([t/'pop', R/'data'])
