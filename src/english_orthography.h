#pragma once

#include "orthography.h"

// Common spelling changes for separate suffix strokes. Explicit dictionary
// outlines still take precedence. Keep rules ordered by specificity.
inline constexpr StenoOrthographyRule english_suffix_rules[] = {
    {R"(^(.+)ie \^ing$)", R"(\1ying)"},
    {R"(^(dye|singe|tinge|binge|swinge) \^(ing|able)$)", R"(\1\2)"},
    {R"(^(.+[^aeiou])e \^(ing|ed)$)", R"(\1\2)"},
    {R"(^(.*e)e \^ed$)", R"(\1ed)"},
    {R"(^(.+[bcdfghjklmnpqrstvwxz])y \^s$)", R"(\1ies)"},
    {R"(^(.+[bcdfghjklmnpqrstvwxz])y \^ed$)", R"(\1ied)"},
    {R"(^(.*(?:s|sh|x|z|tch)) \^s$)", R"(\1es)"},
    {R"(^(.*(?:ea|oa|ee|oo|ou|au|n|l)ch) \^s$)", R"(\1es)"},
};

inline constexpr StenoOrthography english_orthography = {
    .rules = {sizeof(english_suffix_rules) / sizeof(english_suffix_rules[0]), english_suffix_rules},
    .aliases = {0, nullptr},
    .autoSuffixMask = StenoStroke(0),
    .autoSuffixes = {0, nullptr},
    .reverseAutoSuffixes = {0, nullptr},
    .reverseSuffixes = {0, nullptr},
};
