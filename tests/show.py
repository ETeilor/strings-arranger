"""Text piano roll of an arrangement: python3 tests/show.py <strings_cli> in.mid [options]. One row per part and bar."""
import subprocess, sys
NAMES = ['C','C#','D','Eb','E','F','F#','G','Ab','A','Bb','B']
ART = ['', '~', '.', "'", '+', '%', '>']   # long, legato, staccato, spiccato, pizz, tremolo, marcato
out = subprocess.run([sys.argv[1], sys.argv[2], '--dump'] + sys.argv[3:], capture_output=True, text=True).stdout
parts = {}; cur = None
for line in out.split('\n'):
    if line.startswith('TRACK'): cur = ' '.join(line.split()[1:-1]); parts[cur] = []
    elif line.strip() and cur and not line.startswith('ISSUES'): parts[cur].append([int(x) for x in line.split()])
bar = 1920
end = max((n[1] for ns in parts.values() for n in ns), default=0)
for b in range(0, min(end, 6 * bar), bar):
    print(f'--- bar {b // bar + 1}')
    for name, ns in parts.items():
        cells = []
        for n in sorted(ns):
            if b <= n[0] < b + bar:
                cells.append(f"{(n[0]-b)/480+1:g}:{NAMES[n[2]%12]}{n[2]//12-1}{ART[n[3]]}" + (f"({(n[1]-n[0])/480:g})" if n[1]-n[0] not in (120,240) else ''))
        print(f'{name:11s}', ' '.join(cells))
