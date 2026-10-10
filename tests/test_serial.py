import json
import os
import pty
import select
import threading
import unittest
from euthercommand.serial_node import SerialNode
from euthercommand.ir import nec

class SerialTests(unittest.TestCase):
    def test_real_serial_adapter_against_pseudo_terminal(self):
        master, slave=pty.openpty()
        node=SerialNode(os.ttyname(slave))
        self.addCleanup(os.close,master); self.addCleanup(os.close,slave); self.addCleanup(node.close)
        seen=[]
        def firmware():
            buffer=b''
            for expected in ('save','arm','replay','disable'):
                while b'\n' not in buffer:
                    if not select.select([master],[],[],3)[0]: return
                    buffer+=os.read(master,8192)
                line,buffer=buffer.split(b'\n',1)
                cmd=json.loads(line); seen.append(cmd['op'])
                status={'save':'saved','arm':'armed','replay':'transmitted','disable':'disabled'}[expected]
                os.write(master,(json.dumps({'status': 'stale_wrong_reply', 'id': 'previous-command'})+'\n').encode())
                os.write(master,(json.dumps({'status':status, 'id':cmd['id']})+'\n').encode())
        thread=threading.Thread(target=firmware,daemon=True);thread.start()
        self.assertEqual(node.transmit('lab',nec(1,2),'serial-test'),'transmitted')
        thread.join(4)
        self.assertEqual(seen,['save','arm','replay','disable'])
    def test_physical_enable_denial_never_replays(self):
        master,slave=pty.openpty();node=SerialNode(os.ttyname(slave))
        self.addCleanup(os.close,master);self.addCleanup(os.close,slave);self.addCleanup(node.close)
        seen=[]
        def firmware():
            for response in ('saved','physical_enable_required','disabled'):
                line=b''
                while not line.endswith(b'\n'):
                    if not select.select([master],[],[],3)[0]:return
                    line+=os.read(master,8192)
                cmd=json.loads(line)
                seen.append(cmd['op'])
                os.write(master,(json.dumps({'status':response,'id':cmd['id']})+'\n').encode())
        thread=threading.Thread(target=firmware,daemon=True);thread.start()
        with self.assertRaises(PermissionError):node.transmit('lab',nec(1,2),'denied')
        thread.join(4);self.assertEqual(seen,['save','arm','disable'])
