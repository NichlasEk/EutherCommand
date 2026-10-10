"""Bounded, receive-only serial monitor preserving raw boot/capture evidence."""
import argparse
import fcntl
import struct
import termios
import json
import os
from pathlib import Path
import select
import time
from euthercommand.serial_node import SerialNode
from euthercommand.ir import decode

p = argparse.ArgumentParser()
p.add_argument('--serial', default='/dev/ttyUSB0')
p.add_argument('--reset', action='store_true', help='Reset ESP32 via CP2102 RTS before listening')
p.add_argument('--seconds', type=int, default=180)
p.add_argument('--log', default='.local/validation/rx27-serial.log')
a = p.parse_args()
node = SerialNode(a.serial)
Path(a.log).parent.mkdir(parents=True, exist_ok=True)
buffer = b''
try:
    with open(a.log, 'ab', buffering=0) as log:
        if a.reset:
            fcntl.ioctl(node.fd, termios.TIOCMBIC, struct.pack('I', termios.TIOCM_DTR))
            fcntl.ioctl(node.fd, termios.TIOCMBIS, struct.pack('I', termios.TIOCM_RTS))
            time.sleep(.1)
            fcntl.ioctl(node.fd, termios.TIOCMBIC, struct.pack('I', termios.TIOCM_RTS))
        print('Listening; no transmit commands are sent.', flush=True)
        deadline = time.monotonic() + a.seconds
        while time.monotonic() < deadline:
            if not select.select([node.fd], [], [], min(1, max(0, deadline-time.monotonic())))[0]:
                continue
            chunk = os.read(node.fd, 4096)
            if not chunk:
                break
            log.write(chunk)
            buffer += chunk
            while b'\n' in buffer:
                line, buffer = buffer.split(b'\n', 1)
                try:
                    event = json.loads(line)
                    if event.get('event') == 'capture':
                        pulses = event.get('durations_us', [])
                        try:
                            metadata = decode({k: event[k] for k in ('carrier_hz', 'durations_us')})
                        except ValueError:
                            metadata = {'protocol': 'invalid_or_incomplete'}
                        print(json.dumps({'event': 'capture', 'count': len(pulses), 'metadata': metadata,
                                          'possibly_truncated': event.get('possibly_truncated') or len(pulses) >= 127 }), flush=True)
                    else:
                        print(line.decode(errors='replace'), flush=True)
                except (ValueError, AttributeError):
                    print(line.decode(errors='replace'), flush=True)
            if len(buffer) > 16384:
                print('Oversized serial line dropped', flush=True)
                buffer = b''
finally:
    node.close()
