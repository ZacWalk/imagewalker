ImageWalker Collection
======================

Setup installs five editions side by side and chooses x86 or x64 for this PC.
Open Start > ImageWalker Collection and select the edition you want.

For the portable ZIP, extract the whole archive into a writable folder. Open
x64 on a 64-bit Intel or AMD PC, or x86 on a 32-bit PC. Run any of these:

  imagewalker10.exe  ImageWalker 1.0 (1998): read-only thumbnail browser
  imagewalker20.exe  ImageWalker 2.0 (2001): descriptions and print preview
  imagewalker22.exe  ImageWalker 2.2 (2003): Favourites and Print mode
  imagewalker23.exe  ImageWalker 2.3 (2006): tagging and EXIF auto-rotation
  imagewalker30.exe  ImageWalker 3.0 (2026): photos, video and file workflows

Use the Collection on Windows 10 or 11. It does not include every historical feature;
for example, slide shows, Compare mode and Web galleries are not included.
Press F1 in each application to open its guide. On Windows N, video and audio
playback may require the Windows Media Feature Pack.

Report problems at https://github.com/ZacWalk/imagewalker/issues.

Settings, logs and 3.0 recovery files are kept beside the executable. Setup
installs for the current user without elevation. Uninstall removes program
files and shortcuts, but leaves settings, logs, the tools configuration and
recovery files in the install folder.

Close all ImageWalker editions before updating or uninstalling. If either
operation reports a blocked path before it starts, the existing installation
is unchanged. Close any program using that path or restore write access,
then retry.

Source and redistribution
-------------------------

ImageWalker's source code is at https://github.com/ZacWalk/imagewalker.
Use the matching Collection release tag and follow docs/build.md for the
build instructions and pinned dependencies. ImageWalker is MIT licensed;
libdesktop and libfiles are MPL 2.0. libfiles' IPTC table and EXIF presentation
code retain LGPL 2.1 or later.
The source and build instructions allow you to rebuild and relink with modified
libraries. You may modify these files for your own use and reverse engineer the
program to debug those modifications. See Licenses for the full texts.

The JPEG transform code uses software of the Independent JPEG Group.
The English dictionaries are from the MySpell era. The US wordlist draws
on Kevin Atkinson's Aspell/Pspell list (LGPL); its affix rules derive from
Geoff Kuenning's Ispell. The British dictionary credits David Bartlett and
Andrew Brown and its affix file states LGPL. Dictionary source is included
in the distribution and in the ImageWalker source under exe/dic.
