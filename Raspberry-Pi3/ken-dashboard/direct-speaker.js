/* Direct Phicomm R1 control. Media uses AI BOX port 8082; volume uses port 8080. */
class DirectSpeaker {
  constructor(onUpdate,onStatus){this.onUpdate=onUpdate;this.onStatus=onStatus;this.host='';this.media=null;this.control=null;this.retry=null;this.state={playing:false,title:'AI BOX',artist:'Phicomm R1',volume:null,position:0,duration:0,results:[],playlists:[],libraryMode:'search'};}
  normalize(value){return String(value||'').trim().replace(/^wss?:\/\//i,'').replace(/^https?:\/\//i,'').replace(/\/.*$/,'').replace(/:\d+$/,'')}
  connect(value){const host=this.normalize(value);if(!host)throw Error('Chưa cấu hình địa chỉ loa Phicomm R1');if(this.host===host&&this.media?.readyState===1)return;this.close();this.host=host;this.onStatus('connecting');this.media=this.open(`ws://${host}:8082`,'media');this.control=this.open(`ws://${host}:8080`,'control')}
  open(url,kind){const ws=new WebSocket(url);ws.onopen=()=>{if(kind==='media'){clearTimeout(this.retry);this.onStatus('ready');this.sendMedia({action:'playback_state'})}else this.sendControl({type:'get_info'})};ws.onmessage=e=>this.receive(e);ws.onerror=()=>{};ws.onclose=()=>{if(kind==='media'){this.onStatus('offline');clearTimeout(this.retry);this.retry=setTimeout(()=>{if(this.host)this.connect(this.host)},3000)}};return ws}
  receive(event){try{let data=JSON.parse(event.data),changed=false;if(typeof data.data==='string'){try{data={...data,...JSON.parse(data.data)}}catch{}}const type=data.type||data.action;if(type==='playback_state'||type==='play_started'||type==='playback_update'||type==='get_info'){this.state.playing=Boolean(data.is_playing??data.playing??(data.play_status===1));this.state.title=data.title||data.media_title||data.song_name||this.state.title;this.state.artist=data.artist||data.media_artist||data.source||this.state.artist;const vol=Number(data.volume??data.vol),position=Number(data.position??data.position_seconds??data.current_time),duration=Number(data.duration??data.duration_seconds??data.total_time);if(Number.isFinite(vol))this.state.volume=vol>1?vol/100:vol;if(Number.isFinite(position))this.state.position=position;if(Number.isFinite(duration))this.state.duration=duration;changed=true}else if(type==='search_result'||type==='zing_result'||type==='playlist_result'){this.state.results=Array.isArray(data.songs)?data.songs:[];this.state.libraryMode='search';changed=true}else if(type==='playlist_list_result'){this.state.playlists=Array.isArray(data.playlists)?data.playlists:[];changed=true}if(changed)this.onUpdate(this.state)}catch{}}
  send(socket,payload){if(socket?.readyState!==1){this.onStatus('offline');return false}socket.send(JSON.stringify(payload));return true}
  sendMedia(payload){return this.send(this.media,payload)}
  sendControl(payload){return this.send(this.control,payload)}
  playPause(){this.sendMedia({action:this.state.playing?'pause':'resume'});this.state.playing=!this.state.playing;this.onUpdate(this.state)}
  previous(){this.sendMedia({action:'previous'})}
  next(){this.sendMedia({action:'next'})}
  setVolume(level){const vol=Math.round(Math.max(0,Math.min(1,level))*100);this.sendControl({type:'set_vol',vol});this.sendControl({type:'send_message',what:4,arg1:5,arg2:vol});this.state.volume=vol/100;this.onUpdate(this.state)}
  mute(){this.setVolume(0)}
  search(query){if(!String(query||'').trim())return;this.state.libraryMode='search';this.sendMedia({action:'search_songs',query:String(query).trim()})}
  showSearch(){this.state.libraryMode='search';this.onUpdate(this.state)}
  listPlaylists(){this.state.libraryMode='playlists';this.onUpdate(this.state);this.sendMedia({action:'playlist_list'})}
  playSong(id){if(id)this.sendMedia({action:'play_song',video_id:id})}
  playPlaylist(id){if(id!=null)this.sendMedia({action:'playlist_play',playlist_id:id})}
  seek(position){this.sendMedia({action:'seek',position:Number(position)||0});this.state.position=Number(position)||0;this.onUpdate(this.state)}
  close(){clearTimeout(this.retry);for(const ws of [this.media,this.control])if(ws){ws.onclose=null;ws.close()}this.media=null;this.control=null}
}
if(typeof module!=='undefined')module.exports=DirectSpeaker;
