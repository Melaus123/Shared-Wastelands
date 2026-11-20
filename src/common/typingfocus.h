/* src/common/typingfocus.h - T-1393 (G after the map): IS THE PLAYER TYPING?
 *
 * Pure: no engine memory, no globals, no OS call, no include - the offline suite sweeps it. The plugin (ui.cpp
 * UiPlayerTyping) reads the widget that holds MyGUI's key focus and hands its facts here; ping.cpp's G and tags.cpp's
 * Insert ask it before they act.
 *
 * MyGUI's isFocusKey() is true when ANY widget holds key focus. The world map window keeps key focus after it is clicked
 * or closed, so a guard on isFocusKey() alone read the map as typing and G was ignored ("typing in a text box") until the
 * player clicked in the world. The player is typing only when the focus widget is a text box (a MyGUI EditBox, by its
 * type) that is shown, enabled and writable; a read-only EditBox (Kenshi shows text in some) is not typing.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#ifndef COOP_COMMON_TYPINGFOCUS_H
#define COOP_COMMON_TYPINGFOCUS_H

namespace typingfocus {

/* isEditBox: the key-focus widget is a MyGUI EditBox (false when no widget has key focus); visible / enabled: its own and
   its parents' (MyGUI's inherited answers); readOnly: the box refuses typing. */
inline bool Typing(bool isEditBox, bool visible, bool enabled, bool readOnly)
{
    return isEditBox && visible && enabled && !readOnly;
}

} // namespace typingfocus

#endif
