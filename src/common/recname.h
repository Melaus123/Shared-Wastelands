#pragma once
#include <string>
/* The names of a record's data file. The world server writes every record's data - area records included - as
   <id>.platoon in its own folder (the world server's FileOf, store_main.cpp). A game's own mirror folder names an area
   record <id>.zone, the engine's own file name for an area: the area-load swap serves the mirror file, and the
   engine names the area it loads from the file's name. Pure: names only, no files touched. */
namespace recname {

inline bool IsAreaId(const std::string& id) { return id.compare(0, 5, "zone.") == 0; }

/* the data file the world server writes for any record, in its folder dir */
inline std::string ServerDataFile(const std::string& dir, const std::string& safeId) { return dir + "\\" + safeId + ".platoon"; }

/* the data file a game writes in its own mirror folder dir: an area record under the engine's area file name */
inline std::string MirrorDataFile(const std::string& dir, const std::string& safeId, bool area)
{
    return dir + "\\" + safeId + (area ? ".zone" : ".platoon");
}

}   /* namespace recname */
