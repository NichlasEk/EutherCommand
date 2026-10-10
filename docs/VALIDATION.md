# Validation — 2026-10-10


**Physical confirmation, 2026-10-10:** after the NEC repeat-frame correction and
fast Wi-Fi/app update, the user reports “Det funkar kanon!”. The at-TV volume
trial is now user-confirmed. Earlier single-frame results remain superseded;
this does not verify a second installation or spoken multi-TV routing.

## Verified on this host

- 15 automated Python tests passed: authenticated HTTP learning/execution, scope
  denials, oversized input, waveform limits, NEC/SIRC decoding, configuration
  rejection, durable disable, restart-safe duplicate handling, conflicting IDs,
  expired learning, per-node rate limit, uncertain transmission, Swedish exact
  alias policy, and serial transport through a pseudo-terminal.
- Simulated end-to-end demo: NEC address 16/command 12 → capture → named
  `amp_power` → registered execution → duplicate acknowledgement with one send.
- ESP-IDF **v5.4.2**, source commit
  `f5c3654a1c2d2a01f7f67def7a0dc48e691f63c0`: actual Xtensa cross-build passed
  for the representative **esp32** target. Final project compile had no warning.
- Firmware binary: `firmware/build/euthercommand_node.bin`, 247152 bytes,
  SHA-256 `7d6848a330c52c1abe3ef556e2683ece3694c1a2a39504b6a6a42329b98950d8`.
- RX/TX/enable GPIO settings all -1. This is a disabled verification image,
  **not a finalized image for the user's unidentified board**.
- Build artifacts, SDK configuration, Python caches and local logs are ignored.
  Logs: `.local/validation/{tests.log,demo.jsonl,firmware-build.log}`.

## Physical identification — later in the same session

- CP2102 appeared as `/dev/ttyUSB0`. After the user granted serial access,
  esptool `--no-stub chip_id` identified **ESP32-D0WD-V3, revision v3.1**, with a
  40 MHz crystal. The operation reset the board; it did not write firmware.
- User reports IR204C-A-L LEDs and a 2N2222A transistor. The component-tester
  photo shows NPN and tester contacts 1=E, 2=B, 3=C. These are tester contact
  numbers, not an inferred package orientation.
- Receiver marking is A over 6, from an unidentified component bag. Its supply,
  pinout, frequency and operation remain unknown. No receiver measurements have
  been provided yet. See HARDWARE.md for the current parts list and open checks.

## Reproduce

```sh
make test
make demo
# During this session only; /tmp toolchain is disposable:
export IDF_TOOLS_PATH=/tmp/euthercommand-idf-tools
. /tmp/euthercommand-esp-idf/export.sh
idf.py -C firmware build
```

For a persistent setup install ESP-IDF v5.4.2 using Espressif's install.sh and
export.sh, then select the actual target as described in README.md. Host runtime
uses only Python standard library; firmware build dependencies live separately.
Docker was unavailable to the current account, so the build used the native
ESP-IDF toolchain. No permissions or Docker service configuration were changed.

## Not verified

No board flashed, no physical signal received/transmitted, no target response,
no logic-analyzer/current measurement, no Wi-Fi node transport, no production
TLS deployment, no live EutherID authorization, no live EutherVox Swedish voice
round-trip, no EutherNet dynamic registration. Raw RC5/RC6 replay does not yet
handle protocol toggle semantics. OTA signing/rollback remains a documented
strategy; no updater is exposed.

Next gate: identify the development-board pinout, receiver, supply and harmless target, finalize
HARDWARE.md values, assemble receiver-first, then perform the explicit bench
procedure and append real observations without replacing the simulation evidence.

## First physical RX trial — 2026-10-10

User confirmed receiver wiring: 3V3 through 1 kΩ to receiver pin 3, direct GND
to pin 2, pin 1 through 1.5 kΩ to D27. Pin numbering is lens facing user,
leads down, left-to-right. Identity/pin functions remain provisional.

- Original 4 MiB flash backed up locally before any writes. Full reads suffered
  transfer errors; resumable 32 KiB blocks were saved only after esptool's MD5
  verification. Backup: `.local/backups/esp32-before-rx27-20261010.bin`.
  SHA-256: `97a330dd07b48483f4ca2b3330670fd5710756351412f4bf5625d6a33c838191`.
  The backup can contain prior secrets and remains ignored/private.
- RX27 firmware flashed at 115200 baud with write hash verification. First boot
  exposed an invalid RMT filter setting: 100000 ns exceeded the classic ESP32
  limit of 3187 ns. Changed filter to 1250 ns. Also changed the loop delay to
  one FreeRTOS tick to avoid a zero-tick idle-watchdog starvation path.
