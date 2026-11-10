# UI design

Design brief for the macOS UI of PicoStation Multi House V3. All user-visible text is EN-US.
The goal is a native, modern, accessible Mac app: it should feel like a first-party utility
(Disk Utility, Music, Photos), not like a Win32 port.

## Principles

1. Native first: AppKit controls, system fonts (SF Pro, SF Mono for serials), SF Symbols, system
   materials (vibrancy), unified toolbar, sheets instead of free-floating dialogs. Follows the
   system appearance (light and dark) and the system accent only where the brand color is not needed.
2. One obvious primary action per state ("Create V2 Card"), everything else secondary.
3. Nothing hidden behind drag only: every drag action has a menu and keyboard equivalent.
4. Never color alone: every status has an icon and a word as well as a color.
5. Destructive actions (erase a card, remove a case) are explicit: confirmation sheet, red
   destructive button, clear description of what will be lost.
6. Long work shows what is happening: phase, step count, determinate progress, a Cancel button.
7. Respect Reduce Motion and Increase Contrast. Motion is 150 to 250 ms, ease in-out, only
   to explain a change (selection, insert, remove, sheet), never decorative.

## Tokens

Colors (dynamic, light and dark variants; all text pairs at least 4.5:1, large text 3:1):

| Token | Light | Dark | Use |
|-------|-------|------|-----|
| `brandBlue` | #0070D1 | #3B9CFF | primary button, selection accent, links |
| `statusVerified` | system green | system green | verified discs |
| `statusReview` | system orange | system orange | discs that need review (observations) |
| `statusError` | system red | system red | errors, destructive buttons |
| `statusWritten` | `brandBlue` | `brandBlue` | already written to the card |
| `surface` | `windowBackgroundColor` | same | content |
| `surfaceRaised` | `controlBackgroundColor` | same | cards, inspector sections |
| `separator` | `separatorColor` | same | hairlines |

Typography: title 22 semibold (case name), headline 13 semibold, body 13 regular,
caption 11 secondary, serial and numbers in monospaced system font 12. Respect the system text size.

Spacing: 8 pt grid (4, 8, 12, 16, 24). Corner radius 10 for cards, 6 for thumbnails.
Minimum hit target 28 pt tall for buttons, 44 pt for list rows with art.

Icons: SF Symbols only (no emoji, no bitmap icons). Suggested: `plus.rectangle.on.folder` (Add Game),
`opticaldisc` (Add Disc), `slider.horizontal.3` (Menu Settings), `externaldrive` /
`sdcard` (Card), `arrow.clockwise` (Refresh), `checkmark.seal.fill` (Verified),
`exclamationmark.triangle.fill` (Review), `xmark.octagon.fill` (Error),
`sdcard.fill` (Written), `sidebar.left`, `sidebar.right`, `photo` (Replace art), `bolt` (Cheats).

## Window and layout

`NSWindow` with unified toolbar style, full-size content view, minimum 1000 x 640, default
1280 x 800, frame autosaved. Content is an `NSSplitViewController` with three panes.

```
+--------------------------------------------------------------------------------+
| o o o  [sidebar]  Add Game  Add Disc  Menu Settings  Image v     [inspector]    |  unified toolbar
+---------------+------------------------------------------+---------------------+
| LIBRARY       |  Alundra (USA) (Rev 1)                   |  [ case art ]       |
|  [art] Case A |  1 disc                                  |  [ disc art ]       |
|  [art] Case B |  +--------------------------------------+|  Name               |
|  ...          |  | (disc) Title            SLUS-00553   ||  Disc name           |
|               |  |        1 track   Verified            ||  Cheats (0)...       |
|  empty state: |  +--------------------------------------+|  Details            |
|  drop here    |                                          |                     |
+---------------+------------------------------------------+---------------------+
| [sdcard] Card: disk8 | Model | 3.0 GB  v  [refresh]   Step 2 of 4  progress  [ Create V2 Card ] |  bottom bar
+--------------------------------------------------------------------------------+
```

* Sidebar (source list, sidebar vibrancy, 240 to 360 pt): section header "LIBRARY", one row per
  case: 40 pt art thumbnail (rounded 6), case title (1 line, truncates in the middle), caption
  "N discs", trailing status symbol. Section footer shows totals ("3 cases, 3 discs").
  Row context menu: Move Up, Move Down, Rename, Remove.
