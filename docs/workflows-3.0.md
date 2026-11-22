# ImageWalker 3.0 file workflows

Six workflows act on files: Open with, Copy to and Move to folder, Convert or
Resize, Batch Rename, Photo and Media Edit, and Synchronize. Convert, Rename, Edit and
Sync are task views sharing the model below, and Convert, Rename and Sync also
share its planning and safety rules. Copy and Move use a dialog, and Open with
acts directly. [features.md](features.md) describes the rest of the
application, and [app-rules.md](app-rules.md) the development rules.

## 1. Shared model

### Target

A command that acts on several items uses the complete visible selection, shown
as thumbnails and a count. A command that edits one photo acts on the photo
focused in Edit. Sync is the exception: its target is the two folder trees named
in the Sync view, not the item selection.

A workflow rejects a target it cannot process — no selection, a non-local item,
a folder where only a file is supported, a read-only file, or an item that is
not an editable photo — and states the refusal rather than doing nothing.

### Task views

Convert, Rename, Edit and Sync replace the Items surface with a task view whose
toolbar ends in Maximize/Restore and Close. While a task is open, the browsing
sidebars are hidden. Close or Escape returns to Items, after resolving any
active work or unsaved draft. Browse and tasks use the same libdesktop content
frame. The review list, comparison, or photo is on the
left; its tools are on the right. A folder in a controls panel is chosen with
Browse...; its field shows the choice read-only.

### Analyze, review, run

Convert, Rename and Sync separate planning from writing:

- **Analyze** reads the files and folders and builds a snapshot of proposed
  actions.
- **Review** shows the destination or action for every row before Run is
  enabled. Convert and Sync shorten a long path in the middle, so its file
  name stays visible.
- **Run** acts only on that reviewed snapshot.
- Changing a source, destination, output format, direction or collision policy
  discards the plan, because each of those changes what is written where.
- A disabled Run states its reason: a missing destination, an invalid dimension
  or name, an unresolved collision, or a need to analyze again.

Convert and Rename re-analyze automatically when one of those controls changes.
Convert's encoding controls — quality, lossless and the maximum dimension — do
not change any destination, so they redraw the Changes column in place and are
read when Run starts. Sync has an explicit Analyze button, because scanning two
trees may be substantial work.

### Progress, cancellation, results

While analysing or running, the status reads Analyzing or Processing, Run and
Analyze cannot be reinvoked, the current row is scrolled into view, and progress
carries a position and total once the total is known. Convert, Rename and Sync
put a Cancel command on the task toolbar. Photo Save runs to completion.
Media Save can be cancelled until the staged file is published.

Cancel stops future work. It is not a rollback: rows that already succeeded stay
done. Closing a task while work is active asks whether to cancel the operation
or keep it running.

After a run every reviewed row is Success, Failed, Skipped or Not run, and the
status summarises the counts with the first actionable error. One failed item
does not prevent later independent items from being attempted.

### Collisions

Convert and Batch Rename offer three policies in their review view:

- **Block run** — do not allow Run while any reviewed destination is occupied.
- **Skip** — leave the existing destination alone and do not write that row.
- **Auto-rename** — choose a free name and show it in Review.

Sync has no policy selector; it only copies missing files.

A destination claimed by two rows of the same plan is also a collision, and no
plan silently merges two sources into one destination. A destination that
appears between Review and Run is never silently overwritten: Convert and Rename
skip the row when it is the primary destination, Rename fails it when a sidecar
companion was taken, and Sync fails it.

## 2. Open with

The Open menu lists Windows applications registered for the selected extension,
then external tools discovered from `exe\imagewalker30-tools.json`, then a
general **Open with** entry that opens a searchable chooser, also on
`Ctrl+Enter`. The chooser lists internal commands as *(tool)* and registered
programs as *(app)*, and weights frequently used entries.

A registered application receives every selected file when the selection is all
files of one extension. A configured tool using `{item-paths}` receives the
complete selection; one using `{item-path}` receives only the first item, and
its menu entries and chooser rows say so. Configured batch tools may receive
mixed file types when the executable and template declarations accept every
selected item; registered Windows applications are offered only for a uniform
extension.

### Configuration

Configure tools in `imagewalker30-tools.json` before starting ImageWalker.
The `tools` object holds `folders`, the roots to search, and `apps`, the tool
declarations:

| Value | Purpose |
|---|---|
| `exe` | Executable base name, without a full path |
| `invoke` | Command template used to launch it |
| `extensions` | Comma-separated extensions the tool accepts |
| `group` | Optional media group, such as photo, video or audio |
| `text` | Name shown to the user |

