import unittest
import hashlib,hmac,time
from unittest.mock import patch,MagicMock
from euthercommand.network_node import NetworkNode

class FastTests(unittest.TestCase):
    def node(self):
        n=NetworkNode.__new__(NetworkNode)
        n.config={'host':'192.168.1.5','token':'test-only-token'}
        n.fast_clock=('1'*32,1000,time.monotonic(),True)
        return n
    def test_single_authenticated_datagram_without_receive_or_retry(self):
        n=self.node();sock=MagicMock()
        with patch('socket.socket') as factory:
            factory.return_value.__enter__.return_value=sock
            self.assertEqual(n.send_fast('volume_down',23),'dispatched')
        sock.sendto.assert_called_once();sock.recv.assert_not_called();sock.recvfrom.assert_not_called()
        packet,address=sock.sendto.call_args.args
        body,signature=packet.rsplit(b'|',1)
        self.assertEqual(signature,hmac.new(b'test-only-token',body,hashlib.sha256).hexdigest().encode())
        fields=body.decode().split('|')
        self.assertEqual(fields[:3],['EC1','1'*32,'0000000000000017'])
        self.assertEqual(fields[4],'down');self.assertTrue(1499<=int(fields[3],16)<=1600)
        self.assertEqual(address,('192.168.1.5',7426));self.assertNotIn(b'test-only-token',packet)
    def test_stale_disabled_and_unregistered_never_send(self):
        n=self.node()
        with patch('socket.socket') as factory:
            with self.assertRaises(ValueError):n.send_fast('power',1)
            n.fast_clock=('1'*32,1000,time.monotonic()-16,True)
            with self.assertRaises(RuntimeError):n.send_fast('volume_up',1)
            n.fast_clock=('1'*32,1000,time.monotonic(),False)
            with self.assertRaises(RuntimeError):n.send_fast('volume_up',1)
            factory.assert_not_called()
