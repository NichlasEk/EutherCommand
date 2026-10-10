# Fast Wi-Fi volume path — user-confirmed at-TV trial

User correction: the old single-frame signals illuminate the Logitech volume
display but do **not** yet have confirmed volume-change behavior. Retain original
captures while comparing a full press and hold, including NEC repeat frames.
Reference timing: https://github.com/Arduino-IRremote/Arduino-IRremote/blob/master/src/ir_NEC.hpp
NEC repeat behavior is a hypothesis for this device until captured/verified.

## Removed latency

Previously each tap performed status → enable → 300 ms sleep → execute → disable,
using four separate TLS connections. The client then waited for the acknowledgement.
Now each Logitech tap reserves its ID durably, sends one authenticated UDP datagram
to the node's station IPv4 address on port 7426, and returns without waiting.
The gateway refreshes device epoch/clock via pinned HTTPS in the background every
five seconds; this is not in the button path. A missing/stale clock cache rejects
the action rather than queueing it. Other registered commands still use HTTPS.
There is no UDP response, retry, hold-to-repeat or user confirmation dialogue for
these volume buttons. Phone-to-gateway communication retains authenticated WSS.

## Packet

ASCII `EC1|epoch32|sequence16hex|deadline_ms16hex|up/down|hmac64hex` (138/140 bytes).
HMAC-SHA256 covers everything before the final separator, keyed with the existing
private node token as UTF-8 bytes. The token is never sent in UDP. The packet is
authenticated but not encrypted; it contains only direction, replay and time data.
The token remains on the gateway, never in Beam or the Android APK.

Node epoch is random per boot, independently of the old HTTP request ledger.
Only increasing sequence numbers are accepted. Gateway SQLite execution row IDs
supply persistent ordering; do not purge/recreate this ledger while retaining a
node session. Requests have a 500 ms deadline in the node's uptime domain, and
the node rejects deadlines more than two seconds into its future. Authentication
must pass before advancing the sequence. Replayed, expired and out-of-order
packets are dropped, not retried. A busy IR peripheral drops the action rather
than adding an application queue. Radio/kernel buffering remains bounded by the
packet deadline. Volume emissions have a minimum 160 ms interval.

## Disable and startup

This fast path authorizes a single volume attempt per valid signed packet;
it does not use the older three-command arm window. Provisioned firmware starts
ready for authenticated fast packets, with no output until one arrives. The old
`status` field still describes the HTTPS arm window; `fast_enabled` describes
this separate path. USB or HTTPS `disable` latches the fast path off until an
explicit HTTPS `enable` or reboot. Clock refresh does not re-enable it. A physical
enable GPIO, when configured, must still be asserted. Learning before the first
captured signal blocks fast transmission. No arbitrary raw waveform/GPIO operation
is accepted over UDP. Power removal remains the immediate physical stop.

## Validation so far

- Host tests verify one send with no receive/retry, HMAC construction, expiry,
  registered-direction allowlist and disabled/stale-session rejection.
- C parser tests verify bounds, epoch, sequence and deadline rejection.
- Gateway tests verify no success acknowledgement and persistent duplicate
  suppression after a failed send and restart.
- ESP-IDF build and USB flash hash passed. On-device disabled-TX test logged one
  valid packet; duplicate and invalid-signature packets produced no second event.
- Local `sendto` path measured about 2.5 ms in one observation; this is **not**
  end-to-end IR latency or proof of a physical volume change.

## Observed repeat and deployed test

The user's hold capture produced six valid NEC repeat frames around
9008/2228/561 microseconds, generally about 110 ms apart. The registered first
frame is now followed by one repeat at 110 ms from its start; total duration is
121.81 ms. The low gap is split across two RMT duration fields to fit their
15-bit limit. The original captured code/address remain unchanged. This is a
bounded two-frame physical press, not a network retry or continuous repeat loop.
A C test checks the exact start time, polarity, duration and capacity limit.

Final firmware flashed with hash verification. A single request through the live
trusted gateway logged `fast_volume/down/transmitted`; the physical receiver
independently decoded NEC address 8 / command 14. No command acknowledgement was
returned. This verifies node transmission and code reception, **not** actual
Logitech volume change; the user's next at-TV test remains decisive.
Apps: Vox 0.20.0-beta.3 (67), Beam 0.1.0-alpha.14 (14), both build/lint/unit tests
pass. Emulator buttons dispatch through the mock gateway without waiting for a
success response. Update both apps. After powering the node, allow about ten
seconds for Wi-Fi and the background authenticated clock refresh.

## At-TV result, 2026-10-10

After installing the fast-path update with the NEC repeat frame, the user reports
“Det funkar kanon!”. This confirms the requested volume trial works on the
current installation. No numerical end-to-end latency measurement or second-room
trial was performed. Voice aliases and two-TV routing are the next work.
