# Current handoff — 2026-10-10


**Physical confirmation, 2026-10-10:** after the NEC repeat-frame correction and
fast Wi-Fi/app update, the user reports “Det funkar kanon!”. The at-TV volume
trial is now user-confirmed. Earlier single-frame results remain superseded;
this does not verify a second installation or spoken multi-TV routing.

Physical node: 192.168.32.248, ESP32 classic, RX D27, TX D26. USB /dev/ttyUSB0.
Learning/NVS/HTTPS/session-renewal plus authenticated UDP fast volume firmware flashed and verified. Activity GPIO
remains disabled (-1) pending identification of the board's controllable LED.
Final electrical setup and physical Logitech response: HARDWARE.md last sections.

EutherVox branch feature/tv-apparater at /home/nichlas/EutherVox. Version
0.20.0-beta.3/code 67; Beam 0.1.0-alpha.14/code 14. The gateway user service runs here (.88), public Caddy and
EutherOxide on .186 forward authenticated sessions. See EUTHERVOX.md.
Remote .186 EutherOxide src/main.rs and webview/main.ts publish this release;
preserve pre-existing webview/build-info.ts changes. No credentials in tracked files.

Pending human trial: install the authenticated APK download, connect, open TV
apparater and verify Logitech commands. Record a spare remote button, inspect,
save a voice phrase, test once, confirm actual device response. Pair network TVs
inside Vox as necessary. Do not infer physical success from simulator or TX ack.
The earlier network-only capture window expired without a supplied remote signal.

Current fast transport: docs/FAST_IR.md. Live gateway-to-UDP trial emitted one
volume-down press and the physical receiver decoded NEC 8/14. The node adds one
NEC repeat at 110 ms; total IR press 121.81 ms. No command ACK or retry. At-TV
volume change and fast operation are now user-confirmed (“Det funkar kanon!”).
Natural Swedish volume routing for two TV names is being added in EutherVox;
room mapping and node assignments await the user.

## Confirmed rooms and voice routing

User confirms NEC + Logitech on the ground floor, Samsung + Logitech one flight
up, with only one ESP32 currently built. EutherVox assigns the current node to NEC
only; Samsung is a recognized but unavailable IR target. Downstairs aliases:
bottenvåningen/nedervåningen/nere/där nere. Upstairs aliases:
övervåningen/uppe/där uppe/en trappa upp. Gateway service configuration activates
natural volume routing without APK/firmware changes. Explicit NEC/room volume
requests select the downstairs node; an unspecified target asks which TV or uses
the recent same-connection selection. No success speech/ack wait is added.
Second node provisioning and real spoken end-to-end trial remain outstanding.

## ESP8266 upstairs submodule — 2026-10-10

`nodes/esp8266` is the separate private GitHub repository
https://github.com/NichlasEk/EutherCommand-ESP8266. ESP8266EX / 4 MB flash /
MAC 80:7d:3a:3e:20:fc / USB CP2102; user confirms D2 and the same transistor/LED
components. TX GPIO4, no receiver yet. Full 4 MB original flash backup and checksum
are ignored under `.local/backups/esp8266-before-tx-20261010.*`. Firmware 0.1.0
built, flashed with hash verification, booted into password-protected phone Wi-Fi
setup at 192.168.4.1. Unique node credentials and firmware binaries remain ignored.
User wants Wi-Fi/software first, soldering afterward. Household association,
TLS/disabled-TX live checks and final upstairs target activation still pending.
See submodule README for build, wiring, protocol and manual USB recovery limitations.

## Upstairs Wi-Fi verified — 2026-10-10

User successfully completed ESP8266 phone provisioning. Node is now at
192.168.32.253, USB log `ready`, free heap 40488. Private client config:
`nodes/esp8266/.local/client.json`. Live disabled-TX test passed pinned HTTPS,
authentication/HMAC, duplicate and deadline guards, invalid epoch/direction/size,
and no UDP acknowledgement. Two accepted test packets, zero transmissions.
TX left disabled; reboot restores readiness but still requires signed commands.
Do not activate the Samsung target in Vox until the user's soldered circuit is
ready. No receiver is fitted; no actual optical output/Logitech response tested.
Phone provisioning failure is resolved in user observation, root cause unknown.
