import argparse
import logging
import os
import re
import sys

hex_re = re.compile(r'^((?:[a-fA-F0-9]{2})+).*$', re.IGNORECASE)

def parse(line: str) -> str:
    '''Return a normalized hash string'''
    m = re.match(hex_re, line)
    if m is None:
        return None
    return m.group(1).lower()

def read_and_parse(handle) -> str:
    result = None
    while result is None:
        try:
            line = handle.readline()
        except UnicodeDecodeError:
            sys.stderr.write('Unable to decode line\n')
            continue
        if line == '':
            return None
        result = parse(line)
    return result

def count_lines(file_handle) -> int:
    def blocks(files, size=65536):
        while True:
            b = files.read(size)
            if not b:
                break
            yield b
    return sum(bl.count('\n') for bl in blocks(file_handle))

def memcmp(str1, str2, num):
    for i in range(num):
        if i >= len(str1) or i >= len(str2):
            break
        diff = ord(str1[i]) - ord(str2[i])
        if diff != 0:
            return diff

    # If lengths are different after comparing 'num' characters
    if len(str1) != len(str2) and len(str1) < num and len(str2) < num:
        return len(str1) - len(str2)

    return 0

def process(target: str, cracked: str, output: str, nocheck: bool) -> int:
    tf = open(target, 'r', encoding='utf8')
    cf = open(cracked, 'r', encoding='utf8')
    of = sys.stdout if output is None else open(output, 'wt', encoding='utf8')

    # Check the line counts
    if not nocheck:
        tf_count = count_lines(tf)
        cf_count = count_lines(cf)

        if tf_count < cf_count:
            logging.critical('Target set muct be larger than cracked')
            return 1

        # Reset file offsets
        tf.seek(0, os.SEEK_SET)
        cf.seek(0, os.SEEK_SET)

    cracked_hash = read_and_parse(cf)
    if cracked_hash is None:
        logging.warning('Cracked file is empty')
        return 0

    for target_line in tf:
        target_hash = parse(target_line)
        if target_hash is None:
            logging.warning('Invalid target hex: target')
            continue

        if memcmp(target_hash, cracked_hash, len(cracked_hash)) < 0:
            of.write(target_line)
        else:
            while memcmp(target_hash, cracked_hash, len(cracked_hash)) != 0:
                logging.error(f'Spurious line: {cracked_hash} != {target_hash}')
                cracked_hash = read_and_parse(cf)
            cracked_hash = read_and_parse(cf)

def main() -> int:
    parser = argparse.ArgumentParser(description='Removes cracked hashes from sorted list')
    parser.add_argument('target', help='Full sorted wordlist file')
    parser.add_argument('cracked', help='Cracked subset.')
    parser.add_argument('--output', '-o', help='Output file')
    parser.add_argument('--nocheck', action='store_true', help='Do not check line counts')
    args = parser.parse_args()

    logging.basicConfig(level=logging.INFO)

    return process(args.target, args.cracked, args.output, args.nocheck)

if __name__ == '__main__':
    sys.exit(main())
