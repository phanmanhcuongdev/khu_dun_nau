#!/usr/bin/env python3
"""Generate published schema/dictionary from reviewed V1 types, not device secrets."""
import json,re,pathlib
root=pathlib.Path(__file__).resolve().parents[1]
text=(root/'docs/reviews/thingsboard-data-model-proposal.md').read_text()
old=next(json.loads(b) for b in re.findall(r'```json\n(.*?)\n```',text,re.S) if '"$schema"' in b)
props=old['$defs']['values']['properties'];props=json.loads(json.dumps(props))
for p in props.values():
 if '$ref' in p:p.clear();p['enum']=['INITIALIZING','VALID','INVALID','STALE']
props.update(persistence_health={'type':'integer','enum':[0,1,2]},mq2_ao_valid={'type':'boolean'},mq2_ao_mv_valid={'type':'boolean'},largest_free_block_bytes={'type':'integer','minimum':0},network_stack_free_bytes={'type':'integer','minimum':0},safety_stack_free_bytes={'type':'integer','minimum':0},safety_step_max_us={'type':'integer','minimum':0},safety_interval_max_us={'type':'integer','minimum':0},event_expiry_count={'type':'integer','minimum':0},event_reject_count={'type':'integer','minimum':0},event_eviction_count={'type':'integer','minimum':0},event_completed_count={'type':'integer','minimum':0},event_retry_count={'type':'integer','minimum':0},journal_loss_count={'type':'integer','minimum':0})
props.pop('reset_reason',None) # Session metadata belongs in client attributes.
meta=['boot_id','record_seq','replayed','time_synced','captured_uptime_ms']
state=['risk_level','risk_reason','monitoring_status','emergency_latched','physical_ack_seq','ack_eligible','detection_state','persistence_health','buzzer_mode','muted','gy906_quality','mq2_quality','flame_quality','radar_quality']
common={'schema_version':{'const':1},'record_kind':{'enum':['SNAPSHOT','BOOT','RECONNECT','HEARTBEAT']}}
current={'type':'object','properties':dict(props,**common),'required':['schema_version','record_kind']+meta,'additionalProperties':False,'allOf':[{'if':{'properties':{'record_kind':{'enum':['SNAPSHOT','BOOT','RECONNECT']}}},'then':{'required':state}}]}
history={'type':'object','properties':{'schema_version':{'const':1},'record_kind':{'const':'TRANSITION'},**{'event_'+k:v for k,v in props.items() if k not in ['schema_version','record_kind']}},'required':['schema_version','record_kind']+['event_'+k for k in meta+state],'additionalProperties':False}
schema={'$schema':'https://json-schema.org/draft/2020-12/schema','$id':'urn:pccc:v1:telemetry','$defs':{'record':{'oneOf':[current,history]}},'oneOf':[
 {'type':'object','required':['ts','values'],'properties':{'ts':{'type':'integer','minimum':1600000000000},'values':{'allOf':[{'$ref':'#/$defs/record'},{'anyOf':[{'required':['time_synced'],'properties':{'time_synced':{'const':True}}},{'required':['event_time_synced'],'properties':{'event_time_synced':{'const':True}}}]}]}},'additionalProperties':False},
 {'allOf':[{'$ref':'#/$defs/record'},{'anyOf':[{'required':['time_synced'],'properties':{'time_synced':{'const':False}}},{'required':['event_time_synced'],'properties':{'event_time_synced':{'const':False}}}]}]}]}
folder=root/'schemas/pccc-v1';folder.mkdir(parents=True,exist_ok=True)
(folder/'telemetry.schema.json').write_text(json.dumps(schema,indent=2)+'\n')
a={'$schema':'https://json-schema.org/draft/2020-12/schema','type':'object','required':['schema_version','firmware_version','build_id','config_id','hardware_model','zone_id','capabilities','reset_reason'],'properties':{k:{'type':'string'} for k in ['firmware_version','build_id','config_id','hardware_model','zone_id','capabilities','reset_reason']},'additionalProperties':False}
a['properties'].update(schema_version={'const':1},recommended_inactivity_ms={'const':90000})
(folder/'attributes.schema.json').write_text(json.dumps(a,indent=2)+'\n')
dictionary={}
units={'temp_object_c':'degC','temp_ambient_c':'degC','temp_rate_cps':'degC/s','captured_uptime_ms':'ms','mq2_adc_counts':'ADC counts','mq2_adc_mv':'mV at ADC pin','mute_remaining_s':'s','recovery_remaining_s':'s'}
for k,v in props.items():
 if k=='record_kind':continue
 source=('risk_state.'+k if k in ['risk_level','monitoring_status','emergency_latched','physical_ack_seq','ack_eligible','detection_state','persistence_health'] else 'sensor snapshot / publisher context')
 dictionary[k]={'type':v.get('type','enum'),'enum':v.get('enum'),'unit':units.get(k,'none'),'source':source,'category':'telemetry','history_key':'event_'+k,'validity':'Only finite, fresh measurements; quality flags override retained latest values','trigger':'state change / 10s normal / 2s abnormal; health 60s; idle heartbeat 30s'}
