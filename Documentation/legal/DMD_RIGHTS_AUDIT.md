# Death May Die rights and source audit

Audit date: 2026-10-21. **Outcome: DMD content is not cleared for redistribution
in TurnHub or Database Forge on the evidence currently recorded.** No new
community-source population was performed. A free product or free DMD feature is
not, by itself, a permission grant.

This is a source/provenance and published-policy audit, not a legal opinion,
proof of infringement, or an exhaustive determination of every rights holder.

## Scope inspected

- All 264 records in the repository's canonical catalogue, the Site's seed, and
  an authenticated Backup JSON download of the shared Forge catalogue.
- The shared snapshot contains the same record IDs and counts. One skill entry
  has user-edited levels/notes and is physically checked. Those edits remain
  private and unchanged. Physical verification does not establish reuse rights.
- Generated Android content, DMD domain references, Android assets/resources,
  Forge forms/templates, reference-image handling and ZIP export composition.
- Existing legal tracker/notices, root software license, original icon provenance,
  direct installed Forge package license declarations, and the two web fonts.
- The current shared catalogue has **zero uploaded-reference entries**. No R2
  bucket-wide/orphan-file inventory or scan-image inspection was performed.
  Chat-supplied photographs/PDFs have no recorded publisher redistribution grant.
- The hosted snapshot and its detailed findings are retained outside Git under
  `Private/TurnHub-backups/dmd-rights-audit-2026-10-21`. The public source report
  contains no user-edited prose or private review notes.

[Per-record canonical inventory](dmd-catalogue-rights-inventory.json) records
IDs, source URLs and unresolved content fields. No copyrighted card prose or
image binaries are reproduced in that inventory.

## Publisher policy: material release blocker