- Successfully booted filter-fix image SHA-256:
  `0d80fd17a5f9703d2562353d6279f37890ecc4b02c63ac4d316b5d9724d1338a`.
  Node reported `ready`, RX=27, TX=-1, input idle level=1. No transmission.
- User pressed a remote button three times during the observation interval.
  Eight captures appeared: six had 127 durations and driver truncation errors;
  two had 29 and 99 durations. Many pulse pairs were approximately 26–27 us,
  with individual durations as short as 1–3 us. These are electrical captures,
  **not successfully decoded or replayable commands**. Carrier-like timing is
  an observation, not proof of receiver model/carrier/demodulation behavior.
- Firmware's original `possibly_truncated` flag checked the software buffer
  (256 symbols), missing the classic ESP32 hardware limit (64 symbols). Source
  corrected to flag 64-symbol captures; host archive separately marks affected
  first captures. Original logs retained unchanged.
- Logs: `.local/validation/flash-rx27-filterfix.log`,
  `rx27-filterfix-serial.log`, `rx27-first-captures.json`.

Pending: measure receiver supply and output relative to its pin 2 in the actual
ESP32 wiring; resolve short-pulse/truncation behavior before declaring decoded
IR learning successful. No physical IR transmission or appliance control tested.

Truncation-flag fix subsequently built and flashed with hash verification.
Current flashed app SHA-256:
`95d78019db5905e6bcaa6d51692f51bf38fbcb73acc339520a2bc71bb75efe27`.
A 12-second receive-only boot observation reported RX27 ready, TX=-1, idle high,
with no abort/watchdog message. Evidence: `rx27-truncation-boot.log` and
`flash-rx27-truncation.log`. Serial monitor closed normally after that interval.

## RX pull-up removal — physically verified configuration

ESP-IDF v5.4.2 RMT RX initialization enables a pull-up internally. Added explicit
input-only mode and GPIO_FLOATING after rmt_new_rx_channel(), followed by a GPIO
configuration dump. Built and flashed at 115200 baud; esptool verified the write
hash. Current app SHA-256:
`628d2d91df2d514a808fc3761c01aed004820d82d230e5a3efef339f15dbe623`.

On-device boot dump for GPIO27 reports Pullup=0, Pulldown=0, InputEn=1,
OutputEn=0 and GPIO Matrix SigIn ID=83. Ready reports RX27, TX=-1. The signal
lead was requested to remain disconnected during this check; input level and
empty captures in this condition are not evidence of receiver behavior. The
12-second observation completed without an abort/watchdog message.

Evidence: `.local/validation/flash-rx27-no-pull.log` and
`.local/validation/rx27-no-pull-boot.log`. Pending: reconnect the receiver signal
through 1.5 kΩ, repeat supply/output voltage measurements and controlled IR input.

## Remote test after pull-up removal

User confirmed connected voltages: supply pin 3 relative to 2 = 2.547 V;
output pin 1 relative to 2 = 2.485 V. First listening attempt produced unreadable
serial bytes, so no IR conclusion was drawn from it. Reset the board and verified
readable boot, RX27, pull-up/down off and input-only, then requested another
three spaced presses of the same remote button.

Second trial produced 13 captures, including three truncated 127-duration
trains. Pulse pairs are about 26–27 us; shorter extra captures are also present.
No capture passes the current learned-waveform validator and no remote protocol
was decoded. This is consistent with carrier-like electrical activity, but does
not establish a receiver model, correct demodulation or a known-good supply
range. Removing the pull-up corrected the observed DC-level shift, not the
short-pulse behavior. No transmission/replay was attempted. Monitor stopped
normally after collection.

Evidence: `.local/validation/rx27-no-pull-remote-test.log` (unreadable attempt),
`rx27-no-pull-remote-test2.log` (readable trial), `rx27-no-pull-captures.json`.
Next discriminating check: observe receiver output with an oscilloscope/logic
analyzer and assess supply requirements before choosing envelope extraction or
another receiver. Preserve the raw signal rather than relabeling it as a decoded
command.

## Milestone A: physical receive and decode PASS — 1838 receiver

After replacing A6 with the user's metal-shielded, 1838-marked receiver and using
direct 3V3 supply, the same no-pull RX27 firmware reported ready. The user then
pressed one Strong Android-TV remote button three times, about one second apart.
Three complete frames decoded independently as **NEC address 136 (0x88), command
1 (0x01)**, each with 67 durations and no truncation flag. Header examples:
9035/4490 us, 9010/4515 us, 9008/4488 us. Three additional 3-duration captures
were repeat-like (~9010/2226/563 us), retained as raw by the current decoder;
short isolated captures were rejected. No claim that every captured edge is valid.