(folder/'data_dictionary.json').write_text(json.dumps(dictionary,indent=2)+'\n')
print('V1 schema and dictionary generated')
# Explicit operational meaning and ownership; diagnostic-only fields never pretend to be measurements.
sources={
'schema_version':('constant 1','Contract parser compatibility'),
'boot_id':('esp_fill_random(16 bytes) once at boot','Boot/session identity'),
'record_seq':('publisher monotonic sequence allocated on capture','Ordering within boot; gaps are permitted'),
'replayed':('event retry/journal transport metadata','Historical/retry provenance; not current state'),
'time_synced':('SNTP callback + clock_policy trusted anchor','Whether native Unix timestamp is trustworthy'),
'captured_uptime_ms':('risk_state.last_eval_time_us or health capture time','Monotonic capture age; never Unix time'),
'risk_level':('risk_state.risk_level','Authoritative local alarm classification'),
'risk_reason':('risk_state.risk_reason','Engine-selected primary reason; no second classifier'),
'monitoring_status':('risk_state.monitoring_status','Startup/warmup/monitoring fault independent of risk'),
'detection_state':('risk_state.detection_state (WP01)','Present emergency evidence independent of latched alarm'),
'ack_eligible':('risk_state.ack_eligible (WP01)','Physical ACK readiness; not a remote command'),
'emergency_latched':('risk_state.emergency_latched (WP01)','Persistent local latch; Cloud cannot clear'),
'physical_ack_seq':('risk_state.physical_ack_seq (WP01)','Increases only for accepted durable physical latch clear'),
'persistence_health':('risk_state.persistence_health / latch_store_health_t','0 pending, 1 durable, 2 fault/unavailable; not event-journal health'),
'overall_quality':('kitchen_sensor_data_t.v1_quality','Pipeline overall quality, separate from risk'),
'gy906_quality':('gy906_quality + existing freshness window + finite checks','Current MLX90614 data usability'),
'mq2_quality':('mq2_quality + existing freshness window','DO acquisition usability, not proof of physical connection'),
'flame_quality':('flame_quality + existing freshness window','DO acquisition usability, not proof of physical connection'),
'radar_quality':('radar_quality + existing freshness window','Radar acquisition usability; inspect source separately'),
'gas_detected':('mq2_gas_detected (DO interpreted by existing pipeline)','Digital gas/smoke indication; not concentration'),
'flame_detected':('flame_state == FLAME_STATE_FIRE','Existing DO flame indication'),
'presence':('radar_presence','Existing presence flag; no new detection logic'),
'radar_source':('radar_uart_valid ? UART : OUT_FALLBACK','Distinguish validated UART frame from digital fallback'),
'temp_object_c':('temp_object_filtered','Filtered MLX90614 object temperature, not room temperature'),
'temp_ambient_c':('temp_ambient_filtered','Filtered MLX90614 sensor ambient temperature'),
'temp_rate_cps':('dT_dt when dT_dt_valid','Existing derived filtered object-temperature slope'),
'mq2_ao_valid':('mq2_ao_valid + own last-update freshness','Independent actual AO conversion validity'),
'mq2_ao_mv_valid':('mq2_ao_mv_valid + AO validity/freshness','Independent calibration validity'),
'mq2_adc_counts':('mq2_ao_adc_raw (actual AO read, 0..4095)','Actual ADC counts at MCU pin; not pseudo-raw or ppm'),
'mq2_adc_mv':('mq2_ao_adc_mv when calibrated','ADC pin millivolts, not reconstructed sensor supply voltage'),
'buzzer_mode':('buzzer_get_mode() after Local Alarm Controller','Commanded driver mode'),
'buzzer_output_on':('buzzer_state captured by Local Alarm Controller','Output phase sample; not proof of audible operation'),
'muted':('is_muted from Local Alarm Controller','Local WARNING mute state'),
'mute_remaining_s':('mute_remaining_sec','Local mute countdown'),
'recovery_remaining_s':('risk_state.recovery_countdown_sec','Existing continuous-safe recovery countdown'),
'free_heap_bytes':('esp_get_free_heap_size()','Current total free heap'),
'min_free_heap_bytes':('esp_get_minimum_free_heap_size()','Boot low-water mark'),
'largest_free_block_bytes':('heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)','Allocation headroom/fragmentation indicator'),
'network_stack_free_bytes':('uxTaskGetStackHighWaterMark(network task)','ESP-IDF stack minimum unused bytes'),
'safety_stack_free_bytes':('uxTaskGetStackHighWaterMark(main task)','ESP-IDF stack minimum unused bytes'),
'safety_step_max_us':('main: loop start through local alarm controller','Observed maximum in this boot, not sensor-to-alarm worst-case'),
'safety_interval_max_us':('main: successive safety iteration start times','Observed sample-loop interval maximum'),
'event_drop_count':('event rejected + evicted + journal loss counters','Aggregate exposure indicator; may overlap, not unique lost events'),
'event_expiry_count':('event_buffer_stats.expired','Expired/recycled delivery attempts; retained record is retried'),
'event_reject_count':('event_buffer_stats.rejected','Full-buffer enqueue rejections; includes repeated handoff attempts'),
'event_eviction_count':('event_buffer_stats.evicted','Unclaimed lower-priority RAM records replaced'),
'event_completed_count':('event_buffer_stats.completed','RAM records removed after matching broker PUBACK'),
'event_retry_count':('event_buffer_stats.retries','Retry attempts, not distinct incidents'),
'journal_loss_count':('critical_journal_losses()','Failed/unavailable/full/corrupt durable copies; uncertainty, not exact Cloud loss')}
measurement={'temp_object_c','temp_ambient_c','temp_rate_cps','gas_detected','flame_detected','presence','mq2_adc_counts','mq2_adc_mv'}
health={k for k in dictionary if k.endswith('_bytes') or k.endswith('_count')}|{'safety_step_max_us','safety_interval_max_us'}
for k,v in dictionary.items():
 v['source'],v['purpose']=sources[k]
 v['trigger']='60s health; idle heartbeat 30s' if k in health else 'Immediate transition; current snapshot 10s normal / 2s abnormal'
 v['validity']='Fresh/valid at capture; omitted if invalid/stale/non-finite; history retains original observation' if k in measurement else 'Snapshot/export, not proof of physical sensor or actuator health'
 if k.endswith('_bytes'):v['unit']='byte'
 if k.endswith('_us'):v['unit']='us'