A declaration needs an `exe` and an `invoke` template that begins with
`{exe-path}`. The roots are scanned four folders deep, matching the executable
base name case-insensitively, and a tool whose executable is not found is
omitted. Extension-specific tools are ordered before group tools and are never
listed twice for one file type.

The template expands `{exe-path}` to the discovered executable, `{item-path}`
to one selected file, and `{item-paths}` to individually quoted selected paths.
The path handed to the operating system is always the one found on disk, never
text from the configuration file. Missing files, invalid declarations,
unavailable search roots and executables that could not be found are kept as
configuration issues and written to the latest-run diagnostic log.

Launching an external application is not itself a modification. Whatever that
application then does is outside ImageWalker's control and outside its results.

## 3. Copy to and Move to folder

Copy and Move are not task views. `Ctrl+Shift+C` and `Ctrl+Shift+X` open a
dialog that shows the selection and its total size. Type a full path to get
subfolder suggestions, or use Browse. Sidecar companions — `.xmp`, `.thm`, `.aae`
— travel with their file and are surveyed for collisions together with it, so a
decision is never made for half a set.

Where a collision is found, choose Auto-rename or Cancel. Auto-rename
keeps existing items and chooses unused names for incoming files. The
destination folder is created only after this decision.

The run itself is not cancellable: a status window pumps messages so the
application stays responsive, but there is no Cancel. Per-item results are
retained afterwards and the last report can be reopened from the menu. The
selection is refreshed once the run finishes.

Move retains no copy of a source once that row has succeeded.

## 4. Convert or Resize

Output is JPEG, PNG, TIFF or bundled static WebP. WebP does not require an
installed WIC encoder; it supports lossy quality or lossless output with alpha.
Quality applies where the format supports it. A maximum dimension scales down
only; it never enlarges.

Originals are kept, and a source file is never used as its own output. Outputs
are encoded to a temporary sibling and published only after the encode
completes, so a failed or refused write leaves existing bytes untouched.

Outputs are upright and carry the source's colour profile where the format can
hold one, so they look like their source; WebP output carries none. Camera
fields, dates and GPS positions are deliberately not copied, so converted
copies can be shared without them.

## 5. Batch Rename

`Ctrl+Shift+R`. In the template, a run of `#` is the zero-padded sequence number and
`{token}` is a metadata substitution. The camera, date and exposure tokens read
the photo's metadata in every format ImageWalker reads it from; a photo without
a capture date takes its modified date. Templates, sequences and the resulting
names are validated before Run is enabled.

The snapshot covers each file and its sidecars. Run copies them under the
reviewed new names without moving the originals. If stopped, completed copies
remain. To rename one file or folder in place, use Rename (`F2`) instead.

## 6. Photo Edit

`E`. Rotation, straightening and nine colour adjustments are applied as a live
draft over a scaled preview, with auto straighten and auto colour, and a
selector strip of the folder's photos.

Auto straighten levels a tilted photo. Auto colour adjusts its brightness range
and white balance.

Drag the blue comparison handle to see the original on the left and the
finished result on the right. Compare keeps the divider visible; Preview shows
the result side by side with the original. Neither writes to disk.

The right panel groups geometry, colour and save settings. The live RGB
histogram has draggable Darks, Midtones and Lights handles. Double-click a
handle to reset it, or click a numerical readout and type an adjustment.
The rows fit the available panel width and stack their labels on narrow panes.

Straightening turns the picture inside the frame it was given, and the save
crops away the emptied corners. Nothing is written until Save, which re-renders
at full resolution.

The draft starts from the upright photo, and Save writes upright pixels with the
orientation reset to 1. Saving a JPEG as JPEG copies its metadata segments byte
for byte, maker notes included; only the EXIF and XMP orientation, the EXIF
pixel size and the stale EXIF and JFIF thumbnails are corrected, and a
multi-picture index pointing into the old file is dropped. Other saves,
including Save as into another format, carry the source's metadata blocks -
camera, date and GPS fields among them - and an RGB colour profile wherever the
destination format can hold them. A BMP holds none, and a WebP save writes
none; see [known-issues.md](known-issues.md).

An in-place Save always keeps the original at an unused `-original` name. A format that
cannot hold the result asks for an explicit save as JPEG rather than silently
changing the extension. In-place Save refuses multi-page TIFFs, animations,
HDR and higher bit depth images, and files whose content does not match their
extension. Save as writes the edited visible frame to a separate file.
Save as proposes `-edit`, then `-edit 2`, and never an
existing name. Leaving or changing photo with a changed draft always offers
Save, Don't Save or Cancel, and the target changes only after a successful
write. A failed save keeps the draft. Saved pixels are refreshed in the item
and image caches on return to Items.

