"""Dry-run Phase 2 routing model. No HTTP/MQTT clients, no Cloud writes.
Tenant integration must supply authenticated/per-device serialized session boundary.
"""
def route(payload,state,seen,session_verified=False):
    values=payload.get('values',payload)
    if values.get('schema_version')!=1:
        return {'route':'legacy_quarantine','current':{},'alarm_input':None}
    if values.get('record_kind')=='TRANSITION':
        identity=(values['event_boot_id'],values['event_record_seq'])
        if identity in seen:return {'route':'duplicate','current':{},'alarm_input':None}
        seen.add(identity)
        return {'route':'archive_only','identity':identity,'current':{},'alarm_input':None,'archive':values}
    boot=values['boot_id'];seq=values['record_seq']
    if values['record_kind']=='HEARTBEAT':return {'route':'health_only','current':{},'alarm_input':None,'health':values}
    if state.get('boot')!=boot:
        if not session_verified:return {'route':'new_boot_quarantine','current':{},'alarm_input':None}
        state.update(boot=boot,seq=-1)
    if seq<=state['seq']:return {'route':'older_state','current':{},'alarm_input':None}
    state['seq']=seq
    return {'route':'current_state','current':values,'alarm_input':values}
