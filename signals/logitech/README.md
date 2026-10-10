# Logitech audio: learned volume commands

Captured on 2026-10-10 from the user's multi-device remote in its Logitech audio
mode, using the 1838-marked receiver on GPIO27. Exact Logitech model is unknown.
Three complete identical decoded frames per button; preserved JSON is the first
physical frame, not a regenerated waveform. Carrier 38000 Hz is assumed from the
receiver family, not measured. No replay or appliance response verified yet.

| Operation | Learned ID | NEC address | NEC command | File |
|---|---|---|---|---|
| Volume up | logitech_volume_up | 0x08 | 0x1A | volume_up.json |
| Volume down | logitech_volume_down | 0x08 | 0x0E | volume_down.json |

Both are registered under `device.logitech_audio` in root euthercommand.toml and
saved via Core's learning workflow to ignored `.local/state.sqlite`. Operator
access includes the device; the Vox principal has not been granted access yet.
No transmission was executed while importing. To recreate the local database
with the authenticated development API running:

```sh
python -m tools.client learn signals/logitech/volume_up.json logitech_volume_up
python -m tools.client learn signals/logitech/volume_down.json logitech_volume_down
```

Do not repeat import into a database already containing these names. Raw logs
and all three captures per operation are kept under `.local/validation/`.
