#!/usr/bin/env python3
"""Trace the original generated Pop crop into the physical 48x48 sprite grid.

Requires Pillow. Only the eye rim is hinted by hand after area resampling;
the mantle, arms and three hanging tentacles come directly from the concept.
"""
from pathlib import Path
from PIL import Image

ROOT=Path(__file__).resolve().parents[2]
source=Image.open(Path(__file__).with_name('pop-source.png')).convert('L')
scale=46/max(source.size)
size=tuple(round(v*scale) for v in source.size)
small=source.resize(size,Image.Resampling.BOX).point(lambda v:0 if v<128 else 255)
image=Image.new('L',(48,48),255)
image.paste(small,((48-size[0])//2,(48-size[1])//2))
idle=[[int(image.getpixel((x,y))==0) for x in range(48)] for y in range(48)]
# Keep the left highlight enclosed when its subpixel black rim is quantized.
for y,x in [(17,21),(17,22),(18,22),(18,23)]:idle[y][x]=1
blink=[r[:] for r in idle]
for y in range(14,29):
    black=[x for x in range(48) if blink[y][x]]
    if black:
        for x in range(min(black),max(black)+1):blink[y][x]=1
for cx in (19,31):
    for dx,dy in [(-2,0),(-1,-1),(0,-1),(1,0),(2,0)]:blink[23+dy][cx+dx]=0

def shift(a,dy):
    out=[[0]*48 for _ in range(48)]
    for y in range(48):
        if 0<=y+dy<48:out[y+dy]=a[y][:]
    return out
spark=shift(idle,-1)
for y,x in [(1,3),(1,44),(5,1),(5,46)]:spark[y][x]=1
sprites={'idle':idle,'blink':blink,'typing':shift(idle,1),'jump':shift(idle,-1),'spark':spark,'land':shift(blink,1),'sleep':blink}
header='''/* Pop traced from the original concept crop in tools/artwork/pop-source.png.
 * Native 48x48 binary alpha; all previous revisions remain in their own branches.
 */
#define PIXEL(bits, bit) (((bits) & (1u << (bit))) ? 255 : 0)
#define BYTE(bits) PIXEL(bits,7), PIXEL(bits,6), PIXEL(bits,5), PIXEL(bits,4), \\
                   PIXEL(bits,3), PIXEL(bits,2), PIXEL(bits,1), PIXEL(bits,0)
#define ROW(a,b,c,d,e,f) BYTE(a), BYTE(b), BYTE(c), BYTE(d), BYTE(e), BYTE(f)
'''
for name,grid in sprites.items():
    header+=f'\nstatic const uint8_t {name}_pixels[] = {{\n'
    for row in grid:
        values=[int(''.join(map(str,row[x:x+8])),2) for x in range(0,48,8)]
        header+='    ROW('+','.join(f'0x{v:02x}' for v in values)+'), /* '+''.join('#' if v else '.' for v in row)+' */\n'
    header+='};\nPET_IMAGE('+name+');\n'
header+='\n#undef ROW\n#undef BYTE\n#undef PIXEL\n'
(ROOT/'src/zen_r4/display/pet_sprites.h').write_text(header)
