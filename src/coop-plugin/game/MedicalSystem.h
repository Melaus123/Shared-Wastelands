// MedicalSystem.h - the game's MedicalSystem and MedicalSystem::HealthPartStatus.
//
// MedicalSystem is 0x1B0 bytes and polymorphic (+0 vtable pointer); Character embeds one at +0x458. Its fields
// our code reads by name are declared at their offsets in game/gamelayout.h, the rest is padding. HealthPartStatus
// (0x68, NOT polymorphic) is declared in full. Every offset and both sizes are asserted in
// game/layout_asserts.inl. BodyPartKind holds only its last enumerator (the enum convention, game/Enums.h).
// Member functions are the game's (defined in gamecalls.cpp), declared with the game functions' own parameter and return types.
#pragma once

#include "forward.h"
#include "Enums.h"

class MedicalSystem
{
public:
    class HealthPartStatus
    {
    public:
        enum BodyPartKind { BODY_PART_HEAD = 3 };

        GameData* data;
        MedicalSystem::HealthPartStatus::BodyPartKind partKind;
        MedicalSystem* medical;
        Character* me;
        BodySide side;
        RobotLimbItem* prosthetic;
        bool regenerates;
        bool collapses;
        bool fatal;
        float knockoutScale;
        float hitWeight;
        float hitWeightScale;
        float flesh;
        float stunDamage;
        float bandageLevel;
        float splintLevel;
        float limbWear;
        float maxHealthBase;
        float age;
        float healthScale;
        float fleshHealthFraction;

        void recomputeHealth();
    };

    void* vtbl_;
    unsigned char pad_8[0x60 - 0x8];
    float hunger;
    float fed;
    unsigned char pad_68[0x70 - 0x68];
    float blood;
    unsigned char pad_74[0x78 - 0x74];
    float bleedRate;
    unsigned char pad_7C[0xA0 - 0x7C];
    float knockoutClock;
    unsigned char pad_A4[0x158 - 0xA4];
    float nextKnockoutAt;
    unsigned char pad_15C[0x160 - 0x15C];
    bool crippled;
    bool unconcious;
    bool lowHealthKnockout;
    bool bloodLossShock;
    bool dead;
    bool rightArmUsable;
    bool leftArmUsable;
    unsigned char pad_167[0x1B0 - 0x167];

    MedicalSystem::HealthPartStatus* partAt(unsigned __int64 index);
    int countBodyParts() const;
    void clampHealth();

private:
    ~MedicalSystem(); // virtual in the game. Never defined.
};
