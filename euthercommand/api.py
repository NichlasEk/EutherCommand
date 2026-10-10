"""Loopback-only development API; use an authenticated local TLS gateway for LAN."""
import argparse
import json
import logging
import pathlib
import os
import sqlite3
from http.server import BaseHTTPRequestHandler, HTTPServer
from .core import Core

MAX_BODY = 16384

def server(core, port=7425):
    class Handler(BaseHTTPRequestHandler):
        def setup(self):
            super().setup()
            self.connection.settimeout(5)
        def log_message(self, *args):
            pass  # No request bodies, credentials or URLs in logs.
        def do_GET(self):
            self.handle_call()
        def do_POST(self):
            self.handle_call()
        def handle_call(self):
            code = 200
            try:
                auth = self.headers.get('Authorization', '')
                token = auth[7:] if auth.startswith('Bearer ') else ''
                core.auth(token, 'read')
                if self.headers.get('Transfer-Encoding'):
                    raise ValueError('chunked bodies unsupported')
                size = int(self.headers.get('Content-Length', '0'))
                if not 0 <= size <= MAX_BODY:
                    raise ValueError('payload too large')
                data = self.rfile.read(size)
                if len(data) != size:
                    raise ValueError('incomplete body')
                body = json.loads(data) if size else {}
                if not isinstance(body, dict):
                    raise ValueError('JSON object required')
                result = core.call(token, self.command, self.path, body)
            except PermissionError:
                code, result = 403, {'error': 'unauthorized_or_disabled'}
            except (ValueError, KeyError, TypeError, sqlite3.IntegrityError):
                code, result = 400, {'error': 'invalid_request'}
            except TimeoutError:
                code, result = 408, {'error': 'timeout'}
            body = json.dumps(result).encode()
            self.send_response(code)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Connection', 'close')
            self.end_headers()
            self.wfile.write(body)
    return HTTPServer(('127.0.0.1', port), Handler)


def main():
    logging.basicConfig(level=logging.INFO, format='%(message)s')
    os.umask(0o077)
    parser = argparse.ArgumentParser()
    parser.add_argument('--config', default='euthercommand.toml')
    parser.add_argument('--database', default='.local/state.sqlite')
    parser.add_argument('--port', type=int, default=7425)
    parser.add_argument('--serial', help='Explicit USB device; enables physical transport')
    args = parser.parse_args()
    pathlib.Path(args.database).parent.mkdir(parents=True, exist_ok=True)
    transport = None
    if args.serial:
        from .serial_node import SerialNode
        transport = SerialNode(args.serial)
    core = Core(pathlib.Path(args.config).read_text(), args.database, transport)
    if args.serial and len(core.nodes) != 1:
        raise ValueError('USB bench transport supports exactly one configured node')
    print(json.dumps({'event': 'listening', 'address': '127.0.0.1', 'port': args.port, 'transport': 'usb' if args.serial else 'mock'}), flush=True)
    server(core, args.port).serve_forever()

if __name__ == '__main__':
    main()
