# ImageWalker 3.0 file workflows

Six workflows act on files: Open with, Copy to and Move to folder, Convert or
Resize, Batch Rename, Photo Edit, and Synchronize. Convert, Rename and Sync
share one planning and safety model, described first. Copy and Move use a
dialog, and Open with and Edit act directly. The rest of the application is in
[features.md](features.md); the invariants these workflows hold are in
[app-rules.md](app-rules.md).

## 1. Shared model

This section covers the four task views — Convert, Rename, Edit and Sync. Copy,
Move and Open with are described with their own workflows.

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
toolbar ends in Maximize/Restore and Close. Close returns to Items; it does not
exit the application. Convert and Rename put a controls panel beside a review
list, Sync beside a four-column comparison, and Edit beside the photo and a
selector strip of the folder's other photos.

### Analyze, review, run

Convert, Rename and Sync separate planning from writing:

- **Analyze** reads the files and folders and builds a snapshot of proposed
  actions.
- **Review** shows the destination or action for every row before Run is
  enabled.
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
put a Cancel command on the task toolbar; Edit has no Cancel, and its save runs
to completion.

Cancel stops future work. It is not a rollback: rows that already succeeded stay
done. Closing a task while work is active asks whether to cancel the operation
or keep it running.

After a run every reviewed row is Success, Failed, Skipped or Not run, and the
status summarises the counts with the first actionable error. One failed item
does not prevent later independent items from being attempted.

### Collisions

Convert and Rename offer four explicit policies in their review view:

- **Block run** — do not allow Run while any reviewed destination is occupied.
- **Skip** — leave the existing destination alone and do not write that row.
- **Auto-rename** — choose a free name and show it in Review.
- **Replace** — overwrite, and identify that effect in Review.

Sync has no policy selector; replacement follows from the reviewed action.

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
complete selection. Existing `{item-path}` declarations retain single-file
behavior, and their menu entries and chooser rows explicitly say first item only.
Configured batch tools may receive mixed file types when the executable and
template declarations accept every selected item; registered Windows
applications are offered only for a uniform extension.

### Configuration

`imagewalker30-tools.json` is read on a worker at startup by a bounded in-tree
parser; 3.0 takes no third-party dependency for it. The `tools` object holds
`folders`, the roots to search, and `apps`, the tool declarations:

| Value | Purpose |
|---|---|
| `exe` | Executable base name, without a full path |
| `invoke` | Command template used to launch it |
| `extensions` | Comma-separated extensions the tool accepts |
| `group` | Optional media group, such as photo, video or audio |
| `text` | Name shown to the user |

A declaration is accepted only with `exe` and `invoke`. The roots are scanned
four folders deep, matching the executable base name case-insensitively, and a
tool whose executable is not found is omitted. Extension-specific tools are
ordered before group tools and are never listed twice for one file type.

The template expands `{exe-path}` to the discovered executable, `{item-path}`
to one selected file, and `{item-paths}` to individually quoted selected paths.
The path handed to the
operating system is always the one found on disk, never text from the
configuration file. Missing files, invalid declarations, unavailable search
roots and executables that could not be found are retained as configuration
issues and written to the latest-run diagnostic log.

Launching an external application is not itself a modification. Whatever that
application then does is outside ImageWalker's control and outside its results.

## 3. Copy to and Move to folder

Copy and Move are not task views. `Ctrl+Shift+C` and `Ctrl+Shift+X` open a
dialog that previews the selection with its count and total size, and takes a
destination folder with completion. Sidecar companions — `.xmp`, `.thm`, `.aae`
— travel with their file and are surveyed for collisions together with it, so a
decision is never made for half a set.

Where a collision is found, the choice is Auto-rename or Replace, defaulting to
Auto-rename; Replace secures a recovery copy before overwriting. The destination folder is
created after that decision, so cancelling leaves nothing behind.

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

## 5. Batch Rename

`F7`. In the template, a run of `#` is the zero-padded sequence number and
`{token}` is a metadata substitution. Templates, sequences and the resulting
names are validated before Run is enabled.

The snapshot covers each file and its sidecars. Renames that form a cycle are
staged through temporary names, and the snapshot is revalidated immediately
before the first rename. A failed row is restored within bounds, without ever
clobbering a file that already exists, and earlier successful rows stay renamed
and are reported as such.

