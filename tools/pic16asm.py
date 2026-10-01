#!/usr/bin/env python3
"""A small assembler for the PIC16F684 (and the other mid-range, 14-bit-instruction parts).

It exists because the firmware in firmware/ is this project's own and needs building from source
in a place with no MPASM or gputils. It is not a general MPASM replacement: it does the
instructions, the data directives and the expression syntax the firmware here uses, and it
says so when it meets anything else.

    pic16asm.py SOURCE.asm [-o OUT.hex] [--cpp OUT.hpp NAME]

Syntax (MPASM's, the subset that matters):
  * labels at the start of a line, with or without a colon; ';' comments;
  * NAME EQU expr; ORG expr; END; LIST/#include/radix lines are ignored; DT n,n,... emits RETLW;
    DW n,n,... emits raw words;
  * numbers: 123, 0x7F, 7FH, H'7F', B'0101', D'12', .12, O'17', 'A'; $ is the current address;
    expressions use + - * / % << >> & | ^ ~ and parentheses; HIGH(x) and LOW(x) as in MPASM;
  * byte-oriented operands are `f` or `f,d`, d being W, F, 0 or 1 (F, the MPASM default, if omitted);
  * a file register above 7Fh is masked to 7 bits, as MPASM does (with a warning), because the bank
    is selected by STATUS and not by the address.
A literal outside 0..255 is truncated to its low 8 bits with a warning, which is what MPASM does and
what firmware written against it sometimes depends on.
"""
import argparse
import os
import re
import sys

# --- the instruction set ------------------------------------------------------------------
BYTE_OPS = {  # opcode with d and f clear
    'ADDWF': 0x0700, 'ANDWF': 0x0500, 'COMF': 0x0900, 'DECF': 0x0300, 'DECFSZ': 0x0B00,
    'INCF': 0x0A00, 'INCFSZ': 0x0F00, 'IORWF': 0x0400, 'MOVF': 0x0800, 'RLF': 0x0D00,
    'RRF': 0x0C00, 'SUBWF': 0x0200, 'SWAPF': 0x0E00, 'XORWF': 0x0600,
}
FILE_ONLY = {'CLRF': 0x0180, 'MOVWF': 0x0080}
BIT_OPS = {'BCF': 0x1000, 'BSF': 0x1400, 'BTFSC': 0x1800, 'BTFSS': 0x1C00}
LITERAL_OPS = {
    'ADDLW': 0x3E00, 'ANDLW': 0x3900, 'IORLW': 0x3800, 'MOVLW': 0x3000, 'RETLW': 0x3400,
    'SUBLW': 0x3C00, 'XORLW': 0x3A00,
}
JUMP_OPS = {'CALL': 0x2000, 'GOTO': 0x2800}
NO_OPERAND = {'CLRW': 0x0100, 'NOP': 0x0000, 'RETURN': 0x0008, 'RETFIE': 0x0009,
              'SLEEP': 0x0063, 'CLRWDT': 0x0064}
IGNORED = {'LIST', 'RADIX', 'PROCESSOR', 'INCLUDE', '#INCLUDE', 'ERRORLEVEL', '__CONFIG'}

PROG_WORDS = 2048


class AsmError(Exception):
    pass


def tokenize_expr(s):
    toks = re.findall(
        r"[Hh]'[0-9A-Fa-f]+'|[Bb]'[01]+'|[Dd]'[0-9]+'|[Oo]'[0-7]+'|'.'|\.[0-9]+|0[xX][0-9A-Fa-f]+|"
        r"[0-9][0-9A-Fa-f]*[Hh]|[0-9]+|[A-Za-z_?][A-Za-z_0-9?]*|\$|<<|>>|[-+*/%&|^~()]", s)
    return toks


