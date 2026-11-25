# App Manager

Install, update, open and uninstall signed native apps without rebuilding the OS.
The manager uses Badge OS fonts and controls, verified download/install status,
compact package sizes, app previews and a used/reserved/available storage bar.

| App | Package ID | Standalone source |
| --- | --- | --- |
| SpaceInv | `aliens` | `modules/aliens` |
| Asteroids | `rocks` | `modules/rocks` |
| Video | `video` | `modules/video` |
| Codex Board | `codex_board` | `modules/codex_board` |
| Hackaday | `hackaday` | `modules/hackaday` |
| Messages | `messaging` | `modules/messaging` |
| Doom | `doom` | `modules/doom` |

These sources build separate ELFs; `CMakeLists.txt` does not embed them or install
them at boot. The firmware's Home registry and component selection decide which
apps are built in. A fresh core-only profile must omit all seven built-in entries.
An upgrade must retain the existing app store and app data.

## Firmware integration

ESP-IDF 6.0.2, elf_loader 2.1.0 and LittleFS 1.23.1 are pinned. Append
`sdkconfig.defaults` and `sdkconfig.store.defaults` when enabling this component.
Extended launcher help is optional and defaults off. Messages requires the OS
messaging services from PR #6; this component does not replace those services.

The tracked package sources and storage policy default to zero. To enable
downloads, the integrator must supply immutable HTTPS URLs and a public P-256 APP
key for the seven fixed IDs, plus a qualified partition layout and exact physical
partition-table sector digest. All sources use the same APP trust key. Public
package hosting and its signing identity are not provisioned by this tree.
Unconfigured apps remain visible with Install disabled; no development endpoint
or key is included.

`CONFIG_BADGE_APP_MANAGER_FRESH_LAYOUT` qualifies the fresh table and the exact
previously tested managed table, both with appstore at `0x3c0000`, size `0x260000`
and a 20480-byte reserve. It defaults off. Other table fingerprints are rejected.
The integrator supplies an explicitly created empty filesystem for fresh devices;
ordinary firmware updates must never flash that image. Existing partition tables
are retained by app-only OTA. Unknown layouts disable store access. Existing
receipts also need their original APP public key; zero trust preserves their
bytes but cannot verify or open them.

Mounts never format or grow the filesystem. Install/update verifies the signed
manifest, payload hash, identity, version, runtime and ELF before activation.
Current and previous versions remain recoverable; uninstall preserves `data/`.
Native packages run with firmware privileges. The restricted import table and
admission checks are not a sandbox.

Home calls `app_manager_home_apps()` after its first visible core frame and OTA
validation, then on Home re-entry. Discovery uses a read-only mount and verified
installed receipts; missing, incompatible or corrupt packages add no shortcuts.
Core duplicates are omitted. A shortcut uses the manager's normal offline Open.
The drawer and ELF must quiesce before releasing the execution lease; uncertain
cleanup keeps the package pinned and blocks further mutations.

## Package builds

Run from the repository root with the ESP-IDF 6.0.2 ESP32-S3 toolchain. `API_DIR`
is the absolute path to this directory; `OUT` is an output directory outside Git.

```sh
for app in aliens rocks video; do
  python3 "apps/app_manager/modules/$app/build.py" --cc "$XTENSA_CC" --api-dir "$API_DIR" --output "$OUT/$app"
done
for app in codex_board hackaday messaging; do
  python3 "apps/app_manager/modules/$app/build.py" --cc "$XTENSA_CC" --output "$OUT/$app"
done
python3 apps/app_manager/modules/doom/build_module.py --pilot "$DOOM_BUILD" --toolroot "$XTENSA_ROOT" --output "$OUT/doom"
```

Doom also needs an existing matching SDK build's `compile_commands.json` for the
79 tracked `components/doomgeneric` inputs and the original `apps/doom/doom.c`
controls. Its builder checks those source bytes before reusing compile settings.
The other six builders use the sources and frozen API headers in this tree.
`modules/doom/check_module.py MODULE.elf` audits a produced Doom ELF without a
prior build report or runtime artifact.

