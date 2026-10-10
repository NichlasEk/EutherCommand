"""Exclusive local USB bench connection. No shell, network or automatic retries."""
import fcntl
import json
import os
import select
import termios
import time
import uuid
from .ir import validate

class SerialNode:
    def __init__(self, path):
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        try:
            fcntl.flock(self.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            cfg = termios.tcgetattr(self.fd)
            cfg[0] = cfg[1] = cfg[3] = 0
            cfg[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
            cfg[4] = cfg[5] = termios.B115200
            cfg[6][termios.VMIN] = cfg[6][termios.VTIME] = 0
            termios.tcsetattr(self.fd, termios.TCSANOW, cfg)
            self.buffer = b''
        except Exception:
            os.close(self.fd)
            raise
    def close(self):
        os.close(self.fd)
    def receive(self, timeout=3):
        deadline = time.monotonic()+timeout
        while time.monotonic() < deadline:
            while b'\n' in self.buffer:
                line, self.buffer = self.buffer.split(b'\n', 1)
                try:
                    value = json.loads(line)
                    if isinstance(value, dict): return value
                except (ValueError, UnicodeDecodeError): pass
            if select.select([self.fd], [], [], max(0, deadline-time.monotonic()))[0]:
                chunk = os.read(self.fd, 4096)
                if not chunk: raise OSError('node disconnected')
                self.buffer += chunk
                if len(self.buffer) > 16384: raise ValueError('oversized node response')
        raise TimeoutError('node did not acknowledge')
    def command(self, body):
        # Discard stale capture/status before issuing a sequential bench command.
        termios.tcflush(self.fd, termios.TCIFLUSH)
        self.buffer = b''
        correlation_id = uuid.uuid4().hex
        data = json.dumps(dict(body, id=correlation_id), separators=(',', ':')).encode()+b'\n'
        if len(data) > 8192: raise ValueError('serial command too large')
        deadline = time.monotonic()+3
        while data:
            if time.monotonic() >= deadline: raise TimeoutError('serial write timeout')
            if select.select([], [self.fd], [], .1)[1]:
                count = os.write(self.fd, data)
                data = data[count:]
        while time.monotonic() < deadline:
            reply = self.receive(deadline-time.monotonic())
            if 'status' in reply and reply.get('id') == correlation_id:
                return reply['status']
        raise TimeoutError('missing status')
    def transmit(self, node, signal, request_id):
        validate(signal)
        if self.command({'op':'save','signal':signal}) != 'saved':
            raise ValueError('node save failed')
        try:
            if self.command({'op':'arm'}) != 'armed':
                raise PermissionError('physical enable required')
            return self.command({'op':'replay'})
        finally:
            # Best effort disarm; physical power switch remains authoritative.
            try: self.command({'op':'disable'})
            except (OSError, ValueError, TimeoutError): pass
