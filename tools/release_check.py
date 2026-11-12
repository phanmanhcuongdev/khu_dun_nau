#!/usr/bin/env python3
"""Check public release inputs; private/build artifacts are never public release inputs."""
import json,pathlib,sys,re,os
root=pathlib.Path(__file__).resolve().parents[1]
private=root/'private/provisioning.json'
cfg=json.loads(private.read_text()) if private.exists() else {}
secrets=[v for k,v in cfg.items() if k in ['wifi_ssid','wifi_password','tb_access_token'] and v]
if private.exists() and private.stat().st_mode&0o077:raise SystemExit('Private provisioning permissions must be 0600')
paths=[]
for folder in ['main','components','tools','schemas','fixtures','migration','docs']:
 for p in (root/folder).rglob('*'):
  if not p.is_file() or p.suffix in ['.png','.pbm']:continue
  text=p.read_text(errors='ignore')
  if any(s in text for s in secrets):raise SystemExit('Credential literal found in public input: '+str(p.relative_to(root)))
  paths.append(str(p.relative_to(root)))
print('Public source/release inputs contain no current credential literals: PASS; checked',len(paths),'files')
print('Device-specific ELF/bin and build/private headers contain credentials: keep private; not public release assets')
