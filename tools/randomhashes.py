import argparse
import hashlib
import random
import string
import sys

def generate_random_string(args) -> str:
    chars = string.printable if args.charset == 'ascii' else string.ascii_letters + string.digits
    length = random.randint(args.min_length, args.max_length)
    return ''.join(random.choices(chars, k=length))

def main():
    parser = argparse.ArgumentParser(description='Generate random hashes.')
    parser.add_argument('output_file', help='Output file to write the hashes to')
    parser.add_argument('--min-length', type=int, default=1, help='Minimum length of the random string')
    parser.add_argument('--max-length', type=int, default=10, help='Maximum length of the random string')
    parser.add_argument('--count', type=int, default=1000, help='Number of hashes to generate')
    parser.add_argument('--algorithm', choices=['md5', 'sha1', 'sha256'], default='sha1', help='Hash algorithm to use')
    parser.add_argument('--output-password', action='store_true', help='Output password hashes instead of regular hashes')
    parser.add_argument('--allow-duplicates', action='store_true', help='Allow duplicate hashes in the output')
    parser.add_argument('--charset', default='ascii', choices=['ascii', 'alphanumeric'], help='Character set to use for generating random strings')
    args = parser.parse_args()

    created = set()

    with open(args.output_file, 'w') as f:
        # Get hash algorithm from hashlib
        hash_algorithm = getattr(hashlib, args.algorithm)
        for _ in range(args.count):
            # Generate a random string of a random length
            random_string = generate_random_string(args)
            while random_string in created and not args.allow_duplicates:
                random_string = generate_random_string(args)
            created.add(random_string)
            # Create the hash using the specified algorithm
            hash_object = hash_algorithm(random_string.encode())
            # Write the hash to the file
            if args.output_password:
                # If output_password is specified, write the hash as a password
                f.write(f"{hash_object.hexdigest()}:{random_string}\n")
            else:
                f.write(f"{hash_object.hexdigest()}\n")

if __name__ == '__main__':
    main()