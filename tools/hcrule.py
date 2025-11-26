#!/usr/bin/env python3
'''
A python script to parse hashcat rule files.
'''

import argparse
import logging

HASHCAT_RULES = {
    ':': ('Passthrough', 0),

    # Implemented compatible functions
    'l': ('Lowercase', 0),
    'u': ('Uppercase', 0),
    'c': ('Capitalize', 0),
    'C': ('Invert Capitalize', 0),
    't': ('Toggle Case', 0),
    'T': ('Toggle @ N', 1),
    'r': ('Reverse', 0),
    'd': ('Duplicate', 0),
    'p': ('Duplicate N', 1),
    'f': ('Reflect', 0),
    '{': ('Rotate Left', 0),
    '}': ('Rotate Right', 0),
    '$': ('Append Character', 1),
    '^': ('Prepend Character', 1),
    '[': ('Truncate Left', 0),
    ']': ('Truncate Right', 0),
    'D': ('Delete @ N', 1),
    'x': ('Extract range', 2),
    'O': ('Omit range', 2),
    'i': ('Insert @ N', 2),
    'o': ('Overwrite @ N', 2),
    "'": ('Truncate @ N', 1),
    's': ('Replace', 2),
    '@': ('Purge', 1),
    'z': ('Duplicate first N', 1),
    'Z': ('Duplicate last N', 1),
    'q': ('Duplicate all', 0),
    'X': ('Extract memory', 3),
    'M': ('Memorize', 0),
    '4': ('Append memory', 0),
    '6': ('Prepend memory', 0),

    # Rules used to reject plains
    '<': ('Reject less', 1),
    '>': ('Reject greater', 1),
    '_': ('Reject equal', 1),
    '!': ('Reject contain', 1),
    '/': ('Reject not contain', 1),
    '(': ('Reject equal first', 1),
    ')': ('Reject equal last', 1),
    '=': ('Reject equal at', 2),
    '%': ('Reject contains', 2),
    'Q': ('Reject contains (memory equal)', 0),

    # Implemented specific functions
    'k': ('Swap front', 0),
    'K': ('Swap back', 0),
    '*': ('Swap @ N', 2),
    'L': ('Bitwise shift left', 1),
    'R': ('Bitwise shift right', 1),
    '+': ('ASCII increment', 1),
    '-': ('ASCII decrement', 1),
    '.': ('Replace N + 1', 1),
    ',': ('Replace N - 1', 1),
    'y': ('Duplicate block front', 1),
    'Y': ('Duplicate block back', 1),
    'E': ('Title', 0),
    'e': ('Title w/separator', 1),
    '3': ('Toggle w/Nth separator', 2),
}

# Rules that operate on the Nth instance position code 'p'.
# These are syntactically distinct in hashcat (e.g. "Tp", "Dp", "xpM").
# For now we only record the operator and the number of following parameters
# after the 'p' designator; the higher-level logic (finding the p-position)
# is handled separately.
HASHCAT_P_RULES = {
    'T': ('Toggle @ p', 0),
    'D': ('Delete @ p', 0),
    'x': ('Extract range @ p', 1),  # xpM : one extra length parameter M
    'O': ('Omit range @ p', 1),     # OpM : one extra length parameter M
    'i': ('Insert @ p', 1),         # ipX : one extra char X
    'o': ('Overwrite @ p', 1),      # opX : one extra char X
    "'": ('Truncate @ p', 0),
    'X': ('Extract memory @ p', 2), # XpMI : length M and memory index I
    '*': ('Swap @ p', 1),           # *pM : swap p with position M
    'L': ('Bitwise shift left @ p', 0),
    'R': ('Bitwise shift right @ p', 0),
    '+': ('ASCII increment @ p', 0),
    '-': ('ASCII decrement @ p', 0),
    '.': ('Replace p + 1', 0),
    ',': ('Replace p - 1', 0),
}

WHITESPACE = (' ', '\t', '\r',)

def line_from_index(text: str, index: int) -> str:
    """
    Get the line from the text at the specified index.
    """
    start = text.rfind('\n', 0, index) + 1
    end = text.find('\n', index)
    if end == -1:
        end = len(text)
    return text[start:end]

