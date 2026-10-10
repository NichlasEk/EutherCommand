"""Pinned HTTPS node client. No redirects, retries, raw commands, or implicit enable."""
import hashlib
import http.client
import ipaddress
import json
import re
from pathlib import Path
import socket
import ssl
import uuid

class PinnedConnection(http.client.HTTPSConnection):
    def __init__(self, host, context, server_name, fingerprint):
        super().__init__(host,443,timeout=6,context=context)
        self.server_name=server_name
        self.fingerprint=fingerprint
    def connect(self):
        raw=socket.create_connection((self.host,self.port),self.timeout)
        try:
            self.sock=self._context.wrap_socket(raw,server_hostname=self.server_name)
            if hashlib.sha256(self.sock.getpeercert(binary_form=True)).digest()!=self.fingerprint:
                raise ssl.SSLError('node certificate pin mismatch')
        except Exception:
            raw.close()
            if self.sock:self.sock.close()
            raise

class NetworkNode:
    def __init__(self, config='.local/control/client.json'):
        self.config=json.loads(Path(config).read_text())
        address=ipaddress.ip_address(self.config['host'])
        if address.version!=4 or not any(address in ipaddress.ip_network(n) for n in ('10.0.0.0/8','172.16.0.0/12','192.168.0.0/16')):
            raise ValueError('node must use a private LAN IPv4 address')
        cert=Path(self.config['certificate']).read_text()
        self.fingerprint=hashlib.sha256(ssl.PEM_cert_to_DER_cert(cert)).digest()
        self.context=ssl.create_default_context(cafile=self.config['certificate'])
    def request(self,method,path,body=None,token=None):
        conn=PinnedConnection(self.config['host'],self.context,self.config['server_name'],self.fingerprint)
        try:
            headers={'Authorization':'Bearer '+(self.config['token'] if token is None else token),'Content-Type':'application/json','Connection':'close'}
            conn.request(method,path,json.dumps(body) if body is not None else None,headers)
            response=conn.getresponse();data=response.read(8193)
            if len(data)>8192:raise ValueError('oversized node response')
            if response.status!=200:raise PermissionError(f'node HTTP {response.status}')
            return json.loads(data)
        finally:conn.close()
    def status(self):return self.request('GET','/v1/status')
    def command(self,command,request_id=None,boot_id=None):
        if command not in ('enable','disable','volume_up','volume_down') and not re.fullmatch(r'ir_[0-9a-f]{12}',command):raise ValueError('unregistered command')
        boot=boot_id or self.status()['boot_id']
        return self.request('POST','/v1/commands',{'boot_id':boot,'request_id':request_id or uuid.uuid4().hex,'command':command})
    def transmit(self,node,signal,request_id):
        if node!='lab':raise ValueError('unauthorized node')
        for name in ('volume_up','volume_down'):
            registered=json.loads((Path(__file__).resolve().parent.parent/'signals'/'logitech'/f'{name}.json').read_text())
            if signal==registered:
                return self.command(name,request_id)['status']
        raise ValueError('signal not in firmware registry')
