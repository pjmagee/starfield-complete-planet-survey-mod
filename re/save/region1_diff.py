"""
Alignment-based diff of GlobalData region 1 (body[offsetA:offsetB]) between two saves.

Region 1 absolute offsets differ between saves (insertions/deletions elsewhere shift
everything), so a same-offset XOR diff is meaningless. We use difflib.SequenceMatcher
over the two byte slices to recover the genuine edit runs (replace/insert/delete),
ignoring shift noise. For each differing run we print a hex window with context.

Usage: py region1_diff.py <nameA> <nameB>   (names: 12 / 14 / 21)
"""
import sys, struct, difflib
sys.path.insert(0, r"D:\Projects\pjmagee\starfield-complete-planet-survey-mod\re\save")
from sfs_container import BCPSContainer
from sfs_body import SFSBody

BASE = r"C:\Users\patri\OneDrive\Documents\My Games\Starfield\Saves"
PATHS = {
 '12': r'\Save12_98A838ADM467265736820436861726163746572_000043_20260623182623_1_0_4.sfs',
 '14': r'\Save14_98A838ADM467265736820436861726163746572_000043_20260623182648_1_0_4.sfs',
 '21': r'\Save21_98A838ADM467265736820436861726163746572_000043_20260623233715_1_0_4.sfs',
}

def region1(name):
    body = BCPSContainer(BASE + PATHS[name]).decompress()
    sb = SFSBody(body); lt = sb.loc_table
    a, b = lt['offsetA'], lt['offsetB']
    return body, a, b, body[a:b]

def hexrow(buf, base, off, n, mark=()):
    s = buf[off:off+n]
    out = []
    for r in range(0, n, 16):
        row = s[r:r+16]
        asc = ''.join(chr(c) if 32 <= c < 127 else '.' for c in row)
        out.append('    @0x%05X: %-47s %s' % (base+r, row.hex(' '), asc))
    return '\n'.join(out)

def main():
    nA, nB = sys.argv[1], sys.argv[2]
    bodyA, aA, bA, r1A = region1(nA)
    bodyB, aB, bB, r1B = region1(nB)
    print(f'Save{nA} region1 [0x{aA:X}:0x{bA:X}] len {len(r1A)}')
    print(f'Save{nB} region1 [0x{aB:X}:0x{bB:X}] len {len(r1B)}')
    sm = difflib.SequenceMatcher(None, r1A, r1B, autojunk=False)
    blocks = sm.get_opcodes()
    nrep = sum(1 for t,*_ in blocks if t != 'equal')
    print(f'edit runs (non-equal opcodes): {nrep}')
    CTX = 16
    idx = 0
    for tag, i1, i2, j1, j2 in blocks:
        if tag == 'equal':
            continue
        idx += 1
        # absolute body offsets
        absA = aA + i1
        absB = aB + j1
        print('\n' + '='*78)
        print(f'[run {idx}] {tag}  A:[0x{absA:X}..0x{aA+i2:X}] ({i2-i1}B)  B:[0x{absB:X}..0x{aB+j2:X}] ({j2-j1}B)')
        # show A side with context
        sA = max(0, i1-CTX); eA = min(len(r1A), i2+CTX)
        sB = max(0, j1-CTX); eB = min(len(r1B), j2+CTX)
        print(f'  --- Save{nA} (off 0x{aA+sA:X}, run starts +0x{i1-sA:X}) ---')
        print(hexrow(r1A, aA+sA, sA, eA-sA))
        print(f'  --- Save{nB} (off 0x{aB+sB:X}, run starts +0x{j1-sB:X}) ---')
        print(hexrow(r1B, aB+sB, sB, eB-sB))

if __name__ == '__main__':
    main()
