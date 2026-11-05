#!/usr/bin/env python3
"""Apply the pinned engine's Zephyr compatibility edits portably and idempotently."""
import re,sys
from pathlib import Path
root=Path(sys.argv[1])
for relative,old,new in [
 ('dictionary/user_dictionary.h','constexpr StenoUserDictionaryData(const uint8_t','StenoUserDictionaryData(const uint8_t'),
 ('orthography.h','#define USE_ORTHOGRAPHY_CACHE 1','#define USE_ORTHOGRAPHY_CACHE 0')]:
 p=root/relative;text=p.read_text();updated=text.replace(old,new)
 if updated!=text:p.write_text(updated)
pattern=re.compile(r'(?:^|\n)(TEST_BEGIN\(.*?TEST_END)',re.DOTALL)
count=0
for p in root.rglob('*.cc'):
 text=p.read_text();updated=pattern.sub('',text)
 if updated!=text:p.write_text(updated);count+=1
print(f'Engine prepared; removed test blocks in {count} files')
