"""
FAST anchored diff of GlobalData region 1 between two saves.

Strategy: build an index of every 24-byte window in B keyed by hash. Walk A; at each
position try to extend a common run using B's index (greedy LCS-ish anchoring). Emit
the non-matching gaps as edit runs. This is O(n) vs difflib's O(n^2) and finishes in
seconds on 0.5 MB.

Usage: py region1_diff_fast.py <A> <B>   (names: 12 / 14 / 21)
"""
import sys
sys.path.insert(0, r"D:\Projects\pjmagee\starfield-complete-planet-survey-mod\re\save")
from sfs_container import BCPSContainer
from sfs_body import SFSBody

BASE = r"C:\Users\patri\OneDrive\Documents\My Games\Starfield\Saves"
PATHS = {
 '12': r'\Save12_98A838ADM467265736820436861726163746572_000043_20260623182623_1_0_4.sfs',
 '14': r'\Save14_98A838ADM467265736820436861726163746572_000043_20260623182648_1_0_4.sfs',
 '21': r'\Save21_98A838ADM467265736820436861726163746572_000043_20260623233715_1_0_4.sfs',
}
W = 32  # anchor window

def region1(name):
    body = BCPSContainer(BASE + PATHS[name]).decompress()
    sb = SFSBody(body); lt = sb.loc_table
    a, b = lt['offsetA'], lt['offsetB']
    return body, a, b, body[a:b]

def hexrow(buf, base, off, n):
    s = buf[off:off+n]; out = []
    for r in range(0, n, 16):
        row = s[r:r+16]
        asc = ''.join(chr(c) if 32 <= c < 127 else '.' for c in row)
        out.append('    @0x%05X: %-47s %s' % (base+r, row.hex(' '), asc))
    return '\n'.join(out)

def build_index(buf):
    idx = {}
    for i in range(0, len(buf) - W):
        idx.setdefault(buf[i:i+W], i)  # first occurrence
    return idx

def main():
    nA, nB = sys.argv[1], sys.argv[2]
    bodyA, aA, bA, A = region1(nA)
    bodyB, aB, bB, B = region1(nB)
    print(f'Save{nA} R1 [0x{aA:X}:0x{bA:X}] len {len(A)}   Save{nB} R1 [0x{aB:X}:0x{bB:X}] len {len(B)}')
    idxB = build_index(B)
    i = 0; j = 0
    runs = []  # (i1,i2,j1,j2)
    N = len(A)
    while i < N:
        win = A[i:i+W]
        jb = idxB.get(win, -1)
        if jb != -1 and jb >= j:
            # matched anchor: if there is a gap before it, record the edit run
            if i != j or jb != j:
                pass
            gi1, gj1 = i, j
            # everything A[gi1:i] vs B[gj1:jb] is the differing gap (could be empty)
            if i > gi1 or jb > gj1:
                if (i - gi1) or (jb - gj1):
                    runs.append((gi1, i, gj1, jb))
            # extend the match as far as bytes agree
            si, sj = i, jb
            while i < N and sj < len(B) and A[i] == B[sj]:
                i += 1; sj += 1
            j = sj
        else:
            i += 1
    # tail
    if j < len(B) or i < N:
        runs.append((i, N, j, len(B)))
    # merge/clean: drop zero-length, merge adjacent
    clean = [(i1,i2,j1,j2) for (i1,i2,j1,j2) in runs if (i2-i1) or (j2-j1)]
    print(f'edit runs: {len(clean)}')
    for k,(i1,i2,j1,j2) in enumerate(clean,1):
        absA=aA+i1; absB=aB+j1
        print('\n'+'='*78)
        print(f'[run {k}] A:[0x{absA:X}..0x{aA+i2:X}] ({i2-i1}B)  B:[0x{absB:X}..0x{aB+j2:X}] ({j2-j1}B)')
        C=16
        sA=max(0,i1-C); eA=min(len(A),i2+C)
        sB=max(0,j1-C); eB=min(len(B),j2+C)
        print(f'  --- Save{nA} ---'); print(hexrow(A, aA+sA, sA, eA-sA))
        print(f'  --- Save{nB} ---'); print(hexrow(B, aB+sB, sB, eB-sB))

if __name__ == '__main__':
    main()
