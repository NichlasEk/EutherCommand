"""Explicit USB bench trial: three registered Logitech volume-down commands.

No retries; stops on any non-transmitted result. Firmware independently enforces
one-shot arm, two-second arm expiry and a three-send-per-boot software bench limit.
"""
import json
import os
from pathlib import Path
import secrets
import time
import uuid
from euthercommand.core import Core
from euthercommand.serial_node import SerialNode


def main():
    os.environ.setdefault('EUTHERCOMMAND_OPERATOR_TOKEN', secrets.token_hex(32))
    os.environ.setdefault('EUTHERCOMMAND_VOX_TOKEN', secrets.token_hex(32))
    node = SerialNode('/dev/ttyUSB0')
    core = None
    try:
        # Opening a USB UART can reset the board through its control lines.
        # Allow boot to finish before the initial, non-transmitting handshake.
        time.sleep(2)
        if node.command({'op': 'disable'}) != 'disabled':
            raise RuntimeError('Unable to confirm disabled state')
        core = Core(Path('euthercommand.toml').read_text(), '.local/state.sqlite', node)
        token = os.environ['EUTHERCOMMAND_OPERATOR_TOKEN']
        print('READY: at most 3 volume-down frames; first in 5 seconds.', flush=True)
        time.sleep(5)
        trial = uuid.uuid4().hex[:16]
        for index in range(3):
            result = core.call(token, 'POST', '/v1/execute', {
                'device': 'logitech_audio', 'command': 'volume_down',
                'request_id': f'camera-{trial}-{index+1}'})
            print(json.dumps(result), flush=True)
            if result['status'] != 'transmitted':
                raise RuntimeError('Unconfirmed transmission: trial stopped, no retry')
            if index < 2:
                time.sleep(2)
    finally:
        try:
            print(json.dumps({'final_disable': node.command({'op': 'disable'})}), flush=True)
        finally:
            node.close()
            if core:
                core.db.close()

if __name__ == '__main__':
    main()
