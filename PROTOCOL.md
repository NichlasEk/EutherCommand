# Protocol v1

HTTP JSON, loopback port 7425. `Authorization: Bearer <service credential>` on
all requests. Maximum body 16 KiB; no chunked requests. JSON objects only.
Public execute is transport independent; raw waveforms require learning privilege.

| Method/path | Scope | Body/result |
|---|---|---|
| GET /v1/status, /v1/nodes | read | configured visible nodes, mode, disable state |
| GET /v1/devices | read | visible devices and registered command names |
| GET /v1/signals/{name} | read | metadata of an authorized registered learned signal |
| GET /v1/history | read | latest 100 ledger rows filtered by device access |
| POST /v1/learn | learn | `{ "node": "lab" }` → capture ID, 60-second expiry |
| POST /v1/learn/capture | learn | capture ID and signal; one import per session |
| GET /v1/learn/{id} | read | owning principal's capture and decode metadata |
| POST /v1/learn/save | learn | capture ID and unique `name`; persists raw signal |
| POST /v1/execute | execute | device, command, request_id |
| POST /v1/disable | disable | `{ "disabled": true }`; persists across restart |
| POST /v1/confirm | learn | request_id and responded boolean; human observation |

Execute example:

```json
{"device":"test_amplifier","command":"power","request_id":"example-001"}
```

```json
{"device":"test_amplifier","command":"power","request_id":"example-001","status":"simulated","device_state":"unverified"}
```

Statuses: `accepted` = durable reservation only; `simulated` = no physical output;
`transmitted` = node RMT completion acknowledged; `unknown` = completion uncertain.
No current backend reports independent device state verification. A duplicate
returns its original result plus `duplicate: true`, without another side effect.
A reused ID with a different caller or payload is rejected. Credentials are
principal-specific; request IDs are global to this database.

Signal schema: `carrier_hz` integer 30000–60000 and `durations_us` integer array
of alternating mark/space durations starting and ending in mark. Odd length
3–511, individual duration 100–30000 us, total <=250000 us. No repeat count.
Carrier is not inferred from a demodulating receiver. Raw timings are retained
when protocol classification is unavailable. Standard NEC inverse bytes are
validated. Extended NEC currently falls back to raw.

USB bench firmware speaks newline-delimited JSON at 115200 baud. Maximum line
8191 bytes. Commands: `{"op":"disable"}`, `{"op":"arm"}`,
`{"op":"save","signal":{...}}`, `{"op":"replay"}`.
Save validates and stores one bench slot in NVS; the core owns the multi-device
library. Arm still requires a physically held enable input. Firmware emits
boot/capture events and operation status. Local serial has no service token;
OS device permissions and physical possession define that boundary. It is not a
network protocol and must not be bridged to an untrusted TCP port.

Configured node listing is not network discovery. Firmware version and last
contact remain null until an authenticated health protocol exists; `unknown`
availability is not presented as online. No credentials/network secrets appear
in the inventory response.

## Explicit USB software-arm bench profile

The default firmware still requires physical enable. With the deliberately
selected `EC_USB_BENCH_ARM=y` profile and ENABLE_GPIO=-1, `arm` can grant a
software one-shot permission: expires after two seconds, consumed before a single
RMT submission, maximum three submissions per boot. This does not persist across
reset. Denial returns `arm_denied`; GPIO inputs/limits are not bypassed in the
normal profile. Host `tools.tx_trial` sends exactly three separately registered
volume_down requests at most, stops on uncertainty, and never retries. The separate authenticated network
control API is described below. The physical LED-supply disconnect remains
the immediate means of cutting an in-progress emission.

## USB Wi-Fi provisioning

`wifi_setup` disables transmission, sets a one-boot provisioning flag in NVS
and replies `restarting` before reboot. Physical USB command replies echo an
optional ASCII `id` (maximum 64 characters); the host requires a matching random
ID to reject stale replies. This is correlation, not node-level deduplication.
Setup HTTP has no IR operations; see `docs/WIFI_SETUP.md`.

Native HTTPS node protocol and replay rules: [LAN control](docs/LAN_CONTROL.md).

## Authenticated node learning and EutherVox

See [LAN wire protocol](docs/LAN_CONTROL.md) for capture, NVS registration and
replay-safe session renewal; [EutherVox integration](docs/EUTHERVOX.md) documents
user ownership, exact voice aliases, persistent deduplication and confirmations.
