# Given the output of hcmaskgen, figure out the best rule to hit > 90% of the hashes
# The format is <mask> <count>
import argparse
import logging
import re

MASK_REGEX = re.compile(r'^(\??a|\??d|\??l|\??u|\??s)+$')

def get_counts(input_file) -> dict:
    counts = {}
    with open(input_file, 'r') as file:
        lines = file.readlines()

    for line in lines:
        mask, count = line.strip().split()
        if not MASK_REGEX.match(mask):
            logging.warning(f"Skipping invalid mask: {mask}")
            continue
        mask = mask.replace('?', '')
        for index, maskchar in enumerate(mask):
            if index not in counts:
                counts[index] = {}
            if maskchar not in counts[index]:
                counts[index][maskchar] = 0
            counts[index][maskchar] += int(count)

    return counts

def get_percentages(counts) -> dict:
    percentages = {}
    for index, chars in counts.items():
        total = sum(chars.values())
        percentages[index] = {char: (count / total) * 100 for char, count in chars.items()}
    return percentages

def get_best_rule(percentages) -> str:
    best_rule = ''
    for index, chars in percentages.items():
        best_char = max(chars, key=chars.get)
        best_rule += best_char
    return best_rule

def analyse(input_file):
    counts = get_counts(input_file)

    # Calculate percentages for each position
    percentages = get_percentages(counts)
    # Print the percentages for each position to 1 decimal place
    for index, chars in percentages.items():
        print(f"{index}: ", end='')
        for char in ('l', 'u', 'd', 's'):
            percent = chars.get(char, 0)
            print(f" {char}: {percent:.1f}%", end='')
        print()

    # Determine the best rule by combining all characters with percentages > 10%
    best_rule = get_best_rule(percentages)

def main():
    parser = argparse.ArgumentParser(description="Find the best rule to hit > 90% of the hashes from hcmaskgen output.")
    parser.add_argument('input_file', type=str, help="Path to the input file containing hcmaskgen output.")
    args = parser.parse_args()

    analyse(args.input_file)

if __name__ == "__main__":
    main()