// GameOptions.h - the game's GameOptions, the game's options.
//
// 0xE8 bytes, NOT polymorphic, no base. viewDistance is declared at its offset in game/gamelayout.h; the rest is
// padding (the first gap as 8-byte words, so the class keeps 8-byte alignment). The size and the offset are
// asserted in game/layout_asserts.inl.
#pragma once

class GameOptions
{
public:
    unsigned __int64 pad_0[0x18 / 8];
    float viewDistance;
    unsigned char pad_1C[0xE8 - 0x1C];
};
