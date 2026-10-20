# Death May Die Database Forge

The [private browser workbench](https://turnhub-dmd-forge.zanyplume16.chatgpt.site)
authors the Death May Die catalogue. Open it on any device and sign in with the
same ChatGPT account. It offers six entry types, reference/photo preview, rules
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
reference photos and a SHA-256 manifest. Build snapshots and explicit checkpoints
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
instructions. `lib/catalogue.mjs` is copied from the repository compiler and must
remain byte-for-byte identical at each coordinated release. `lib/seed.json` is
only the initial seed; deployment never overwrites an existing user's D1 draft.
Use Import to intentionally update that draft. JSON backups do not embed images;
retain the build ZIP when you need reference photos too.

## Storage

Site owner-private access and ChatGPT sign-in protect the editor. Every API is
scoped by authenticated user ID. D1 stores revisioned JSON and immutable history;
R2 stores reference images. Images removed from a draft remain available to
older snapshots. Browser storage is not authoritative. Keep downloaded backups
if you need copies independent of hosting. Same-account cross-device edits use
optimistic revision checks, not simultaneous live collaboration.
