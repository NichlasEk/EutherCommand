# Current handoff — 2026-10-10


**Correction, 2026-10-10:** the user withdrew the earlier volume-change confirmation.
IR wakes the Logitech volume display but has not been shown to change the volume.
Earlier successful-response notes below are historical and superseded.

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
volume change and perceived latency are pending the user's new trial.
