import hashlib
import json
import logging
import os
import re
import secrets
import sqlite3
import threading
import time
import tomllib
from .ir import validate, decode


def identity(value):
    if not isinstance(value, str) or not re.fullmatch(r'[a-zA-Z0-9_-]{1,64}', value):
        raise ValueError('invalid identifier')
    return value


class MockIR:
    def __init__(self):
        self.sent = []
    def transmit(self, node, signal, request_id):
        self.sent.append((node, signal, request_id))
        return 'simulated'


class Core:
    def __init__(self, config, database, transport=None):
        self.config = tomllib.loads(config) if isinstance(config, str) else config
        self.devices = self.config['device']
        self.nodes = self.config['node']
        for node, data in self.nodes.items():
            identity(node)
            if data['transport'] != 'infrared':
                raise ValueError('unsupported transport')
        for key, device in self.devices.items():
            identity(key)
            if device['node'] not in self.nodes or device['transport'] != 'infrared' or device['category'] not in ('audio', 'display', 'test'):
                raise ValueError('invalid device/node/category')
            for command, ref in device['commands'].items():
                identity(command)
                if not isinstance(ref, str) or not ref.startswith('learned:'):
                    raise ValueError('expected learned reference')
                identity(ref[8:])
        self.principals = []
        for name, p in self.config['principal'].items():
            token = os.environ.get(p['token_env'], '')
            if len(token) < 32:
                raise ValueError('credential missing or too short: ' + p['token_env'])
            if any(token == old[1] for old in self.principals):
                raise ValueError('credentials must be unique')
            if any(d not in self.devices for d in p['devices']) or not set(p['scopes']) <= {'read', 'execute', 'learn', 'disable'}:
                raise ValueError('invalid principal permissions')
            self.principals.append((name, token, p))
        self.transport = transport or MockIR()
        self.lock = threading.RLock()
        self.db = sqlite3.connect(database, check_same_thread=False)
        self.db.executescript('''CREATE TABLE IF NOT EXISTS signals(id TEXT PRIMARY KEY, node TEXT, data TEXT, metadata TEXT);
        CREATE TABLE IF NOT EXISTS requests(id TEXT PRIMARY KEY, fingerprint TEXT, result TEXT);
        CREATE TABLE IF NOT EXISTS captures(id TEXT PRIMARY KEY, owner TEXT, node TEXT, expires REAL, signal TEXT);
        CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY, value TEXT);
        CREATE TABLE IF NOT EXISTS confirmations(id TEXT PRIMARY KEY, responded INTEGER);
        ''')
        self.db.commit()
        self.last = {}

    def auth(self, token, scope, device=None):
        for name, expected, policy in self.principals:
            if secrets.compare_digest(token, expected):
                if scope not in policy['scopes'] or (device is not None and device not in policy['devices']):
                    break
                return name, policy
        raise PermissionError('unauthorized')

    def call(self, token, method, path, body=None):
        with self.lock:
            return self._call(token, method, path, body or {})

    def _call(self, token, method, path, b):
        if method == 'GET':
            who, policy = self.auth(token, 'read')
            if path == '/v1/devices':
                return {k: v for k, v in self.devices.items() if k in policy['devices']}
            if path in ('/v1/nodes', '/v1/status'):
                allowed = {self.devices[d]['node'] for d in policy['devices']}
                return {'transmission_disabled': self.disabled(), 'nodes': {k: {'transport': v['transport'], 'availability': 'simulated' if isinstance(self.transport, MockIR) else 'unknown', 'firmware_version': None, 'last_contact': None} for k, v in self.nodes.items() if k in allowed}}
            if path == '/v1/history':
                return [json.loads(r[0]) for r in self.db.execute('SELECT result FROM requests ORDER BY rowid DESC LIMIT 100') if json.loads(r[0])['device'] in policy['devices']]
            if path.startswith('/v1/signals/'):
                name = identity(path.split('/')[-1])
                allowed = {ref[8:] for d in policy['devices'] for ref in self.devices[d]['commands'].values()}
                if name not in allowed:
                    raise PermissionError('signal not authorized')
                row = self.db.execute('SELECT node,metadata FROM signals WHERE id=?', (name,)).fetchone()
                if not row:
                    raise ValueError('unknown signal')
                return {'name': name, 'node': row[0], 'metadata': json.loads(row[1])}
            if path.startswith('/v1/learn/'):
                row = self.capture(path.split('/')[-1], who)
                return {'capture_id': path.split('/')[-1], 'expires': row[2], 'signal': json.loads(row[3]) if row[3] else None, 'metadata': decode(json.loads(row[3])) if row[3] else None}
        if method == 'POST' and path == '/v1/execute':
            if set(b) != {'device', 'command', 'request_id'}:
                raise ValueError('expected device, command, request_id')
            who, _ = self.auth(token, 'execute', b['device'])
            rid = identity(b['request_id'])
            device = self.devices[b['device']]
            if b['command'] not in device['commands']:
                raise ValueError('unregistered command')
            fingerprint = hashlib.sha256(json.dumps([who, b], sort_keys=True).encode()).hexdigest()
            previous = self.db.execute('SELECT fingerprint,result FROM requests WHERE id=?', (rid,)).fetchone()
            if previous:
                if previous[0] != fingerprint:
                    raise ValueError('request_id conflict')
                return dict(json.loads(previous[1]), duplicate=True)
            if self.disabled():
                raise PermissionError('transmission disabled')
            node = device['node']
            if time.monotonic() - self.last.get(node, -10) < .5:
                raise ValueError('rate limited')
            row = self.db.execute('SELECT node,data FROM signals WHERE id=?', (device['commands'][b['command']][8:],)).fetchone()
            if not row or row[0] != node:
                raise ValueError('signal not learned for this node')
            signal = validate(json.loads(row[1]))
            result = dict(b, status='accepted', device_state='unverified')
            # Durable reservation BEFORE side effect: uncertain sends are never retried automatically.
            self.db.execute('INSERT INTO requests VALUES(?,?,?)', (rid, fingerprint, json.dumps(result)))
            self.db.commit()
            self.last[node] = time.monotonic()
            try:
                result['status'] = self.transport.transmit(node, signal, rid)
                if result['status'] not in ('simulated', 'transmitted'):
                    result['status'] = 'unknown'
            except Exception:
                result['status'] = 'unknown'
            self.db.execute('UPDATE requests SET result=? WHERE id=?', (json.dumps(result), rid))
            self.db.commit()
            logging.getLogger('euthercommand').info(json.dumps({'event': 'command_result', **result}))
            return result
        if method == 'POST' and path == '/v1/disable':
            self.auth(token, 'disable')
            if set(b) != {'disabled'} or type(b['disabled']) is not bool:
                raise ValueError('expected disabled boolean')
            self.db.execute("INSERT OR REPLACE INTO settings VALUES('disabled',?)", (str(int(b['disabled'])),))
            self.db.commit()
            return {'transmission_disabled': b['disabled']}
        if method == 'POST' and path == '/v1/learn':
            who, policy = self.auth(token, 'learn')
            if set(b) != {'node'} or b['node'] not in {self.devices[d]['node'] for d in policy['devices']}:
                raise ValueError('unauthorized node')
            self.db.execute('DELETE FROM captures WHERE expires < ?', (time.time(),))
            if self.db.execute('SELECT count(*) FROM captures').fetchone()[0] >= 16:
                raise ValueError('too many captures')
            cid = secrets.token_hex(16)
            self.db.execute('INSERT INTO captures VALUES(?,?,?,?,NULL)', (cid, who, b['node'], time.time()+60))
            self.db.commit()
            return {'capture_id': cid, 'status': 'learning', 'expires_in': 60}
        if method == 'POST' and path in ('/v1/learn/capture', '/v1/learn/save'):
            who, _ = self.auth(token, 'learn')
            expected = {'capture_id', 'signal'} if path.endswith('capture') else {'capture_id', 'name'}
            if set(b) != expected:
                raise ValueError('invalid learning fields')
            row = self.capture(b['capture_id'], who)
            if path.endswith('capture'):
                if row[3] is not None:
                    raise ValueError('capture already received')
                signal = validate(b['signal'])
                self.db.execute('UPDATE captures SET signal=? WHERE id=?', (json.dumps(signal), b['capture_id']))
                result = {'metadata': decode(signal), 'signal': signal}
            else:
                if not row[3]:
                    raise ValueError('no captured signal')
                name = identity(b['name'])
                self.db.execute('INSERT INTO signals VALUES(?,?,?,?)', (name, row[1], row[3], json.dumps(decode(json.loads(row[3])))))
                self.db.execute('DELETE FROM captures WHERE id=?', (b['capture_id'],))
                result = {'name': name, 'status': 'saved'}
            self.db.commit()
            return result
        if method == 'POST' and path == '/v1/confirm':
            if set(b) != {'request_id', 'responded'} or type(b['responded']) is not bool:
                raise ValueError('invalid confirmation')
            row = self.db.execute('SELECT result FROM requests WHERE id=?', (identity(b['request_id']),)).fetchone()
            if not row:
                raise ValueError('unknown request')
            self.auth(token, 'learn', json.loads(row[0])['device'])
            self.db.execute('INSERT OR REPLACE INTO confirmations VALUES(?,?)', (b['request_id'], b['responded']))
            self.db.commit()
            return {'request_id': b['request_id'], 'user_reported_response': b['responded'], 'device_state': 'unverified'}
        self.auth(token, 'read')
        raise ValueError('unknown operation')

    def capture(self, cid, owner):
        row = self.db.execute('SELECT owner,node,expires,signal FROM captures WHERE id=?', (identity(cid),)).fetchone()
        if not row or row[0] != owner or row[2] < time.time():
            raise ValueError('unknown or expired capture')
        return row

    def disabled(self):
        row = self.db.execute("SELECT value FROM settings WHERE key='disabled'").fetchone()
        return bool(row and row[0] == '1')
