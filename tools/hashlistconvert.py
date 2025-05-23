#!/bin/env python3
import argparse
import logging
import sys

# pylint: disable=logging-fstring-interpolation
# pylint: disable=bare-except
# pylint: disable=missing-function-docstring

def main() -> int:
    # Parse the input and output files from the command line arguments
    parser = argparse.ArgumentParser(description='Hash list to binary file converter')
    parser.add_argument('input', help='Input UTF-8 text file')
    parser.add_argument('output', nargs='?', help='Output flat binary file')
    args = parser.parse_args()

    if args.output is None and args.input.endswith('.txt'):
        args.output = args.input[:-4] + '.bin'
    elif args.output is None:
        args.output = args.input + '.bin'

    # Open the input and output streams
    with open(args.input, 'rt', encoding='utf8') as fhr:
        with open(args.output, 'wb') as fhw:
            for line in fhr:
                line = line.rstrip()
                # Check that we have a valid hash before conversion
                if len(line) not in (128 / 8 * 2, 160 / 8 * 2, 256 / 8 * 2):
                    logging.warning(f'Ignoring: {line} (invalid length: {len(line)}))')
                    continue
                # Try writing the hash to the output file
                try:
                    fhw.write(bytes.fromhex(line))
                except:
                    logging.warning(f'Error writing hash: {line}')
    return 0

if __name__ == '__main__':
    sys.exit(main())
