/* Home Assistant WebSocket client: no third-party runtime dependencies. */
class HAClient {
  constructor(onStates,onStatus){this.onStates=onStates;this.onStatus=onStatus;this.pending=new Map();this.states={};this.id=0;this.ready=false;this.stopped=true;}
  connect(url,token){this.stop();const u=new URL(url);if(!['http:','https:'].includes(u.protocol)||u.username||u.password)throw Error('Dùng địa chỉ http:// hoặc https:// của HA.');u.protocol=u.protocol==='https:'?'wss:':'ws:';u.pathname='/api/websocket';u.search='';u.hash='';this.url=u.href;this.token=token;this.stopped=false;this.open();}
  open(){if(this.stopped)return;this.onStatus('connecting');this.id=0;this.states={};this.ready=false;const ws=this.ws=new WebSocket(this.url);let authTimer=setTimeout(()=>ws.close(),15000);
    ws.onmessage=async e=>{if(this.ws!==ws)return;let m;try{m=JSON.parse(e.data)}catch{return}
      if(m.type==='auth_required')ws.send(JSON.stringify({type:'auth',access_token:this.token}));
      else if(m.type==='auth_invalid'){this.stopped=true;this.onStatus('auth_invalid');ws.close();}
      else if(m.type==='auth_ok'){clearTimeout(authTimer);this.ready=true;try{this.initializing=true;this.buffer=[];await this.request({type:'subscribe_events',event_type:'state_changed'});const states=await this.request({type:'get_states'});if(this.ws!==ws)return;this.states=Object.fromEntries(states.map(s=>[s.entity_id,s]));for(const d of this.buffer)this.apply(d);this.initializing=false;this.onStates(this.states);this.onStatus('ready');this.heartbeat=setInterval(()=>this.request({type:'ping'}).catch(()=>ws.close()),25000);}catch{ws.close();}}
      else if(m.type==='result'||m.type==='pong'){const p=this.pending.get(m.id);if(p){this.pending.delete(m.id);clearTimeout(p.timer);if(m.success===false)p.reject(Error(m.error?.message||'HA từ chối lệnh'));else p.resolve(m.result);}}
      else if(m.type==='event'&&m.event?.event_type==='state_changed'){const d=m.event.data;if(this.initializing)this.buffer.push(d);else{this.apply(d);this.onStates(this.states);}}
    };
    ws.onclose=()=>{clearTimeout(authTimer);if(this.ws!==ws)return;this.ready=false;clearInterval(this.heartbeat);for(const p of this.pending.values()){clearTimeout(p.timer);p.reject(Error('Mất kết nối HA'));}this.pending.clear();if(!this.stopped){this.onStatus('offline');this.retry=setTimeout(()=>this.open(),4000);}};
    ws.onerror=()=>ws.close();
  }
  apply(d){if(d.new_state)this.states[d.entity_id]=d.new_state;else delete this.states[d.entity_id];}
  request(payload){return new Promise((resolve,reject)=>{if(!this.ready||this.ws?.readyState!==1){reject(Error('Chưa kết nối HA'));return;}const id=++this.id;const timer=setTimeout(()=>{this.pending.delete(id);reject(Error('HA không phản hồi sau 12 giây'));},12000);this.pending.set(id,{resolve,reject,timer});this.ws.send(JSON.stringify({id,...payload}));});}
  call(entity,service,data={}){return this.request({type:'call_service',domain:entity.split('.')[0],service,target:{entity_id:entity},service_data:data});}
  stop(){this.stopped=true;this.ready=false;clearTimeout(this.retry);clearInterval(this.heartbeat);if(this.ws){this.ws.onclose=null;this.ws.close();this.ws=null;}for(const p of this.pending.values()){clearTimeout(p.timer);p.reject(Error('Đã ngắt kết nối'));}this.pending.clear();}
}
if(typeof module!=='undefined')module.exports=HAClient;
