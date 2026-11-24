"""The game's men: every speaker id in lines.tsv that isn't one of kWomenFaces, with the names and titles the source
gives them (kPort, kSea, the face pools, Say/Script calls)."""
import re, collections, os
SRC = r'C:\Users\USER\repos\Broadside-modloader\src'
HERE = os.path.dirname(os.path.abspath(__file__))
ph = open(SRC + r'\people.h', encoding='utf-8').read()
mc = open(SRC + r'\main.cpp', encoding='utf-8').read()
a = ph.index('inline const char* const kWomenFaces[] = {')
women = set(re.findall(r'"([a-z_0-9]+)"', ph[a:ph.index('};', a)]))
ids = []
for l in open(HERE + r'\lines.tsv', encoding='utf-8'):
    i = l.split('\t')[0]
    if i not in ids:
        ids.append(i)
names = collections.defaultdict(set)
for m in re.finditer(r'\{"([a-z_0-9]+)", "([^"]*)", "([^"]*)",\s*\n?\s*\{', ph):
    names[m.group(1)].add(m.group(2) + ' | ' + m.group(3))
for m in re.finditer(r'\{"([a-z_0-9]+)", "([^"]+)"\}', ph):
    names[m.group(1)].add(m.group(2))
for m in re.finditer(r'(?:Say|Script)\([^"]*"([a-z_0-9]+)", "([^"]+)", "([^"]*)"', mc):
    names[m.group(1)].add(m.group(2) + ' | ' + m.group(3))
men = [i for i in ids if i not in women]
print(len(ids), 'speakers,', len(men), 'men')
for i in men:
    print(f"{i:20} {' / '.join(sorted(names[i])) or '-'}")
