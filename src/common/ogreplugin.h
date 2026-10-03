/* ogreplugin.h - the pieces SharedWastelandsLoader.dll needs to register itself as an Ogre plugin object without Ogre's
 * headers and without a C runtime: the layout of the name string Ogre reads, the vtable slots Ogre calls, and the names
 * of the OgreMain_x64.dll exports used.
 *
 * WHY AN OGRE PLUGIN OBJECT: Kenshi loads the Ogre plugins named in Plugins_x64.cfg (calling each one's dllStartPlugin)
 * BEFORE its launcher window; the launcher writes data\mods.cfg only when the player presses OK. Ogre's Root::initialise,
 * which the game reaches only after the launcher closed with OK, calls initialise() on every plugin object installed with
 * Root::installPlugin. So the loader installs one such object from dllStartPlugin and makes its decision in initialise().
 *
 * THE OBJECT: Ogre::Plugin has no data members (Plugin -> PluginAlloc has none), so an object is one pointer, its vtable.
 * The vtable has six slots (Ogre::Plugin's own vtable in OgreMain_x64.dll, ??_7Plugin@Ogre@@6B@, has six; read from the
 * DLL's bytes): +0x00 the scalar deleting destructor, +0x08 getName, +0x10 install, +0x18 initialise, +0x20 shutdown,
 * +0x28 uninstall. OgreMain's Root::installPlugin calls +0x08 and +0x10 (and +0x18 when Root is already initialised),
 * Root::initialisePlugins calls +0x18, Root::shutdownPlugins +0x20, Root::uninstallPlugin +0x08, +0x20 and +0x28 (read
 * from the same DLL's bytes). Ogre never deletes an object it did not create, so +0x00 is never called.
 *
 * THE TYPE-INFORMATION SLOT: with MSVC's RTTI the 8 bytes just before a vtable hold a pointer to the class's "complete-
 * object locator", which typeid and dynamic_cast read. The loader's table is preceded by the locator Ogre::Plugin's own
 * vtable has (its slot [-1]), so code walking Ogre's installed plugins sees an Ogre::Plugin. IsObjectLocator checks a
 * pointer really is one before it is copied.
 *
 * THE NAME: getName returns `const Ogre::String&`, and Ogre::String is VS2010's std::string (OgreMain_x64.dll imports
 * MSVCP100.dll; OGRE_STRING_USE_CUSTOM_MEMORY_ALLOCATOR is 0). VS2010's x64 std::string is 40 bytes: the character buffer
 * (or, above 15 characters, a pointer) at +0, the length at +16, the capacity at +24 (15 while the text is in the buffer)
 * and the empty allocator at +32. Ogre only reads the name (it copies it into its log line), so a name of at most 15
 * characters is laid out here by hand, in the buffer. The offline suite (built with the same VS2010 library) checks this
 * layout against a real std::string.
 *
 * PURE, NO LIBRARY CALLS (SharedWastelandsLoader.dll has no C runtime). C++03 (VS2010).
 */
#ifndef KENSHICOOP_OGREPLUGIN_H
#define KENSHICOOP_OGREPLUGIN_H

namespace swogre {

/* VS2010's x64 std::string, for a text of at most 15 characters (kept in the buffer) */
struct Vs2010String
{
    char buf[16];
    unsigned long long size;
    unsigned long long capacity;
    unsigned long long allocator;   /* the empty allocator's 8 bytes - never read */
};

enum { kVs2010StringInBuffer = 15 };

/* Lay `text` out in *s as VS2010's std::string would hold it; false (and *s holds "") when it is longer than 15 characters. */
inline bool Vs2010StringSet(Vs2010String* s, const char* text)
{
    unsigned long long n = 0;
    while (text != 0 && text[n] != 0) ++n;
    if (n > (unsigned long long)kVs2010StringInBuffer) n = 0;
    for (int i = 0; i < 16; ++i) s->buf[i] = (i < (int)n) ? text[i] : 0;
    s->size = n;
    s->capacity = kVs2010StringInBuffer;
    s->allocator = 0;
    return text != 0 && text[n] == 0;
}

/* Ogre::Plugin's vtable slots, by index (offset / 8) */
enum PluginSlot
{
    kSlotDestructor = 0,
    kSlotGetName = 1,
    kSlotInstall = 2,
    kSlotInitialise = 3,
    kSlotShutdown = 4,
    kSlotUninstall = 5,
    kPluginSlots = 6
};

/* OgreMain_x64.dll and the exports used (their exact decorated names, read from the DLL's export table):
   static Root* Root::getSingletonPtr()           - 0 when no Root exists
   void Root::installPlugin(Plugin*)              - this in rcx, the plugin in rdx
   void Root::uninstallPlugin(Plugin*)            - the same */
inline const wchar_t* OgreMainDll() { return L"OgreMain_x64.dll"; }
inline const char* RootGetSingletonPtrExport() { return "?getSingletonPtr@Root@Ogre@@SAPEAV12@XZ"; }
inline const char* RootInstallPluginExport() { return "?installPlugin@Root@Ogre@@QEAAXPEAVPlugin@2@@Z"; }
inline const char* RootUninstallPluginExport() { return "?uninstallPlugin@Root@Ogre@@QEAAXPEAVPlugin@2@@Z"; }
/* Ogre::Plugin's own vtable (an exported table of pointers, not a function) */
inline const char* PluginVtableExport() { return "??_7Plugin@Ogre@@6B@"; }

/* MSVC x64's complete-object locator is six 32-bit little-endian fields: signature (1 on x64), the object's offset in the
   complete object, the constructor-displacement offset, then the image-relative addresses of the type descriptor, the
   class hierarchy descriptor and the locator itself. True when `col` is a locator of a whole object (offset 0) inside the
   image that starts at `image` and is `imageSize` bytes long: all 24 bytes inside it, 4-byte aligned, signature 1, both
   descriptors inside the image, and its own image-relative address the one it is at. Reads only those 24 bytes, and only
   once they are known to lie inside the image. */
inline bool IsObjectLocator(const unsigned char* image, unsigned long long imageSize, const void* col)
{
    const unsigned long long b = (unsigned long long)image, c = (unsigned long long)col;
    if (image == 0 || col == 0 || imageSize < 24 || c < b || c - b > imageSize - 24 || ((c - b) & 3) != 0) return false;
    const unsigned char* p = (const unsigned char*)col;
    unsigned long long f[6];
    for (int i = 0; i < 6; ++i)
        f[i] = (unsigned long long)p[4 * i] | ((unsigned long long)p[4 * i + 1] << 8) | ((unsigned long long)p[4 * i + 2] << 16)
               | ((unsigned long long)p[4 * i + 3] << 24);
    return f[0] == 1 && f[1] == 0 && f[3] != 0 && f[3] < imageSize && f[4] != 0 && f[4] < imageSize && f[5] == c - b;
}

}   /* namespace swogre */

#endif
