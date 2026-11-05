"""Check the binary with the pinned engine's lookup algorithm, not the builder's slot list."""
import importlib.util,json,struct,zlib
from pathlib import Path
spec=importlib.util.spec_from_file_location('compiler',Path(__file__).with_name('compile_dict.py'));c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
def u32(b,o):return struct.unpack_from('<I',b,o)[0]
def lookup(blob,stroke):
 definition=u32(blob,36);maximum=blob[definition+1];text=u32(blob,definition+8);arr=u32(blob,definition+12)
 words=c.parse_outline(stroke);n=len(words)
 if n>maximum:return None
 sd=arr+12*n;mask=u32(blob,sd)
 if mask==0:return None
 data=u32(blob,sd+4);blocks=u32(blob,sd+8)
 index=zlib.crc32(b''.join(struct.pack('<I',v) for v in words))&mask
 for _ in range(mask+1):
  block=blocks+20*(index//128);bit=index%128;mi=bit//32;bi=bit%32;word=u32(blob,block+4*mi)
  if not word&(1<<bi):return None
  # Match C++ uint32 left-shift/inclusive-popcount and unsigned base offset.
  rank=((((word<<(31-bi))&0xffffffff).bit_count()+u32(blob,block+16)+sum(u32(blob,block+4*j).bit_count() for j in range(mi)))&0xffffffff)
  ptr=data+rank*(3+3*n)
  assert data<=ptr and ptr+3+3*n<=blocks,(ptr,data,blocks)
  found=tuple(int.from_bytes(blob[ptr+3+3*j:ptr+6+3*j],'little') for j in range(n))
  if found==words:
   off=int.from_bytes(blob[ptr:ptr+3],'little');end=blob.index(0,text+off);return blob[text+off:end].decode()
  index=(index+1)&mask
 return None
assert c.parse_stroke('TP')==0x14
assert c.parse_stroke('-T')==0x80000
assert c.parse_stroke('KAT')==0x80108
assert c.parse_stroke('S')==2
raw=json.loads((Path(__file__).resolve().parents[2]/'dicts/cat-test.json').read_text());b=c.DictionaryCollectionBuilder().build([('cat-test',raw)])
assert u32(b,0)==0x3443534a
assert u32(b,32)==u32(b,u32(b,12)+u32(b,8))
for stroke,value in raw.items():assert lookup(b,stroke)==value
assert lookup(b,'TKPW') is None
seq=['TP','-T','KAT','S','-T','KAT'];output=' '.join(lookup(b,s) for s in seq);assert output=='if the cat is the cat'
# Exercise collisions and block base offsets beyond the four-word fixture.
ks=['S','T','K','P','W','H','R'];large={''.join(k for i,k in enumerate(ks) if bits&(1<<i)):'word'+str(bits) for bits in range(1,128)}
large.update({'KAT/TP':'two strokes','TP/-T/KAT':'three strokes'})
big=c.DictionaryCollectionBuilder().build([('larger-test',large)])
for stroke,value in large.items():assert lookup(big,stroke)==value,(stroke,lookup(big,stroke),value)
print(f'PASS: {len(b)} byte fixture, exact six-stroke phrase, missing entry, 129-entry collision/multi-stroke lookup')
