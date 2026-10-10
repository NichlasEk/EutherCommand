import json
import os
from pathlib import Path
import tempfile
import threading
import unittest
import urllib.request
import urllib.error
from euthercommand.core import Core, MockIR
from euthercommand.ir import nec, sirc, decode, validate
from euthercommand.api import server
from euthercommand.integrations import VoxAdapter

class SystemTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.token, self.vox = 'a'*32, 'b'*32
        os.environ['EUTHERCOMMAND_OPERATOR_TOKEN'] = self.token
        os.environ['EUTHERCOMMAND_VOX_TOKEN'] = self.vox
        self.config = Path('euthercommand.toml').read_text()
        self.db = self.tmp.name + '/state.sqlite'
        self.core = Core(self.config, self.db)
        self.addCleanup(self.core.db.close)
    def post(self, path, data, token=None):
        return self.core.call(token or self.token, 'POST', '/v1/' + path, data)
    def learn(self):
        cid = self.post('learn', {'node':'lab'})['capture_id']
        self.post('learn/capture', {'capture_id':cid, 'signal':nec(16, 12)})
        self.post('learn/save', {'capture_id':cid, 'name':'amp_power'})
    def request(self):
        return {'device':'test_amplifier','command':'power','request_id':'test-001'}
    def test_learning_replay_restart_duplicate(self):
        self.learn()
        self.assertEqual(self.post('execute',self.request())['status'],'simulated')
        self.assertTrue(self.post('execute',self.request())['duplicate'])
        self.assertEqual(len(self.core.transport.sent),1)
        restarted=Core(self.config,self.db)
        self.addCleanup(restarted.db.close)
        self.assertTrue(restarted.call(self.token,'POST','/v1/execute',self.request())['duplicate'])
        self.assertEqual(restarted.transport.sent,[])
    def test_auth_and_scope(self):
        for token in ('', 'wrong', self.vox):
            with self.assertRaises(PermissionError): self.post('learn',{'node':'lab'}, token=token or 'wrong')
        with self.assertRaises(PermissionError): self.post('execute',dict(self.request(),device='other'),self.vox)
    def test_disable_persists(self):
        self.learn()
        self.post('disable',{'disabled':True})
        with self.assertRaises(PermissionError): self.post('execute',self.request())
        other=Core(self.config,self.db)
        self.addCleanup(other.db.close)
        self.assertTrue(other.disabled())
    def test_rate_limit(self):
        self.learn(); self.post('execute',self.request())
        with self.assertRaises(ValueError): self.post('execute',dict(self.request(),request_id='test-002'))
    def test_unknown_command_and_payload(self):
        for changes in ({'command':'raw'}, {'repeat':100}, {'request_id':'../bad'}):
            with self.assertRaises(ValueError): self.post('execute',dict(self.request(),**changes))
    def test_failed_send_never_retries(self):
        self.learn()
        class Broken:
            def transmit(self,*args): raise TimeoutError()
        self.core.transport=Broken()
        self.assertEqual(self.post('execute',self.request())['status'],'unknown')
        self.assertTrue(self.post('execute',self.request())['duplicate'])
    def test_conflicting_duplicate(self):
        self.learn(); self.post('execute',self.request())
        with self.assertRaises(ValueError): self.post('execute',self.request(),self.vox)
    def test_expired_capture(self):
        cid=self.post('learn',{'node':'lab'})['capture_id']
        self.core.db.execute('UPDATE captures SET expires=0'); self.core.db.commit()
        with self.assertRaises(ValueError): self.post('learn/capture',{'capture_id':cid,'signal':nec(1,2)})
    def test_protocols(self):
        self.assertEqual(decode(nec(16,12)),{'protocol':'NEC','address':16,'command':12})
        self.assertEqual(decode(sirc(3,21))['value'],3<<7|21)
        self.assertEqual(decode({'carrier_hz':38000,'durations_us':[1000,2000,1000]})['protocol'],'raw')
        bad=nec(1,2); bad['durations_us'][3]=10000
        self.assertEqual(decode(bad)['protocol'],'raw')
    def test_physical_strong_capture(self):
        signal = json.loads(Path('tests/fixtures/strong-nec-88-01.json').read_text())
        self.assertEqual(decode(signal), {'protocol': 'NEC', 'address': 136, 'command': 1})

    def test_physical_logitech_volume_captures(self):
        for name, command in [('volume_up', 26), ('volume_down', 14)]:
            signal = json.loads(Path(f'signals/logitech/{name}.json').read_text())
            self.assertEqual(decode(signal), {'protocol': 'NEC', 'address': 8, 'command': command})

    def test_malformed_signal(self):
        for p in ([1,2,3],[500]*513,[30000]*11,[True,500,500],[500,500]):
            with self.assertRaises(ValueError): validate({'carrier_hz':38000,'durations_us':p})
    def test_config_fail_closed(self):
        with self.assertRaises(ValueError): Core(self.config.replace('category = "audio"','category = "lock"'),':memory:')
        with self.assertRaises(ValueError): Core(self.config.replace('node = "lab"','node = "missing"'),':memory:')
    def test_vox_exact_alias(self):
        adapter=VoxAdapter({'sätt på förstärkaren':('test_amplifier','power')},['owner'])
        self.assertEqual(adapter.plan('Sätt på förstärkaren.','owner','phone')['name'],'device.command')
        self.assertIsNone(adapter.plan('Sätt inte på förstärkaren','owner','phone'))
        with self.assertRaises(PermissionError): adapter.plan('Sätt på förstärkaren','stranger','phone')
        self.assertIn('Ingen IR',adapter.spoken({'status':'simulated'}))
    def test_http_end_to_end(self):
        http=server(self.core,0)
        worker=threading.Thread(target=http.serve_forever,daemon=True); worker.start()
        self.addCleanup(http.server_close); self.addCleanup(http.shutdown)
        def request(path,body=None,token=None):
            req=urllib.request.Request(f'http://127.0.0.1:{http.server_port}/v1/'+path,
                data=json.dumps(body).encode() if body is not None else None,
                headers={'Authorization':'Bearer '+(token or self.token),'Content-Type':'application/json'})
            with urllib.request.urlopen(req) as response: return json.load(response)
        with self.assertRaises(urllib.error.HTTPError) as error: request('status',token='bad')
        error.exception.close()
        cid=request('learn',{'node':'lab'})['capture_id']
        request('learn/capture',{'capture_id':cid,'signal':nec(16,12)})
        request('learn/save',{'capture_id':cid,'name':'amp_power'})
        self.assertEqual(request('execute',self.request())['status'],'simulated')
        self.assertTrue(request('execute',self.request())['duplicate'])
        with self.assertRaises(urllib.error.HTTPError) as error: request('execute',{'padding':'x'*17000})
        error.exception.close()
        self.assertEqual(len(request('history')),1)

if __name__=='__main__': unittest.main()
