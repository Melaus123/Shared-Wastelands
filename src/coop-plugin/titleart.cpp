// titleart.cpp - T-513 (owner 446/447): while the mod is on, Kenshi's title screen shows the mod's own art,
// <mod folder>\title-background.png, in place of the game's background.
//
// WHERE IT GOES. Kenshi_MainMenu.layout puts one full-screen ImageBox under Root (position_real 0 0 1 1, align Stretch) drawing
// the game's ImageResource Kenshi_TitleScreen; the menu buttons are its children. That ImageBox - found every frame as the
// parent of the layout's ExitButton, as ui.cpp and bugreport.cpp find it - is given the mod's texture. Nothing else changes:
// the game's Kenshi_TitleScreen resource stays as it is, so loading screens and every in-game widget keep Kenshi's art.
//
// HOW THE FILE BECOMES A TEXTURE (once per process, kept). MyGUI's Ogre platform reads GUI files from one Ogre resource group
// (its data manager asks Ogre for a file by name in that group, and its textures load from that group). That group is the
// one holding Kenshi_MainMenu.layout, which MyGUI itself loaded. The mod folder is added to it as a read-only file-system
// location, not searched below (only the folder's own files), and then MyGUI's render manager makes a texture of the file
// (RenderManager::createTexture + ITexture::loadFromFile, Ogre's own image loading) - the road the game's own GUI images take.
// ImageBox::setImageInfo then draws one crop of it: the part that COVERs the box (src/common/titleart.h CoverCrop - the art's
// shape kept, no empty edge; centred across, the top kept so the painted title always shows). A new box size (a new window
// size, or the title screen made again) gets a new crop; the box carries a user string naming the size its crop was made for.
//
// WHEN ANYTHING FAILS - no file, no resource group, Ogre or MyGUI refusing, a texture with no size - the game's own art stays
// and one log line says why. A fault in a MyGUI or Ogre call turns the feature off for the rest of the process. Every such call
// runs behind an SEH frame with no C++ object in it (C2712), as in ui.cpp and bugreport.cpp.

#include <string>
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_Widget.h>
#include <mygui/MyGUI_ImageBox.h>
#include <mygui/MyGUI_RenderManager.h>
#include <mygui/MyGUI_DataManager.h>
#include <mygui/MyGUI_ITexture.h>
#include <windows.h>
#include "titleart.h"
#include "ui.h"                    /* UiFindLayoutSuffix */
#include "coop_log.h"
#include "command_channel.h"       /* PathNextToDll - the mod folder */
#include "../common/titleart.h"    /* kTitleArtFile, CoverCrop, ArtRowOnBox */

/* The two members of Ogre's resource-group manager called here, declared as OgreMain_x64.dll exports them (the vendored
   OgreResourceGroupManager.h needs the configured build's boost thread headers, which are not vendored). */
namespace Ogre {
class __declspec(dllimport) ResourceGroupManager
{
public:
    static ResourceGroupManager& getSingleton();
    void addResourceLocation(const std::string& name, const std::string& locType, const std::string& resGroup, bool recursive, bool readOnly);
    const std::string& findGroupContainingResource(const std::string& filename);
};
}

