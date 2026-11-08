"""Add Lapwing's repeat-translation command to a private pinned-engine copy."""

from pathlib import Path
import sys


def prepare(source):
    anchor = '      if (Str::Eq(lookupText, "=retro_toggle_asterisk")) {'
    if source.count(anchor) != 1:
        raise ValueError("Unexpected pinned segment builder near retro-toggle command")
    branch = '''      if (Str::Eq(lookupText, "=repeat_last_translation")) {
        // Replay the complete prior translation, preserving formatting commands.
        // Keep the repeat stroke as its own segment so one undo removes one repeat.
        // The hidden marker requests history when this segment is rebuilt later.
        constexpr char marker[] = "{:==repeat_last_translation}";
        BufferWriter writer;
        writer.WriteString(marker);
        if (context.segments.IsNotEmpty()) {
          const char *previous = context.segments.Back().lookup.GetText();
          // Repeating a repeat must not grow a chain of hidden markers.
          while (Str::HasPrefix(previous, marker)) {
            previous += sizeof(marker) - 1;
          }
          writer.WriteString(previous);
        }
        context.segments.Add(StenoSegment(
            length, SegmentLookupType::DIRECT, states + offset,
            StenoDictionaryLookupResult::CreateFromBuffer(writer)));
        offset += length;
        lookup.Destroy();
        return true;
      }
'''
    return source.replace(anchor, branch + anchor)


if __name__ == "__main__":
    Path(sys.argv[2]).write_text(prepare(Path(sys.argv[1]).read_text()))