def parse_rule_file(file_path: str) -> list:
    """
    Parse a hashcat rule file and return a list of rules.
    """
    rules = []
    with open(file_path, 'rt', encoding='utf-8') as file:
        text = file.read()
    
    # State machine to parse the rules
    STATES = ('START', 'RULE', 'INVALID_LINE',)
    state = 'START'
    rule = []
    sub_expression = []
    remaining_params = 0
    for index,ch in enumerate(text):
        if state == 'START':
            if ch == '#':
                state = 'INVALID_LINE'
            elif ch in WHITESPACE:
                continue
            elif ch == '\n':
                if sub_expression and remaining_params == 0:
                    assert False
                elif sub_expression:
                    logging.warning(f"Rule '{''.join(sub_expression)}' is incomplete.")
                    sub_expression = []
                elif rule:
                    rules.append(rule)
                    rule = []
                    sub_expression = []
                state = 'START'
            elif ch in HASHCAT_RULES:
                rule_name, param_count = HASHCAT_RULES[ch]
                sub_expression.append(ch)
                if param_count == 0:
                    rule.append(tuple(sub_expression))
                    sub_expression = []
                else:
                    state = 'RULE'
                    remaining_params = param_count
            else:
                logging.warning(f"Invalid character '{ch}' in line {line_from_index(text, index)}.")
                state = 'INVALID_LINE'
        elif state == 'INVALID_LINE':
            if ch == '\n':
                state = 'START'
            else:
                continue
        elif state == 'RULE':
            if remaining_params > 0:
                sub_expression.append(ch)
                remaining_params -= 1
                if remaining_params == 0:
                    rule.append(tuple(sub_expression))
                    sub_expression = []
                    state = 'START'
            else:
                logging.warning(f"Unexpected character '{ch}' in rule '{''.join(sub_expression)}'.")
                state = 'INVALID_LINE'
    # Handle the last rule if it wasn't followed by a newline
    if sub_expression and remaining_params == 0:
        rule.append(tuple(sub_expression))
        rules.append(rule)
    elif sub_expression and remaining_params > 0:
        logging.warning(f"Rule '{''.join(sub_expression)}' is incomplete.")
    elif rule:
        rules.append(rule)
    
    return rules

def format_rules(rules: list, separator: str) -> str:
    """
    Format the parsed rules into a string.
    """
    formatted_rules = []
    for rule in rules:
        formatted_rule = separator.join([''.join(r) for r in rule])
        formatted_rules.append(formatted_rule)
    return '\n'.join(formatted_rules)

def difference(rules1: list, rules2: list) -> list:
    """
    Calculate the difference between two sets of rules.
    """
    set1 = {tuple(rule) for rule in rules1}
    set2 = {tuple(rule) for rule in rules2}
    return list(set1 - set2)

def main():
    parser = argparse.ArgumentParser(description='Parse hashcat rule files')
    parser.add_argument('action', choices=['clean', 'difference', 'merge'], help='Action to perform')
    parser.add_argument('files', nargs='+', help='Input file')
    parser.add_argument('--output', '-o', help='Output file')
    parser.add_argument('--separator', '-s', default=' ', help='Separator for output rules')
    parser.add_argument('--no-append', action='store_true', help='Remove append-only rules')
    parser.add_argument('--no-prepend', action='store_true', help='Remove prepend-only rules')
    args = parser.parse_args()

    logging.basicConfig(level=logging.INFO, format='%(levelname)s - %(message)s')
    
    if args.action == 'clean':
        rules = parse_rule_file(args.files[0])
        formatted_rules = format_rules(rules, args.separator)
        if args.output is None:
            print(formatted_rules)
        else:
            with open(args.output, 'w', encoding='utf-8') as output_file:
                output_file.write(formatted_rules)
    elif args.action == 'difference':
        rules = parse_rule_file(args.files[0])
        for file in args.files[1:]:
            rules2 = parse_rule_file(file)
            rules = difference(rules, rules2)
        formatted_diff_rules = format_rules(rules, args.separator)
        if args.output is None:
            print(formatted_diff_rules)
        else:
            with open(args.output, 'w', encoding='utf-8') as output_file:
                output_file.write(formatted_diff_rules)
    elif args.action == 'merge':
        all_rules = []
        for file in args.files:
            rules = parse_rule_file(file)
            all_rules.extend(rules)
        unique_rules = list({tuple(rule) for rule in all_rules})
        formatted_rules = format_rules(unique_rules, args.separator)
        if args.output is None:
            print(formatted_rules)
        else:
            with open(args.output, 'w', encoding='utf-8') as output_file:
                output_file.write(formatted_rules)
    

if __name__ == '__main__':
    main()
