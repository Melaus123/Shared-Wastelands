// fontguard.h - T-730: a MyGUI text with no font answers its font name with an empty name instead of crashing Kenshi's title
// screen. See fontguard.cpp; the pure rules are in src/common/fontguard.h.
#pragma once

namespace coop {

// Once, from InstallStore, before the title screen is built: finds MyGUI's EditText::getFontName, checks its body bytes and
// detours it. Logs one [FONTGUARD] line either way. MAIN THREAD.
void InstallFontGuard();

// One [FONTGUARD] line: how many times a text with no font was answered (the `report` fan-out). Any thread.
void ReportFontGuard();

}
