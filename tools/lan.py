"""Explicit local control. Enable grants at most three frames within 60 seconds."""
import argparse
import json
from euthercommand.network_node import NetworkNode

def main():
    p=argparse.ArgumentParser()
    p.add_argument('operation',choices=['status','enable','disable','volume_up','volume_down'])
    p.add_argument('--config',default='.local/control/client.json')
    p.add_argument('--request-id')
    a=p.parse_args();node=NetworkNode(a.config)
    result=node.status() if a.operation=='status' else node.command(a.operation,a.request_id)
    print(json.dumps(result))
if __name__=='__main__':main()
