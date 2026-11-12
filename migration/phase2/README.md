# Phase 2 handoff — no Cloud changes performed

Deploy schema validation/routing **before** changing dashboards/alarm rules.
Firmware current snapshots use scalar current keys. All TRANSITION/history fields
are `event_*`, including boot/sequence/time metadata. This deliberate schema-v1
history namespace prevents stale backlog from overwriting current risk even under
the default ThingsBoard Save Timeseries path. `schema_version`/`record_kind` remain
unprefixed routing keys; they are not a current-risk ordering high-water mark.

`normalize.py` is a fixture-tested dry routing reference, not a production Rule
Chain upload. Integrate authenticated originator/session boundaries, originator-
serialized processing, durable dedup by device+event_boot_id+event_record_seq,
archive event records without same-ms overwrite, then update alarm rules using
current_state only. Heartbeat must not advance current-state sequence. New boot
without verified session boundary is quarantined; do not order random boot IDs.

Current fields: risk, monitoring, detection, physical latch/ACK seq and output mode
are separate. Cloud acknowledge must not call any device API (none subscribed).
EMERGENCY alarm stays active while device latch true. Offline doesn't clear fire.
90s inactivity is a proposed server attribute; firmware attributes only advertise
recommended_inactivity_ms=90000, never claim to set server-side policy.

Inspect existing tenant normalization: legacy payloads may be nested JSON objects.
Do not map legacy MQ2_raw/flame_raw to ADC. Preserve old history as legacy series;
route unsupported/legacy messages explicitly, do not delete data. Promotion needs
tenant export/backup, quota measurement, alarm fixtures, RBAC and rollback tests.
No production blackhole or fake FIRE injection is required/performed in Phase 1.
