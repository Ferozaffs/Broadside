"""The anime pack's review page (review.html): who's who (names, titles, voice types), every line the pack changes,
and the questions for the user. Built from the draft pack files (anime/) and the game's own exports, so the page is
what the game will do."""
import difflib, html, json, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import swap  # noqa: E402

E = html.escape
SRC = swap.SRC
ph = open(os.path.join(SRC, 'people.h'), encoding='utf-8').read()
mc = open(os.path.join(SRC, 'main.cpp'), encoding='utf-8').read()
sim = open(os.path.join(SRC, 'sim.cpp'), encoding='utf-8').read()
game = json.load(open(os.path.join(HERE, 'voices_game.json'), encoding='utf-8'))
voices = {}
for l in open(os.path.join(HERE, 'anime', 'voices.tsv'), encoding='utf-8'):
    if l.strip() and not l.startswith('#'):
        i, a, d = l.rstrip('\n').split('\t')
        voices[i] = (a, d)
men = set(l.split('\t')[0] for l in open(os.path.join(HERE, 'anime', 'people.tsv'), encoding='utf-8'))

# names and titles, as the game has them
named = {}
for m in re.finditer(r'\{"([a-z_0-9]+)", "([^"]*)", "([^"]*)",\s*\n?\s*\{', ph):
    named.setdefault(m.group(1), (m.group(2), m.group(3)))
for m in re.finditer(r'\{"([a-z_0-9]+)", "([^"]+)"\}', ph):
    named.setdefault(m.group(1), (m.group(2), ''))
for m in re.finditer(r'(?:Say|Script)\([^"]*"([a-z_0-9]+)", "([^"]+)", "([^"]*)"', mc):
    named.setdefault(m.group(1), (m.group(2), m.group(3)))
named['dread_corwin'] = ('Gentleman Corwin', 'Dread captain')
named['dread_rime'] = ('Old Rime', 'Dread captain')
named['fleet_7'] = ('Captain Dong Bang', 'Her own fleet')
for k, n in [('dread_calypso', 'Calypso'), ('dread_kate', 'Kate'), ('dread_widow', 'The Widow')]:
    named.setdefault(k, (n, 'Dread captain'))


def role(i):  # (the game's voice line, its first clause: who they are)
    d = game.get(i, ['', ''])[1]
    return re.split(r':', d)[0]


GROUPS = [('The ports', lambda i: re.match(r'(haverly|saltmarsh|greywater|coral|dorada|palmhaven|frostholm|iceward|cinderport|ashfall|mournwick|lantern)_', i)),
          ('The story', lambda i: i.startswith(('story_', 'officer_', 'narrator', 'dread_'))),
          ('At sea', lambda i: True)]
ARCH = sorted(set(a for a, _ in voices.values()))


def person_row(i):
    n, t = named.get(i, ('', ''))
    a, d = voices[i]
    conv = i in men
    nn, nt = swap.T(n) if n else '', swap.T(t) if t else ''
    if conv:
        who = (f'<span class="old">{E(n)}</span><span class="arrow">→</span><span class="new">{E(nn)}</span>' if n and nn != n
               else f'<span class="new">{E(nn or n)}</span>' if n else f'<span class="anon">{E(role(i))}</span>')
    else:
        who = f'<span class="new">{E(n)}</span>' if n else f'<span class="anon">{E(role(i))}</span>'
    title = ''
    if t:
        title = (f'<div class="title"><span class="old">{E(t)}</span> → {E(nt)}</div>' if nt != t else f'<div class="title">{E(t)}</div>')
    tag = '<span class="tag conv">was a man</span>' if conv else '<span class="tag">woman</span>'
    return (f'<tr data-arch="{E(a)}" data-conv="{1 if conv else 0}" data-text="{E((i + " " + n + " " + nn + " " + a + " " + d).lower())}">'
            f'<td class="who"><code>{E(i)}</code>{tag}<div class="name">{who}</div>{title}</td>'
            f'<td><span class="arch a-{ARCH.index(a)}">{E(a)}</span></td>'
            f'<td class="voice">{E(d)}<details><summary>the game\'s voice</summary>{E(game[i][1])}</details></td></tr>')


ids = list(voices)
people_html = []
for g, f in GROUPS:
    rows = [i for i in ids if f(i)]
    ids = [i for i in ids if i not in rows]
    people_html.append(f'<tr class="group"><th colspan="3">{E(g)} <span>{len(rows)}</span></th></tr>' + ''.join(person_row(i) for i in rows))


def diff(a, b):
    ta, tb = re.findall(r"\w+|\W+", a), re.findall(r"\w+|\W+", b)
    o, n = [], []
    for op, i1, i2, j1, j2 in difflib.SequenceMatcher(None, ta, tb, autojunk=False).get_opcodes():
        x, y = E(''.join(ta[i1:i2])), E(''.join(tb[j1:j2]))
        if op == 'equal':
            o.append(x); n.append(y)
        else:
            if x: o.append(f'<del>{x}</del>')
            if y: n.append(f'<ins>{y}</ins>')
    return ''.join(o), ''.join(n)


