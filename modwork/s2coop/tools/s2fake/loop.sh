#!/bin/sh
set -u
cd "$HOME/s2fake" || exit 1
rm -rf run1 run2 run3
mkdir run1 run2 run3
cd run1
python3 -I ../fakehub.py --listen 27900 --bind 127.0.0.1 --max 4 --seconds 42 --summary hub.json --log hub.log >/dev/null 2>&1 &
sleep 1
for i in 1 2 3 4; do
  extra=""; secs=40
  [ $i = 2 ] && { extra="--bye"; secs=22; }
  [ $i = 3 ] && extra="--down-at 18"
  [ $i = 4 ] && extra="--dead-at 32"
  python3 -I ../fakepeer.py --peer 127.0.0.1:27900 --bind 127.0.0.1 --listen 2791$i --name fake$i --seconds $secs --phase $((i*90)) $extra --summary p$i.json --log p$i.log >/dev/null 2>&1 &
done
sleep 2
python3 -I ../fakepeer.py --peer 127.0.0.1:27900 --bind 127.0.0.1 --listen 27915 --name fake5 --seconds 36 --summary p5.json --log p5.log >/dev/null 2>&1 &
cd ../run2
python3 -I ../fakehub.py --listen 27901 --bind 127.0.0.1 --max 4 --seconds 16 --host-down-at 6 --summary hub.json --log hub.log >/dev/null 2>&1 &
sleep 1
python3 -I ../fakepeer.py --peer 127.0.0.1:27901 --bind 127.0.0.1 --listen 27921 --name a --seconds 14 --down-at 4 --take-at -1 --quest-at -1 --summary pa.json --log pa.log >/dev/null 2>&1 &
python3 -I ../fakepeer.py --peer 127.0.0.1:27901 --bind 127.0.0.1 --listen 27922 --name b --seconds 14 --down-at 8 --take-at -1 --quest-at -1 --summary pb.json --log pb.log >/dev/null 2>&1 &
cd ../run3
python3 -I ../fakehub.py --listen 27902 --bind 127.0.0.1 --max 4 --seconds 24 --summary hub.json --log hub.log >/dev/null 2>&1 &
sleep 1
python3 -I ../fakepeer.py --peer 127.0.0.1:27902 --bind 127.0.0.1 --listen 27931 --name r1 --seconds 22 --down-at 4 --take-at -1 --quest-at -1 --summary pr1.json --log pr1.log >/dev/null 2>&1 &
sleep 0.5
python3 -I ../fakepeer.py --peer 127.0.0.1:27902 --bind 127.0.0.1 --listen 27932 --name r2 --seconds 21 --revive 7:3 --take-at -1 --quest-at -1 --summary pr2.json --log pr2.log >/dev/null 2>&1 &
sleep 0.5
python3 -I ../fakepeer.py --peer 127.0.0.1:27902 --bind 127.0.0.1 --listen 27933 --name r3 --seconds 20 --down-at 3 --dead-at 14 --revive 6:1 --take-at -1 --quest-at -1 --summary pr3.json --log pr3.log >/dev/null 2>&1 &
wait
for r in run1 run2 run3; do
  cd "$HOME/s2fake/$r"
  echo "=== $r hub"
  grep -h -E "S2HUB (PEER|FULL|GAME|qn|ground|REVIVE|HOST)" hub.log
  for f in p*.json; do python3 -I -c "import json,sys;d=json.load(open(sys.argv[1]));print(d['name'],'slot',d['slot'],'full',d['full'],'host_up',d['host_up'],'others',{k:(v['n'],v['life']) for k,v in d['others'].items()},'go',d['go'],'go_events',d.get('go_events'),'go_dead',d.get('go_dead'),'revived_by',d.get('revived_by'),'rv',d.get('rv'),'rx',d['rx'])" $f; done
  cat hub.json; echo
done
