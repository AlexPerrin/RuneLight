"""python3 tools/protmap/test_protmap.py: transforms and the C reader on small cases."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cparse  # noqa: E402
import validate  # noqa: E402

T = ['p1', 'p1Alt1', 'p1Alt2', 'p1Alt3', 'p2', 'p2Alt1', 'p2Alt2', 'p2Alt3', 'p3', 'p3Alt1', 'p3Alt2', 'p3Alt3',
     'p4', 'p4Alt1', 'p4Alt2', 'p4Alt3']


class Transforms(unittest.TestCase):
    def test_inverse(self):
        for t in T:
            for v in (0, 1, 0x7f, 0x80, (1 << (8 * int(t[1]))) - 1, 0x123456 & ((1 << (8 * int(t[1]))) - 1)):
                self.assertEqual(validate.dec(t, validate.enc(t, v)), v, t)

    def test_wire(self):
        self.assertEqual(validate.enc('p2Alt2', 0x1234), [0x12, 0xb4])
        self.assertEqual(validate.enc('p4Alt3', 0x11223344), [0x22, 0x11, 0x44, 0x33])


class Reader(unittest.TestCase):
    SYM = {'B': 'B', 'P': 'P'}

    def test_read(self):
        body = '''
    lVar1 = P + 2;
    pbVar2 = (byte *)(B + P);
    P = P + 7;
    x = pbVar2[1] ^ 0x80 | (uint)*pbVar2 << 8;
    y = *(int *)(B + lVar1 + 1);
    z = -(uint)*(byte *)(B + lVar1) - 0x80 & 0xff;
'''
        self.assertEqual(cparse.read_layout(body, self.SYM),
                         [(0, 2, 'p2Alt2'), (2, 1, 'p1Alt3'), (3, 4, 'p4Alt1')])

    def test_char_literal(self):
        self.assertEqual(cparse.read_layout("    if (*(char *)(B + P) == '\\xff') {\n      x = 1;\n    }", self.SYM), [(0, 1, 'p1Alt2')])


if __name__ == '__main__':
    unittest.main()
