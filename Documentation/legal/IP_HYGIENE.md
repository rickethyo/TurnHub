# TurnHub IP Hygiene Rules

These rules are intended to keep TurnHub easy to review and commercially flexible as the project grows. They are engineering/process rules, not legal advice.

## Hard rules

1. Do not copy proprietary source code, firmware, UI implementations, CAD, artwork, documentation, sounds, icons, or other protected material into TurnHub.
2. Do not use a third-party asset or dependency unless its source and license/permission are known.
3. Record every new dependency or externally sourced asset in `DEPENDENCY_TRACKER.md` at the time it is added.
4. Prefer original implementations built from public standards, documented APIs, datasheets, and our own requirements.
5. Prefer permissive licenses when several technically suitable options exist.
6. Treat strong copyleft, custom licenses, noncommercial terms, source-available licenses, and unknown licenses as production-blocking until reviewed.
7. Do not assume code found on a forum, blog, Q&A site, gist, tutorial, video description, or repository may be copied merely because it is publicly visible.
8. Keep TurnHub branding, UI, enclosure language, sounds, icons, and product presentation visibly its own. Compatibility with another tabletop game does not require copying that game's visual identity.
9. Use third-party trademarks only when reasonably needed for descriptive compatibility/reference purposes, and never imply sponsorship or endorsement without permission.
10. The root TurnHub license never overrides the license of third-party components.

## External code snippets

Before incorporating a nontrivial external snippet:

- Identify the author/source.
- Identify an explicit license or permission that covers reuse.
- Record the source if the snippet materially survives in TurnHub.
- If no clear reuse permission exists, implement the behavior independently from the underlying idea/specification instead of copying the expression.

Tiny conventional language idioms and API usage patterns generally do not need individual tracker rows, but copied substantive implementations do.

## Libraries and frameworks

Before adding a library:

1. Confirm the canonical upstream project.
2. Record the exact license.
3. Determine whether the library is build-only, dynamically used, statically linked, compiled into firmware, bundled in the app, or otherwise distributed.
4. Check transitive dependencies when practical.
5. Classify it GREEN, YELLOW, or RED in the tracker.
6. If YELLOW or RED, document the decision that allows development to continue and what must be resolved before release.

Prototype use does not automatically mean production use is approved.

## Images, icons, fonts, audio, and 3D models

For every externally sourced creative asset intended to ship:

- Keep the original source URL/reference in the tracker or adjacent documentation.
- Record creator/owner and license.
- Record whether attribution is required.
- Record whether modification and commercial use are allowed.
- Keep a copy of required attribution text with the release notices.

Do not rely on “royalty free” as a license description. Record the actual terms.

## Hardware and reference designs

Using public datasheets, pinouts, standards, connector specifications, and vendor application guidance to design compatible hardware is normal engineering work. If TurnHub incorporates a vendor reference schematic, PCB layout, CAD file, or other reusable design artifact, record the source and permission/license rather than treating it as TurnHub-original work.

## AI-assisted development

AI assistance does not change these rules. Do not ask for or knowingly accept reconstructed proprietary code or deliberately cloned protected visual/product designs. When a solution can be implemented from public specifications and our own requirements, prefer that independent route.

Generated output should still be reviewed for suspiciously distinctive third-party text, code, visual assets, names, or branding before release.

## Pull request / feature checklist

For meaningful additions, ask:

- Did this add a dependency?
- Did this add or copy an asset?
- Did this incorporate external sample/reference code?
- Did this add a third-party product/game name or logo?
- Did this introduce a license notice obligation?
- Does `THIRD_PARTY_NOTICES.md` need updating?
- Does `DEPENDENCY_TRACKER.md` need updating?

If all answers are no, proceed normally. If any answer is yes, update the appropriate record in the same development cycle.

## Release gate

Before Kickstarter prototypes intended for external distribution, paid beta hardware, app-store publication, manufacturing, or retail sale:

1. Freeze exact dependency versions.
2. Generate/review the production software bill of materials.
3. Resolve every RED tracker item.
4. Review every YELLOW tracker item.
5. Include required license and attribution notices.
6. Review third-party trademark references and marketing copy.
7. Verify that TurnHub's own copyright/license information is complete.
8. Have qualified counsel review the product/IP posture appropriate to the release stage.

Last established: 2026-09-19
