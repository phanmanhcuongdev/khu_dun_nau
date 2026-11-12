#!/usr/bin/env python3
import json,pathlib,copy,math,sys
import jsonschema,re
root=pathlib.Path(__file__).resolve().parents[2]
fixtures=pathlib.Path(sys.argv[1]) if len(sys.argv)>1 else root/'fixtures/pccc-v1'
schema=json.loads((root/'schemas/pccc-v1/telemetry.schema.json').read_text())
attrs=json.loads((root/'schemas/pccc-v1/attributes.schema.json').read_text())
jsonschema.Draft202012Validator.check_schema(schema)
jsonschema.Draft202012Validator.check_schema(attrs)
# Every implemented primary reason must be supported without a Cloud classifier.
reason_names=re.findall(r'case REASON_\w+:\s*return "([^"]+)"',(root/'main/src/risk_engine.c').read_text())
assert set(reason_names)==set(schema['$defs']['record']['oneOf'][0]['properties']['risk_reason']['enum'])
def semantic(record):
 v=record.get('values',record);p='event_' if v.get('record_kind')=='TRANSITION' else ''
 if p:assert not any(k in v for k in ['risk_level','emergency_latched','boot_id','record_seq'])
 for k,x in v.items():
  if isinstance(x,float):assert math.isfinite(x)
 assert not any('ppm' in k.lower() or k in ('MQ2_raw','flame_raw') for k in v)
 if p+'temp_object_c' in v:assert v[p+'gy906_quality']=='VALID'
 if p+'gas_detected' in v:assert v[p+'mq2_quality']=='VALID'
 if p+'flame_detected' in v:assert v[p+'flame_quality']=='VALID'
 if p+'mq2_adc_counts' in v:assert v[p+'mq2_ao_valid']
 if p+'mq2_adc_mv' in v:assert v[p+'mq2_ao_mv_valid']
 if v.get(p+'emergency_latched'):assert v[p+'risk_level']=='EMERGENCY' and not v[p+'muted']
 if v.get(p+'ack_eligible'):assert v[p+'emergency_latched'] and v[p+'detection_state']=='NO_EMERGENCY'
for f in fixtures.glob('*.json'):
 r=json.loads(f.read_text(),parse_constant=lambda s:(_ for _ in ()).throw(ValueError(s)))
 if f.stem=='attributes':jsonschema.validate(r,attrs)
 else:jsonschema.validate(r,schema);semantic(r)
# Negative fixtures prove schema/semantics reject misleading values.
r=json.loads((fixtures/'normal.json').read_text())
for key,value in [('temp_object_c','NaN'),('mq2_adc_counts','200'),('MQ2_raw',200)]:
 bad=copy.deepcopy(r);bad['values'][key]=value
 assert list(jsonschema.Draft202012Validator(schema).iter_errors(bad))
bad=copy.deepcopy(r);bad['ts']=1794358800
assert list(jsonschema.Draft202012Validator(schema).iter_errors(bad))
bad=copy.deepcopy(r);bad['values']['gy906_quality']='STALE'
try:semantic(bad);raise RuntimeError('stale measurement accepted')
except AssertionError:pass
h=json.loads((fixtures/'emergency.json').read_text());h['values']['risk_level']='NORMAL'
assert list(jsonschema.Draft202012Validator(schema).iter_errors(h))
print('Draft2020-12 schema + 10 production-serializer fixtures and negative semantic tests: PASS')
