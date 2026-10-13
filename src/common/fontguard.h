// fontguard.h - T-730: the pure rules of the font guard (src/coop-plugin/fontguard.cpp): MyGUI's EditText::getFontName body as
// this build was read against it, the jumps from its export to that body, whether a text needs the guard, and the layout of the
// empty name the guard hands back. Pure: no Windows, no MyGUI. src/coop-test/test_main.cpp tests the rules.
#pragma once

namespace swfont {

/* MyGUIEngine_x64.dll, EditText::getFontName (body at DLL offset 0xC58F0): mov rax,[rcx+0xE0] / add rax,8 / ret - the address of
   the name inside the text's font object, with no test for a missing font. */
const int kBodySize = 12;
const unsigned char kGetFontNameBody[kBodySize] = { 0x48, 0x8B, 0x81, 0xE0, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC0, 0x08, 0xC3 };
const int kEditTextFontOffset = 0xE0;   /* the text's font pointer, the field the body reads */
const int kMaxJumps = 4;                /* the export may be a jump stub; at most this many jumps are followed to the body */

/* 1 when the first kBodySize bytes are exactly the body above; 0 for a short or missing buffer or any byte different (a jump
   stub included). */
inline int FontGuardBodyMatches(const unsigned char* bytes, int n)
{
    if (bytes == 0 || n < kBodySize) return 0;
    for (int i = 0; i < kBodySize; ++i)
        if (bytes[i] != kGetFontNameBody[i]) return 0;
    return 1;
}

/* 1 when the text's font pointer is null - MyGUI's body would answer address 8 - so the guard answers instead; 0 otherwise. */
inline int FontGuardNeeded(const void* fontPtr)
{
    return fontPtr == 0 ? 1 : 0;
}

/* The jump at `at` (its first n bytes in b):
     kJumpRel32    E9 rel32     *next = the jump's target (at + 5 + rel32)
     kJumpIndirect FF 25 rel32  *next = the address of the 8-byte slot holding the target (at + 6 + rel32); the caller reads it
     kNoJump       anything else (or too few bytes); *next is not written */
enum { kNoJump = 0, kJumpRel32 = 1, kJumpIndirect = 2 };
inline int FontGuardJump(const unsigned char* b, int n, unsigned long long at, unsigned long long* next)
{
    if (b == 0 || next == 0) return kNoJump;
    if (n >= 5 && b[0] == 0xE9)
    {
        const unsigned int u = (unsigned int)b[1] | ((unsigned int)b[2] << 8) | ((unsigned int)b[3] << 16) | ((unsigned int)b[4] << 24);
        *next = at + 5ull + (unsigned long long)(long long)(int)u;
        return kJumpRel32;
    }
    if (n >= 6 && b[0] == 0xFF && b[1] == 0x25)
    {
        const unsigned int u = (unsigned int)b[2] | ((unsigned int)b[3] << 8) | ((unsigned int)b[4] << 16) | ((unsigned int)b[5] << 24);
        *next = at + 6ull + (unsigned long long)(long long)(int)u;
        return kJumpIndirect;
    }
    return kNoJump;
}

/* The game's runtime std::string (MSVC x64, VS2010 runtime MSVCP100, release): a 16-byte inline buffer, the size at +0x10, the
   capacity at +0x18 and the runtime's 8-byte allocator slot after it - 40 bytes; a capacity of 15 means the characters live in
   the inline buffer. The guard's empty name is one of these, zeroed with capacity 15. */
struct RuntimeString
{
    char buf[16];
    unsigned long long size;
    unsigned long long capacity;
    unsigned long long allocatorSlot;
};
const unsigned long long kInlineCapacity = 15;

}   /* namespace swfont */
