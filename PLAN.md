## What the current ESPHome component is missing

Background: I compared `components/neewerlight/neewer_light_output.*` against the open-source references we cloned earlier (Keefo’s macOS app + Taburineagle’s NeewerLite-Python). The source citations below point to the exact code that demonstrates each capability in those projects.

1. **Power command support** – Our driver never emits the on/off packets Neewer lights expect (`0x78 0x81 0x01 0x01/0x02 …`). NeewerLite-Python calls these before and after most operations (`tmp/NeewerLite-Python/NeewerLite-Python.py:3653-3667`, `3762-3895`), which explains why their lights reliably wake from standby and ours do not.
2. **State validation via notify** – The Python tool reads the notify characteristic `69400003…` using `[120,133,0,253]` (power) and `[120,132,0,252]` (channel) to confirm the device accepted a command (`tmp/NeewerLite-Python/NeewerLite-Python.py:3616-3684`). Our component never subscribes or reads, so it blindly assumes success.
3. **Correct CCT/GM mapping for RGB62-class lights** – Keefo’s database (type 40 entry, `tmp/NeewerLite/Database/lights.json:617-665`) shows RGB62 expects CCT bytes 25–85 (~2500–8500 K) plus a GM byte and trailing zeros. ESPHome hardcodes 3200–5600 K and omits GM, so every CT write we send to RGB62 is malformed.
4. **Scene/FX commands** – RGB62 lists 17 effect patterns plus music mode driven by `{cmdtag} {fxsubtag} …` frames. Our ESPHome code defines `effect_prefix_` but never sends scene packets, meaning all animation features are inaccessible even though the reference apps implement them (`tmp/NeewerLite/Database/lights.json:626-665`, `tmp/NeewerLite-Python/NeewerLite-Python.py` FX handling around `setInfinityMode`).
5. **Infinity / MAC-addressed packets** – NeewerLite-Python rewrites outgoing packets for Infinity lights into MAC-prefixed frames (`tmp/NeewerLite-Python/NeewerLite-Python.py:3824-3895`). We cap packets at 10 bytes and only send the old short format, so any Infinity-class device (even hybrids) will ignore us. While we’re focusing on RGB62 first, this gap explains why other models fail completely.

These gaps were identified by diffing the actual write paths and inspecting Keefo’s `commandPatterns`. The ESPHome component effectively only covers the minimal RGB660-Pro commands from the public gist.
## RGB62 (NW-RGB62) specifics

- `commandPatterns` (`tmp/NeewerLite/Database/lights.json:617-665`):
  - Power: standard `0x78 0x81` on/off packets.
  - CCT: `0x78 0x87` with brightness, byte-range 25–85 for CCT (≈2500–8500 K), **plus** a GM byte and two trailing zeros.
  - HSI: same `0x78 0x86 0x04` payload as RGB660.
  - FX: 17 effect definitions plus a music mode, all using `fxsubtag` (`0x8B`).
- NeewerLite maps names containing `RGB62` to light type 40, and even seeds saved configs with `rawname = "NW-RGB62"`.
- MAC prefix example: `F8:0C:31:D6:74:8C` (user’s light).

## Next steps (RGB62-only focus)

1. **Model targeting**: Introduce configuration (even if temporary/hardcoded) that assumes RGB62 semantics: enable GM in CT packets, map Kelvin to byte 25–85, and keep the trailing `0x00 0x00` bytes.
2. **Power control**: Add explicit on/off commands to the ESPHome component so the light can be woken before mode changes and turned off reliably.
3. **Status validation**: Implement basic notify/read handling to confirm power state after writes (full channel parsing optional for now).
4. **Buffer sizing**: Raise `MSG_MAX_SIZE` so future work (FX, longer packets) isn’t blocked.
5. **Deferred**: Infinity protocol, FX scenes, legacy split-writes, and broad model coverage remain deferred until RGB62 is solid.

## Implementation notes

- `components/neewerlight/neewer_light_output.*` now track a `light_on_` flag and send the Neewer on/off packets (`0x78 0x81 0x01 0x01/0x02`) before any color data. Turning the light off immediately sends the power-off frame and skips RGB/CT payloads.
- Helper methods (`prepare_power_msg_`, `send_power_command_`, `set_old_rgbct`) consolidate buffer updates and keep the cached “old” values and output state in sync after each command. This keeps the change-detection logic stable even when the light is toggled rapidly.
- BLE status polling is wired up via the notify characteristic (`6940...03`): we now register for notifications, periodically request the power/channel status frames (`0x78 0x85` / `0x78 0x84`), and update `light_on_` + a cached channel id based on the replies. Timeouts log warnings so we can see when a light stops responding.
- On boot (once notifications are ready) we immediately fire a power+channel refresh so the ESPHome state mirrors whatever the panel was doing before the ESP restarted.
