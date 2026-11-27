import argparse
import os

REPO_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_GAME = os.path.join(os.path.dirname(REPO_DIR), 'BT4decomp')


def main():
    ap = argparse.ArgumentParser(description='Extract BT4 character ID -> name table from BT4decomp.exe.')
    ap.add_argument('--exe', default=os.path.join(DEFAULT_GAME, 'BT4decomp.exe'), help='Path to BT4decomp.exe')
    ap.add_argument('--out', default=os.path.join(REPO_DIR, 'data', 'characters.tsv'), help='Output TSV path')
    args = ap.parse_args()

    data = open(args.exe, 'rb').read()
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

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    open(args.out, 'w', encoding='utf-8').write(content)
    print(f'Wrote {len(names)} characters -> {args.out}')


if __name__ == '__main__':
    main()
