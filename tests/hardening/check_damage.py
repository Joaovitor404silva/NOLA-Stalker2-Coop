from pathlib import Path
import sys
p=Path(sys.argv[1])
logs=list(p.glob('no_bleedout*.log'))
assert logs, 'Missing downed-state test output'
for f in logs:
    a=f.read_text(errors='replace')
    assert a.count('caido=true') >= 2, f'{f.name}: not down at both 31 and 600 seconds'
    assert 'caido=false' in a and 'DE PE (' in a, f'{f.name}: did not revive'
    assert 'FIM DE JOGO PARA TODOS' not in a, f'{f.name}: unexpected team wipe'
b=(p/'all_down.log').read_text(errors='replace')
assert 'FIM DE JOGO PARA TODOS' in b, 'All-down team wipe missing'
assert '#GO:1,alldown' in b, 'Incorrect wipe reason'
print(f'PASS: {len(logs)} downed-state cases; no timed death/stand-up, late revive; all-down team wipe')

for name in ['nearest','farther_alive','release','blocked','standing','no_target','stale','four_seconds']:
    f=p/('rescue_'+name+'.log')
    assert f.exists(), f'Missing {f.name}'
    text=f.read_text(errors='replace')
    ok=[l for l in text.splitlines() if l.startswith('TELEPORT ok')]
    if name in ['nearest','farther_alive']:
        assert len(ok)==1, (name,ok)
        assert ('1000,120,0' if name=='nearest' else '3000,120,0') in ok[0], ok
    else:
        assert not ok, (name,ok)
    if name=='nearest':
        for n in range(1,6): assert f'UI Rescue in {n} seconds' in text
        assert 'UI Moved beside' in text
    if name=='release': assert 'UI_CLEAR s2m_rescue' in text
    if name=='blocked': assert 'UI Rescue blocked:' in text
    if name=='nearest':
        assert 'caido=true' in text and 'hp=15.0 alive=true' in text
    if name=='blocked': assert text.count('TELEPORT blocked')==8
print('PASS: HOME hold, nearest standing target, release cancellation, blocked destinations, stale/silent exclusion, downed state preserved, no repeat')

if (p/'revive_hold.log').exists():
    hold=(p/'revive_hold.log').read_text()
    assert hold.count('CALL itemRemove P Medkit 1') == 2, 'Holding F must not consume another medkit until release'
    assert 'Press F to revive' in hold and 'Revive requested' in hold
    no=(p/'revive_no_medkit.log').read_text()
    assert 'Cannot revive: you need a medkit' in no and 'CALL itemRemove P Medkit 1' not in no
    retry=(p/'revive_stale_retry.log').read_text()
    assert 'waiting for a fresh teammate update' in retry and 'DE PE (' in retry
    for name in ['revive_stale_timeout', 'revive_session_changed']:
        assert 'DE PE (' not in (p/(name+'.log')).read_text(), name
    assert 'rescuer session changed' in (p/'revive_session_changed.log').read_text()
    print('PASS: revive prompts, no-medkit feedback, held-key consumption guard, stale snapshot retry/expiry and session change cancellation')

for f in logs:
    a=f.read_text(errors='replace')
    assert 'Waiting for revival' in a and 'Revived by ' in a, f.name
    assert 'You die in' not in a and 'You get up in' not in a, f.name
print('PASS: persistent downed notice without death or auto-stand countdown')

notice=(p/'down_notification.log').read_text()
assert notice.count('UI PLAYER 1 is down!') == 2, notice
assert 'Press F to revive' not in notice, 'Out-of-range teammate must not receive interaction prompt'
print('PASS: remote down notice, no per-tick/stale-gap spam, new notice after second down')

refund=(p/'revive_refund.log').read_text()
assert refund.count('CALL itemRefund Medkit') == 1, refund
assert refund.count('OUT inv Medkit=3') == 2, refund
assert 'medkit was returned' in refund
success=(p/'revive_ack_success.log').read_text()
assert 'CALL itemRefund' not in success and 'accepted the revive' in success
for name in ['revive_ack_wrong', 'revive_ack_session']:
    a=(p/(name+'.log')).read_text()
    assert 'CALL itemRefund' not in a, a
    assert a.count('CALL itemRemove') == 1 and 'OUT inv Medkit=2' in a, a
    assert 'result unresolved' in a, a
failed=(p/'revive_refund_failure.log').read_text()
assert failed.count('CALL itemRefund') == 1 and 'medkit was returned' not in failed, failed
assert 'result unresolved' in failed
late=(p/'revive_debit_delayed.log').read_text()
assert 'OUT #RV:0,0,0' in late and 'OUT #RV:1,0,1' in late, late
assert late.count('CALL itemRemove') == 1, late
missing=(p/'revive_debit_unconfirmed.log').read_text()
assert missing.count('CALL itemRemove') == 1 and 'OUT #RV:1' not in missing, missing
assert 'OUT inv Medkit=3' in missing and 'result unresolved' in missing
busy=(p/'revive_competing.log').read_text()
assert '|3,1,0,0' in busy and '|2,1,1,0' in busy and 'DE PE (' in busy, busy
print('PASS: verified debit, delayed debit, rejected revive refund once, accepted revive, wrong sender/sequence/session rejection, missing acknowledgement lock, failed refund no retry, competing rescuers')

single=(p/'revive_refund_legacy_mode.log').read_text()
assert single.count('CALL itemRefund Medkit') == 1 and single.count('OUT inv Medkit=3') == 2, single
print('PASS: two-player legacy topology also refunds exactly once')
