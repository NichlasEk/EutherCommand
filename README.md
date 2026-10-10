# EutherCommand


**Correction, 2026-10-10:** the user withdrew the earlier volume-change confirmation.
IR wakes the Logitech volume display but has not been shown to change the volume.
Earlier successful-response notes below are historical and superseded.

**Give the Digital World Hands.** Local first. Open protocols. Physical control.

**From intention to action.**

A first working development slice: authenticated local API → registered command →
bounded IR transport, with a simulator and an ESP-IDF USB bench node.
No cloud, runtime Python dependencies or arbitrary model-generated device actions.
Python 3.11+ is required.

## Run without hardware

```sh
python -m unittest discover -s tests -v
python -m tools.demo
export EUTHERCOMMAND_OPERATOR_TOKEN="$(python -c 'import secrets; print(secrets.token_hex(32))')"
export EUTHERCOMMAND_VOX_TOKEN="$(python -c 'import secrets; print(secrets.token_hex(32))')"
python -m euthercommand.api
```

In another terminal with the same operator environment:

```sh
python -m tools.client status
python -m tools.client history
python -m tools.client disable true
```

API listens only on `127.0.0.1:7425`. Simulator is the default and always returns
`simulated`, never `transmitted`. Protect `.local/` with account permissions and
retain its SQLite database: it holds signals, confirmations and durable request IDs.
Do not delete that database to retry an uncertain transmission.

## First physical signal

Read [HARDWARE.md](HARDWARE.md) before wiring. Exact board, LED, receiver, power
supply and test device are still required. Pins default to -1 (disabled).

1. Install ESP-IDF v5.4.2, export its environment, select the **actual** chip with
   `idf.py -C firmware set-target <chip>`. Then `idf.py -C firmware menuconfig`.
2. Set reviewed RX/TX/physical-enable pins under EutherCommand. Build with
   `idf.py -C firmware build`, flash with `idf.py -C firmware -p <port> flash`.
3. Close serial monitors. With IR LED power disconnected, run
   `python -m tools.bench --serial <port>`. Press the original remote once.
4. Inspect `.local/capture.json`. Decode with
   `python -c 'import json; from euthercommand.ir import decode; print(decode(json.load(open(".local/capture.json"))))'`
   (or inspect the metadata printed by the bench tool).
5. Start `python -m euthercommand.api --serial <port>` using the same credential
   environment. Only one configured node is supported by this USB backend.
6. `python -m tools.client learn .local/capture.json amp_power`
7. Connect the reviewed driver supply and hold the physical enable switch.
   `python -m tools.client execute test_amplifier power bench-001`
8. Observe the test device, then record
   `python -m tools.client confirm bench-001 true` (or `false`).
   A human report is recorded separately; it is not independent state telemetry.
9. `python -m tools.client disable true`, release enable and disconnect LED power.

Use a new request ID only for a deliberate new action. No automatic retransmission.
One serial owner at a time. Core CLI learning uses capture/import; EutherVox
uses the authenticated node learning API documented below.

## Scope and milestones

| Milestone | Current implementation | Physical evidence |
|---|---|---|
| A capture | RMT RX → USB JSON → host decode | PASS: 1838 + Strong remote, 3 matching NEC frames (0x88 / 0x01) |
| B replay | Persistent learned signal → RMT TX, physical/software enable | Volume display responds; actual volume change not yet confirmed |
| C network | Pinned HTTPS node and registered Logitech commands | Wi-Fi and decoded IR verified; volume change pending |
| D voice | Authenticated gateway and TV app with exact Swedish aliases | Gateway deployed; phone spoken test pending |
| E discovery | Configured node inventory API; verified EutherNet gap | Registration unavailable in inspected API |
| F expansion | Multiple logical devices/nodes in core; persistent duplicate protection | Single physical HTTPS node verified; multi-node setup pending |

NEC and SIRC host decoding, NEC and 12-bit SIRC fixture encoding are implemented.
RC5/RC6 and other protocols retain raw timing capture/replay; protocol-aware toggle
bits and repeated frames are not implemented. Some devices will require those.
The 38 kHz receiver cannot measure carrier frequency; 38 kHz capture metadata is
an explicit assumption. Confirm carrier before replay, especially Sony's 40 kHz.

See [architecture](ARCHITECTURE.md), [protocol](PROTOCOL.md),
[ecosystem evidence](docs/ECOSYSTEM.md) and [validation](docs/VALIDATION.md).

## Wi-Fi provisioning

Mobile setup is available through a temporary password-protected ESP32 network.
See [phone setup instructions](docs/WIFI_SETUP.md). After setup, [local HTTPS control](docs/LAN_CONTROL.md) supports the registered
Logitech commands through an explicit, bounded enable window.

Physical milestone: user confirmed Logitech volume-down response to a registered
command sent over pinned HTTPS/Wi-Fi, using two 270-ohm LED resistors in parallel.
This is user-observed response; automated appliance-state feedback is absent.

## EutherVox TV apparater beta

The Android TV tab now includes EutherBeam and authenticated IR learning with
user-owned spoken aliases. See [deployment, workflow and validation](docs/EUTHERVOX.md).

Fast Logitech volume without acknowledgements: [protocol and evidence](docs/FAST_IR.md).
