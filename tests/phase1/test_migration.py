import importlib.util,json,pathlib
root=pathlib.Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('routing',root/'migration/phase2/normalize.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
def fixture(name):return json.loads((root/'fixtures/pccc-v1'/f'{name}.json').read_text())
state={};seen=set()
r=m.route(fixture('normal'),state,seen,True);assert r['route']=='current_state'
old=state.copy();r=m.route(fixture('emergency'),state,seen,True);assert r['route']=='archive_only' and r['alarm_input'] is None and state==old
assert m.route(fixture('emergency'),state,seen,True)['route']=='duplicate'
assert m.route(fixture('normal'),state,seen,True)['route']=='older_state'
assert m.route(fixture('health'),state,seen,True)['route']=='health_only'
assert m.route({'old':'payload'},state,seen)['route']=='legacy_quarantine'
print('Phase2 migration dry-run archive/current separation, dedup, ordering, heartbeat, legacy quarantine: PASS (no Cloud writes)')
