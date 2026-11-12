# Local private build provisioning

No production credentials belong in public headers, review documents or fixtures.
Existing credentials were migrated unchanged to `private/provisioning.json` (directory 0700, file 0600), excluded by `.gitignore`. This is a local build input, not a new Cloud provisioning service. No credential rotation, NVS erase, eFuse, secure boot or encryption changes occurred.

For a new development checkout, copy `tools/provisioning.example.json` to that private location, fill it locally, then restrict its permissions. Do not paste values into shell command arguments, source control, CI logs or reports. A missing/empty private configuration still builds, with network disabled and local monitoring/latch enabled. Invalid types/lengths fail configuration without printing secrets.

```sh
source /home/cuongdso/esp/esp-idf/export.sh
idf.py -DPCCC_PRIVATE_CONFIG=/home/cuongdso/iot/khu_dun_nau/private/provisioning.json build
python3 tools/release_check.py
```

CMake generates a mode-0600 header under `build/esp-idf/main/private/`. Firmware uses Wi-Fi RAM storage; the previous NVS Wi-Fi values are deliberately not erased. The firmware binary/ELF and original baseline backup also contain device credentials and must remain private. This protects the public source/release documentation, not physical extraction from unencrypted firmware. Do not publish device-specific build artifacts. The implementation set the local build directory to 0700 and app binary/ELF to 0600; preserve private permissions in CI/artifact storage. Rotation/revocation can be scheduled after a separately validated migration.

TLS uses the bundled root CA, default hostname checks, and enabled certificate date checks. A sane actual SNTP synchronization must occur before MQTT connects; no bypass exists for unsynchronized time. Standard unauthenticated SNTP is still a bootstrap trust limitation: build-time bounds, sync-age and jump checks do not turn it into authenticated time. A failed network/clock/bootstrap leaves local monitoring running. No Cloud command subscriptions exist.
