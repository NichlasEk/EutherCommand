"""Local CLI: python -m tools.client learn .local/capture.json amp_power"""
import argparse
import json
import os
import urllib.request

p=argparse.ArgumentParser()
p.add_argument('--port',type=int,default=7425)
s=p.add_subparsers(dest='op',required=True)
l=s.add_parser('learn'); l.add_argument('file'); l.add_argument('name'); l.add_argument('--node',default='lab')
e=s.add_parser('execute'); e.add_argument('device'); e.add_argument('command'); e.add_argument('request_id')
s.add_parser('status'); s.add_parser('history')
d=s.add_parser('disable'); d.add_argument('value',choices=['true','false'])
c=s.add_parser('confirm'); c.add_argument('request_id'); c.add_argument('responded',choices=['true','false'])
a=p.parse_args()
token=os.environ['EUTHERCOMMAND_OPERATOR_TOKEN']
def call(path,body=None):
    req=urllib.request.Request(f'http://127.0.0.1:{a.port}/v1/'+path,data=json.dumps(body).encode() if body is not None else None,headers={'Authorization':'Bearer '+token,'Content-Type':'application/json'})
    with urllib.request.urlopen(req,timeout=10) as r: return json.load(r)
if a.op=='learn':
    with open(a.file) as f: signal=json.load(f)
    capture=call('learn',{'node':a.node})['capture_id']
    print(json.dumps(call('learn/capture',{'capture_id':capture,'signal':signal})))
    result=call('learn/save',{'capture_id':capture,'name':a.name})
elif a.op=='execute': result=call('execute',{'device':a.device,'command':a.command,'request_id':a.request_id})
elif a.op=='disable': result=call('disable',{'disabled':a.value=='true'})
elif a.op=='confirm': result=call('confirm',{'request_id':a.request_id,'responded':a.responded=='true'})
else: result=call(a.op)
print(json.dumps(result,ensure_ascii=False,indent=2))