## 6. Photo Edit

`F12`. Rotation, straightening and nine colour adjustments are applied as a live
draft over a scaled preview, with auto straighten and auto colour, and a
selector strip of the folder's photos.

Both automatic adjustments are ported from ImageWalker 2.3. Auto straighten
chooses the angle that packs the picture's strong edge points into the fewest
bands. Auto colour stretches the middle 98% of the luminance range to fill the
scale and applies a grey-world white balance.

Straightening turns the picture inside the frame it was given, and the save
crops away the emptied corners. Nothing is written until Save, which re-renders
at full resolution.

An in-place Save writes an original backup copy first by default. A format that
cannot hold the result asks for an explicit save as JPEG rather than silently
changing the extension. Save as proposes `-edit`, then `-edit 2`, and never an
existing name. Leaving or changing photo with a changed draft always offers
Save, Don't Save or Cancel, and the target changes only after a successful
write. A failed save keeps the draft. Saved pixels are refreshed in the item
and image caches on return to Items.

Perspective editing places four normalized corners on the original photo and
rectifies their quadrilateral before rotation, straightening, crop and colour
adjustments. Crop edges and corners can be dragged; the interior moves the
crop. Escape cancels an active drag rather than closing the task.

Auto document looks for a complete, contrasting quadrilateral. It refuses
clipped, low-contrast, transparent or non-quadrilateral subjects instead of
inventing a page. Its result remains editable through the corner handles.
Perspective output is limited to 128 million pixels and four times the source
area; rejected geometry and allocation failures retain the draft.

Draft Undo/Redo restores geometry and colour adjustments without writing files.
A successful save or a new photo establishes a new draft baseline.

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

Analyze is explicit and walks both trees completely. The reviewed plan lists
copies and deletions with their direction. Deletion is permanent, and running a
plan that contains deletions requires a confirmation that states how many files
will be deleted. The plan is revalidated per row as it runs, and each row keeps
its own result.

## 8. Recovery

| Workflow | Source retained | Existing-destination risk | After a partial run |
|---|---|---|---|
| Open with | Yes | Controlled by the external application | No write, and no rollback for external edits |
| Copy | Yes | Replace overwrites; Auto-rename is the default | Not cancellable; per-item results retained |
| Move | No, once a row succeeds | Replace overwrites; Auto-rename is the default | Not cancellable; moved items stay moved |
| Convert | Yes | Explicit Block, Skip, Auto-rename or Replace | Completed outputs remain |
| Rename | Names change in place | Explicit Block, Skip, Auto-rename or Replace | Bounded restoration for a failed row; earlier rows stay renamed |
| Edit Save | Secured in Undo history; an additional visible backup is optional | In-place Save replaces the photo | Unsaved drafts are protected |
| Edit Save as | Yes | The user confirms the destination | A failed save leaves the draft |
| Sync | Depends on the reviewed action | Reviewed copy may replace; deletion is permanent | Row results retained; nothing is rolled back |

Copy, Move and Edit saves run to completion. Only Convert, Rename and Sync offer
Cancel.

### Session Undo

Edit > Undo (`Ctrl+Z`) undoes the active Photo Edit draft, or, after closing the
task, the latest completed file task. File Undo asks for confirmation and reports
restored paths, conflicts and failures. It covers Convert, batch/direct Rename,
Sync, the Copy/Move dialog and Photo Edit saves. Partial runs retain history for
the parts that actually completed, including partially transferred sidecars.

Recovery copies are secured before destructive changes. Undo checks file
identity, contents, parent identity and bundle relationships; later external
edits and newly appeared destinations are refused rather than overwritten.
Directory merges record only the nodes and files actually changed. An unrelated
child prevents removal of a created directory.

Automatic Undo is current-session only. Flushed manifests and original bytes
remain in `exe/imagewalker30.undo/` after exit or a crash for manual recovery.
They are not replayed automatically or evicted; failed/conflicting operations
report the retained recovery location. Cross-volume recovery can also retain
documented staging siblings at the original volume.

Clipboard/drop operations managed by the Windows shell and Recycle Bin
deletions use Windows recovery, not this history. External-application changes
and settings/navigation are not tracked. The file Undo confirmation names the
recorded task so it cannot be mistaken for undoing an untracked action.
