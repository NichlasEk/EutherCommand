# Local HTTPS control

The node's HTTPS server starts after provisioning. Port 443 accepts requests
addressed to its station IP only. Every route requires a separate random bearer
token. The host validates both the local certificate's TLS identity and its exact
SHA-256 pin before sending the credential. No redirects or automatic retries.
The setup Wi-Fi password is not a control credential.

`python -m tools.provision_control` generates a local P-256 server identity and
32-byte random token without overwriting existing credentials. Private material
stays in `.local/control/` and ignored `firmware/main/control_local.h`. Never
publish the device-specific firmware binary. Without that header, firmware
builds without a LAN control listener. This development device lacks secure boot
and flash encryption; physical flash access can recover its credentials.

Update `host` in `.local/control/client.json` if DHCP changes the address. A DHCP
reservation is preferable; discovery integration is not yet implemented.

```
python -m tools.lan status
python -m tools.lan enable
python -m tools.lan volume_down
python -m tools.lan disable
```

Enable explicitly grants at most three transmissions over 60 seconds. No repeat
loops; at least one second between physical frames. Boot, expiry, exhausted
quota, or disable stops LAN transmission. USB disable also cancels LAN enable.
A configured physical enable input must also be active. In the current bench
hardware there is no physical switch; the authenticated software window is the
explicit mechanism. Disconnecting power always stops transmission.

The two captured Logitech controls are built in. Authenticated learning and
registration add up to eight named IR signals in NVS; execution only accepts
registered identifiers. No GPIO-selection or firmware-write endpoint exists.
EutherVox now connects through its authenticated gateway; see [integration](EUTHERVOX.md).
Core's separate development API remains loopback-only.

## Wire protocol

- `GET /v1/status`: authenticated boot ID and enabled/disabled status.
- `POST /v1/commands`: exactly `boot_id`, `request_id`, `command`.
- Commands: `enable`, `disable`, `volume_up`, `volume_down`, registered `ir_` + 12 hex IDs.
- `POST /v1/learn` starts a 60-second capture and disables transmission.
- `GET /v1/learn` returns capture status and bounded raw timings.
- `POST /v1/signals` registers `{name, signal}` (4096-byte body limit).
- `POST /v1/session` accepts exactly the current `boot_id`, only while disabled,
  and returns a fresh epoch with an empty request ledger. Old epoch requests fail.
- Status includes `history_remaining`; the gateway rotates before fewer than three entries remain.
- Body limit 512 bytes; request ID 1–64 ASCII letters, digits, `_` or `-`.
- Every boot receives a fresh random 128-bit ID. Old-boot commands are rejected.
- A 64-entry, non-evicting per-epoch ledger reserves IDs before hardware effects.
  Duplicates return the original status without executing; different operations
  with the same ID conflict. When full, further commands are denied except
  disable. An authenticated epoch rotation or reboot starts a new node session.
  Clients must never refresh and retry an uncertain transmission. Gateway IDs
  remain recorded durably across node sessions.
- New operations are rate-limited to four per second, except disable. Physical
  transmissions additionally have a one-second limit. Rejected rate-limited
  requests do not consume ledger entries.
- Results distinguish `transmitted`, `disabled`, `unknown`, `rate_limited`, and
  `history_full`; `device_state` remains `unverified`. A duplicate `enabled`
  result describes the original request and does not extend its permission.

TLS encrypts and authenticates network traffic. The shared token grants only
this node's learning and registered audio/TV controls; it is a local service credential, not an
EutherID integration. Denial of service by a LAN peer remains possible. Do not
forward the port to the internet. OTA and rollback remain unimplemented; keep
the USB recovery path.

## Validation

`make test` covers host routing, no-retry behavior, setup UI and interface guards.
`python -m tools.check_lan` tests the physical HTTPS endpoint while disabled:
wrong credentials/pin, duplicate/conflicting IDs, stale boot, unregistered
commands, size bounds, and disabled transmission. It sends no IR.
A successful physical target response still requires the user's device trial.
