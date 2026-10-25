import json
class Marionette:
 def __init__(self,s):self.s=s;self.buf=b'';self.serial=0;self.greeting=self.receive()
 def receive(self):
  while b':' not in self.buf:
   block=self.s.recv(65536)
   if not block:raise RuntimeError('Marionette closed')
   self.buf+=block
  prefix,body=self.buf.split(b':',1);length=int(prefix)
  while len(body)<length:
   block=self.s.recv(65536)
   if not block:raise RuntimeError('Marionette closed')
   body+=block
  self.buf=body[length:];return json.loads(body[:length])
 def call(self,name,args=None):
  self.serial+=1;msg=json.dumps([0,self.serial,name,args or {}]).encode();self.s.sendall(str(len(msg)).encode()+b':'+msg)
  response=self.receive();assert response[0]==1 and response[1]==self.serial,response
  if response[2]:raise RuntimeError(json.dumps(response[2]))
  value=response[3];return value.get('value',value) if isinstance(value,dict) else value
 def script(self,source):return self.call('WebDriver:ExecuteScript',dict(script=source,args=[],newSandbox=True,sandbox='default'))