First complete raw signal saved as `.local/strong-nec-88-01.json` and copied to
`tests/fixtures/strong-nec-88-01.json` as an independently acquired regression
fixture. The button function is not yet known, so it is NOT named power and is
not mapped to an executable appliance command. Carrier_hz=38000 remains assumed
metadata; the demodulated signal does not measure optical carrier frequency.

Evidence: `.local/validation/rx27-1838-test.log`, `rx27-1838-summary.log`,
`rx27-1838-captures.json`. **16 host tests passed**, including the new physical
capture fixture (`tests-first-physical-rx.log`). Monitor stopped after capture.

Milestone A now has physical receive/decode evidence. Milestone B remains open:
no physical IR transmission, replay or target-appliance response was tested.
Receiver replacement and supply change occurred together; this does not prove
which factor caused the earlier A6 behavior or establish that A6 is a B&O receiver.

## Logitech volume learning PASS

User identified the previous Strong NEC 0x88/0x01 capture as numeric button 1.
The multi-device remote was then used in Logitech audio mode. Separate prompted
sessions each produced three complete matching NEC frames, without truncation:
volume up address 0x08 command 0x1A, volume down address 0x08 command 0x0E.
Representative original waveforms are in `signals/logitech/`. Full original
logs: `.local/validation/logitech-volume-up.log` and `logitech-volume-down.log`.

Registered device `logitech_audio` and learned IDs `logitech_volume_up` /
`logitech_volume_down`; both saved through Core learning into `.local/state.sqlite`.
No execute call or transmission occurred. Seventeen tests pass including the
new independently captured Logitech fixtures. Button names are user-provided;
actual replay effectiveness remains unverified. Next physical step is the IR LED
driver circuit; current flashed firmware still has TX=-1.

## TX26 USB camera trial — 2026-10-10

Firmware now uses TX26 and the explicit USB bench arm profile. Disabled replay
and expired two-second arm were rejected before the trial. Serial replies carry
request correlation IDs to reject stale acknowledgements.

The first trial stopped on its initial disable handshake timeout, before any
execute call; its final disable succeeded. Added a two-second boot settling
delay after opening the USB UART. The next trial completed exactly three
registered Logitech volume-down requests, two seconds apart, with all three
reported `transmitted` and final disable acknowledged. Evidence:
`.local/validation/logitech-camera-trial-02.log`; request prefix
`camera-c4b1d4a20fb84fba`. These are firmware transmission acknowledgements;
optical output and target-device response await human verification.

Camera follow-up: user saw no flashes in the first completed trial, but confirmed
the same camera sees the ordinary remote. At user request, reset the node and
ran another bounded three-frame volume-down trial. All three returned transmitted
and final disable succeeded. Log: `.local/validation/logitech-camera-trial-03.log`,
request prefix `camera-01cffd498a8a4a56`. Optical result still awaits user report;
firmware acknowledgements do not establish LED output or appliance response.

User reported the previous IR LED defective and replaced it. At user request,
ran three further volume-down frames, two seconds apart. All three firmware
acknowledgements were transmitted and final disable succeeded. Log:
`.local/validation/logitech-replaced-led-trial.log`, request prefix
`camera-43d0d1369758430d`. Optical output and appliance response remain
unverified pending user observation.

## Optical output confirmed and Wi-Fi setup build

User confirmed camera-visible IR output after replacing the defective LED.
This verifies optical emission, not Logitech appliance response. Added bounded
WPA2 SoftAP provisioning with a Swedish phone form, AP-only HTTP access, form
token, successful-DHCP-before-save and automatic shutdown. ESP-IDF 5.4.2 build
passed (application 0xd3240 bytes); 17 Python tests and five JavaScript UI
scenarios passed. Physical phone provisioning remains pending.

Wi-Fi firmware flashed via USB with esptool hash verification. Application
SHA-256: `67e70b77ea7dfe514675f75f0aa7ffdebf2fb830f57c055e459efdcd9fbaba50`.
Physical reboot reported disabled IR, RX27/TX26 ready and setup AP started.
Temporary setup credentials are confined to ignored private serial logs.
Host has no Wi-Fi adapter, so radio association and the phone flow await the
user. No IR commands were sent during this provisioning work.

## Setup page access correction

User could join the setup AP but could not load the page. Inspection of the
IDF HTTP server confirmed its IPv6 listener with IPv4-mapped connections.
Our AP-interface guard incorrectly supplied an IPv4-only socket address.
Changed it to sockaddr_storage and explicit IPv4 / IPv4-mapped IPv6 handling;
LAN addresses and native IPv6 remain denied. Host C regression tests cover
accepted AP addresses, rejected LAN addresses, short buffers and native IPv6.
Raised the bounded HTTP request-header limit from 512 to 2048 bytes for mobile
browsers. Firmware build passed. Phone verification of the fix remains pending.

