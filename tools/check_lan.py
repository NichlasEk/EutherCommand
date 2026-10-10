"""Physical LAN safety checks. No enable or successful transmission is requested."""
import json
import ssl
import time
import uuid
from euthercommand.network_node import NetworkNode

def main():
    n=NetworkNode();s=n.status();assert s['status']=='disabled',s
    print('PASS authenticated status: disabled')
    try:n.request('GET','/v1/status',token='wrong')
    except PermissionError:print('PASS wrong credential rejected')
    else:raise AssertionError('wrong credential accepted')
    original=n.fingerprint;n.fingerprint=b'\0'*32
    try:n.status()
    except ssl.SSLError:print('PASS wrong certificate pin rejected')
    else:raise AssertionError('wrong pin accepted')
    finally:n.fingerprint=original
    boot=s['boot_id'];rid='check-'+uuid.uuid4().hex
    first=n.command('disable',rid,boot);second=n.command('disable',rid,boot)
    assert first['status']==second['status']=='disabled' and second['duplicate']
    print('PASS duplicate returns cached result')
    try:n.command('volume_down',rid,boot)
    except PermissionError:print('PASS conflicting request ID rejected')
    else:raise AssertionError('conflicting request accepted')
    try:n.command('disable',boot_id='0'*32)
    except PermissionError:print('PASS stale boot rejected')
    else:raise AssertionError('stale boot accepted')
    time.sleep(.3)
    result=n.command('volume_down',boot_id=boot);assert result['status']=='disabled',result
    print('PASS disabled execution rejected; no IR sent')
    try:n.request('POST','/v1/commands',{'boot_id':boot,'request_id':uuid.uuid4().hex,'command':'raw_gpio'})
    except PermissionError:print('PASS unregistered operation rejected')
    else:raise AssertionError('raw operation accepted')
    try:n.request('POST','/v1/commands',{'padding':'X'*600})
    except PermissionError:print('PASS oversized request rejected')
    else:raise AssertionError('oversized request accepted')
    assert n.command('disable')['status']=='disabled'
    print(json.dumps({'boot_id':boot,'status':'checks_passed','physical_ir_sent':False}))
if __name__=='__main__':main()
