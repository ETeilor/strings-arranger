"""Run the C++ CLI on every reference case of the Python prototype and compare note by note."""
import json, subprocess, sys
ref = json.load(open(sys.argv[1]))
cli = sys.argv[2]
root = sys.argv[3]
bad = 0
for i, c in enumerate(ref):
    args = [cli, f"{root}/{c['file']}", '--dump', f"--style={c['style']}", f"--complexity={c['cx']}"]
    if c.get('octav'): args.append('--octavists')
    if c['style'] == 'gregorian':
        args += [f"--texture={c['texture']}", f"--outofmode={c['policy']}"]
    out = subprocess.run(args, capture_output=True, text=True).stdout.split('\n')
    tracks, cur, issues = {}, None, None
    for line in out:
        if line.startswith('ISSUES'): issues = int(line.split()[1])
        elif line.startswith('TRACK'): cur = line.split()[1]; tracks[cur] = []
        elif line.strip() and cur: tracks[cur].append([int(x) for x in line.split()])
    for k in tracks: tracks[k].sort()
    exp = {k: sorted(v) for k, v in c['tracks'].items()}
    if tracks != exp or issues != c['issues']:
        bad += 1
        if bad <= 8:
            desc = {k: c[k] for k in c if k not in ('tracks',)}
            print('MISMATCH', desc, 'issues', issues)
            for k in exp:
                a, b = exp[k], tracks.get(k, [])
                if a != b:
                    diff = [(x, y) for x, y in zip(a, b) if x != y][:3]
                    print('   ', k, len(a), len(b), diff)
print(f'{len(ref) - bad}/{len(ref)} cases identical')
