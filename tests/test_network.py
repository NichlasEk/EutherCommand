import json
from pathlib import Path
import unittest
from unittest.mock import Mock
from euthercommand.network_node import NetworkNode

class NetworkTests(unittest.TestCase):
    def node(self):
        n=NetworkNode.__new__(NetworkNode)
        n.request=Mock(return_value={'status':'transmitted','boot_id':'boot123'})
        return n
    def test_registered_mapping_never_enables(self):
        n=self.node();signal=json.loads(Path('signals/logitech/volume_down.json').read_text())
        self.assertEqual(n.transmit('lab',signal,'id1'),'transmitted')
        self.assertEqual(n.request.call_count,2)
        self.assertEqual(n.request.call_args.args[2],{'boot_id':'boot123','command':'volume_down','request_id':'id1'})
    def test_raw_signal_or_other_node_rejected(self):
        n=self.node()
        for name in ('lab','other'):
            with self.assertRaises(ValueError):n.transmit(name,{'carrier_hz':38000,'durations_us':[500,500,500]},'id')
        n.request.assert_not_called()
    def test_no_retry_after_uncertain_result(self):
        n=self.node();n.request.side_effect=[{'boot_id':'boot123'},TimeoutError('timeout')]
        with self.assertRaises(TimeoutError):n.command('volume_down')
        self.assertEqual(n.request.call_count,2)
    def test_explicit_boot_does_not_refresh(self):
        n=self.node();n.command('disable','id','oldboot')
        self.assertEqual(n.request.call_count,1)
        self.assertEqual(n.request.call_args.args[2]['boot_id'],'oldboot')
    def test_arbitrary_operation_rejected(self):
        n=self.node()
        with self.assertRaises(ValueError):n.command('gpio26')
        n.request.assert_not_called()
