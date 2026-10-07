# Steam content, cloud saves and orientation

Work on branch `ui-ux`, based on `madeira-fixes`. These changes require a new
native app build; copying a Wine DLL into an older IPA does not add this UI.

## DLC: diagnosis and supported path

The inherited `docs/STEAM_LIBRARY.md` explicitly excludes DLC installation.
The library fetcher hides DLC as standalone games and `installDepots` excludes
every depot with `dlcappid`. This establishes the old scope, not the author's
reason for choosing it. No evidence of a general Steam restriction was found.

[Valve's DLC documentation](https://partner.steamgames.com/doc/store/application/dlc)
distinguishes content shipped inside the base game from separately downloaded
depots. DLC depots belong to the **base app's** depot list, including language,
OS and architecture variants. A store listing is not an ownership license.

The game's Steam section and install sheet now link to **Downloadable content
(DLC)**. The list intersects PICS relationships with the current account's
package licenses. Download one add-on or all missing compatible add-ons.
Nothing is automatically selected simply because the account owns it.
Content already included in the base game has no separate Download button;
Valve's client/game still decides whether that license is usable at launch.

Downloads use the current base build and its selected DLC together, reusing
verified chunks. Selection is saved per account and game for restart, resume,
update and repair. An existing install record supplies the initial selection.
Refreshing the DLC list removes selections no longer licensed. Every install
refreshes DLC ownership, and Steam still supplies the real depot keys and
manifest authorization. Refusal of selected DLC fails the download rather than
silently reporting the base game complete. Completed depot records carry the
DLC app ID. No Microsoft runtime depots are enabled.

An additional gap was found in the inherited downloader: it sized all files
before writing all depots, with no handling of paths shared by two depots.
Different sizes/content and per-depot resume journals can corrupt such a file.
The new preflight rejects case-insensitive file collisions **before any game
file is resized or journal removed**. For now, overlapping content must be
installed through Valve's client. This is an explicit limitation, not a claim
of support for every DLC. No depot priority is guessed.

## Steam Cloud

**When saves differ → Always keep local** is the default in this fork, saved
per account/game; **Ask each time** restores the previous conflict behavior.
Local files win even when only the cloud has changed. Uploads still verify the
local file and back up the cloud version before replacing it. A cloud-only
save never synced here may download; a previously synced save deleted locally
stays absent, while its cloud copy is retained. A local-only file is uploaded
even if it disappeared from the cloud. This policy also applies before Play
and to Upload saves and close Madeira. Transfer failures remain visible.

Differing file details are collapsed by default under **Show differing files**.
File counts and sync status remain visible.

## Orientation

**Settings → Display → Always use landscape** applies immediately and is
remembered across launches. The app delegate restricts allowed orientations
and the foreground window scene requests landscape via Apple's public
`requestGeometryUpdate` API, including when iPhone portrait lock is enabled.
This rotates the real scene, including Metal and touch-control windows, rather
than only applying a SwiftUI visual transform. Turning it off returns to
portrait and restores system-controlled rotation. UIKit failures are shown
in Settings with a retry button. Multiwindow/iPad restrictions still belong to
UIKit; no private API or system rotation-lock setting is modified.

## Validation

[CI passed for code commit `4acdc67`](https://github.com/LordierClaw/Madeira/actions/runs/37572179370)
on 2026-10-07: Linux download fixtures with AddressSanitizer, macOS cloud
fixtures, Swift syntax checks, and the iOS SDK typecheck described below.

Host fixtures cover DLC ownership/selection, refusal, install records,
collision preflight, and existing encrypted download/resume/update behavior.
Cloud fixtures cover both policy choices and deletion/reset cases. An iOS SDK
typecheck checks the production changed views, model methods and orientation
code with unrelated runtime services stubbed. CI does not contact Steam or
include vendor runtimes, credentials, game data or device logs.

[The full native Debug app and JIT helper now build and link](https://github.com/LordierClaw/Madeira/actions/runs/37581572994)
from `dbcd1ce`. Local IPA packaging preserves the new native code and rebuilt
DataFix loader while adding separately supplied compatibility resources; see
[BUILDING.md](BUILDING.md). iPhone checks remain required: owned DLC in-game,
resume after termination, cloud upload/backup with real saves, and landscape
with portrait lock on at cold launch and when returning from the JIT shortcut.
