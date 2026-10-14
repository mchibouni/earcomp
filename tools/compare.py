"""Compare an EarComp probe result (JSON from tools/probe) with the measured Mimi map."""
import json, sys, pathlib, statistics

root = pathlib.Path(__file__).resolve().parent.parent
mimi = json.loads((root / 'data/mimi-map.json').read_text())
ours = json.loads(pathlib.Path(sys.argv[1]).read_text())
F, Lv = mimi['freqs'], mimi['levels']

print('static tone map, error = ours - Mimi (dB)')
for key, name in (('default', 'Recommended'), ('up', 'Richer'), ('down', 'Softer')):
    errs = [o - m for ro, rm in zip(ours['static'][key]['L'], mimi['static'][key]['L']) for o, m in zip(ro, rm)]
    rms = (sum(e * e for e in errs) / len(errs)) ** 0.5
    print(f'  {name:12} mean {statistics.mean(errs):+.2f}  rms {rms:.2f}  max |e| {max(map(abs, errs)):.2f}')
if '-v' in sys.argv:
    print('  Recommended error per level (rows) x freq:')
    print('  lvl ' + ''.join(f'{f:>6}' for f in F))
    for lev, ro, rm in zip(Lv, ours['static']['default']['L'], mimi['static']['default']['L']):
        if lev % 10 == 0:
            print(f'  {lev:>3} ' + ''.join(f'{o - m:6.1f}' for o, m in zip(ro, rm)))

print('pink noise, gain per 1/3 octave (ours / Mimi):')
for lev in ('-40', '-20'):
    o, m = ours['noise'][lev], mimi['noise'][lev]
    e = [a - b for a, b in zip(o, m)][:-1]  # 16 kHz band is filter leakage in both
    print(f'  {lev} dBFS  rms err {(sum(x*x for x in e)/len(e))**0.5:.2f}  ' +
          ' '.join(f'{f}:{a:.1f}/{b:.1f}' for f, a, b in zip(F, o, m) if f in (125, 500, 1000, 2000, 4000, 8000, 12500)))

print('timing (gain dB at times after the step):')
for f in ('1000', '8000'):
    to, tm = ours['timing'][f], mimi['timing'][f]
    def at(t, sec): return t['gainDb'][min(len(t['gainDb']) - 1, int(sec / t['blockSec']))]
    pts = [(1.0, 'up+0'), (1.02, '+20ms'), (1.05, '+50ms'), (1.1, '+100ms'), (1.3, '+300ms'), (2.02, 'dn+20ms'), (2.05, '+50ms'), (2.1, '+100ms'), (2.3, '+300ms')]
    print(f'  {f:>5} Hz ' + ' '.join(f'{lab}:{at(to, s):.1f}/{at(tm, s):.1f}' for s, lab in pts))
