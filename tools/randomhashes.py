import hashlib
import random
import string
import sys

with open(sys.argv[1], 'wt', encoding='utf8') as fh:
    for i in range(1000):
        wordlen = random.randrange(2,4)
        word = ''
        for c in range(wordlen):
            word += random.choice(string.ascii_lowercase)
        sha256 = hashlib.md5(word.encode('utf8')).hexdigest()
        fh.write(f'{sha256}\n')
