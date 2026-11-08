"""Guard the pinned engine's empty stroke history in a private build copy."""

from pathlib import Path
import sys


source = Path(sys.argv[1]).read_text()
anchor = "  if (history.GetCount() < maximumConversionStrokes) {"
if source.count(anchor) != 1:
    raise ValueError("Unexpected pinned engine history check")

replacement = """  // A dictionary list with no active entries has a maximum outline of zero.
  // In that case an empty history still has no last stroke to examine.
  if (history.GetCount() == 0 || history.GetCount() < maximumConversionStrokes) {"""
Path(sys.argv[2]).write_text(source.replace(anchor, replacement))
