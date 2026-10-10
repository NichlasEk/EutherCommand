# Wi-Fi setup from a phone

The node provides a temporary WPA2-protected setup network, with a fresh random
24-character password on each setup boot. Its SSID, password and local URL are
printed once over physical USB. Keep serial setup logs private: they contain
this temporary password. Home-network credentials are never logged.

1. Join `EutherCommand-xxxxxx` on the phone using the USB-provided password.
2. Stay connected even if the phone reports no internet. Open
   `http://192.168.4.1` explicitly (HTTP, not HTTPS).
3. Enter the exact 2.4 GHz SSID and WPA2/WPA3 personal passphrase (8–63 bytes).
   Open networks, enterprise authentication and 64-digit raw PSKs are not supported.
4. Wait for “Ansluten och sparad” and note the assigned address. Credentials are
   saved only after DHCP succeeds. The setup network closes 15 seconds later.
5. Rejoin the home network. The node reconnects automatically on future boots.

Setup expires after ten minutes (an in-flight connection attempt may add up to
30 seconds). A wrong password leaves the form available for correction. If a
previously configured home network is unavailable, the node retries without
opening a setup hotspot automatically. Reconfiguration is explicitly requested
over USB with `{"op":"wifi_setup"}`; this disables IR and reboots into setup.
An unconfigured node opens setup again after a power cycle.

The setup HTTP server rejects requests addressed to the station interface and
stops after setup. A random per-boot form token prevents cross-origin credential
changes; no CORS access is enabled. WPA2 protects the radio link; HTTP setup is
not end-to-end TLS. Anyone given the setup Wi-Fi password can provision the node.
Credentials reside in ordinary NVS in this development build; flash encryption
and secure boot are not enabled. Physical flash access can reveal them.

The provisioned node now offers authenticated HTTPS control; see
[LAN control](LAN_CONTROL.md). Provisioning itself never transmits IR. USB bench
commands retain their three-transmissions-per-boot limit. Secure updates remain
separate work.

Implementation uses ESP-IDF 5.4.2 Wi-Fi and HTTP server APIs:
https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32/api-guides/wifi.html

Checks: `make test`, `node tests/web/setup_test.cjs`, ESP-IDF firmware build.
Phone association, form submission and reboot persistence require a physical
phone/network trial; do not infer them from compilation or JavaScript tests.

Local bench override: an ignored `firmware/main/wifi_setup_local.h` can define
`EC_LOCAL_SETUP_PASSWORD` (8–63 bytes). This device uses a user-requested fixed
setup password. Builds without the local header retain random passwords. The
override is embedded in the firmware binary; do not publish that local binary.
