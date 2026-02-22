'''
makemask.py - Generate hashcat masks for common patterns like IPv4 addresses and email addresses.
'''
import argparse

class Mask:
    def __init__(self, mask: str=''):
        self.mask = mask
        self.charsets = []
    def _add_charset(self, charset: str) -> int:
        # If the charset does not exist, add it
        if charset not in self.charsets:
            self.charsets.append(charset)
        # Return the index of the charset
        return self.charsets.index(charset) + 1
    def append(self, mask: str, custom_chars: str = None):
        if custom_chars is None:
            self.mask += mask
            return
        assert '?*' in mask, "Mask must contain a ?* placeholder for custom chars"
        # First add the custom chars to the charset
        charset_index = self._add_charset(custom_chars)
        # Then replace the ?* placeholder with the index of the custom charset
        self.mask += mask.replace('?*', f'?{charset_index}')
    def __str__(self):
        if not self.charsets:
            return self.mask
        return ','.join(self.charsets) + ',' + self.mask

def make_ipv4_mask():
    def __octmask(octet):
        if octet == 1:
            return None,"?d"
        elif octet == 2:
            return None,"?d?d"
        elif octet == 3:
            return "12","?*?d?d"
    for oct1 in range(1, 4):
        c1, m1 = __octmask(oct1)
        for oct2 in range(1, 4):
            c2, m2 = __octmask(oct2)
            for oct3 in range(1, 4):
                c3, m3 = __octmask(oct3)
                for oct4 in range(1, 4):
                    c4, m4 = __octmask(oct4)
                    mask = Mask()
                    mask.append(m1, c1)
                    mask.append('.')
                    mask.append(m2, c2)
                    mask.append('.')
                    mask.append(m3, c3)
                    mask.append('.')
                    mask.append(m4, c4)
                    yield(mask)

def make_email_mask(local_part_max=8, domain_part_max=10, tld_part_max=5):
    for locallen in range(2, local_part_max + 1):
        for domainlen in range(4, domain_part_max + 1):
            for tld1len in range(2, tld_part_max + 1):
                mask = Mask()
                # Local part
                mask.append('?*', '?l?u?d-_')
                mask.append('?*' * (locallen - 1), '?l?u?d.-_')
                mask.append('@')
                # Domain part
                mask.append('?*' * domainlen, '?l?d')
                mask.append('.')
                # TLD part
                mask.append('?l' * tld1len)
                yield(mask)
                if tld1len == 2:
                    mask = Mask()
                    # Local part
                    mask.append('?*', '?l?u?d-_')
                    mask.append('?*' * (locallen - 1), '?l?u?d.-_')
                    mask.append('@')
                    # Domain part
                    mask.append('?*' * domainlen, '?l?d')
                    mask.append('.')
                    # TLD part
                    mask.append('?l' * 2)
                    mask.append('.')
                    mask.append('?l' * 2)
                    yield(mask)

def make_email_mask_with_domains(domains: str, local_part_max=8):
    '''Given a text file with domains, generate email masks for those domains.'''
    with open(domains, 'r') as f:
        for domain in f:
            domain = domain.strip()
            if not domain:
                continue
            for locallen in range(2, local_part_max + 1):
                mask = Mask()
                # Local part
                mask.append('?*', '?l?u?d-_')
                mask.append('?*' * (locallen - 1), '?l?u?d.-_')
                if domain[0] != '@':
                    mask.append('@')
                # Domain part
                mask.append(domain)
                yield(mask)

def main():
    parser = argparse.ArgumentParser(description='Generate hashcat masks')
    parser.add_argument('--no-ipv4', action='store_true', help='Dont generate masks for IPv4 addresses')
    parser.add_argument('--no-email', action='store_true', help='Dont generate masks for email addresses')
    parser.add_argument('--email-domains', type=str, help='Generate email masks for specific domains from a text file')
    parser.add_argument('--email-local-part-max', type=int, default=16, help='Maximum length of the local part of email addresses')
    parser.add_argument('--email-domain-part-max', type=int, default=10, help='Maximum length of the domain part of email addresses')
    parser.add_argument('--email-tld-part-max', type=int, default=5, help='Maximum length of the TLD part of email addresses')
    args = parser.parse_args()

    if not args.no_ipv4:
        ips = list(make_ipv4_mask())
        for ip in sorted(ips, key=lambda m: len(m.mask)):
            print(ip)
    if not args.no_email:
        if args.email_domains:
            emails = list(make_email_mask_with_domains(args.email_domains, args.email_local_part_max))
        else:
            emails = list(make_email_mask(args.email_local_part_max, args.email_domain_part_max, args.email_tld_part_max))
        for email in sorted(emails, key=lambda m: len(m.mask)):
            print(email)

if __name__ == "__main__":
    main()