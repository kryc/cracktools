#!/usr/bin/env python3
import argparse
import multiprocessing
import re
import sys
import unidecode

def _ishex(word: str) -> bool:
    '''Check if a word is hex-encoded'''
    return len(word) > 6 and len(word) % 2 == 0 and \
           word.startswith('$HEX[') and word.endswith(']') and \
            all(c in '0123456789abcdefABCDEF' for c in word[5:-1])

def _unhexlify(word: str, encoding: str) -> str:
    '''Unhexlify a word if it is hex-encoded'''
    if _ishex(word):
        return bytes.fromhex(word[5:-1]).decode(encoding, errors='ignore')
    return word

def _hexlify(word: str, encoding: str) -> str:
    if not word.isprintable():
        return '$HEX[' + word.encode(encoding).hex() + ']'
    return word

def read(path: str, encoding: str, include_comments: bool=False, verbose: bool=False) -> set:
    '''Read wordlist file to set'''
    # Check if it has already been read by a thread, if so we can skip it
    if isinstance(path, set):
        return path
    if verbose:
        sys.stderr.write(f'Reading file: {path}\n')
    with open(path, encoding=encoding, errors='ignore') as fh:
        return {_unhexlify(word.rstrip('\n'), encoding) for word in fh if include_comments or not word.startswith('#')}

def reads(paths: list, encoding: str) -> list:
    return [read(p, encoding) for p in paths]
    
def output(words: set) -> None:
    words = [_hexlify(word, 'utf8') for word in words]
    words.sort()
    for word in words:
        print(word)

def main():
    operations = ('union', 'difference', 'intersection', 'symmetric-difference', 'lower', 'upper', 'title', 'clean',)
    parser = argparse.ArgumentParser(description='Wordlist manipulation tool')
    parser.add_argument('operation', choices=operations, help='Operation to perform')
    parser.add_argument('files', nargs='+', help="Files to operate on")
    parser.add_argument('--verbose', action='store_true', help='Verbose output')
    parser.add_argument('--encoding', default='utf8', help='Character encoding')
    parser.add_argument('--include-comments', action='store_true', help='Do not ignore lines starting with "#"')
    parser.add_argument('--threads', '-t', type=int, help='Number of threads to use')
    args = parser.parse_args()

    # Pre-parse all the files if using threads, otherwise we can just read them as we go
    if args.threads:
        with multiprocessing.Pool(args.threads) as pool:
            args.files = pool.starmap(read, [(file, args.encoding, args.include_comments, args.verbose) for file in args.files])

    # Handle operations
    if args.operation == 'union':
        w1 = read(args.files[0], args.encoding, args.include_comments, args.verbose)
        for file in args.files[1:]:
            w1 |= read(file, args.encoding, args.include_comments, args.verbose)
        output(w1)
    elif args.operation == 'difference':
        w1 = read(args.files[0], args.encoding, args.include_comments, args.verbose)
        for file in args.files[1:]:
            w1 -= read(file, args.encoding, args.include_comments, args.verbose)
        output(w1)
    elif args.operation == 'intersection':
        w1 = read(args.files[0], args.encoding, args.include_comments, args.verbose)
        for file in args.files[1:]:
            w1 &= read(file, args.encoding, args.include_comments, args.verbose)
        output(w1)
    elif args.operation == 'symmetric-difference':
        w1 = read(args.files[0], args.encoding, args.include_comments, args.verbose)
        w1.symmetric_difference_update(*reads(args.files[1:], args.encoding))
        output(w1)
    elif args.operation == 'lower':
        w1 = {w.lower() for w in read(args.files[0], args.encoding)}
        for file in args.files[1:]:
            w1 |= {w.lower() for w in read(file, args.encoding)}
        output(w1)
    elif args.operation == 'upper':
        w1 = {w.upper() for w in read(args.files[0], args.encoding)}
        for file in args.files[1:]:
            w1 |= {w.upper() for w in read(file, args.encoding)}
        output(w1)
    elif args.operation == 'title':
        w1 = {w.title() for w in read(args.files[0], args.encoding)}
        for file in args.files[1:]:
            w1 |= {w.title() for w in read(file, args.encoding)}
        output(w1)
    elif args.operation == 'clean':
        w1 = {w.lower() for w in read(args.files[0], args.encoding)}
        for file in args.files[1:]:
            w1 |= {w.lower() for w in read(file, args.encoding)}
        # Add plurals
        for word in [w for w in w1 if w.endswith('\'s')]:
            w1.add(word[:-2] + 's')
            w1.remove(word)
        # Add unidecoded version
        for word in list(w1):
            decoded = unidecode.unidecode(word)
            if decoded not in w1:
                w1.add(decoded)
        # Add hyphenated
        for word in [w for w in w1 if '-' in w]:
            w1.add(word.replace('-', ''))
            for w in word.split('-'):
                w1.add(w)
            w1.remove(word)
        # Add space-separated
        for word in [w for w in w1 if ' ' in w]:
            w1.add(word.replace(' ', ''))
            for w in word.split(' '):
                w1.add(w)
            w1.remove(word)
        # Clear out any invalid characters
        for word in [w for w in w1 if re.match(r'^[^a-z]$', w)]:
            w1.remove(word)
        if '' in w1:
            w1.remove('')
        output(w1)

if __name__ == '__main__':
    main()