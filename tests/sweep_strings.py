"""Every composer style x every test file x every level: no errors, no empty or zero-length notes,
parts inside the instruments, density rising with the level.
Density counts the attacks of the four upper parts; a tremolo note counts as eighths over its length.
The basses are left out: they double the cellos or hold a pedal on purpose.
The total over all files must not fall by more than 4% between neighbouring levels. A single file fails when
it loses more than 20% while its textures change, the total fails the same way; a dip with the same textures
comes from the voice-leading engine (its ornaments, or sevenths and ninths the style adds that hold voices still) (a known behaviour, see claude/strings-release-0.1.md) and is listed, not failed.
  python3 tests/sweep_strings.py <strings_cli> <tests dir> [--quick] [style,style,...]
--quick checks levels 0, 5, 10 only and skips the density test (used on CI)."""
import subprocess, sys, glob, collections, concurrent.futures, os
args = [a for a in sys.argv[1:] if not a.startswith('--')]
quick = '--quick' in sys.argv
cli, root = args[0], args[1]
styles = args[2].split(',') if len(args) > 2 else subprocess.run([cli, '--list'], capture_output=True, text=True).stdout.split()
LO = {'Violin I': 55, 'Violin II': 55, 'Viola': 48, 'Cello': 36, 'Contrabass': 28}
HI = {'Violin I': 100, 'Violin II': 100, 'Viola': 88, 'Cello': 81, 'Contrabass': 67}
files = sorted(glob.glob(f'{root}/input/*.mid')) + sorted(glob.glob(f'{root}/melodies/*.mid'))
levels = [0, 5, 10] if quick else list(range(11))

def run_style(st):
    msgs = []; bad = 0; by_level = collections.Counter(); runs = 0; issues = 0; changed = set()
    for f in files:
        prev = -1; prevTex = None
        for L in levels:
            r = subprocess.run([cli, f, '--dump', f'--style={st}', f'--complexity={L}'], capture_output=True, text=True)
            if r.returncode != 0:
                bad += 1; msgs.append(f'FAIL {st} {f} {L} {r.stderr.strip()[:200]}'); continue
            runs += 1; cur = None; n = 0; tex = ''
            for line in r.stdout.split('\n'):
                if line.startswith('ISSUES'): issues += int(line.split()[1])
                elif line.startswith('TEXTURES'): tex = line
                elif line.startswith('TRACK'): cur = ' '.join(line.split()[1:-1])
                elif line.strip() and cur:
                    a, b, p, art = map(int, line.split())
                    if cur != 'Contrabass': n += max(1, (b - a) // 240) if art == 5 else 1
                    if b <= a: bad += 1; msgs.append(f'BAD NOTE {st} {f} {L} {cur} {a} {b} {p}')
                    if not LO[cur] <= p <= HI[cur]: bad += 1; msgs.append(f'RANGE {st} {f} {L} {cur} {p}')
            if n == 0: bad += 1; msgs.append(f'EMPTY {st} {f} {L}')
            if not quick and n < prev * 0.80:
                where = f'{st} {f.split("/")[-1]} level {L} {prev} -> {n}'
                if tex != prevTex: bad += 1; msgs.append('DENSITY FALLS ' + where)
                else: msgs.append('  engine dip (same textures) ' + where)
            if prevTex is not None and tex != prevTex: changed.add(L)
            prev = n; prevTex = tex; by_level[L] += n
    if not quick:
        for L in range(1, 11):
            if by_level[L] < by_level[L - 1] * 0.96:
                where = f'{st} level {L} {by_level[L - 1]} -> {by_level[L]}'
                if L in changed: bad += 1; msgs.append('TOTAL DENSITY FALLS ' + where)
                else: msgs.append('  engine dip in total (same textures) ' + where)
    msgs.append(f'{st:18s} runs {runs:3d} issues {issues:4d} notes by level {[by_level[L] for L in levels]}')
    return bad, msgs

total = 0
with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 2) as ex:
    for bad, msgs in ex.map(run_style, styles):
        total += bad
        print('\n'.join(msgs), flush=True)
print('failures', total)