def eval_expr(text, symbols, pc):
    toks = tokenize_expr(text.strip())
    pos = [0]

    def peek():
        return toks[pos[0]] if pos[0] < len(toks) else None

    def take():
        t = peek()
        pos[0] += 1
        return t

    def number(t):
        u = t.upper()
        if u == '$':
            return pc
        if u.startswith("H'"):
            return int(t[2:-1], 16)
        if u.startswith("B'"):
            return int(t[2:-1], 2)
        if u.startswith("D'"):
            return int(t[2:-1], 10)
        if u.startswith("O'"):
            return int(t[2:-1], 8)
        if t.startswith("'") and len(t) == 3:
            return ord(t[1])
        if t.startswith('.'):
            return int(t[1:], 10)
        if u.startswith('0X'):
            return int(t, 16)
        if re.fullmatch(r'[0-9][0-9A-Fa-f]*[Hh]', t):
            return int(t[:-1], 16)
        if re.fullmatch(r'[0-9]+', t):
            return int(t, 10)       # MPASM's default radix is hexadecimal; this project always writes the radix
        raise AsmError("bad number %r" % t)

    def primary():
        t = take()
        if t is None:
            raise AsmError("expression ends too soon: %r" % text)
        if t == '(':
            v = expr(0)
            if take() != ')':
                raise AsmError("missing ) in %r" % text)
            return v
        if t == '-':
            return -primary()
        if t == '+':
            return primary()
        if t == '~':
            return ~primary()
        u = t.upper()
        if u in ('HIGH', 'LOW'):
            if take() != '(':
                raise AsmError("%s needs parentheses" % u)
            v = expr(0)
            if take() != ')':
                raise AsmError("missing ) after %s" % u)
            return (v >> 8) & 0xFF if u == 'HIGH' else v & 0xFF
        if re.fullmatch(r"[A-Za-z_?][A-Za-z_0-9?]*", t) and not re.fullmatch(r"[0-9][0-9A-Fa-f]*[Hh]", t):
            if t.upper() in symbols:
                return symbols[t.upper()]
            raise AsmError("undefined symbol %r" % t)
        return number(t)

    prec = {'|': 1, '^': 2, '&': 3, '<<': 4, '>>': 4, '+': 5, '-': 5, '*': 6, '/': 6, '%': 6}

    def expr(minp):
        left = primary()
        while True:
            op = peek()
            if op not in prec or prec[op] < minp:
                return left
            take()
            right = expr(prec[op] + 1)
            left = {'|': lambda a, b: a | b, '^': lambda a, b: a ^ b, '&': lambda a, b: a & b,
                    '<<': lambda a, b: a << b, '>>': lambda a, b: a >> b, '+': lambda a, b: a + b,
                    '-': lambda a, b: a - b, '*': lambda a, b: a * b,
                    '/': lambda a, b: a // b, '%': lambda a, b: a % b}[op](left, right)

    v = expr(0)
    if pos[0] != len(toks):
        raise AsmError("junk after expression: %r" % text)
    return v


def split_operands(s):
    """Splits on top-level commas (not inside parentheses or quotes)."""
    out, depth, cur, q = [], 0, '', False
    for ch in s:
        if ch == "'":
            q = not q
        if not q:
            if ch == '(':
                depth += 1
            elif ch == ')':
                depth -= 1
            elif ch == ',' and depth == 0:
                out.append(cur.strip())
                cur = ''
                continue
        cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


class Line:
    def __init__(self, no, label, op, operands, text):
        self.no, self.label, self.op, self.operands, self.text = no, label, op, operands, text


def parse(source):
    lines = []
    for no, raw in enumerate(source.splitlines(), 1):
        text = raw.split(';', 1)[0].rstrip()
        if not text.strip():
            continue
        starts_blank = text[0] in ' \t'
        body = text.strip()
        m = re.match(r'^(?:([A-Za-z_?][A-Za-z_0-9?]*):?)\s*(.*)$', body)
        label, rest = None, body
        first = body.split(None, 1)[0]
        known = (set(BYTE_OPS) | set(FILE_ONLY) | set(BIT_OPS) | set(LITERAL_OPS) | set(JUMP_OPS)
                 | set(NO_OPERAND) | IGNORED | {'ORG', 'END', 'EQU', 'DT', 'DW', 'DB'})
        if not starts_blank and first.upper().rstrip(':') not in known:
            label = first.rstrip(':')
            rest = body[len(first):].strip()
        elif not starts_blank and first.endswith(':'):
            label = first.rstrip(':')
            rest = body[len(first):].strip()
        if not rest:
            lines.append(Line(no, label, None, [], text))
            continue
        parts = rest.split(None, 1)
        op = parts[0].upper()
        args = parts[1].strip() if len(parts) > 1 else ''
        if op == 'EQU':
            lines.append(Line(no, label, 'EQU', [args], text))
            continue
        # `NAME EQU value` where the label was not recognised above (EQU is the second token)
        if len(parts) > 1 and args.upper().startswith('EQU'):
            lines.append(Line(no, op, 'EQU', [args[3:].strip()], text))
            continue
        lines.append(Line(no, label, op, split_operands(args), text))
    return lines


def assemble(source, warn=lambda m: None):
    lines = parse(source)
    symbols = {}
    # pass 1: addresses
    pc = 0
    for ln in lines:
        if ln.op == 'EQU':
            continue
        if ln.label and ln.op != 'EQU':
            symbols[ln.label.upper()] = pc
        if ln.op == 'ORG':
            pc = eval_expr(ln.operands[0], symbols, pc)
        elif ln.op in ('DT', 'DW', 'DB'):
            pc += sum(1 for _ in ln.operands)
        elif ln.op in IGNORED or ln.op in (None, 'END'):
            if ln.op == 'END':
                break
        else:
            pc += 1
    # EQUs may refer to labels and to earlier EQUs: evaluate in order, after labels are known
    for ln in lines:
        if ln.op == 'EQU':
            symbols[ln.label.upper()] = eval_expr(ln.operands[0], symbols, 0)
    # pass 2
    words = {}
    pc = 0

    def reg(expr, ln):
        v = eval_expr(expr, symbols, pc)
        if v < 0 or v > 0x1FF:
            raise AsmError("line %d: register %s out of range" % (ln.no, expr))
        if v > 0x7F:
            warn("line %d: register %s is above 7Fh; using its low 7 bits (the bank comes from STATUS)" % (ln.no, expr))
        return v & 0x7F

    def lit8(expr, ln):
        v = eval_expr(expr, symbols, pc)
        if v < -128 or v > 255:
            warn("line %d: literal %d does not fit in 8 bits; using %d" % (ln.no, v, v & 0xFF))
        return v & 0xFF

    for ln in lines:
        op = ln.op
        if op is None or op == 'EQU' or op in IGNORED:
            continue
        if op == 'END':
            break
        if op == 'ORG':
            pc = eval_expr(ln.operands[0], symbols, pc)
            continue
        if op in ('DT', 'DW', 'DB'):
            for o in ln.operands:
                v = eval_expr(o, symbols, pc)
                words[pc] = (0x3400 | (v & 0xFF)) if op == 'DT' else (v & 0x3FFF)
                pc += 1
            continue
        a = ln.operands
        try:
            if op in NO_OPERAND:
                if a:
                    raise AsmError("%s takes no operand" % op)
                w = NO_OPERAND[op]
            elif op in BYTE_OPS:
                if not 1 <= len(a) <= 2:
                    raise AsmError("%s needs f or f,d" % op)
                d = 1
                if len(a) == 2:
                    ds = a[1].strip().upper()
                    d = 0 if ds in ('W', '0') else 1 if ds in ('F', '1') else eval_expr(a[1], symbols, pc)
                    if d not in (0, 1):
                        raise AsmError("destination must be W or F")
                w = BYTE_OPS[op] | (d << 7) | reg(a[0], ln)
            elif op in FILE_ONLY:
                if len(a) != 1:
                    raise AsmError("%s needs one register" % op)
                w = FILE_ONLY[op] | reg(a[0], ln)
            elif op in BIT_OPS:
                if len(a) != 2:
                    raise AsmError("%s needs f,b" % op)
                b = eval_expr(a[1], symbols, pc)
                if not 0 <= b <= 7:
                    raise AsmError("bit number out of range")
                w = BIT_OPS[op] | (b << 7) | reg(a[0], ln)
            elif op in LITERAL_OPS:
                if len(a) != 1:
                    raise AsmError("%s needs a literal" % op)
                w = LITERAL_OPS[op] | lit8(a[0], ln)
            elif op in JUMP_OPS:
                if len(a) != 1:
                    raise AsmError("%s needs an address" % op)
                t = eval_expr(a[0], symbols, pc)
                if t < 0 or t >= PROG_WORDS:
                    raise AsmError("address %d is outside program memory" % t)
                w = JUMP_OPS[op] | (t & 0x7FF)
            else:
                raise AsmError("unsupported instruction or directive %r" % op)
        except AsmError as e:
            raise AsmError("line %d: %s  [%s]" % (ln.no, e, ln.text.strip()))
        if pc >= PROG_WORDS:
            raise AsmError("line %d: program does not fit in %d words" % (ln.no, PROG_WORDS))
        words[pc] = w
        pc += 1
    return words, symbols


def to_hex(words):
    """Intel HEX, byte addresses, little-endian words, in MPASM's record layout."""
    out = [':020000040000FA']
    addrs = sorted(words)
    i = 0
    while i < len(addrs):
        start = addrs[i]
        run = [words[start]]
        while len(run) < 8 and i + len(run) < len(addrs) and addrs[i + len(run)] == start + len(run):
            run.append(words[start + len(run)])
        data = b''.join(w.to_bytes(2, 'little') for w in run)
        rec = bytes([len(data), (start * 2 >> 8) & 0xFF, (start * 2) & 0xFF, 0]) + data
        out.append(':' + rec.hex().upper() + '%02X' % ((-sum(rec)) & 0xFF))
        i += len(run)
    out.append(':00000001FF')
    return '\r\n'.join(out) + '\r\n'


def to_cpp(words, name, source_name):
    n = max(words) + 1
    flat = [words.get(i, 0x3FFF) for i in range(n)]
    body = []
    for i in range(0, n, 8):
        body.append('    ' + ', '.join('0x%04X' % w for w in flat[i:i + 8]) + ',')
    return ('#pragma once\n// GENERATED by tools/pic16asm.py from firmware/varimode/%s. Do not edit; edit the assembly and rebuild\n'
            '// (make firmware). %d words of 14-bit PIC16F684 program memory, from address 0.\n'
            '#include <cstdint>\n\nnamespace paysched {\nnamespace firmware {\n\n'
            'static const int k%sWords = %d;\nstatic const uint16_t k%s[%d] = {\n%s\n};\n\n'
            '} // namespace firmware\n} // namespace paysched\n') % (
                os.path.basename(source_name), n, name, n, name, n, '\n'.join(body))


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('source')
    ap.add_argument('-o', '--output')
    ap.add_argument('--cpp', nargs=2, metavar=('OUT.hpp', 'NAME'))
    ap.add_argument('-q', '--quiet', action='store_true')
    args = ap.parse_args(argv)
    src = open(args.source, encoding='latin-1').read()
    warnings = []
    try:
        words, symbols = assemble(src, warnings.append)
    except AsmError as e:
        print('pic16asm: error: %s' % e, file=sys.stderr)
        return 1
    if not args.quiet:
        for w in warnings:
            print('pic16asm: warning: %s' % w, file=sys.stderr)
    hexa = to_hex(words)
    if args.output:
        open(args.output, 'w', newline='').write(hexa)
    if args.cpp:
        open(args.cpp[0], 'w').write(to_cpp(words, args.cpp[1], args.source))
    if not args.output and not args.cpp:
        sys.stdout.write(hexa)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
