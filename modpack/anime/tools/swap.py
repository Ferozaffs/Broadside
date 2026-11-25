"""mods.cpp's T() in Python (text.tsv exactly, else words.tsv's swaps), to see what a pack does to the text that
isn't in --lines-export: names, titles, quest titles and briefs, the ships' names. `parity` checks it against the
game's own export (lines.tsv vs lines_anime.tsv); `review` writes every change to review.tsv."""
import os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
PACK = os.path.join(HERE, 'anime') if os.path.isdir(os.path.join(HERE, 'anime')) else os.path.dirname(HERE)  # (the scratchpad's pack/anime/, or modpack/anime/ in the repo)
SRC = r'C:\Users\USER\repos\Broadside-modloader\src'


def unesc(s):
    o, i = [], 0
    while i < len(s):
        if s[i] == '\\' and i + 1 < len(s):
            c = s[i + 1]
            o.append('\n' if c == 'n' else '\t' if c == 't' else c)
            i += 2
        else:
            o.append(s[i])
            i += 1
    return ''.join(o)


def rows(name):
    p = os.path.join(PACK, name)
    if not os.path.exists(p):
        return []
    out = []
    for l in open(p, encoding='utf-8-sig').read().split('\n'):
        l = l.rstrip('\r')
        if not l or l[0] == '#' or '\t' not in l:
            continue
        k, v = l.split('\t', 1)
        out.append((unesc(k), unesc(v)))
    return out


TEXT = dict(rows('text.tsv'))
ANY, EXACT, PHRASES = {}, {}, []
for k, v in rows('words.tsv'):
    if not k:
        continue
    if not all((ord(c) >= 0x80 or c.isascii() and c.isalpha()) for c in k):
        PHRASES.append((k, v))
    elif any(c.isascii() and c.isupper() for c in k):
        EXACT[k] = v
    else:
        ANY[k] = v
PHRASES.sort(key=lambda p: -len(p[0].encode('utf-8')))


def letter(c):
    return ord(c) >= 0x80 or (c.isascii() and c.isalpha())


def cased(rep, was):
    up = len(was.encode('utf-8')) > 1 and all(ord(c) >= 0x80 or not (c.isascii() and c.isalpha()) or c.isupper() for c in was)
    if up:
        return ''.join(c.upper() if c.isascii() else c for c in rep)
    if was and was[0].isascii() and was[0].isupper() and rep:
        return (rep[0].upper() if rep[0].isascii() else rep[0]) + rep[1:]
    return rep


def swap(s):
    o, i, n = [], 0, len(s)
    while i < n:
        if not (letter(s[i]) and (i == 0 or not letter(s[i - 1]))):
            o.append(s[i]); i += 1; continue
        hit = False
        for k, v in PHRASES:
            if s.startswith(k, i) and not (letter(k[-1]) and i + len(k) < n and letter(s[i + len(k)])):
                o.append(v); i += len(k); hit = True; break
        if hit:
            continue
        j = i
        while j < n and letter(s[j]):
            j += 1
        w = s[i:j]
        if w in EXACT:
            o.append(EXACT[w])
        elif w.lower() in ANY:
            o.append(cased(ANY[w.lower()], w))
        else:
            o.append(w)
        i = j
    return ''.join(o)


def T(s):
    return TEXT[s] if s in TEXT else swap(s)


def literals():
    seen = []
    for f in ['people.h', 'main.cpp', 'sim.cpp']:
        s = open(os.path.join(SRC, f), encoding='utf-8').read()
        for m in re.finditer(r'"((?:[^"\\\n]|\\.)*)"', s):
            lit = m.group(1).replace('\\"', '"').replace("\\'", "'")
            if re.search(r'[A-Za-z]', lit) and lit not in seen:
                seen.append(lit)
    return seen


if __name__ == '__main__':
    if sys.argv[1] == 'parity':
        a = [l.split('\t')[3] for l in open(os.path.join(HERE, 'lines.tsv'), encoding='utf-8')]
        b = [l.split('\t')[3] for l in open(os.path.join(HERE, 'lines_anime.tsv'), encoding='utf-8')]
        bad = [(x, y, T(x)) for x, y in zip(a, b) if T(x) != y]
        print(len(a), 'lines,', len(bad), 'differ from the game')
        for x, y, t in bad[:5]:
            print(' game:', y, '\n   py:', t)
    elif sys.argv[1] == 'review':
        voiced = set(l.split('\t')[3] for l in open(os.path.join(HERE, 'lines.tsv'), encoding='utf-8'))
        with open(os.path.join(HERE, 'review_other.tsv'), 'w', encoding='utf-8') as f:
            for lit in literals():
                if lit in voiced:
                    continue
                t = T(lit)
                if t != lit:
                    f.write(lit + '\t' + t + '\n')
                    print('- ' + lit[:150] + '\n+ ' + t[:150])