Horizontal and Vertical correct perspective with sliders. Corners places four
normalized points on the original photo and
rectifies their quadrilateral before rotation, straightening, crop and colour
adjustments. Crop edges and corners can be dragged; the interior moves the
crop. Escape cancels an active drag rather than closing the task.

Auto document looks for a complete, contrasting quadrilateral. It refuses
clipped, low-contrast, transparent or non-quadrilateral subjects instead of
inventing a page. Its result remains editable through the corner handles.
Perspective output is limited to about 134 million pixels (2^27) and four times
the source area; rejected geometry and allocation failures keep the draft.

Draft Undo/Redo restores geometry and colour adjustments without writing files.
A successful save or a new photo establishes a new draft baseline.

### Media Edit

Press `E` on the focused audio or video file. Drag the trim handles under the
preview or press `I` and `O` to mark the playhead; Space plays or pauses. Clear
trim restores the whole recording. Remove Sound omits audio tracks from a
video; Left and Right set a quarter turn on supported MP4 and QuickTime
outputs. **Put the MP4/QuickTime index first** enables progressive playback.

FFmpeg copies compressed packets rather than re-encoding. The export can
start at an earlier keyframe and the end uses packet boundaries. Edit saves
in the source container where it can or offers an MP4 copy when compatible.
Save As can rebuild a readable file without edits. If the staged result does
not retain the expected tracks, the save fails. Track count and codec checks
do not guarantee preservation of all private container metadata.

Before publication, Cancel save leaves the source and destination unchanged.
Save As requires an unused name and in-place Save verifies and retains the
original under `-original`. The preview uses the same bundled decoding as
ordinary playback, with Windows codecs as a compatibility fallback. Packet
copy and playback support are distinct.

## 7. Synchronize

Sync compares one local scope against a remote tree. The local scope is either a
single folder or a saved collection-folder list, edited in the view with Add
collection folder and Remove selected folder. That list is Sync's own setting,
independent of the browser's folder shortcuts.

Each collection root maps to `Remote\<root-name>`, including when only one root
remains; a single-folder scope maps directly to Remote. A missing root stays
visible in the saved configuration and blocks analysis rather than silently
reducing its scope. Unreadable, overlapping, ambiguous and reparse-point trees
are refused.

Analyze is explicit and walks both trees completely. The plan lists missing
files and their proposed copy directions. If both sides have a file at the
same path, both are kept; different sizes or modified times are flagged. The
plan is revalidated per row as it runs, and each row keeps its own result.

## 8. Recovery

| Workflow | Source retained | Existing-destination risk | After a partial run |
|---|---|---|---|
| Open with | Yes | Controlled by the external application | No write, and no rollback for external edits |
| Copy | Yes | Uses new names only | Not cancellable; per-item results retained |
| Move | No, once a transfer succeeds | Uses new names only | Not cancellable; moved items stay moved |
| Convert | Yes | Block, Skip or Auto-rename | Completed outputs remain |
| Batch Rename | Yes | Block, Skip or Auto-rename | Completed copies remain |
| Edit Save | Kept beside the edited photo and in session Undo | Acts only on the verified original | A failed save leaves the draft |
| Edit Save as | Yes | Uses a new name only | A failed save leaves the draft |
| Sync | Yes | Copies only missing files in the chosen direction | Completed copies remain |

### Session Undo

Edit > Undo (`Ctrl+Z`) undoes the active Photo Edit draft, or, after closing the
task, the latest completed file task. File Undo asks for confirmation and
reports restored paths, conflicts and failures. It covers Convert, batch and
direct Rename, Sync, the Copy/Move dialog and Photo Edit saves. Partial runs
keep history for the parts that actually completed, including partially
transferred sidecars.

Recovery copies are secured before destructive changes. Undo checks file
identity, contents, parent identity and bundle relationships; later external
edits and newly appeared destinations are refused rather than overwritten.
A file or folder whose file system gives no usable identity - a share that
reports none, or a ReFS volume whose file IDs are wider than 64 bits - cannot be
checked, so Undo refuses it rather than proceeding unverified.
Directory merges record only the nodes and files actually changed. An unrelated
child prevents removal of a created directory.

Automatic Undo is current-session only. Flushed manifests and original bytes
remain in `exe\imagewalker30.undo\` after exit or a crash for manual recovery.
They are neither replayed nor evicted automatically, and a failed or
conflicting operation reports where its recovery copies are. Cross-volume
recovery can also leave a `.iw30-undo-*` staging item beside the original path,
named in the recovery record.

Clipboard/drop operations managed by the Windows shell and Recycle Bin
deletions use Windows recovery, not this history. External-application changes
and settings/navigation are not tracked. The file Undo confirmation names the
recorded task so it cannot be mistaken for undoing an untracked action.
