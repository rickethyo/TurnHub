# Implemented Atlas client HTTP contract

Atlas remains authoritative. Browser and native clients share these routes and
must not implement game rules. Responses use `Cache-Control: no-store`. Connect
to Atlas's existing Wi-Fi network first. Discovery, automatic Wi-Fi connection,
BLE, events and the generic JSON Intent envelope remain unimplemented.

## Connection and first PASS

1. `GET /api/v1/info` returns public device identity (`THA-` plus station MAC),
   firmware, HTTP API version `1`, logical protocol `0.1`, radio version `1`,
   boot ID, revision and capability flags. MAC identity is not authentication.
   No Wi-Fi password, PIN, profile data or session token is included.
2. `GET /api/v1/state` returns one main-loop snapshot conforming to
   [state-v0.1.schema.json](state-v0.1.schema.json). This is public table state,
   consistent with `/api/status` and `/api/seats`. No profile statistics or
   credentials are included. Names remain presentation metadata from `/api/seats`.
3. Use `GET /api/profiles`, then `POST /api/session/login` with form fields
   `profileId` and `pin`, or `POST /api/profiles/register` with `name` and `pin`.
   Keep the returned token private; send it in `X-TurnHub-Token` on authenticated
   requests. `POST /api/session/join` joins the authenticated profile.
4. `GET /api/session/me` resolves the session's current `module`, `slot`, `player`
   and `participating` status. Physical companion attachment retains its existing
   [physical claim flow](../Documentation/engineering/PROFILE_LOGIN_AND_VIRTUAL_PLAY.md).
5. Fetch state. Map semantic `PASS` to `POST /api/control/pass` with
   `application/x-www-form-urlencoded` fields, not the draft JSON envelope.
   Atlas resolves the actor from the session, never client-supplied seat IDs.
6. Success returns `ok`, `status: "ACCEPTED"`, `message`, `bootId` and `revision`.
   Fetch state again to render the authoritative outcome.

Controls dispatch through the same `IntentDispatcher` as Sigils and Atlas's
button. PASS acceptance may arm **or cancel** the existing three-second grace
period; it does not mean the next turn has started. State confirms the later commit.

## Revisions and reconnect

- Compare `(atlasId, bootId, revision)`. Boot ID is a public random epoch generated
  when the HTTP service starts, not an authentication credential.
- Revision is a 32-bit in-memory version of the published gameplay projection.
  Atlas observes completed dispatches and refreshes before reads. A meaningful
  projection change advances it; rejected and accepted no-op requests do not.
  Nested dispatches may increment it more than once. It is not an event count.
- The projection covers membership/participant identity, settings, lifecycle,
  active/starter/winner, completed turns, elimination, PASS/countdown state, win
  decisions, life, Commander damage and life approval records. It does not version
  names, account policies, pairing, button gestures or network health.
- Clock samples (`sampledAtMs`, `gameElapsedMs`, `turnElapsedMs`, remaining PASS
  time) may change at the same revision. Millisecond timestamps wrap at 32 bits;
  use unsigned subtraction within one boot. Pending life approval expires
  15,000 ms after `requestedAtMs` under Atlas's existing rules.
- Missing Commander entries mean zero damage. Each entry's two values represent
  commander 1 and commander 2 of `sourcePlayer`. Lobby life is `null`.
- `moduleId` retains the existing wire spelling for an Atlas controller handle:
  physical handles 0–7, virtual handles 8–23. It is not a durable device ID.
  Slots are 1/2. `participantId` is stable while that participant is at the table;
  re-resolve identity after reconnect or lifecycle changes.
- Always fetch fresh state after reconnect. If identity/boot changes or revision
  decreases (including wrap), discard cached state and stale commands. Sessions
  are volatile and expire after eight hours of inactivity. A 401 requires login
  again; rejoining still obeys existing lobby restrictions.
- Never automatically replay an ambiguous timed-out control. Request IDs are not
  deduplicated. Reconcile from state, then let the user issue a new action.

## Optional concurrency check on session controls

The existing `runControl` adapter accepts `expectedRevision` (canonical unsigned
decimal text) and `expectedBootId`. Supply both from the latest snapshot. A stale
revision, missing/wrong boot ID, or unavailable revision provider returns HTTP 409
with `status: "CONFLICT"` before dispatch. Invalid revision text returns HTTP 400.

Supported routes: `/api/control/pass`, `pause`, `concede`, `win`, `confirm`, `deny`,
`starter`, `start`, `cancel-start`, `rematch`, `reset`. Omitting the check preserves
browser behavior. Life, counters, participation and settings retain their existing
contracts and do not yet accept this check.

After authentication and seat resolution, handler rejection returns HTTP 409 with
`ok: false`, `status: "REJECTED"`, `error`, `bootId` and `revision`. Earlier
authentication/validation failures retain existing JSON errors; not every error
contains a revision or semantic status. Fetch state after a conflict or rejection.

## Fixtures and remaining work

[Response fixtures](examples/) come from host scenarios with public identity,
epoch and revision normalized for readability. `pass.request.json` describes
the live HTTP adapter request, not an Intent body.
`intent-v0.1.schema.json` remains a draft. `/api/v1/intent` and `/api/v1/events`
do not exist and are advertised as unsupported.

The next Android slice can test info → login/join → snapshot → PASS → snapshot
→ reconnect through an `AtlasRepository`, with a fake transport and these fixtures.
Generic envelopes, richer per-intent statuses, deduplication and events remain
separate future work. No Android application ID is selected here.
