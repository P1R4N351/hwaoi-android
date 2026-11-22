"""The donation targets stay consistent: README.md carries the Support with Bitcoin badge (linking #support) and a '## Support' section, the Bitcoin
address there is the one in .github/FUNDING.yml, and FUNDING.yml lists exactly the two targets (Bitcoin and the VitaDB profile). No YAML library
needed: FUNDING.yml is one flow list of quoted strings."""
from pathlib import Path
import re
import unittest

REPO = Path(__file__).resolve().parents[3]
README = REPO / 'README.md'
FUNDING = REPO / '.github/FUNDING.yml'
BADGE = re.compile(r'^\[!\[Support with Bitcoin\]\([^)\s]+\)\]\(#support\)$', re.M)
ADDRESS = re.compile(r'\bbc1[02-9ac-hj-np-z]{11,71}\b')   # bech32 alphabet, no 1/b/i/o after the separator
VITADB = 'https://www.rinnegatamante.eu/vitadb/#/user/PiSCES'


def funding_targets(text):
    # the quoted items of the one `custom: [...]` line; any other key is reported as itself
    keys = [ln.split(':', 1)[0].strip() for ln in text.split('\n') if ln.strip() and not ln.lstrip().startswith('#')]
    m = re.search(r'^custom:\s*\[(.*)\]\s*$', text, re.M)
    items = re.findall(r"'([^']*)'|\"([^\"]*)\"", m.group(1)) if m else []
    return keys, [a or b for a, b in items]


class Support(unittest.TestCase):
    def test_readme_has_badge_and_section(self):
        text = README.read_text(encoding='utf-8')
        self.assertRegex(text, BADGE, 'README.md: the "Support with Bitcoin" badge line linking #support is missing')
        self.assertRegex(text, re.compile(r'^## Support\s*$', re.M), 'README.md: the "## Support" heading is missing')

    def test_bitcoin_address_matches_funding(self):
        readme = set(ADDRESS.findall(README.read_text(encoding='utf-8')))
        _, targets = funding_targets(FUNDING.read_text(encoding='utf-8'))
        funding = {t[len('bitcoin:'):] for t in targets if t.startswith('bitcoin:')}
        self.assertEqual(len(funding), 1, f'.github/FUNDING.yml: expected one bitcoin: target, found {sorted(funding)}')
        self.assertEqual(readme, funding, 'README.md and .github/FUNDING.yml name different Bitcoin addresses')

    def test_funding_lists_exactly_two_targets(self):
        keys, targets = funding_targets(FUNDING.read_text(encoding='utf-8'))
        self.assertEqual(keys, ['custom'], f'.github/FUNDING.yml: only the custom key is expected, found {keys}')
        self.assertEqual(len(targets), 2, f'.github/FUNDING.yml: expected 2 targets, found {targets}')
        self.assertEqual(sum(t.startswith('bitcoin:') for t in targets), 1)
        self.assertIn(VITADB, targets)


if __name__ == '__main__':
    unittest.main()
