# Web Portal (historical)

*Verified* (owner, 2026-10-30): the embedded admin portal is removed. Atlas
serves no web pages: `/portal`, `/login`, `/stats`, `/tablet`, `/dev`,
`/update`, `/sigil-update` and `/assets/*` answer 404, the firmware carries no
web assets, and the Atlas screen's QR codes are only the table Wi-Fi.

Every administration API the portal called stays (see
[`protocol/http-v1.md`](../../protocol/http-v1.md)): accounts, permissions,
merge and archive (`/api/accounts*`), presence codes, network and home Wi-Fi
(`/api/network*`), pairing, speaker, table code, upload approval, device names,
forget and factory reset, diagnostics, and the signed firmware uploads
(`/api/firmware`, `/api/sigil-firmware`). The Android app is the one client for
them; the settings it does not offer yet are staged in
[Staged changes](STAGED_CHANGES.md). The portal's last source is in git history
before this change.
