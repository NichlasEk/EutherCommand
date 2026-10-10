"""Receive one physical capture and save a reviewable JSON file; never transmit."""
import argparse
import json
from pathlib import Path
import time
from euthercommand.serial_node import SerialNode
from euthercommand.ir import validate, decode

p=argparse.ArgumentParser()
p.add_argument('--serial',required=True)
p.add_argument('--output',default='.local/capture.json')
a=p.parse_args()
node=SerialNode(a.serial)
try:
    node.command({'op':'disable'})
    print('Press the remote once; waiting at most 60 seconds.')
    deadline=time.monotonic()+60
    while time.monotonic()<deadline:
        event=node.receive(deadline-time.monotonic())
        if event.get('event')!='capture': continue
        if event.get('possibly_truncated'): raise ValueError('capture may be truncated; rejected')
        signal=validate({k:event[k] for k in ('carrier_hz','durations_us')})
        Path(a.output).parent.mkdir(parents=True,exist_ok=True)
        Path(a.output).write_text(json.dumps(signal,indent=2)+'\n')
        print(json.dumps({'saved':a.output,'metadata':decode(signal),'carrier_source':event.get('carrier_source')},ensure_ascii=False))
        break
    else: raise TimeoutError('no capture')
finally: node.close()
