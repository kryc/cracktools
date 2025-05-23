#!/usr/bin/env python3
'''
Tool to output sorted hashcat masks given a wordlist
'''

import argparse
import logging
import re
import string
import sys

def get_mask(word: str) -> str:
    """
    Generate a mask for a given word.
    """
    mask = []
    for char in word:
        if char in string.ascii_lowercase:
            mask.append('l')
        elif char in string.ascii_uppercase:
            mask.append('u')
        elif char in string.digits:
            mask.append('d')
        elif char in string.punctuation:
            mask.append('s')
        else:
            return None
    return tuple(mask)

def is_ascii_printable(value: str) -> bool:
    """
    Check if a string is ASCII printable.
    """
    return all(c in string.printable for c in value)

def get_masks(wordlist: str, minlen: int = None, maxlen: int = None) -> list:
    masks = set()
    mask_counts = {}
    valid, short, long, invalid, skipped = 0, 0, 0, 0, 0
    logging.info(f"Analyzing wordlist: {wordlist}")
    with open(wordlist, 'r', encoding='utf-8', errors='ignore') as fh:
        for line in fh:
            line = line.rstrip('\r\n')
            if not is_ascii_printable(line):
                skipped += 1
                continue
            if minlen and len(line) < minlen:
                short += 1
                continue
            if maxlen and len(line) > maxlen:
                long += 1
                continue
            mask = get_mask(line)
            if mask is None:
                invalid += 1
                continue
            valid += 1
            logging.debug(f"Mask: {''.join(mask)}")
            if mask not in masks:
                masks.add(mask)
                mask_counts[mask] = 1
            else:
                mask_counts[mask] += 1
    # return masks sorted by count
    logging.info(f"Valid: {valid}, Short: {short}, Long: {long}, Invalid: {invalid}, Skipped: {skipped}")
    masks = sorted(mask_counts.items(), key=lambda x: x[1], reverse=True)
    return masks

def ending_digits(minlen: int, maxlen: int) -> str:
    '''Generates mask list for characters followed by digits'''
    for i in range(minlen, maxlen + 1):
        for y in range(1, i - 1):
            yield '1' + 'l' * (y - 1) + 'd' * (i - y)

def main():
    parser = argparse.ArgumentParser(description='Generate hashcat masks from wordlist')
    parser.add_argument('wordlist', help='Wordlist file')
    parser.add_argument('--output', '-o', help='Output file')
    parser.add_argument('--minlen', '-m', type=int, help='Minimum mask length')
    parser.add_argument('--maxlen', '-M', type=int, help='Maximum mask length')
    parser.add_argument('--mincount', '-x', type=int, default=2, help='Minimum mask count')
    parser.add_argument('--loglevel', '-l', choices=['DEBUG', 'INFO', 'WARNING', 'ERROR', 'CRITICAL'], default='INFO', help='Set the logging level')
    parser.add_argument('--output-count', '-c', action='store_true', help='Output mask counts')
    args = parser.parse_args()

    logging.basicConfig(level=args.loglevel, format='%(levelname)s - %(message)s')

    outstream = open(args.output, 'w', encoding='utf-8') if args.output else sys.stdout

    if args.wordlist == 'appenddigits':
        if args.minlen is None or args.maxlen is None:
            parser.error('Minimum and maximum lengths are required for append digit mode')
        for mask in ending_digits(args.minlen, args.maxlen):
            formatted = '?' + '?'.join(mask)
            outstream.write(f'?u?l,{formatted}\n')
        return
    
    masks = get_masks(args.wordlist, args.minlen, args.maxlen)

    for mask, count in masks:
        if count < args.mincount:
            continue
        formatted = '?' + '?'.join(mask)
        if args.output_count:
            outstream.write(f'{formatted}:{count}\n')
        else:
            outstream.write(f'{formatted}\n')

if __name__ == '__main__':
    main()
