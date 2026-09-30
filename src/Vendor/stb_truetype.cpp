// The single translation unit that compiles stb_truetype (and stb_rect_pack, which its
// PackFontRanges uses for tight atlases when it is included first).
#define STB_RECT_PACK_IMPLEMENTATION
#include <stb_rect_pack.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
