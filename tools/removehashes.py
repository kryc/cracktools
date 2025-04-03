#!/bin/env python3
import argparse
import logging
import os
import re
import sys

hex_re = re.compile(r'^([0-9a-f]+)', re.IGNORECASE)

def parse(line: str) -> str:
    '''Return a normalized hash string'''
    m = re.search(hex_re, line)
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

        if cracked_hash != target_hash:
            of.write(target_line)
        else:
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
