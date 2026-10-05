// stb_vorbis as a single translation unit (Ogg Vorbis decode for MusicStream).
// Built as EmeraldVorbis without ASan on MSVC (see this folder's CMakeLists.txt).
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <stb_vorbis.c>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
