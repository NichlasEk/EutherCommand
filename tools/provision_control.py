"""Generate local-only TLS identity and control credential; never overwrite."""
from pathlib import Path
import os
import secrets
import subprocess
import json

root=Path('.local/control');root.mkdir(mode=0o700,parents=True,exist_ok=True)
key=root/'server-key.pem';cert=root/'server.pem';config=root/'client.json'
if not key.exists() and not cert.exists():
    subprocess.run(['openssl','req','-x509','-newkey','ec','-pkeyopt','ec_paramgen_curve:prime256v1','-nodes','-days','3650','-subj','/CN=euthercommand-lab','-addext','subjectAltName=DNS:euthercommand-lab','-keyout',str(key),'-out',str(cert)],check=True,capture_output=True)
if not key.exists() or not cert.exists(): raise SystemExit('Incomplete TLS identity; inspect local files')
if not config.exists():
    config.write_text(json.dumps({'host':'192.168.32.248','server_name':'euthercommand-lab','certificate':str(cert.resolve()),'token':secrets.token_hex(32)},indent=2)+'\n')
for p in (key,cert,config): p.chmod(0o600)
data=json.loads(config.read_text())
header=Path('firmware/main/control_local.h')
def literal(s):return json.dumps(s)
header.write_text('#pragma once\n#define EC_CONTROL_CERT '+literal(cert.read_text())+'\n#define EC_CONTROL_KEY '+literal(key.read_text())+'\n#define EC_CONTROL_TOKEN '+literal(data['token'])+'\n')
header.chmod(0o600)
print('Local control identity ready; secrets not printed.')
