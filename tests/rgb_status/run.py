#!/usr/bin/env python3
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'components/rgb_status/rgb_status.c').read_text()
a=s.index('    uint8_t c[3]'); b=s.index('    for (int i=0;i<3;i++) {\n        ledc_set_duty',a)
header=(root/'components/rgb_status/include/rgb_status.h').read_text()
enum=header[header.index('typedef enum'):header.index('/*')]
code='#include <stdint.h>\n#include <assert.h>\n'+enum+"""
static int64_t now,epoch;
static uint8_t manual[3];
static int64_t esp_timer_get_time(void) {return now*1000;}
static void check(rgb_status_t state,int t,int r,int g,int blue) {
now=t;
"""+s[a:b]+"""
assert(c[0]==r && c[1]==g && c[2]==blue);
}
int main(void) {
for(int cycle=0;cycle<100;cycle++) {
check(RGB_BOOTING,0,0,0,255); check(RGB_BOOTING,500,0,0,0);
check(RGB_INFO,999,0,180,0);
check(RGB_WARNING,499,255,100,0); check(RGB_WARNING,500,0,0,0);
check(RGB_FIRE,159,255,0,0); check(RGB_FIRE,160,0,0,0); check(RGB_FIRE,320,255,0,0);
check(RGB_SENSOR_ERROR,0,160,0,255); check(RGB_SENSOR_ERROR,100,0,0,0);
check(RGB_SENSOR_ERROR,200,160,0,255); check(RGB_SENSOR_ERROR,300,0,0,0);
check(RGB_SENSOR_ERROR,999,0,0,0); check(RGB_SENSOR_ERROR,1000,160,0,255);
check(RGB_OFF,0,0,0,0);
for(int i=0;i<3;i++) {manual[i]=255;check(RGB_MANUAL,0,manual[0],manual[1],manual[2]); manual[i]=0;}
}
}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d); (p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('RGB production pattern calculations: PASS (host; no LEDC/hardware coverage)')

# Compile the exact application presentation mapping; no risk reclassification.
import re
main=(root/'main/main.c').read_text()
expr=main.split('rgb_status_t rgb_live = (',1)[1].split(');',1)[0]
expr=expr.replace('sensor_data.risk_state.risk_level','risk').replace('sensor_data.risk_state.monitoring_status','monitoring')
types=(root/'main/include/sensor_types.h').read_text()
enums=re.findall(r'typedef enum \{.*?\} (?:risk_level_t|monitoring_status_t);',types,re.S)
# Pull only the enum containing each named type.
enums=[]
for name in ['risk_level_t','monitoring_status_t']:
 end=types.index('} '+name+';')+len('} '+name+';')
 start=types.rfind('typedef enum {',0,end)
 enums.append(types[start:end])
code='#include <assert.h>\n'+enum+'\n'+'\n'.join(enums)+'\n'
code+='static rgb_status_t map(risk_level_t risk,monitoring_status_t monitoring) {return '+expr+';}\n'
code+="""
int main(void) {
 for(int repeat=0;repeat<1000;repeat++) {
  for(int m=MONITORING_OK;m<=MONITORING_FAULT;m++) {
   assert(map(RISK_EMERGENCY,m)==RGB_FIRE);
   assert(map(RISK_WARNING,m)==(m==MONITORING_FAULT?RGB_SENSOR_ERROR:RGB_WARNING));
   assert(map(RISK_NORMAL,m)==(m==MONITORING_FAULT?RGB_SENSOR_ERROR:RGB_INFO));
   assert(map(RISK_SUSPICIOUS,m)==(m==MONITORING_FAULT?RGB_SENSOR_ERROR:RGB_INFO));
   assert(map(RISK_UNKNOWN,m)==(m==MONITORING_FAULT?RGB_SENSOR_ERROR:RGB_BOOTING));
  }
 }
}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d); (p/'mapping.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'mapping.c'),'-o',str(p/'mapping')],check=True)
 subprocess.run([str(p/'mapping')],check=True)
config=(root/'sdkconfig').read_text()
for channel,pin in [('R',39),('G',38),('B',48)]:
 assert f'CONFIG_RGB_STATUS_{channel}_GPIO={pin}\n' in config
assert ('# CONFIG_RGB_STATUS_WIRING_CONFIRMED is not set' in config or
        'CONFIG_RGB_STATUS_WIRING_CONFIRMED=y' in config)
print('Risk presentation mapping, FIRE precedence, repeated mapping and R39/G38/B48 config: PASS')
print('Concurrent setters, lifecycle fault injection, physical timing and LEDC reuse: NOT TESTED')

self_test=(root/'main/include/rgb_self_test.h').read_text()
self_test='\n'.join(line for line in self_test.splitlines() if not line.startswith('#'))
code='#include <stdbool.h>\n#include <stdint.h>\n#include <assert.h>\n'+enum+self_test+"""
int main(void) {
 rgb_self_test_t t={0};
 const rgb_status_t seq[]={RGB_BOOTING,RGB_INFO,RGB_WARNING,RGB_FIRE,RGB_SENSOR_ERROR};
 for(int i=0;i<25;i++) assert(rgb_self_test_select(&t,RGB_INFO,1000000LL+i*1000000LL)==seq[i/5]);
 assert(rgb_self_test_select(&t,RGB_INFO,26000000)==RGB_INFO && t.finished);
 assert(rgb_self_test_select(&t,RGB_BOOTING,27000000)==RGB_BOOTING);
 for(int phase=0;phase<5;phase++) {
  rgb_self_test_t u={0};
  rgb_self_test_select(&u,RGB_INFO,0);
  rgb_self_test_select(&u,RGB_INFO,phase*5000000LL);
  assert(rgb_self_test_select(&u,RGB_FIRE,phase*5000000LL+1)==RGB_FIRE && u.finished);
  assert(rgb_self_test_select(&u,RGB_INFO,phase*5000000LL+2)==RGB_INFO);
 }
 rgb_self_test_t w={0},e={0};
 assert(rgb_self_test_select(&w,RGB_WARNING,0)==RGB_WARNING && w.finished);
 assert(rgb_self_test_select(&e,RGB_SENSOR_ERROR,0)==RGB_SENSOR_ERROR && e.finished);
}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d); (p/'selftest.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'selftest.c'),'-o',str(p/'selftest')],check=True)
 subprocess.run([str(p/'selftest')],check=True)
print('LED-only self-test sequence, timeout, FIRE preemption in every phase and no restart: PASS (host)')
