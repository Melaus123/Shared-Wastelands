// TripleInt.h - the game's TripleInt: three ints. 0x0C bytes, `value` at +0.
// Only the three-int constructor is used; gamecalls.cpp defines it (the game's whole function, read in the
// exe's bytes). The other members are declared and never defined, so a new use of one fails to link
// instead of reaching anything. No static ZERO is declared.
#pragma once

class TripleInt
{
public:
    int value[0x3];
    TripleInt(const TripleInt& who);
    TripleInt(int a, int b, int c);
    TripleInt();
    const TripleInt& operator=(const TripleInt& a);
    int operator[](int i) const;
    int& operator[](int i);
};
