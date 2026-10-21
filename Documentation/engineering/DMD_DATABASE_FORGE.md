# Death May Die Database Forge

The [private browser workbench](https://turnhub-dmd-forge.zanyplume16.chatgpt.site)
authors the Death May Die catalogue. Open it on any device and sign in with
an invited ChatGPT account. All invited accounts share the same catalogue, history
and reference photos. It offers twelve entry types, reference/photo preview, rules
and skill editing, physical-reference review checks, validation, shared saved
drafts, checkpoints and build snapshots. It requires internet for shared storage.
Unsaved edits live in the current tab; save before moving between devices.

## Author and build

1. Select an entry or create one. IDs are generated from the initial name and
   remain stable after creation. Keep existing IDs when correcting content.
2. Enter source details, rules, aliases, collection and skill references. Attach
   JPEG, PNG or WebP reference photographs (8 MB maximum) as needed.
3. Compare against the physical reference before checking it. Relevant edits
   clear review flags. These authoring flags never bypass in-game verification.
4. Save the shared draft. A stale device cannot overwrite a newer revision;
   download Backup JSON before reloading to preserve its unsaved draft.
5. Build database. Hard validation errors block the package. Warnings identify
   incomplete/unverified content and are included in the report.

ZIP output includes source JSON, generated Android Kotlin, validation report,
reference photos, tile footprint SVGs, engine authoring JSON and a SHA-256 manifest. Build snapshots and explicit checkpoints
are retained in History; restore creates a draft that needs saving. Backup JSON
also exports unsaved edits. Import validates JSON before replacing the local
working draft and requires an explicit confirmation, then Save to persist it.

Photos stay in private authoring storage and the build package. App image URLs
remain explicit references; card image IDs share an existing geekdo image map.
No OCR, rules inference, automatic effects, APK building, release publishing or
live match modification occurs in this tool.

## Repository import

Canonical source: [catalogue.json](../../data/death-may-die/catalogue.json).
The initial migration preserves 264 entries: 132 investigators, 16 skill
presets/signatures, 35 conditions, 20 relics, 18 Elder Ones and 43 episodes.
All imported review flags start false. Missing skills/rules remain incomplete.

With Node 22.13+ at the repository root:

```text
node tools/dmd-forge/build.mjs --input /path/to/catalogue.json --validate-only
node tools/dmd-forge/build.mjs --input /path/to/catalogue.json --import
node --test tools/dmd-forge/catalogue.test.mjs
node tools/dmd-forge/build.mjs --check
```

Review both JSON and generated Kotlin diffs, then run Android's unit tests and
normal build. Generated `DmdContent.generated.kt` supplies existing domain
catalogues; limits, personal card verification, game state and effects remain
owned by their original code. Source URLs and review notes accompany card drafts.
Editing generated Kotlin directly fails CI's generation check. A new database
enters installed apps only through the normal reviewed app build/distribution.

The private Site is a separate source repository. Local checkout for this slice:
`D:/TurnHub/sites/dmd-database-forge`; its README owns browser build/development
instructions. `lib/catalogue.mjs` and `lib/content.mjs` are copied from the repository compiler modules and must
remain byte-for-byte identical at each coordinated release. `lib/seed.json` is
only the initial seed; deployment never overwrites an existing shared D1 draft.
Use Import to intentionally update that draft. JSON backups do not embed images;
retain the build ZIP when you need reference photos too.

## Storage

A private Site allowlist and ChatGPT sign-in protect the editor. Every API requires
authentication. Invited accounts use one shared D1 catalogue and history, and the
same R2 reference namespace. The initial workspace setup preserves the original
private catalogue storage prefix, including existing snapshots and photos.
D1 stores revisioned JSON and immutable history;
R2 stores reference images. Images removed from a draft remain available to
older snapshots. Browser storage is not authoritative. Keep downloaded backups
if you need copies independent of hosting. Edits across accounts and devices use
optimistic revision checks, not simultaneous live collaboration. Your PC does not
need to be on. Only the owner manages the Site allowlist; visitors can edit content
inside the app without receiving source-code editing access.

## Episode and engine content

Discovery cards, Mythos cards, monsters, tokens, items/components and map tiles
are dedicated categories alongside the original six. Entries support printed
IDs, exact rules, flavor text, physical-source notes, episode links and reference
scans. Episode records include season/number, setup, ordered end-of-turn text,
ritual/victory/defeat conditions and named unknown-monster slots with monster links.

Box inventory rows record kind, label, whole quantity, optional catalogue entry
and face/color/variant notes. Unlinked deck totals may be saved as drafts;
validation reminds the author to catalogue the individual cards. Split total rows
as individual entries are linked so each physical copy is counted once. The two
episode-box examples supplied by the owner have optional inventory/flavor draft
buttons on their corresponding episode entries. No existing shared data is
automatically replaced, and the examples do not invent unreadable card rules.

Trigger definitions capture timing windows, resolution order, conditions,
targets, costs, effects/replacements, duration, limits, choices and interaction
notes. Reference-only, draft and reviewed automation states track preparation;
reviewed automation requires physical verification and nonempty effects. Content
edits clear verification and demote reviewed automation. The engine export is
authoring data; the current runtime does not execute these definitions.

Tiles are classified by collection, printed ID, side and shape (square, rectangle,
L, T or custom). An editable connected-cell grid renders a schematic footprint,
with SVG download/build output. Actual map artwork uses attached reference scans;
rooms/space IDs and connections are entered as reference notes. The outline does
not infer rooms, doors or movement. Store each printed face as a separate record.

These are optional schema-1 additions. Existing drafts load without alteration,
and all expanded data survives JSON backup/import/build. Android generation keeps
its original supported catalogue projections; future engine implementation must
review and explicitly consume the authoring definitions.
