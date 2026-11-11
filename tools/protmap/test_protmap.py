"""python3 tools/protmap/test_protmap.py: transforms and the C reader on small cases."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cparse  # noqa: E402
import match  # noqa: E402
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


    def test_self_assignment(self):  # 241 CAM_ROTATETO_COORDINATE_V3 byte 3
        self.assertEqual(cparse.read_layout('    c = *(char *)(B + P);\n    c = -0x80 - c;\n', self.SYM), [(0, 1, 'p1Alt3')])


class Senders(unittest.TestCase):
    # the shapes Ghidra gives a sender function that picks its packet by menu op (242 0x5cdd88, 0x66b510)
    C = """
  if (op < 3) {
    if (op == 1) {
      F(&b,0x13,x);
      goto LAB_end;
    }
    else {
      if (op != 2) goto LAB_end;
      v = 0x14;
LAB_shared:
      F(&b,v,x);
      goto LAB_end;
    }
  }
  else if (op == 3) {
    F(&b,0x3e,x);
  }
  else {
    if (op != 4) {
      if (op != 6) goto LAB_end;
      v = 0x41;
      goto LAB_shared;
    }
    F(&b,6,x);
  }
LAB_end:
  return;
"""

    def test_guards_and_register_opcodes(self):
        self.assertEqual(sorted((op, g) for op, _, g in cparse.senders(self.C, 'F')),
                         [(6, 4), (0x13, 1), (0x14, 2), (0x3e, 3), (0x41, 6)])

    def test_successor(self):
        pool = {'OPLOC1_V2': 8, 'OPLOC1_V3': 9, 'MESSAGE_PUBLIC': -1}
        self.assertEqual(match.successor('OPLOC1', 8, pool), 'OPLOC1_V2')
        self.assertEqual(match.successor('OPLOC1', 9, pool), 'OPLOC1_V3')
        self.assertEqual(match.successor('MESSAGE_PUBLIC', -1, pool), 'MESSAGE_PUBLIC')
        self.assertIsNone(match.successor('OPOBJ1', 8, pool))


if __name__ == '__main__':
    unittest.main()
