import os

EXE = r'G:\Jueguitos\BT4decomp-windows-x64\BT4decomp\BT4decomp.exe'
OUT_REPO = r'G:\Jueguitos\BT4decomp-windows-x64\BT4RankedDetails\data\characters.tsv'
OUT_GAME = r'G:\Jueguitos\BT4decomp-windows-x64\BT4decomp\ranked_details\data\characters.tsv'

data = open(EXE, 'rb').read()
start = data.find(b'Goku (Early)\x00')
assert start != -1, 'Character table not found in BT4decomp.exe'

pos = start
names = []
while pos < len(data) and len(names) < 200:
    end = data.find(b'\x00', pos)
    if end == -1 or end == pos:
        break
    s = data[pos:end].decode('utf-8', 'ignore')
    if 'lobby' in s.lower():
        break
    names.append(s)
    pos = end + 1

lines = ['# BT4 Rankeds Details: character id -> name (extracted from BT4decomp.exe)']
for idx, nm in enumerate(names):
    lines.append(f'{idx}\t{nm}')
content = '\n'.join(lines) + '\n'

for dst in (OUT_REPO, OUT_GAME):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    open(dst, 'w', encoding='utf-8').write(content)
    print(f'Wrote {len(names)} characters -> {dst}')
