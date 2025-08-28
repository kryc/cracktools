import argparse
import string
import sys

def is_printable_ascii(word: bytes) -> bool:
    '''Check if all characters in the word are printable ASCII characters.'''
    return all(ord(' ') <= ord(c) <= ord('~') for c in word)

def is_valid_utf8(word: bytes) -> bool:
    '''Check if the byte sequence is valid UTF-8.'''
    try:
        word.decode('utf-8')
        return True
    except UnicodeDecodeError:
        return False

def is_hexlified(word) -> bool:
    '''Check if the word is already hexlified.'''
    # Check the lengths first
    if len(word) < 7 or len(word) % 2 == 0:
        return False
    # Check the $HEX[...] format
    if isinstance(word, str):
        return word.startswith('$HEX[') and word.endswith(']') and all(c in string.hexdigits for c in word[5:-1])
    return word.startswith(b'$HEX[') and word.endswith(b']') and all(c in string.hexdigits.encode('utf8') for c in word[5:-1])

def need_hexlify(word: bytes, use_ascii: bool, separator: str) -> bool:
    '''Determine if a word needs to be hexlified based on conditions.'''
    # Check if the word is valid UTF-8
    if not is_valid_utf8(word):
        return True
    word = word.decode('utf-8')
    # Word contains non-ASCII printable characters
    if use_ascii and not is_printable_ascii(word):
        return True
    # Word contains non-printable utf8 characters
    if not word.isprintable():
        return True
    # Word contains the separator character
    if separator in word:
        return True
    # Word is invalidly hexlified (eg $HEX[xyz] or $HEX[123]ab)
    if word.startswith('$HEX[') and ']' in word:
        return True
    # Word ends with whitespace
    if word and word[-1] in string.whitespace:
        return True
    # No need to hexlify
    return False

def hexlify(word: bytes, use_ascii: bool, separator: str) -> str:
    '''Hexlify a word if needed, otherwise return it unchanged.'''
    if is_hexlified(word):
        return word
    if need_hexlify(word, use_ascii, separator):
        return f'$HEX[{word.hex()}]'.encode('utf-8')
    return word

def main():
    parser = argparse.ArgumentParser(description='Hexlify wordlist files to match hashcat output')
    parser.add_argument('-i', '--input', help='Files to hexlify, or stdin if none provided')
    parser.add_argument('-o', '--out', help='Output file, or stdout if none provided')
    parser.add_argument('-e', '--encoding', default='utf-8', help='File encoding (default: utf-8)')
    parser.add_argument('-s', '--separator', default=':', help='Output separator for encoding (default: ":")')
    parser.add_argument('-a', '--ascii', action='store_true', help='Enforce ASCII printable')
    args = parser.parse_args()

    instream = sys.stdin.buffer if args.input is None else open(args.input, 'rb')
    outstream = sys.stdout.buffer if args.out is None else open(args.out, 'wb')

    while True:
        line = instream.readline()
        next_word = line.rstrip(b'\n')
        output_line = hexlify(next_word, args.ascii, args.separator)
        outstream.write(output_line + b'\n')

    return 0

if __name__ == '__main__':
    exit(main())