namespace coop {
namespace {

const char* const kMark       = "SWTitleArt";               /* the ImageBox's user string: the box size its crop was made for */
const char* const kLayoutFile = "Kenshi_MainMenu.layout";   /* the title screen's layout - its resource group is the GUI's */

enum { kArtUntried = 0, kArtReady = 1, kArtFailed = 2 };
int g_art = kArtUntried;
int g_texW = 0, g_texH = 0;
std::string g_shownKey;            /* the box size the last "shown" line named */
volatile LONG g_off = 0;
volatile LONG g_lastCode = 0;

std::string NumI(long long v) { char b[32]; _snprintf(b, 31, "%lld", v); b[31] = 0; return b; }
std::string SizeKey(int w, int h) { return NumI(w) + "x" + NumI(h); }

void Refuse(const std::string& why)
{
    g_art = kArtFailed;
    ErrorLog("[UI] title art: the game's own title art stays - " + why);
}

/* The file to a texture, once per process (tried once whatever the outcome). */
void LoadOnce()
{
    g_art = kArtFailed;
    const std::string name = swtitle::kTitleArtFile;
    const std::string path = PathNextToDll(swtitle::kTitleArtFile);
    const DWORD at = ::GetFileAttributesA(path.c_str());
    if (at == INVALID_FILE_ATTRIBUTES || (at & FILE_ATTRIBUTE_DIRECTORY) != 0) { Refuse("no " + name + " in the mod folder (" + path + ")"); return; }
    const size_t cut = path.find_last_of("\\/");
    if (cut == std::string::npos) { Refuse("the mod folder could not be told from " + path); return; }
    const std::string dir = path.substr(0, cut);
    MyGUI::RenderManager* rm = MyGUI::RenderManager::getInstancePtr();
    MyGUI::DataManager* dm = MyGUI::DataManager::getInstancePtr();
    if (rm == 0 || dm == 0) { Refuse("MyGUI's render or data manager is not up"); return; }
    if (rm->getTexture(name) != 0 || dm->isDataExist(name)) { Refuse("the game's GUI already has a resource named " + name); return; }
    std::string group;
    try { group = Ogre::ResourceGroupManager::getSingleton().findGroupContainingResource(kLayoutFile); }
    catch (...) { Refuse(std::string("Ogre knows no resource group holding ") + kLayoutFile); return; }
    try { Ogre::ResourceGroupManager::getSingleton().addResourceLocation(dir, "FileSystem", group, false, true); }
    catch (...) { Refuse("Ogre did not take the mod folder into resource group '" + group + "'"); return; }
    if (!dm->isDataExist(name)) { Refuse("MyGUI does not find " + name + " after the mod folder joined resource group '" + group + "'"); return; }
    MyGUI::ITexture* t = 0;
    try { t = rm->createTexture(name); t->loadFromFile(name); }
    catch (...)
    {
        if (t != 0) { try { rm->destroyTexture(t); } catch (...) {} }
        Refuse(name + " could not be read as an image");
        return;
    }
    const int w = t->getWidth(), h = t->getHeight();
    if (w <= 0 || h <= 0) { rm->destroyTexture(t); Refuse(name + " loaded with no size"); return; }
    g_texW = w;
    g_texH = h;
    g_art = kArtReady;
    DebugLog("[UI] title art: " + name + " loaded as a " + SizeKey(w, h) + " texture (the mod folder joined resource group '" + group + "')");
}

/* The title screen's ImageBox: the parent of the layout's ExitButton (0 when the title screen is not up). */
MyGUI::Widget* FindArt(MyGUI::Gui* gui)
{
    MyGUI::Widget* exitBtn = 0;
    MyGUI::EnumeratorWidgetPtr roots = gui->getEnumerator();
    while (exitBtn == 0 && roots.next()) exitBtn = UiFindLayoutSuffix(roots.current(), "ExitButton");
    return exitBtn != 0 ? exitBtn->getParent() : 0;
}

__declspec(noinline) void TickInner()
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) return;
    MyGUI::Widget* art = FindArt(gui);
    if (art == 0) return;
    const int pw = art->getWidth(), ph = art->getHeight();
    if (pw < 200 || ph < 200) return;
    if (g_art == kArtUntried) LoadOnce();
    if (g_art != kArtReady) return;
    const std::string key = SizeKey(pw, ph);
    if (art->getUserString(kMark) == key) return;
    MyGUI::ImageBox* box = art->castType<MyGUI::ImageBox>(false);
    if (box == 0) { Refuse("the title screen's background is not an ImageBox"); return; }
    const swtitle::Rect c = swtitle::CoverCrop(g_texW, g_texH, pw, ph);
    if (c.w <= 0 || c.h <= 0) return;
    box->setImageInfo(swtitle::kTitleArtFile, MyGUI::IntCoord(c.x, c.y, c.w, c.h), MyGUI::IntSize(c.w, c.h));
    art->setUserString(kMark, key);
    if (key != g_shownKey)
    {
        g_shownKey = key;
        DebugLog("[UI] title art shown at " + key + ": the texture's x=" + NumI(c.x) + " y=" + NumI(c.y) + " w=" + NumI(c.w) + " h=" + NumI(c.h)
                 + " of " + SizeKey(g_texW, g_texH) + " (cover: centred across, top kept)");
    }
}

int Filter(unsigned long code) { ::InterlockedExchange(&g_lastCode, (LONG)code); return EXCEPTION_EXECUTE_HANDLER; }
// The SEH frame. No local object with a destructor may appear here, and none does.
int TickGuarded()
{
    __try { TickInner(); }
    __except (Filter(GetExceptionCode())) { return 1; }
    return 0;
}

}   // namespace

void TitleArtTick()
{
    if (::InterlockedCompareExchange(&g_off, 0, 0) != 0) return;
    if (TickGuarded() == 0) return;
    ::InterlockedExchange(&g_off, 1);
    g_art = kArtFailed;
    char b[200];
    _snprintf(b, 199, "[UI] title art: a MyGUI or Ogre call faulted (code 0x%08lX) - the feature is off for the rest of this process",
              (unsigned long)(LONG)g_lastCode);
    b[199] = 0;
    ErrorLog(b);
}

int TitleArtNoteBand(MyGUI::Widget* art, int* bandTop, int* bandBottom)
{
    if (art == 0 || g_art != kArtReady || ::InterlockedCompareExchange(&g_off, 0, 0) != 0) return 0;
    const int pw = art->getWidth(), ph = art->getHeight();
    if (art->getUserString(kMark) != SizeKey(pw, ph)) return 0;
    const swtitle::Rect c = swtitle::CoverCrop(g_texW, g_texH, pw, ph);
    if (c.h <= 0) return 0;
    *bandTop = swtitle::ArtRowOnBox(swtitle::kTitleBottom, g_texH, c, ph);
    *bandBottom = swtitle::ArtRowOnBox(swtitle::kFiguresTop, g_texH, c, ph);
    return 1;
}

}   // namespace coop
