#!/usr/bin/env python3
"""Same inverted scan, but classified by SURFACE: what reaches a user vs what
is internal machinery. Only the shipped surface is launch-relevant."""
import os, re, collections

SKIP_DIRS = {'.git','secp256k1','univalue','crc32c','leveldb','minisketch',
             'build','build-mingw','depends','node_modules'}
SKIP_SUBSTR = ('src/secp256k1/','src/univalue/','src/crc32c/','src/leveldb/','src/minisketch/')

ACTIONABLE = [
 ('binary name',   re.compile(r'\bbitcoin(d|-cli|-tx|-wallet|-util|-qt|-node|-gui)\b')),
 ('config file',   re.compile(r'\bbitcoin\.conf\b')),
 ('datadir',       re.compile(r'\.bitcoin[/\\]|Application Support[/\\]Bitcoin\b|LOCALAPPDATA%\\Bitcoin|AppData[/\\](Local|Roaming)[/\\]Bitcoin\b')),
 ('bundle id',     re.compile(r'org\.bitcoin[a-z.]*')),
 ('upstream host', re.compile(r'bitcoincore\.org|(?<!//)\bbitcoin\.org\b')),
 ('upstream repo', re.compile(r'github\.com/[\w.-]+/bitcoin[\w.-]*|github\.com/bitcoin[/-]')),
 ('unix user',     re.compile(r'"bitcoin" (user|group)|User=bitcoin|Group=bitcoin')),
 # Prose misattribution: the project calling ITSELF Bitcoin. A separate defect
 # class from a wrong identifier -- it breaks no command, it tells the reader
 # they are running someone else's software. Matched on the name itself, so
 # LEGIT below carries the burden of allowing correct uses.
 ('self-name',     re.compile(r'\bBitcoin Core\b')),
 ('self-name',     re.compile(r'\bMultiprocess Bitcoin\b|^# Bitcoin Core|Clone Bitcoin Rep')),
]
# Contexts where naming Bitcoin is CORRECT for a fork: describing the fork
# relationship, upstream history, upstream-maintained subtrees, upstream
# programmes TrueNorth does not take part in, and retained MIT copyright.
LEGIT = re.compile(
    r'upstream|inherit|unlike|fork of|derived from|subtree|'
    r'OSS-Fuzz|security disclosure policy|Copyright|PR Review Club|'
    r"What's different from|than Bitcoin\b|Bitcoin 0\.\d|"
    r'notariz|anchor chain|high-hashrate|\bBIP[- ]?\d|bitcoinops|'
    r'Bitcoin-style|Bitcoin-genesis|testnet3 (baggage|conventions)',
    re.I)

def surface(rel):
    r = rel.replace(os.sep,'/')
    if r.startswith('doc/man/'):               return 'SHIPPED'
    if r.startswith('doc/') and r.endswith('.md'): return 'SHIPPED'
    if '/' not in r and r.endswith('.md'):     return 'SHIPPED'
    if r.startswith('contrib/init/'):          return 'SHIPPED'
    if r.startswith('share/examples/'):        return 'SHIPPED'
    if r.startswith('test/'):                  return 'INTERNAL test'
    if r.startswith('src/'):                   return 'INTERNAL src'
    if r.startswith('contrib/'):               return 'INTERNAL contrib'
    if r.startswith('.github/'):               return 'INTERNAL ci'
    return 'INTERNAL other'

rows = []
legit = []
for dp, dn, fns in os.walk('.'):
    dn[:] = [d for d in dn if d not in SKIP_DIRS]
    if any(s in dp.replace(os.sep,'/')+'/' for s in SKIP_SUBSTR): continue
    for fn in fns:
        p = os.path.join(dp, fn); rel = os.path.relpath(p, '.')
        if os.path.splitext(fn)[1] not in {'.md','.sh','.py','.service','.openrc',
            '.openrcconf','.init','.plist','.conf','.yml','.yaml','.1','.txt'}: continue
        try:
            for ln, line in enumerate(open(p, encoding='utf-8', errors='replace'), 1):
                if 'bitcoin' not in line.lower(): continue
                for cat, rx in ACTIONABLE:
                    if rx.search(line):
                        if LEGIT.search(line):
                            legit.append((rel, ln))
                            break
                        rows.append((surface(rel), cat, rel, ln, line.strip()[:110]))
                        break
        except Exception: pass

print("="*74); print("ACTIONABLE occurrences by surface")
print(f"({len(legit)} suppressed as legitimate upstream references)"); print("="*74)
bys = collections.Counter(r[0] for r in rows)
for s, n in bys.most_common(): print(f"  {n:5d}  {s}")

ship = [r for r in rows if r[0]=='SHIPPED']
print(f"\n{'='*74}\nSHIPPED SURFACE -- reaches users, in release archives ({len(ship)})\n{'='*74}")
bycat = collections.Counter(r[1] for r in ship)
for c, n in bycat.most_common(): print(f"  {n:5d}  {c}")
print()
byfile = collections.Counter(r[2] for r in ship)
for f, n in byfile.most_common(): print(f"  {n:4d}  {f}")
print(f"\n{'-'*74}\nLine detail (shipped only)\n{'-'*74}")
for s,c,f,ln,txt in sorted(ship, key=lambda r:(r[2],r[3])):
    print(f"  {f}:{ln}  [{c}]  {txt}")
