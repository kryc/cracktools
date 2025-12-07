# Generate hashcat masks for email addresses
import argparse

def generate_email_masks(local_part_max, domain_part_max):
    for p1len in range(1, local_part_max):
        for p2len in range(4, domain_part_max):
            local_part_mask = "?2" * (p1len + 1)
            # Emails cannot start with a dot
            local_part_mask = '?1' + local_part_mask[2:]
            domain_part_mask = '?l?l' + "?3" * (p2len + 1 - 4) + '?l?l'
            yield f"{local_part_mask}@{domain_part_mask}"

def main():
    parser = argparse.ArgumentParser(description="Generate hashcat masks for email addresses.")
    parser.add_argument("--local-part-max", type=int, default=8, help="Maximum length of the local part")
    parser.add_argument("--domain-part-max", type=int, default=10, help="Maximum length of the domain part")

    args = parser.parse_args()

    masks = list(generate_email_masks(args.local_part_max, args.domain_part_max))
    # Sort masks by total length (local + domain)
    masks.sort(key=lambda mask: len(mask.replace("@", "")))

    for mask in masks:
        print(f'?l?u?d,?l?u?d.-_,?l.,{mask}')


if __name__ == "__main__":
    main()