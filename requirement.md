 this needs the 1.0.65 rows before it'll build.

I only have the 1.0.68 exe so I only added the new rows to that table. Youve got both exes so it should just be:

    python tools/gen_table_1068.py --reverse --rows MapMarkerColour,TownGetMapIcon,TownUpdateSize,MapMarkerPlayerColour 

Paste what it prints into the 1.0.65 table, bump its !count by 4, then run

    python tools/gen_signatures.py

Also had to bump kAddrMaxEntries to 1024 since the 4 new rows went past 512 and the mod shut itself off. World server protocol went to 96 because the TOWNS rows carry the outpost stage now.

Tested on 1.0.68 with two of us on a team. Didn't get a base past the hut so the tower/castle icons aren't tested yet, and I didn't check save/reload.



-Mass
