# Versions

The [README](../README.md) lists the five releases with their directories and
years, and [features.md](features.md) what each offers.

## About boxes

Each About box names the application, its release year and its theme song,
with a link to the song. The 1.0 and 2.x About boxes give the architecture and
describe features available in the executable. The Collection installer has
its own package version; it does not change the five app versions.

| Application | Theme song |
|---|---|
| ImageWalker 1.0 | "It's Like That" by Run-DMC vs. Jason Nevins |
| ImageWalker 2.0 | "It Wasn't Me" by Shaggy |
| ImageWalker 2.2 | "Where Is the Love?" by The Black Eyed Peas |
| ImageWalker 2.3 | "Crazy" by Gnarls Barkley |
| ImageWalker 3.0 | "Ordinary" by Alex Warren |

## Keyboard shortcuts

ImageWalker follows Diffractor's navigation and commands where the feature
exists. ImageWalker 2.0, 2.2 and 2.3 share their accelerator table. Keys in
the table apply when the browser or viewer has focus; an address field,
dialog or focused control can take the key first. A dash means no dedicated
default shortcut.

| Key | ImageWalker 1.0 | ImageWalker 2.0 / 2.2 / 2.3 | ImageWalker 3.0 | Diffractor |
|---|---|---|---|---|
| Enter | Open folder; fullscreen for a photo, next photo if already fullscreen | Open folder; fullscreen for a photo, next photo if already fullscreen | Open folder, show photo fullscreen, start audio/video, or open the app chooser for another file. Advance to the next photo in fullscreen. | Open folder; fullscreen for media, next item if already fullscreen; Open with for other files |
| Space | Next photo | Next photo | Play/pause audio or video; does not advance through photos | Play/pause when playable media is active |
| Ctrl+Space | Toggle the focused item's selection in Items | Toggle the focused item's selection in Items | Toggle the focused item's selection | Toggle the focused item's selection in Items |
| Escape | Cancel a drag or address edit, reset preview zoom, or leave fullscreen | Dismiss temporary state, leave fullscreen, or return to Normal | Cancel an active drag, stop inline playback, reset zoom, leave fullscreen, or close the active task | Cancel the active interaction or unwind slideshow, zoom, fullscreen, or the current view |
| Alt+F4 | Close application | Close application | Close application | Close application |
| Ctrl+F4 | Back out first; quit from the ordinary browser | Back out first; quit from Normal | Back out of zoom, fullscreen, or the active task; quit from the ordinary browser | Close the current view or unwind fullscreen/zoom/slideshow; quit from ordinary Items or Media |
| F11 / Ctrl+Shift+Space | Toggle photo fullscreen | Toggle fullscreen | Toggle photo fullscreen | Toggle fullscreen |
| Arrows | Move through items | Navigate items or step photos, depending on pane | Navigate items; step through fullscreen photos or pan a zoomed image | Navigate items or step media; pan a zoomed image |
| Alt+Left / Alt+Right | Back / Forward | Back / Forward | Back / Forward | Back / Forward |
| Backspace / Alt+Up | Back / Parent (Alt+Backspace and Ctrl+P also work) | Back / Parent | Back / Parent | Back / Parent |
| Ctrl+O | - | Open file | Open file | - |
| Ctrl+Enter | - | Open with | Open with | Open with |
| Ctrl+Shift+Enter | - | File properties | - | File properties |
| Alt+Enter | - | Save while editing | Save while editing | Save while editing |
| Ctrl+A | Select all items | Select all items | Select all items | Select all items |
| Ctrl+D | Invert selection | Invert selection | - | Invert selection |
| Ctrl+C | Copy selected files | Copy selected files | Copy selected files | Copy selected files |
| Ctrl+X / Ctrl+V | - | Cut / Paste | Cut / Paste | Cut / Paste |
| Delete | - | Recycle selection | Recycle selection | Delete selection |
| F1 / F5 | Help / Refresh | Help / Refresh | Help / Refresh | Help / Refresh |
| F2 | - | Rename | Rename one item | Rename |
| F3 / Ctrl+F | - | Search | - | Search |
| F4 / Shift+F4 | - | No Explore view / Folders | No Explore view / Toggle folder tree | Explore / Toggle sidebar |
| F6 | - | Options | Options | Options |
| F7 | - | - | Mute/unmute the media in Browse or Media Edit | Mute/unmute |
| E | - | Edit photo when the preview has focus (F12 or Ctrl+Shift+E anywhere outside text entry) | Edit photo | Edit photo |
| Z | Toggle Fit/Actual Size | Toggle zoom when the preview has focus | Toggle zoom | Toggle zoom |
| Ctrl+Shift+C / Ctrl+Shift+X | - | Copy / Move to folder | Copy / Move to folder | Copy / Move to folder |
| Ctrl+F8 | - | Convert Images | Convert or Resize | Convert or Resize |
| Ctrl+Shift+F9 | - | - | Synchronize folders | Synchronize folders |
| Ctrl+Shift+R | - | - | Batch Rename copies | - |
| Ctrl+S | - | Save in Edit; Ctrl+Shift+S saves as a new file | Save and Save as are toolbar commands in Edit | Save a video frame; Edit Save is Alt+Enter |
| Ctrl+Z | - | Undo image edit | Undo draft or file task; Ctrl+Y or Ctrl+Shift+Z redoes a draft edit | Not a general file Undo shortcut |

Escape never quits. In 2.x, the items list keeps letter keys for jumping to a
filename. Copy Image is still in the Image menu; use Ctrl+Shift+P for Print
preview and Ctrl+Shift+N to return to Normal. Diffractor's Shift+Space slideshow
and Enter-to-run task shortcuts are not available in ImageWalker.

The comparison follows the compiled legacy accelerator resources and the
keyboard handlers in
[1.0](../src10/ImageWalker.rc),
[2.x](../src23/ImageWalker.rc),
[3.0](../src30/ImageCanvas.cpp), and
Diffractor's [defaults](https://github.com/ZacWalk/diffractor/blob/master/src/app_toolbar.cpp)
and [key dispatch](https://github.com/ZacWalk/diffractor/blob/master/src/app.cpp).
See [3.0 workflows](workflows-3.0.md) for what each file task does.
