#!/usr/bin/env python3
"""Verify the actual linked tiny dictionary inside an nRF52840 UF2 artifact."""
import json,struct,sys,zlib
from pathlib import Path
p=Path(sys.argv[1]);b=p.read_bytes();assert len(b)%512==0
memory={};families=set();seen=set();declared=None
for off in range(0,len(b),512):
 block=b[off:off+512];magic0,magic1,flags,addr,size,num,count,family=struct.unpack_from('<8I',block)
 assert (magic0,magic1)==(0x0a324655,0x9e5d5157) and struct.unpack_from('<I',block,508)[0]==0x0ab16f30
 assert size<=476 and flags&0x2000 and family==0xada52840
 assert num not in seen;seen.add(num);declared=count;families.add(family)
 assert 0<=addr<0x100000 and addr+size<=0x100000
 for i,v in enumerate(block[32:32+size]):
  assert addr+i not in memory or memory[addr+i]==v
  memory[addr+i]=v
assert len(seen)==declared
lo,hi=min(memory),max(memory)+1;image=bytes(memory.get(i,255) for i in range(lo,hi));locations=[];start=0
while True:
 idx=image.find(b'JSC4',start)
 if idx<0:break
 addr=lo+idx
 def u32(a):return sum(memory[a+i]<<(8*i) for i in range(4))
 # Require exact known collection structure, not a magic constant in code.
 if memory.get(addr+4)==1 and memory.get(addr+5)==0 and u32(addr+8)==14 and addr<=u32(addr+12)<addr+142:locations.append(addr)
 start=idx+4
result={'file':str(p.resolve()),'family':'nRF52840','blocks':len(seen),'min_flash_address':hex(lo),'end_flash_address':hex(hi),'payload_bytes':len(memory),'dictionary_candidates':[hex(a) for a in locations]}
if 'left' in p.name:
 assert len(locations)==1;origin=locations[0]
 def u32(a):return sum(memory[a+i]<<(8*i) for i in range(4))
 assert u32(origin+32)==u32(u32(origin+12)+u32(origin+8))
 definition=u32(origin+36);assert origin<=definition<origin+142
 text=u32(definition+8);arr=u32(definition+12);sd=arr
 assert origin<=sd and sd+12<=origin+142, "stroke table outside dictionary"
 mask=u32(sd);data=u32(sd+4);blocks=u32(sd+8)
 assert origin<=data<=blocks and blocks+20<=origin+142, "invalid stroke-table pointers"
 def lookup(word):
  index=zlib.crc32(struct.pack('<I',word))&mask
  for _ in range(mask+1):
   block=blocks+20*(index//128);mi=(index%128)//32;bi=index%32;v=u32(block+4*mi)
   if not v&(1<<bi):return None
   rank=((((v<<(31-bi))&0xffffffff).bit_count()+u32(block+16)+sum(u32(block+4*j).bit_count() for j in range(mi)))&0xffffffff)
   ptr=data+rank*6;assert origin<=ptr<origin+142
   stroke=sum(memory[ptr+3+j]<<(8*j) for j in range(3))
   if stroke==word:
    t=text+sum(memory[ptr+j]<<(8*j) for j in range(3));out=[]
    while memory[t]:out.append(memory[t]);t+=1
    return bytes(out).decode()
   index=(index+1)&mask
  return None
 words=[0x14,0x80000,0x80108,0x2,0x80000,0x80108];phrase=' '.join(lookup(w) for w in words);assert phrase=='if the cat is the cat';result['linked_dictionary_phrase_lookup']=phrase;result['dictionary_bytes']=142
else:assert not locations
print(json.dumps(result,indent=2))
