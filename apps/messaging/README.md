# Messages

Compose public messages from a phone, keep persistent conversations, and
optionally relay retained messages during nearby badge encounters. No cloud or
delivery receipt is provided. Names, badge IDs and phone times are unverified;
nearby relay is unencrypted.

Press **Phone**, scan the Wi-Fi QR, keep the connection without internet, and
open `http://192.168.4.1` if needed. The phone has one message field and **Send**;
replies retain their conversation. **Saved on this badge** confirms local
storage only. An unconfirmed send retains its ID/content/time for retry. Keep
the tab open until confirmed; captive browsers may discard session storage.

The badge's left pair selects **Up/Down**; top-right **Open** opens a card. In a
conversation these become **Older/Latest** and **Reply**. Bottom-right opens or
closes the phone pane, or goes Back. **Close menu** keeps the session running.
Hold both top MENU buttons for Home to end phone mode and restore ranging and
enabled sharing. New arrivals/sends in the inbox and Close menu select newest;
opened older conversations remain anchored.

- **8 messages total across all threads**, including relayed messages. Full
  storage rejects new messages without evicting history.
- Bodies accept **1–240 plain ASCII characters** and line breaks; an automatic
  thread label uses up to 32 characters. Captured Settings names use up to 12.
- Phone **Options** controls relay and local thread deletion. The last **16
  deleted IDs** are blocked locally. Other badges keep their copies; new replies
  can arrive and older suppression IDs eventually age out.
- Relay defaults **ON only for new stores**; saved ON/OFF preferences remain.
  Phone mode pauses relay; Home resumes it only when enabled. Brief encounters
  may miss records; physical two-badge validation remains pending.

Relative ages use valid badge time, otherwise fresh phone time plus monotonic
uptime. That runtime reference resets on reboot and never changes the system
clock or stored send/retry metadata. Phone timestamps accept 2020–2100. Unknown,
unavailable-current-time and future-time states have explicit labels. The
existing app loop redraws only when visible age labels change.

## Storage and updates

The versioned NVS snapshot uses `badge_msg` / `state`, at most **3,041 bytes**,
with Settings/GC headroom reserved. No partition, raw factory-tail allocation,
automatic NVS erase or silent eviction is added. Unavailable/unknown/damaged
stores are preserved and reported; failed writes disable mutations until reboot.

Use firmware-only OTA or wired Update to retain settings/messages. Merged factory
installers span NVS and are **not preserving upgrades**; avoid full-chip erases.
Unmodified older firmware, including v0.8, has its own NVS-error erase recovery,
which this app cannot prevent after downgrade. Physical fault recovery and
OTA/rollback tests remain pending.

## Source and checks

The app/portal/page live here; background service/store/sync/NVS sources are in
`components/os/messages/`, with API `components/os/include/badge/messaging.h`.
The OS retains sole ESP-NOW callback ownership and forwards bounded packets.

Run `sh apps/messaging/tests/run.sh` from the repository root. These are actual
source checks with host doubles and sanitizers, not hardware validation. Set
`MSG_QR_COMPONENT` if the installed QR component is outside its normal managed
location. `BADGE_FACTORY_IMAGE` optionally supplies a public stock reference for
read-only preservation simulations; no binary is vendored.
