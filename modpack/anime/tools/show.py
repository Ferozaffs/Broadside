import json, sys, os
S = os.path.dirname(os.path.abspath(__file__))
t = json.load(open(os.path.join(S, 'cast_texts.json'), encoding='utf-8'))
v = {l.split('\t')[0]: l.rstrip('\n').split('\t')[1:] for l in open(os.path.join(S, 'anime', 'voices.tsv'), encoding='utf-8') if not l.startswith('#') and l.strip()}
for i in sys.argv[1:]:
    a = v.get(i, ['?', '?'])
    print(i, '|', a[0], '|', a[1].split('. Voiced')[0], '\n   ', t[i].replace('The character is ', ''), '\n')
