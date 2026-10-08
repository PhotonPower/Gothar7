// The implementation of miniaudio and of stb_vorbis (OGG Vorbis decoding, ADR 0007), once for the module.
// stb_vorbis comes in twice: its declarations before miniaudio (which then decodes Vorbis), its body after.
// Third-party code: its warnings are silenced here (GCC/Clang treat the include directory as a system one).

#if defined(_MSC_VER)
#pragma warning(push, 0)
#pragma warning(disable : 4701 4703 4244 4245 4456 4457 4127)
#endif

#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include <miniaudio.h>

#undef STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
