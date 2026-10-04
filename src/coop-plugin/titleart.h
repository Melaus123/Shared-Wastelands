// titleart.h - T-513 (owner 446/447): the mod's own art as the title screen's background. See titleart.cpp; the pure rules
// (the cover crop, the note's place on the art) are in src/common/titleart.h.
#pragma once

namespace MyGUI { class Widget; }

namespace coop {

// Every title frame (coop.cpp detour_titleUpdate), BEFORE UiTitleTick and BugReportTick(1), so the menu column and the note
// are placed for the art on screen. The
// first frame the title screen's ImageBox is found, the art is loaded once for the process; every frame the ImageBox's size
// has no crop made for it yet, the crop that covers it is set. MAIN THREAD.
void TitleArtTick();

// `art` = the title screen's ImageBox (the parent of the layout's ExitButton). 1 when the mod's art is on it at its present
// size, with *bandTop / *bandBottom = the rows of the box (0 = its top) between which the art is open sky and horizon (under
// the painted title, above the figures); 0 when the game's own art is there. *bandTop is also the painted title's bottom
// row, under which ui.cpp starts the menu column. MAIN THREAD, inside a caller's SEH frame.
int TitleArtNoteBand(MyGUI::Widget* art, int* bandTop, int* bandBottom);

}