dictionary['record_kind']={'type':'enum','enum':['SNAPSHOT','BOOT','RECONNECT','HEARTBEAT','TRANSITION'],'unit':'none','source':'publisher stream selector','category':'telemetry','validity':'Always present','trigger':'Every record','purpose':'Separate current, health and archive streams'}
(folder/'data_dictionary.json').write_text(json.dumps(dictionary,indent=2)+'\n')
ad={k:{'type':v.get('type','integer'),'unit':'ms' if k.endswith('_ms') else 'none','source':'generated build configuration / reset cause / fixed device capabilities','category':'client_attribute','trigger':'Boot/reconnect, QoS1','purpose':'Low-change identity/config metadata'} for k,v in a['properties'].items()}
(folder/'attributes_dictionary.json').write_text(json.dumps(ad,indent=2)+'\n')
md=['# PCCC ThingsBoard Data Contract V1','', 'Authoritative implementation dictionary. Example fixtures are illustrative values generated by the real serializer, not hardware measurements. All telemetry is flat scalar; `event_` fields mirror the table for historical transitions. Native `ts` (milliseconds) wraps `values` only after trusted clock synchronization.','', '| Field | Type | Unit | Source | Trigger | Purpose |','|---|---|---|---|---|---|']
for k,v in dictionary.items():md.append('| '+ ' | '.join([k,str(v['type']),v['unit'],v['source'],v['trigger'],v['purpose']])+' |')
md+=['','Measurement validity: '+next(v['validity'] for k,v in dictionary.items() if k in measurement), '', 'Client attributes: '+', '.join(ad)+'. No shared attributes, RPC subscriptions, remote ACK or remote threshold control.', '', 'Quality enum VALID describes software acquisition/validation only. INITIALIZING, INVALID and STALE omit affected measurements. Missing keys do not erase ThingsBoard Latest values: operational widgets/rules MUST check current quality and capture age. Historical values use quality at original capture, not replay time.', '', '`persistence_health`: PENDING=0, DURABLE=1, FAULT=2 (WP01). Per-boot diagnostic counters reset on reboot; physical_ack_seq is persistent WP01 history. Aggregate event_drop_count may overlap journal-copy failures and RAM rejection; it is not an exact distinct-event-loss count. No offline critical-record TTL: expiry count measures retry attempts, not silent history deletion.', '', 'Compatibility: producer schema is strict to catch accidental keys/types. Consumers gate schema_version, ignore documented optional unknown extensions and route TRANSITION separately. Breaking names/units/meaning require a new version. `event_*` is an intentional pre-release refinement from the baseline proposal: historical replay cannot overwrite unprefixed live risk even under default ThingsBoard timeseries saving. Phase 2 must adopt these names before new Cloud rules are enabled.']
(folder/'README.md').write_text('\n'.join(md)+'\n')
