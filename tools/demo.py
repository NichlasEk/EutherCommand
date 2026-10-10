"""Self-contained simulated learning/replay; never touches physical hardware."""
import json
import os
from pathlib import Path
import secrets
import tempfile
from euthercommand.core import Core
from euthercommand.ir import nec

os.environ['EUTHERCOMMAND_OPERATOR_TOKEN'] = secrets.token_hex(32)
os.environ['EUTHERCOMMAND_VOX_TOKEN'] = secrets.token_hex(32)
token = os.environ['EUTHERCOMMAND_OPERATOR_TOKEN']
with tempfile.TemporaryDirectory() as tmp:
    core = Core(Path('euthercommand.toml').read_text(), tmp + '/state.sqlite')
    def post(path, data):
        result = core.call(token, 'POST', '/v1/' + path, data)
        print(json.dumps(result, ensure_ascii=False))
        return result
    capture = post('learn', {'node': 'lab'})['capture_id']
    post('learn/capture', {'capture_id': capture, 'signal': nec(16, 12)})
    post('learn/save', {'capture_id': capture, 'name': 'amp_power'})
    request = {'device': 'test_amplifier', 'command': 'power', 'request_id': 'demo-001'}
    post('execute', request)
    post('execute', request)
