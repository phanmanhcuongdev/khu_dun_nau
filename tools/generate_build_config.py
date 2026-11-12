#!/usr/bin/env python3
"""Local provisioning -> private build header; never logs credential values."""
import sys,json,hashlib,pathlib,datetime
root=pathlib.Path(sys.argv[1]);out=pathlib.Path(sys.argv[2]);private=pathlib.Path(sys.argv[3])
if private.exists() and private.stat().st_size>4096:raise SystemExit('Private configuration too large')
cfg=json.loads(private.read_text()) if private.exists() else {}
keys=['wifi_ssid','wifi_password','tb_access_token']
for k in keys:
 if not isinstance(cfg.get(k,''),str):raise SystemExit('Invalid private config type')
if any('\x00' in cfg.get(k,'') for k in keys) or len(cfg.get('tb_access_token','').encode())>256:raise SystemExit('Invalid credential value')
if len(cfg.get('wifi_ssid','').encode())>32 or len(cfg.get('wifi_password','').encode())>63:raise SystemExit('Invalid Wi-Fi configuration length')
def cstr(s):return '"'+''.join('\\%03o'%b for b in s.encode())+'"'
ready=all(cfg.get(k) for k in keys)
source=hashlib.sha256()
inputs=list((root/'main').rglob('*.c'))+list((root/'main/include').glob('*.h'))+list((root/'components').rglob('*.c'))+list((root/'components').rglob('*.h'))+[root/'CMakeLists.txt',root/'main/CMakeLists.txt',root/'sdkconfig',root/'dependencies.lock']
for p in sorted(inputs):source.update(str(p.relative_to(root)).encode()+p.read_bytes())
source.update(('provisioned='+str(int(ready))).encode())
out.mkdir(parents=True,exist_ok=True)
text='#pragma once\n'+''.join('#define PCCC_'+k.upper()+' '+cstr(cfg.get(k,''))+'\n' for k in keys)
text+='#define PCCC_PROVISIONED '+str(int(ready))+'\n#define PCCC_FIRMWARE_VERSION "1.1.0-phase1"\n'
text+='#define PCCC_BUILD_ID "'+source.hexdigest()[:16]+'"\n'
text+='#define PCCC_CONFIG_ID "'+hashlib.sha256(b''.join((root/p).read_bytes() for p in ['main/include/safety_config.h','main/include/board_pins.h','sdkconfig'])).hexdigest()[:16]+'"\n'
text+='#define PCCC_BUILD_EPOCH '+str(int(datetime.datetime.now(datetime.timezone.utc).timestamp()))+'LL\n'
p=out/'pccc_build_config.h'
# Avoid changing build time and causing every incremental build to relink.
if not p.exists() or '\n'.join(p.read_text().splitlines()[:-1])!='\n'.join(text.splitlines()[:-1]):p.write_text(text);p.chmod(0o600)
print('Local provisioning: configured' if ready else 'Local provisioning absent: network disabled; local safety remains enabled')
