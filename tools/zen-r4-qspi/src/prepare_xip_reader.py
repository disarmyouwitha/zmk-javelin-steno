"""Use synchronized word loads in the pinned compact dictionary lookup path."""

from pathlib import Path
import sys


path = Path(sys.argv[1])
source = path.read_text()
changes = (
    ('#include "compact_map_dictionary.h"', '#include "compact_map_dictionary.h"\n#include "xip_load.h"'),
    ('masks[bitIndex / 32] &', 'xip_load_word(&masks[bitIndex / 32]) &'),
    ('strokes[i] != this->strokes[i].ToUint32()', 'strokes[i] != xip_load_u24(&this->strokes[i])'),
    ('block.masks[maskIndex] <<', 'xip_load_word(&block.masks[maskIndex]) <<'),
    ('+ block.baseOffset;', '+ xip_load_word(&block.baseOffset);'),
    ('PopCount(block.masks[i])', 'PopCount(xip_load_word(&block.masks[i]))'),
    ('textBlock + entry->textOffset.ToUint32());', 'textBlock + xip_load_u24(&entry->textOffset));'),
)
for old, new in changes:
    if source.count(old) != 1:
        raise SystemExit(f"Unexpected pinned compact dictionary source near {old!r}")
    source = source.replace(old, new)
path.write_text(source)
