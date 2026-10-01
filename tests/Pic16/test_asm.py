#!/usr/bin/env python3
"""tools/pic16asm.py: encodings, directives, expressions and the things MPASM does that firmware
written for it depends on. The other half of the assembler's test is that it reproduces the Modular
in a Week Day 10 firmware's .HEX from its source word for word, which needs those files and is run
by test_varimode.sh when VARIMODE_ASM and VARIMODE_HEX are set."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'tools'))
import pic16asm  # noqa: E402


def asm(text):
    warnings = []
    words, symbols = pic16asm.assemble(text, warnings.append)
    return words, symbols, warnings


def one(line):
    words, _, _ = asm('        ' + line + '\n')
    return words[0]


class Encodings(unittest.TestCase):
    def test_literals(self):
        self.assertEqual(one('MOVLW 0x5A'), 0x305A)
        self.assertEqual(one('ADDLW 1'), 0x3E01)
        self.assertEqual(one('SUBLW 3'), 0x3C03)
        self.assertEqual(one('ANDLW 0xF0'), 0x39F0)
        self.assertEqual(one('IORLW 1'), 0x3801)
        self.assertEqual(one('XORLW 0xFF'), 0x3AFF)
        self.assertEqual(one('RETLW 7'), 0x3407)

    def test_byte_oriented(self):
        self.assertEqual(one('MOVWF 0x20'), 0x00A0)
        self.assertEqual(one('CLRF 0x20'), 0x01A0)
        self.assertEqual(one('MOVF 0x20,W'), 0x0820)
        self.assertEqual(one('MOVF 0x20,F'), 0x08A0)
        self.assertEqual(one('MOVF 0x20'), 0x08A0, "destination defaults to F")
        self.assertEqual(one('ADDWF 0x21,1'), 0x07A1)
        self.assertEqual(one('SUBWF 0x21,0'), 0x0221)
        self.assertEqual(one('DECFSZ 0x22,F'), 0x0BA2)
        self.assertEqual(one('INCFSZ 0x22,W'), 0x0F22)
        self.assertEqual(one('RLF 0x23,F'), 0x0DA3)
        self.assertEqual(one('RRF 0x23,W'), 0x0C23)
        self.assertEqual(one('SWAPF 0x24,W'), 0x0E24)
        self.assertEqual(one('COMF 0x24,F'), 0x09A4)
        self.assertEqual(one('XORWF 0x25,F'), 0x06A5)
        self.assertEqual(one('IORWF 0x25,F'), 0x04A5)
        self.assertEqual(one('ANDWF 0x25,W'), 0x0525)

    def test_bit_oriented(self):
        self.assertEqual(one('BCF 3,5'), 0x1283)
        self.assertEqual(one('BSF 3,5'), 0x1683)
        self.assertEqual(one('BTFSC 3,0'), 0x1803)
        self.assertEqual(one('BTFSS 3,2'), 0x1D03)

    def test_control(self):
        self.assertEqual(one('GOTO 0x123'), 0x2923)
        self.assertEqual(one('CALL 0x07FF'), 0x27FF)
        self.assertEqual(one('RETURN'), 0x0008)
        self.assertEqual(one('RETFIE'), 0x0009)
        self.assertEqual(one('CLRW'), 0x0100)
        self.assertEqual(one('NOP'), 0x0000)
        self.assertEqual(one('SLEEP'), 0x0063)
        self.assertEqual(one('CLRWDT'), 0x0064)


class Directives(unittest.TestCase):
    def test_labels_and_forward_references(self):
        words, sym, _ = asm("START   GOTO END2\n        NOP\nEND2    NOP\n        END\n")
        self.assertEqual(words[0], 0x2802)
        self.assertEqual(sym['END2'], 2)

    def test_label_with_colon_and_label_alone(self):
        words, sym, _ = asm("LOOP:\n        NOP\nTWO\n        GOTO LOOP\n")
        self.assertEqual(sym['LOOP'], 0)
        self.assertEqual(sym['TWO'], 1)
        self.assertEqual(words[1], 0x2800)

    def test_equ_and_expressions(self):
        words, _, _ = asm("A EQU 0x10\nB EQU A+2\n        MOVLW B*2\n        MOVLW HIGH(0x1234)\n        MOVLW LOW(0x1234)\n"
                          "        MOVLW (3<<2)|1\n")
        self.assertEqual([words[i] & 0xFF for i in range(4)], [0x24, 0x12, 0x34, 0x0D])

    def test_literal_syntaxes(self):
        words, _, _ = asm("        MOVLW B'00000101'\n        MOVLW D'255'\n        MOVLW .56\n        MOVLW 7FH\n"
                          "        MOVLW H'7F'\n        MOVLW 0x7F\n        MOVLW 'A'\n")
        self.assertEqual([w & 0xFF for w in (words[i] for i in range(7))], [5, 255, 56, 0x7F, 0x7F, 0x7F, 65])

    def test_current_address(self):
        words, _, _ = asm("        NOP\n        NOP\n        GOTO $-1\n")
        self.assertEqual(words[2], 0x2801)

    def test_org_and_dt(self):
        words, sym, _ = asm("        ORG 0x10\nTBL     DT 1,2,3\n        DW 0x3FFF\n")
        self.assertEqual(sym['TBL'], 0x10)
        self.assertEqual([words[0x10], words[0x11], words[0x12], words[0x13]], [0x3401, 0x3402, 0x3403, 0x3FFF])

    def test_ignored_directives_and_comments(self):
        words, _, _ = asm("        LIST P=16F684 ; a comment\n        ORG 0\n        NOP ; done\n        END\n        NOP\n")
        self.assertEqual(list(words), [0])
        self.assertEqual(words[0], 0)


class MpasmBehaviour(unittest.TestCase):
    def test_register_above_7f_is_masked_with_a_warning(self):
        words, _, warn = asm("TRISC EQU 87H\n        MOVWF TRISC\n")
        self.assertEqual(words[0], 0x0087)
        self.assertTrue(any('7Fh' in w for w in warn))

    def test_literal_over_255_is_truncated_with_a_warning(self):
        words, _, warn = asm("        SUBLW .259\n")
        self.assertEqual(words[0], 0x3C03)
        self.assertTrue(any('259' in w for w in warn))

    def test_errors_are_reported_with_the_line(self):
        for bad in ("        FROB 1\n", "        MOVLW\n", "        BSF 3,9\n", "        GOTO NOWHERE\n", "        MOVLW (1\n"):
            with self.assertRaises(pic16asm.AsmError):
                asm(bad)


class Output(unittest.TestCase):
    def test_hex_records_checksum_and_layout(self):
        words, _, _ = asm("        GOTO 1\n        MOVLW 4\n        NOP\n")
        lines = pic16asm.to_hex(words).split('\r\n')
        self.assertEqual(lines[0], ':020000040000FA')
        self.assertTrue(lines[1].startswith(':06000000012804300000'))
        for ln in lines[:-1]:
            if ln:
                self.assertEqual(sum(bytes.fromhex(ln[1:])) & 0xFF, 0, ln)
        self.assertEqual(lines[-2], ':00000001FF')

    def test_round_trip_through_the_hex_loader_format(self):
        words, _, _ = asm("        ORG 0\n        GOTO 3\n        ORG 0x100\n        RETLW 9\n")
        text = pic16asm.to_hex(words)
        self.assertIn(':020200000934', text)    # word 0x100 is byte 0x200: RETLW 9 = 3409h, little-endian


if __name__ == '__main__':
    unittest.main(verbosity=1)
