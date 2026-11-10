"""Every composer style x every test file x every level: no errors, no empty or zero-length notes,
parts inside the instruments, density rising with the level.
Density is judged on the total over all files: it must not fall by more than 3% between neighbouring levels.
Known cause of small dips: from level 2 the bass may take the third of a chord, and a bass note shared by
two chords is then tied into one. A single file fails only when it loses more than 20% of its notes."""
import subprocess, sys, glob, collections
cli, root = sys.argv[1], sys.argv[2]
styles = subprocess.run([cli, '--list'], capture_output=True, text=True).stdout.split()
LO = {'Violin I': 55, 'Violin II': 55, 'Viola': 48, 'Cello': 36, 'Contrabass': 28}
HI = {'Violin I': 100, 'Violin II': 100, 'Viola': 88, 'Cello': 81, 'Contrabass': 67}
files = sorted(glob.glob(f'{root}/input/*.mid')) + sorted(glob.glob(f'{root}/melodies/*.mid'))
bad = 0
for st in styles:
    by_level = collections.Counter(); issues = 0; runs = 0; drops = 0
    for f in files:
        prev = -1
        for L in range(11):
            r = subprocess.run([cli, f, '--dump', f'--style={st}', f'--complexity={L}'], capture_output=True, text=True)
            if r.returncode != 0:
                bad += 1; print('FAIL', st, f, L, r.stderr.strip()[:200]); continue
            runs += 1; cur = None; n = 0
            for line in r.stdout.split('\n'):
                if line.startswith('ISSUES'): issues += int(line.split()[1])
                elif line.startswith('TRACK'): cur = ' '.join(line.split()[1:-1])
                elif line.strip() and cur:
                    a, b, p, art = map(int, line.split())
                    n += 1
                    if b <= a: print('BAD NOTE', st, f, L, cur, a, b, p); bad += 1
                    if not LO[cur] <= p <= HI[cur]: print('RANGE', st, f, L, cur, p); bad += 1
            if n == 0: print('EMPTY', st, f, L); bad += 1
            if n < prev * 0.80: drops += 1; print('DENSITY FALLS', st, f.split('/')[-1], 'level', L, prev, '->', n)
            prev = n; by_level[L] += n
    print(f'{st:12s} runs {runs:3d} issues {issues:4d} density drops {drops}  notes by level', [by_level[L] for L in range(11)])
    for L in range(1, 11):
        if by_level[L] < by_level[L - 1] * 0.97: drops += 1; print('TOTAL DENSITY FALLS', st, 'level', L, by_level[L - 1], '->', by_level[L])
    bad += drops
print('failures', bad)
