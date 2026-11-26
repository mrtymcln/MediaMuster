MEDIAMUSTER 1.0.0
by Marty McLean

This app helps editors and assistants manage their Avid media.
Find your media files. Copy or move to another drive. Safely delete.

INSTALL ON MACOS
Drag and drop MediaMuster.app into your Applications folder.
Eject the disk image, then launch MediaMuster.app from Applications.

INSTALL ON WINDOWS
Right-click the ZIP and choose 'Extract All'. Open the extracted folder and
double-click MediaMuster.exe.
Keep all files together for the app to function correctly.

SCANNING A MEDIA DRIVE
1. Select a media drive from the Volumes list. To add a location manually,
   choose 'Add Folder or Volume' from the File menu.
2. Click 'Scan Selected' to list the media files on that drive.
3. Scan multiple drives in one go. Each new scan replaces the last one.

FIND THE MEDIA FOR A BIN
Click 'Filter by Bin', then '+' and choose your .avb file. Once the bin
loads, click 'Done'. The table now shows the files from your scan that
match the bin. If you see 'Results may be incomplete', check the Console
for details before you use the list for a handover.

COPY A BIN'S MEDIA TO ANOTHER DRIVE
1. In the table, select the files you want to take with you. To grab every
   row in the filtered list, click a row and press Command+A on Mac or
   Ctrl+A on Windows.
2. Click 'Manage Media', choose 'Copy', then pick a destination.
   The preview will show where each media file will land.
3. If a conflict is detected, choose 'Skip' or 'Keep Both'.
4. When you're happy, click the 'Copy' button.

MOVE OR DELETE FILES
If you're clearing a drive and want the files somewhere else, choose
'Move' in 'Manage Media'. Check the destination in the preview before you
click 'Move'. When it finishes, the Console tells you how many files
moved and whether any were skipped.

To delete files, select their rows, open 'Manage Media' and choose
'Delete'. Check the filenames in the preview before you click 'Delete'.
On a local drive, files go to the Trash or Recycle Bin. If the system
bin refuses them, MediaMuster can offer to keep them in its own trash
folder. On a network drive, look for the _MediaMuster_Trash folder. The
files still take up space there.

If a job gets interrupted, reconnect the drives. In the File menu, choose
'Unfinished Business', select the job and click 'Resume' to carry on.

SAVE A FILE LIST
Want to send the next editor a list of what's on the shuttle? Filter the
table to the files you want and click 'Export CSV'. If you've selected
rows, choose 'Selected' for just those files or 'All' for everything
currently visible. Pick a name and save the CSV.

ACKNOWLEDGEMENTS AND LICENCES
Matthew Skiles designed the app icon. Bella the Kelpie gets the assistant
editor credit.

Special thanks to Jack Brown, Ben Gold, Hamada Hanoura, Nathan Katsikaros,
John Lynn, Sandro Mazzanti, Harry B. Miller III, Jean-Denis Rouette and
Nacho Santana.

Thanks to Philip de Nier and the BBC for their work on libMXF. Thanks
also to Mark Reid, whose pyavb helped me make sense of Avid bin files.
The Licences folder includes the BBC's BSD notice and pyavb's MIT licence.

MediaMuster uses Qt 6.5.3 from The Qt Company and its contributors. The
bundled Qt libraries are available under the GNU Lesser General Public
License, version 3. You'll find the LGPL and its accompanying GPL in the
Licences folder. Qt's source and third-party notices are available here.
https://download.qt.io/archive/qt/6.5/6.5.3/single/
https://doc.qt.io/qt-6.5/licenses-used-in-qt.html

The Windows download also includes the Microsoft Visual C++ runtime.
https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution

MediaMuster © 2026 Marty McLean. All rights reserved.

https://martymclean.com/mediamuster
