# Ecosystem inspection — 2026-10-10

Read-only local source inspection before implementation. No live server deployment.

| Component | Verified source | Findings and reuse |
|---|---|---|
| EutherVox | `/home/nichlas/EutherVox`, commit `390fc9e`, `server/gateway/actions.py`, `session.py`, `scryer.py`, `main.py` | Python async WebSocket gateway; `DeviceAction` and deterministic allowlist; `action.request`, `action.status`, `action.completed`; authenticated user checked for service adapters. EutherCommand's adapter follows this shape and exact aliases. |
| EutherNet | `/home/nichlas/EutherNet`, commit `3f7d5fa`, `scripts/euthernet_http.py`, README | inventory/diagnostics with `/api/euthernet/status`, `/inventory`, `/map`; POST ask/refresh/run/preflight/eutherium award. No dynamic device registration endpoint found. Do not invent one. |
| EutherID | `/home/nichlas/EutherID/src/lib.rs` | constant-time service token comparison; `x-eutherid-consumer-token` or internal token for action-proof consumption; HS256 issuer/audience/time/jti validation. Local directory is not a Git checkout. |

No established MQTT device-command broker contract was verified. HTTP and Vox
WebSockets are established, so this slice uses versioned HTTP rather than adding
a broker. EutherID action proofs need an explicit registered audience, expected
action binding, provisioning and user-to-device authorization contract; do not
reuse an unrelated service's token or merely decode a JWT without verifying it.
The current scoped local tokens are EutherCommand credentials, not EutherID sessions.

Vox next integration point: add an EutherCommand service alongside existing
lighting/television adapters, pass the authenticated user into the exact-alias
planner before the general media planner, dispatch `device.command` through the
scoped Core API with action_id as request_id, and speak `VoxAdapter.spoken` only
after receiving its result. Reject unmatched/negated phrases. Do not expose raw
carrier/pulse control to an LLM. Current adapter is contract-tested here; the
running Vox gateway has NOT been wired or restarted.

EutherNet next step: agree a collector schema or add an authenticated registration
endpoint in its own project. Core `/v1/nodes` provides a safe starting inventory
without addresses or credentials, but is not an existing EutherNet integration.

Repository correction: this directory initially had no `.git`, so prior remote
commands acted on the parent EutherScryer repository. Restored its known original
origin and initialized a separate `main` branch here with the requested
`https://github.com/NichlasEk/EutherCommand.git` origin. No parent worktree files
were altered by implementation.