* Content: header (case title as an editable large field, caption with disc count), then the disc
  list as cards (rows 64 pt): round disc art, title, serial pill (monospaced), tracks, status chip
  (icon plus word). Rows reorder by drag or by Move Up and Move Down (menu, context menu,
  keyboard: option-command-up and option-command-down).
* Inspector (right, 300 to 360 pt, collapsible, toggle in toolbar): case art preview, "Replace Case Art...",
  disc art preview, "Replace Disc Art...", labeled fields "Case name" and "Disc name",
  "Cheats (n)..." button, "Details" disclosure group with the verification notes text.
* Bottom bar (material, 52 pt): left "Card" popup with SF Symbol and refresh button; center
  status line and determinate progress with phase text; right the primary button.
  The primary button reads "Create V2 Card" (or "Apply Changes" when a V2 card is loaded for
  editing), uses the prominent bezel in `brandBlue`, and is disabled with a short reason in
  its tooltip and in the status line when it cannot be used.

Empty state (no cases): centered large symbol (`opticaldisc` 72 pt, tinted gradient), headline
"Build your PS1 library", secondary text "Drop game folders or .cue files here, or choose
Add Game.", buttons "Add Game..." (prominent) and "Add Disc...". While dragging over the window, the content area shows a dashed
`brandBlue` rounded border and the text "Drop to add".

## Sheets and dialogs

* Write confirmation sheet: warning symbol, title "Erase and write to <disk name>?", body
  explaining that everything on the card will be erased, a summary (cases, discs, estimated
  size, card model and size), the checkbox "I confirm this is the correct card and I authorize
  the raw write." (button stays disabled until checked), buttons "Cancel" and a destructive
  "Erase and Write".
* Progress sheet (modal to the window): phase title, "Step n of N", determinate bar, current
  item caption, "Cancel" (becomes disabled with caption "Finishing safely..." when cancelling is
  not safe). Result state: success (checkmark, summary, "Done") or failure (error text in a
  selectable text view, "Copy Details", "Close").
* Menu Settings sheet (menu defaults): grouped form with popups and a switch, "Restore Defaults",
  "Cancel", "Save".
* Alerts use `NSAlert` as sheets with plain English titles (what happened) and informative text
  (what to do), no technical jargon in the title.
* About: standard About panel with credits text and links (ALBERONISSE, repository).
* Cheats windows keep their function but follow the same tokens, toolbar-less panels with a
  search field, table with alternating rows, clear primary and secondary buttons.

## Menu bar

App: About, Hide, Quit. File: Add Game... (cmd N), Add Disc... (shift cmd N), Open Image... (cmd O),
Save Image As... (shift cmd S), Close (cmd W). Edit: standard items, Remove (cmd delete).
Library: Move Up, Move Down, Replace Case Art..., Replace Disc Art..., Cheats..., Menu Settings...,
Refresh Cards (cmd R), Create V2 Card / Apply Changes (cmd return). View: Show/Hide Sidebar
(option cmd S), Show/Hide Inspector (option cmd I), Enter Full Screen. Window and Help standard.
Every command available by mouse is available from the menu bar.

## Accessibility checklist

* Every control and row has an accessibility label, role and help; rows read like
  "Alundra, 1 disc, verified".
* Visible keyboard focus everywhere, logical tab order (toolbar, sidebar, content, inspector,
  bottom bar). Full operation without a pointer.
* Contrast at least 4.5:1 in light, dark and Increase Contrast; status is never color only.
* Reduce Motion: no sliding or fading beyond cross-fades; Reduce Transparency: solid surfaces.
* Text scales with system settings where AppKit allows; nothing is clipped at the minimum window size.
* Drag has non-drag alternatives (menu and keyboard).

## Strings

All UI text is EN-US, Title Case for buttons and menu items, sentence case for messages. Glossary:
Case (game box, keep the word), Disc, Card (SD card), Create V2 Card, Apply Changes, Verify,
Cheats, Art, Raw write, Review (was "OBS"), Verified, Written. Text that is written to the card or
that belongs to file formats (card headers, PSTCFG2 text, `MHDialect=` cheat file keywords,
database titles) is data and is not translated.