Builders produce unsigned ELFs/manifests. A package publisher must sign the exact
manifest bytes and serve `manifest.json`, `manifest.sig` and `module.elf` over
HTTPS. Private signing keys stay outside firmware and Git. Certificate chain,
hostname and date checks are required. Offline Open needs no network or clock sync.

Video additionally needs its separately signed stock clip and BVA1 receipt.
Doom needs the existing matching compressed IWAD at `0x650000`; downloading its
ELF does not provision that data. This component embeds no clip, WAD or package
binaries. Repository GPL-2.0 applies; retain the Doom,
Messages and QR library license files and original source attribution.

## Supply a package channel

Set `BASE_URL` to your HTTPS channel, `PUBLIC_PEM` to its P-256 public key and
`CHANNEL_HEADER` to an output path outside Git. No C editing or private overlay is
needed. The generated header contains only public configuration.

```sh
python3 apps/app_manager/tools/configure_channel.py --base-url "$BASE_URL" --public-key "$PUBLIC_PEM" --output "$CHANNEL_HEADER"
idf.py -DBADGE_APP_MANAGER=ON -DBADGE_APP_MANAGER_CHANNEL="$CHANNEL_HEADER" build
```

Public sources use the OS CA bundle. `--redirect-host HOST` permits the existing
bounded redirect policy. For the existing local verifier, supply `--ca CA.pem`
with an exact `https://IPv4:8443` base; local redirects are disabled. Omitting the
CMake channel input keeps every source unconfigured and makes no network attempt.

For each builder output, sign the exact manifest using an external private key.
`CHANNEL` is a staging directory outside Git, served at `BASE_URL`:

```sh
for id in aliens rocks video codex_board hackaday messaging doom; do
  mkdir -p "$CHANNEL/$id"
  cp "$OUT/$id/unsigned-manifest.json" "$CHANNEL/$id/manifest.json"
  cp "$OUT/$id/"*.app.elf "$CHANNEL/$id/module.elf"
  openssl dgst -sha256 -sign "$APP_SIGNING_KEY" -out "$CHANNEL/$id/manifest.sig" "$CHANNEL/$id/manifest.json"
done
python3 apps/app_manager/tools/prepare_video.py --output "$CHANNEL/video/assets"
openssl dgst -sha256 -sign "$APP_SIGNING_KEY" -out "$CHANNEL/video/assets/receipt.sig" "$CHANNEL/video/assets/receipt.bin"
```

`prepare_video.py` copies the tracked `apps/video/rickroll.vid` unchanged and
checks its exact 819646-byte length and SHA256 before emitting the 56-byte receipt.
No original movie, ffmpeg or re-encoding is needed. For Doom, obtain the matching
4196020-byte shareware IWAD separately; `apps/doom/tools/make_wad.py` packs it as
DWZ1. The compressed file must be 1749852 bytes with SHA256
`d37d78d341bad67886d26aa871cbd18a9cffc20d59e6e0bf034a5d0e8486ecab`.
Provisioning that flash data is separate from this package build and firmware OTA.

## Focused host checks

```sh
python3 apps/app_manager/tests/test_native_target.py
python3 apps/app_manager/tests/test_native_ui.py --compact-core14 /tmp/app-manager.pgm
python3 apps/app_manager/tests/run_native_transactions.py "$SIGNED_SPACEINV" "$PUBLIC_PEM"
python3 apps/app_manager/tests/test_home_apps.py "$SIGNED_SPACEINV" "$PUBLIC_PEM"
```

The tests use actual C store/controller/rendering code with SDK and ELF execution
doubles. Signed package and per-app runtime checks are in `tests/`. Host checks
do not establish target timing, flash power-loss behavior or physical panel output.