Phone still reported page timeout after the address-guard correction. Its DHCP
settings were confirmed by the user: 192.168.4.2/24, gateway and DNS 192.168.4.1.
Added USB-only client-association, TCP-open and interface-guard diagnostics,
plus an on-device HTTP GET self-test. No request bodies, tokens or home Wi-Fi
credentials are logged. Enabled HTTP idle-session eviction. This is diagnostic
work; the earlier address correction alone did not resolve the reported issue.

On-device TCP self-test returned HTTP 200 and AP address guard allowed the
IPv4-mapped IPv6 connection. This verifies local HTTP handling, not the phone
radio path. First diagnostic self-test closed early and caused expected send
errors; source now drains the response (pending subsequent firmware build).

User confirmed the phone access worked after the diagnostic retry. The precise
cause of the previous phone timeout is not established. This confirms page
access; successful home-network provisioning still requires a saved/connected
result.

## Physical Wi-Fi provisioning PASS

User reported successful save in the phone UI. A USB-controlled reboot then
confirmed automatic station reconnection to 192.168.32.248, with IR disabled
and RX27/TX26 ready. Two host ICMP probes succeeded; ARP matched node MAC
94:e6:86:b5:01:fc. Thus phone provisioning, credential persistence and LAN
reachability are physically verified. LAN IR execution remains unimplemented.
Evidence: `.local/validation/wifi-saved-reboot-summary-private.log`.

## Native HTTPS control verified, physical target trial pending

Flashed HTTPS control firmware with hash verification. SHA-256: `f12c5ea5674d328dc09c31e251395d9a2df6b984bb4648039cf943acf0c46398`.
The physical node at 192.168.32.248 returned authenticated disabled status.
`tools.check_lan` passed wrong-token, wrong-certificate-pin, duplicate ID,
conflicting ID, stale boot, disabled execute, unregistered operation and body
size tests. No IR was transmitted. Evidence: `.local/validation/lan-physical-checks.log`.
22 Python tests, five setup UI scenarios and the C interface-address regression
test passed. User is moving the node for the first Wi-Fi-to-appliance trial.
User response and wireless optical transmission remain unverified. TLS keys
and node credentials are ignored local files, never printed or committed.

## First relocated Wi-Fi target trial

After the user moved the node and confirmed ready, pinned HTTPS reached the
node in its new boot session, initially disabled. Explicitly enabled, sent
exactly one registered volume_down command, received transmitted, and received
final disabled acknowledgement. No retry. Request ID:
`physical-ae8c685df13f4df888d023da1187e704`. Evidence:
`.local/validation/lan-first-target-trial.log`. Target response awaits user
confirmation; firmware acknowledgement alone does not verify appliance state.

User missed observing the first target trial and requested another. Sent exactly
one further volume_down frame over HTTPS; transmitted and final disabled were
acknowledged. Request `physical-44771139af4a4da886883fd92fb273d2`; log
`.local/validation/lan-second-target-trial.log`. Appliance response still awaits
user observation.

## Physical Logitech control over Wi-Fi PASS

User confirmed replacement LED is IR204C-A-L, measured 4.9 V supply, and
completed the requested two 270-ohm parallel resistor modification (135 ohms).
Sent exactly one registered volume_down over pinned HTTPS; firmware returned
transmitted and final disable succeeded. User then confirmed the target worked.
Request: `physical-797725131685440f8eef618efde16210`. Log:
`.local/validation/lan-135ohm-target-trial.log`. This is user-observed physical
response, not independent electronic state feedback. Earlier 270-ohm attempts
had no observed target response; increased current is a plausible contributor,
not an isolated causal measurement. No additional commands were sent.

## EutherVox integration, 2026-10-10

- Firmware learning/registration and epoch rotation compiled with ESP-IDF 5.4.2;
  app-only flash verified by esptool. TLS node status confirmed disabled afterward.
- `make test`: 22 Python tests, five provisioning JavaScript cases, C address guard pass.
- `tools.check_lan`: all nine disabled-node checks pass on final firmware.
- Authenticated session rotation changes the epoch and rejects the previous ID.
- EutherVox gateway: 288 tests pass; live trusted-proxy list gives two seeded
  Logitech commands, unauthorized user/direct forged identity rejected.
- Android app and Beam JVM tests pass; app lint and debug build pass.
- Emulator mock workflow records, inspects, saves, executes and confirms a
  command; Samsung/NEC/Android TV setup panels open without crash.
- New network capture was armed but expired without a user signal. This is not
  a failed receiver test and is not physical learning validation.
- APK publication remains behind existing website authentication (anonymous 401),
  as does the public voice WebSocket. Server artifact hash is verified locally.
- Phone end-to-end voice/learning and network-TV pairing remain user trials.