The [official campaign announcement](https://gamefound.com/en/projects/asmodee/dmd-forbidden-reaches/updates/72)
reports Asmodee's acquisition of the DMD IP on 2025-10-20. DMD Forbidden Reaches
also appears in the [current FFG product catalogue](https://www.fantasyflightgames.com/product/cthulhu-death-may-die-forbidden-reaches/).
Confirm the applicable rights and legacy/crossover exceptions with the current
rights holder; an old CMON attribution does not establish present permission.

The published [Asmodee North America/FFG community IP policy, August 2024](https://images-cdn.fantasyflightgames.com/filer_public/51/c8/51c8035b-240e-4a12-8ab6-a7b34b97c819/ip_policy_2024_aug20.pdf)
is explicitly not a license. Guideline 6 says **“Do NOT make Apps”** and extends
that restriction to software even when other guidelines are followed. Guideline 5
also excludes substantial copied game material, including downloadable card
packs. Its noncommercial conditions cover more than paywalls: donations,
fundraising and sponsorship are also addressed. It does not clear third-party IP.
Consequently this policy supplies no clearance for our proposed DMD text/art app
or complete digital card library. We have not confirmed a DMD-specific exception
or separate written software/content license. The current legal landing page is
sparse; the hosted policy PDF is evidence, not a guarantee no later agreement exists.

An independently implemented game method and a descriptive name require separate
analysis from copied artwork or prose. The [U.S. Copyright Office games guidance](https://www.copyright.gov/register/tx-games.html)
distinguishes methods/names from protected literary and visual expression. This
audit does not label the independently written engine unlawful or treat all
metadata as licensed publisher content. Trademark, compilation, contractual and
cross-territory issues remain outside that distinction.

## Content findings

| Material present | Inventory | Finding / required action |
| --- | --- | --- |
| Names, IDs, collections, aliases and factual associations | 132 investigators, 16 skills, 35 conditions, 20 relics, 18 Elder Ones, 43 episodes | Descriptive/factual baseline, not an open content license. Preserve provenance and avoid copied descriptions, branding or implied endorsement. |
| Rules/reminder prose | 30 base-rule entries plus 10 transformed faces | Project records describe compact original mechanical paraphrases. Independent wording/origin has not been established field by field against the underlying sources. Keep unresolved for content redistribution. |
| Skill descriptions | 16 entries, four levels each | Same provenance issue. The shared user's edited skill remains unresolved for reuse even though physically verified. |
| External artwork/photo references | 54 entry references, 44 distinct image URLs: 36 investigator images and 8 shared condition/relic photos | No applicable per-image/publisher reuse grant recorded. Runtime fetching, hotlinking and caching are uses, not clearance. No image binaries are bundled in the canonical catalogue. Attribution alone does not establish rights. |
| Episode example flavor text in Forge source | Two copied box-back passages | Recorded-permission gap in the editor templates. Removed from template population in this audit follow-up; inventory counts remain. Existing saved user data is not rewritten. |
| Uploaded scans | None referenced in the shared snapshot | Future scans need provenance and a distribution decision. Owning the physical copy does not transfer its copyright: [17 USC 202](https://www.copyright.gov/title17/92chap2.html#202). No blanket conclusion of personal-use scanning clearance is made here. |
| Tile SVGs and original UI geometry | Generic programmatic footprint renderer; no map-tile records in the shared snapshot | Original schematic output, not copied tile artwork. Actual tile scans, printed layouts and any later third-party images need their own review. |
| Discovery/Mythos/monster/token/item/tile entries | Zero saved entries in the audited shared snapshot | Categories exist; no community dataset was silently imported. Proposed source imports remain on hold. |
| JSON/Kotlin/ZIP exports | Can include text; ZIP can also include uploaded photos | A generated format does not change rights. Physical verification, generation success and checksums must not be presented as rights approval. |

The 46 records containing rule or skill prose and the artwork findings overlap;
these are field-level findings, not disjoint record totals. Blank rules stay blank.

## Community sources evaluated

- **BoardGameGeek:** [upload licensing](https://boardgamegeek.com/licenses)
  defaults to All Rights Reserved. The episode/tile spreadsheet's specific grant
  was not verified; no spreadsheet or image pack was copied. A photographer's
  license would still need to cover the underlying publisher work appropriately.
- **Fandom:** [licensing policy](https://www.fandom.com/licensing) generally covers
  community-authored text under CC BY-SA 3.0, subject to wiki exceptions. It does
  not establish a publisher grant for copied card wording or embedded art.
  Share-alike/attribution obligations and exact page/file provenance would need
  recording before reusing eligible original prose. No new wiki prose was imported.
- **Make Craft Game:** factual associations and links already exist in TurnHub.
  No general reuse grant for its guide prose/photos was found in the checked
  material. Existing source links are not a license. No new guide material copied.
- **CMON/Asmodee PDFs, Gamefound, Dized, artist sites and other linked articles:**
  available as references, without a recorded blanket content-reuse grant in this
  project. Free-to-read/download is not an open license. No new prose/art copied.
- **Crossover content:** Godzilla and other licensed/promotional material need
  rights-holder-specific review; no publisher policy is assumed to clear all IP.

## Original software, fonts and notices

TurnHub's documented original UI icons and generic renderer do not import DMD art.
The root MIT software license still has placeholder copyright fields; resolve
that before formal release. It does not relicense publisher/community material.

All 25 currently installed declared Forge dependencies identify MIT, ISC or
Apache-2.0 licenses in their package manifests. This confirms declarations, not
completion of their notice obligations or a full transitive release SBOM.
DM Sans and Manrope have upstream [DM Sans OFL-1.1](https://raw.githubusercontent.com/google/fonts/main/ofl/dmsans/OFL.txt)
and [Manrope OFL-1.1](https://raw.githubusercontent.com/google/fonts/main/ofl/manrope/OFL.txt)
licenses. Preserve appropriate notices with any font redistribution. Android
Inter/Cinzel and original icon provenance remain covered by the existing tracker.
No new game artwork/audio/logo/font binaries were imported in this audit.

## Disposition

1. Pause the proposed community import and any claim that current DMD prose/art
   is licensed for free or paid redistribution. This applies before public release,
   not only before monetization. No production catalogue contents were deleted.
2. Do not ship a full copied card/scan library on the assumption that ownership,
   source attribution, a physical-review checkmark or no paywall provides permission.
3. Obtain rights-holder/licensing confirmation covering the intended software use:
   private shared authoring, Android companion/admin engine, card text/flavor,
   image display/cache, hosted storage, ZIP exports and any eventual distribution.
   Clarify retained CMON-era rights and crossover exceptions. Routine support
   silence is not approval; the published policy itself disclaims individual advice.
4. For photos or guides owned by community contributors, resolve their independent
   rights/terms as well. Do not contact anyone on the user's behalf without instruction.
5. Continue independently authored schema/tools and factual metadata work with
   provenance. Keep the content import held until its permitted scope is resolved.

The existing app/site has not been represented as fully cleared by this report.
No release, public-sharing expansion, rights-holder message or new community
content import was performed.