# the voiced lines the pack changes, with who says them
said = {}
for l in open(os.path.join(HERE, 'lines.tsv'), encoding='utf-8'):
    p = l.rstrip('\n').split('\t')
    said.setdefault(p[3], [])
    if p[0] not in said[p[3]]:
        said[p[3]].append(p[0])


def who_says(ids_):
    out = []
    for i in ids_[:3]:
        n = named.get(i, ('', ''))[0]
        out.append(swap.T(n) if n else i)
    more = f' +{len(ids_) - 3}' if len(ids_) > 3 else ''
    return ', '.join(out) + more


lines_html = []
hand = set(swap.TEXT)
n_lines = 0
for a in sorted(said, key=lambda s: (s not in hand, s)):
    b = swap.T(a)
    if b == a:
        continue
    n_lines += 1
    o, nw = diff(a, b)
    badge = '<span class="tag hand">rewritten by hand</span>' if a in hand else ''
    lines_html.append(f'<li data-text="{E((a + " " + b).lower())}"><div class="meta">{E(who_says(said[a]))}{badge}</div>'
                      f'<p class="o">{o}</p><p class="n">{nw}</p></li>')

# the rest: names, titles, pirates, captives, quest text the game puts together
other = []
for l in open(os.path.join(HERE, 'review_other.tsv'), encoding='utf-8'):
    a, b = l.rstrip('\n').split('\t', 1)
    if '(' in a or ';' in a or len(a.split()) < 2 and a not in ('Sir', 'King'):
        continue  # (code, and the lone first names the game's name lists use for matching faces)
    if a in ('Sir', 'King', 'PIRATE KING! EVERY POWER-UP') or a.startswith(('A senior admiral', 'Reading out', 'Proud and', 'Quiet, uneasy', 'His name')):
        continue  # (the voices' directions, and words the pack can't reach)
    other.append((a, b))
pirates = re.findall(r'"([^"]+)"', sim[sim.index('std::string PirateName'):sim.index('return k[seed % 32]')])
captives = re.findall(r'"([^"]+)"', sim[sim.index('const char* CaptiveName'):sim.index('const char* CaptiveName') + 1200])[:24]
pir_html = ''.join(f'<li>{"<span class=old>" + E(p) + "</span> → " + E(swap.T(p)) if swap.T(p) != p else E(p)}</li>' for p in pirates)
cap_html = ''.join(f'<li>{"<span class=old>" + E(c) + "</span> → " + E(swap.T(c)) if swap.T(c) != c else E(c)}</li>' for c in captives)
skip_names = set(named[i][0] for i in named) | set(named[i][1] for i in named) | set(pirates) | set(captives)
other_html = ''.join(f'<li><p class="o">{diff(a, b)[0]}</p><p class="n">{diff(a, b)[1]}</p></li>' for a, b in other if a not in skip_names)

words = [(k, v) for k, v in swap.rows('words.tsv')]
kept = [k for k, v in words if k == v]
swaps = [(k, v) for k, v in words if k != v and not k[:1].isupper()]
names_sw = [(k, v) for k, v in words if k != v and k[:1].isupper()]
words_html = ''.join(f'<li><span class="old">{E(k)}</span> → {E(v)}</li>' for k, v in swaps)
kept_html = ', '.join(f'<code>{E(k)}</code>' for k in kept)

counts = {a: sum(1 for x, _ in voices.values() if x == a) for a in ARCH}
chips = ''.join(f'<button type="button" class="chip a-{ARCH.index(a)}" data-arch="{E(a)}" aria-pressed="false">{E(a)} <b>{counts[a]}</b></button>'
                for a in sorted(ARCH, key=lambda a: -counts[a]))

page = open(os.path.join(HERE, 'review_template.html'), encoding='utf-8').read()
for k, v in {'PEOPLE': ''.join(people_html), 'LINES': ''.join(lines_html), 'N_LINES': str(n_lines), 'N_HAND': str(len(hand)),
             'OTHER': other_html, 'PIRATES': pir_html, 'CAPTIVES': cap_html, 'WORDS': words_html, 'KEPT': kept_html,
             'CHIPS': chips, 'N_MEN': str(len(men)), 'N_ALL': str(len(voices)), 'N_TSUN': str(counts.get('tsundere', 0))}.items():
    page = page.replace('{{' + k + '}}', v)
open(os.path.join(HERE, 'review.html'), 'w', encoding='utf-8').write(page)
print('review.html', len(page) // 1024, 'KB;', n_lines, 'lines,', len(other), 'other')
