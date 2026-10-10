const fs=require('fs'),vm=require('vm'),assert=require('node:assert/strict');
const script=fs.readFileSync('firmware/main/setup.html','utf8').split('<script>')[1].split('</script>')[0];
async function trial(states, postOK=true, networkError=false){
 const elements=Object.fromEntries(['status','submit','form','ssid','password'].map(id=>['#'+id,{value:id==='ssid'?'Test network':'test-password',textContent:'',disabled:false}]));
 let calls=[];
 const ctx={document:{querySelector:id=>elements[id]},setTimeout:fn=>fn(),fetch:async(url,opts)=>{calls.push([url,opts]);if(networkError)throw Error('Offline');return url==='/configure'?{ok:postOK}:{json:async()=>states.shift()||{state:'connecting'}};}};
 vm.runInNewContext(script,ctx); await elements['#form'].onsubmit({preventDefault(){}});
 return {elements,calls};
}
(async()=>{
 let t=await trial([{state:'connecting'},{state:'saved',ip:'192.168.1.42'}]);
 assert.match(t.elements['#status'].textContent,/192.168.1.42/);assert.equal(t.elements['#password'].value,'');assert.equal(t.elements['#submit'].disabled,true);
 assert.deepEqual(JSON.parse(t.calls[0][1].body),{ssid:'Test network',password:'test-password'});
 assert.ok(t.calls[0][1].headers['X-Setup-Token']);
 t=await trial([{state:'failed'}]);assert.match(t.elements['#status'].textContent,/misslyckades/);assert.equal(t.elements['#submit'].disabled,false);
 t=await trial([],false);assert.match(t.elements['#status'].textContent,/Kunde inte/);assert.equal(t.calls.length,1);
 t=await trial([],true,true);assert.equal(t.elements['#submit'].disabled,false);
 t=await trial([]);assert.match(t.elements['#status'].textContent,/Ingen bekräftelse/);assert.equal(t.calls.length,41);
 console.log('PASS: saved, wrong credentials, rejected request, network loss, bounded timeout');
})().catch(e=>{console.error(e);process.exit(1)});
