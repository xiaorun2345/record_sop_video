const $=s=>document.querySelector(s),video=$('#camera-video');let streamFrame=null;
const params=new URLSearchParams(location.search);
const streamUrl=params.get('stream');
let mediaMtx=params.get('mediamtx');
// In deployment the processed stream is always exposed by MediaMTX on the
// same device.  Auto-detect it so the workbench shows the algorithm result
// without requiring a hand-written query string.
if(!streamUrl && !mediaMtx && location.protocol !== 'file:') {
  mediaMtx = `${location.protocol}//${location.hostname}:8889`;
}
if(!streamUrl && mediaMtx){
  const host=mediaMtx.replace(/\/$/,'');
  let currentStreamPath='';
  streamFrame=video; video.style.display='block';
  video.autoplay=true; video.playsInline=true;
  video._setStream=async(path)=>{ if(path===currentStreamPath && video._pc) return; if(video._pc) video._pc.close(); const pc=new RTCPeerConnection(); video._pc=pc; pc.ontrack=e=>{if(e.streams[0]&&video.srcObject!==e.streams[0]){video.srcObject=e.streams[0];video.play().catch(()=>{});}}; pc.addTransceiver('video',{direction:'recvonly'}); try { const offer=await pc.createOffer(); await pc.setLocalDescription(offer); await new Promise(r=>{if(pc.iceGatheringState==='complete')r();else pc.onicegatheringstatechange=()=>{if(pc.iceGatheringState==='complete')r();};}); const r=await fetch(host+'/'+path+'/whep',{method:'POST',headers:{'Content-Type':'application/sdp'},body:pc.localDescription.sdp}); if(!r.ok) throw new Error('WebRTC WHEP HTTP '+r.status); await pc.setRemoteDescription({type:'answer',sdp:await r.text()}); currentStreamPath=path; } catch(e) { if(video._pc===pc){pc.close();video._pc=null;currentStreamPath='';} throw e; } };
  video._clearStream=()=>{currentStreamPath=''; if(video._pc){video._pc.close();video._pc=null;} video.pause(); video.srcObject=null;};
  const badge=document.createElement('span'); badge.className='stream-live-badge'; badge.textContent='算法结果 · WebRTC'; video.parentElement.appendChild(badge);
}
if(streamUrl){video.pause();video.removeAttribute('src');video.src=streamUrl;video.controls=false;video.play().catch(()=>{});}
// 检测框和关键点由算法画面自行决定，工作台不再叠加静态示意图。
const play=$('#play-toggle');
const controlApi=(params.get('api') || `${location.protocol}//${location.hostname}:8080`).replace(/\/$/,'');
const landmarkVisibility=$('#landmark-visibility');
const landmarkVisibilityLabel=$('#landmark-visibility-label');
async function syncDeviceIdentity(){try{const s=await (await fetch(controlApi+'/api/device/overview',{cache:'no-store'})).json();const i=s.identity||{},name=i.device_name||'DenseAI Edge';const top=$('#top-device-name'),side=$('#sidebar-device-name'),station=$('#sidebar-station-id');if(top)top.textContent=name;if(side)side.textContent=i.device_id?`${name} · ${i.device_id}`:name;if(station)station.textContent=i.station_id||'未设置';}catch(e){}}
async function setLandmarkVisibility(enabled, revertOnError=true){
  if(!landmarkVisibility) return;
  try {
    const r=await fetch(controlApi+'/api/visualization/hand-landmarks',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({enabled})});
    const payload=await r.json().catch(()=>({}));
    if(!r.ok) throw new Error(payload.error||('HTTP '+r.status));
    landmarkVisibility.checked=Boolean(payload.hand_landmarks_visible);
    landmarkVisibilityLabel.textContent=landmarkVisibility.checked?'关键点可视化已启用':'关键点可视化已关闭';
  } catch(e) {
    if(revertOnError) landmarkVisibility.checked=!enabled;
    alert('关键点可视化设置失败：'+(e.message||'控制接口不可用'));
  }
}
landmarkVisibility?.addEventListener('change',()=>setLandmarkVisibility(landmarkVisibility.checked));
async function algorithmControl(action){
  if(!controlApi){ alert('未配置算法控制接口'); return false; }
  try {
    const resolution=$('#resolution-select')?.value || '640x480';
    const endpoint=action==='camera-start'?'/api/camera/start':action==='camera-stop'?'/api/camera/stop':'/api/algorithm/'+action;
    const r=await fetch(controlApi.replace(/\/$/,'')+endpoint,{method:'POST',headers:{'Content-Type':'application/json'},body:action==='start'||action==='camera-start'?JSON.stringify({resolution}):'{}'});
    let payload={}; try { payload=await r.json(); } catch(e) {}
    if(!r.ok) throw new Error(payload.error || ('HTTP '+r.status));
    return true;
  }
  catch(e){
    const isCamera=action.startsWith('camera-');
    const verb=action.endsWith('start')?'启动':'关闭';
    alert((isCamera?'摄像头':'算法')+verb+'失败：'+(e.message || '控制接口不可用'));
    return false;
  }
}
async function setAlgorithm(action){
  if(await algorithmControl(action)){
    const starting=action==='start'; play.innerHTML=starting?'■<small>关闭算法</small>':'▶<small>启动算法</small>';
    $('#job-start').disabled=starting; $('#job-pause').disabled=!starting;
    if(!starting && streamFrame) { streamFrame.src='about:blank'; }
    if(starting) waitForStream();
  }
}
async function setCamera(action){
  if(await algorithmControl(action)) sync();
}
async function waitForStream(){
  const deadline=Date.now()+30000;
  while(Date.now()<deadline){
    try { const s=await (await fetch(controlApi+'/api/algorithm/status',{cache:'no-store'})).json();
      if(s.algorithm==='running' && s.stream_ready){ if(streamFrame&&video._setStream) video._setStream('sop'); return; }
    } catch(e){}
    await new Promise(r=>setTimeout(r,500));
  }
  alert('算法已启动，但视频流尚未就绪，请稍后刷新页面');
}
play.onclick=()=>setAlgorithm(play.textContent.includes('启动算法')?'start':'stop');
$('#screenshot-button').onclick=()=>{
  if(streamFrame){ alert('WebRTC 嵌入流暂不支持跨域截图，请使用浏览器截图功能'); return; }
  const out=document.createElement('canvas'); out.width=video.videoWidth||640; out.height=video.videoHeight||480;
  out.getContext('2d').drawImage(video,0,0,out.width,out.height);
  const a=document.createElement('a'); a.download=`screenshot_${new Date().toISOString().replace(/[:.]/g,'-')}.png`; a.href=out.toDataURL('image/png'); a.click();
};
$('#record-button').onclick=async e=>{const b=e.currentTarget, active=b.classList.contains('recording'); try { const mode=$('#record-mode-select')?.value||'processed'; const r=await fetch(controlApi+'/api/recording/'+(active?'stop':'start'),{method:'POST',headers:{'Content-Type':'application/json'},body:active?'{}':JSON.stringify({mode})}); const p=await r.json(); if(!r.ok) throw new Error(p.error||('HTTP '+r.status)); b.classList.toggle('recording',!active); b.innerHTML=!active?'■<small>录制中</small>':'⦿<small>录制</small>'; if(!active&&p.recording_dir) b.title='保存位置：'+p.recording_dir; } catch(err){ alert('录像操作失败：'+(err.message||'控制接口不可用')); }};
$('.fullscreen').onclick=()=>$('.video-stage').requestFullscreen?.();$('#dismiss-alarm').onclick=()=>$('#alarm').classList.remove('show');
$('.soft-button').onclick=()=>alert('设备在线\nIP：'+location.hostname+'\nAPI：'+controlApi);
document.querySelectorAll('.nav-item').forEach(n=>n.onclick=e=>{if(n.dataset.nav!=='工作台'){e.preventDefault();location.href=n.getAttribute('href');return;}e.preventDefault();document.querySelectorAll('.nav-item').forEach(x=>x.classList.remove('active'));n.classList.add('active');});
document.querySelectorAll('.tab').forEach(t=>t.onclick=()=>{document.querySelectorAll('.tab').forEach(x=>x.classList.remove('active'));t.classList.add('active');if(t.textContent.includes('历史')){fetch(controlApi+'/api/algorithm/status').then(r=>r.json()).then(s=>alert('录像目录：'+s.recording_dir+'\n最近录像：\n'+(s.recordings||[]).join('\n'))).catch(()=>alert('无法读取录像列表'));}});
$('.config-button').onclick=()=>alert('检测配置由 config/sop_config.txt 管理；当前模型：YOLOv8s-Hand & Part');
document.querySelector('.video-controls .control-icon:not(#play-toggle):not(#screenshot-button):not(#record-button):not(.fullscreen)').onclick=e=>{const b=e.currentTarget;b.classList.toggle('recording');b.querySelector('small').textContent=b.classList.contains('recording')?'补光灯已开':'补光灯';};
$('.events a').onclick=e=>{e.preventDefault();alert('当前告警共 '+document.querySelectorAll('#event-list li').length+' 条');};
$('.notes button').onclick=()=>{const input=$('.notes input');if(input.value.trim()){localStorage.setItem('sop-note',input.value.trim());input.value='';alert('备注已保存');}else alert('请输入备注内容');};
$('#camera-start').onclick=()=>setCamera('camera-start');
$('#camera-stop').onclick=()=>setCamera('camera-stop');
$('#job-start').onclick=()=>setAlgorithm('start');
$('#job-pause').onclick=()=>setAlgorithm('stop');
$('#job-finish').onclick=()=>{setAlgorithm('stop');$('#step-title').textContent='作业已结束';$('#step-desc').textContent='本次检测记录已保存，可在数据管理中查看'};
function clock(){$('#system-time').textContent=new Intl.DateTimeFormat('zh-CN',{hour:'2-digit',minute:'2-digit',second:'2-digit'}).format(new Date())}setInterval(clock,1000);clock();setTimeout(()=>$('#alarm').classList.add('show'),3500);
async function sync(){try{const s=await (await fetch(controlApi+'/api/algorithm/status',{cache:'no-store'})).json();const camera=s.camera==='running',active=s.algorithm==='running';$('#camera-start').disabled=camera;$('#camera-stop').disabled=!camera;$('#job-start').disabled=!camera||active;$('#job-pause').disabled=!active;play.innerHTML=active?'■<small>关闭算法</small>':'▶<small>启动算法</small>';if(Number.isFinite(Number(s.fps))){$('#fps').textContent=Number(s.fps).toFixed(1);$('#metric-fps').textContent=Number(s.fps).toFixed(1);}if(Number.isFinite(Number(s.npu_latency_ms))){$('#metric-npu-latency').textContent=Number(s.npu_latency_ms).toFixed(1);}if(Number.isFinite(Number(s.cpu_temperature_c))){$('#metric-cpu-temperature').textContent=Number(s.cpu_temperature_c).toFixed(1);}if(landmarkVisibility&&typeof s.hand_landmarks_visible==='boolean'){landmarkVisibility.checked=s.hand_landmarks_visible;landmarkVisibilityLabel.textContent=s.hand_landmarks_visible?'关键点可视化已启用':'关键点可视化已关闭';}const rb=$('#record-button');if(rb&&s.recording&&s.recording!=='stopped'){rb.classList.add('recording');rb.innerHTML='■<small>录制中</small>';rb.title='保存位置：'+s.recording_dir;}if(streamFrame){const path=active?'sop':'raw';const ready=active?s.stream_ready:s.raw_stream_ready;if(camera&&ready&&video._setStream) video._setStream(path).catch(()=>{});if(!camera&&video._clearStream) video._clearStream();}}catch(e){}}setInterval(sync,1000);sync();
syncDeviceIdentity();setInterval(syncDeviceIdentity,3000);
const recordingsDialog=$('#recordings-dialog');
async function openRecordings(){recordingsDialog.hidden=false;const list=$('#recordings-list'),empty=$('#recordings-empty');list.innerHTML='';empty.textContent='正在加载录像列表…';try{const data=await (await fetch(controlApi+'/api/recordings',{cache:'no-store'})).json();const items=data.items||[];empty.textContent=items.length?'':'暂无已保存录像';items.forEach(item=>{const li=document.createElement('li');const when=new Date(item.modified_at*1000).toLocaleString('zh-CN');li.innerHTML=`<span>${item.filename}<br><small>${item.size_mb} MB · ${when}</small></span><button class="download">下载</button><button class="delete">删除</button>`;li.querySelector('.download').onclick=()=>{const a=document.createElement('a');a.href=controlApi+'/api/recordings/'+encodeURIComponent(item.filename)+'/download';a.download=item.filename;a.click()};li.querySelector('.delete').onclick=async()=>{if(!confirm('确认删除录像 '+item.filename+'？'))return;const r=await fetch(controlApi+'/api/recordings/'+encodeURIComponent(item.filename),{method:'DELETE'});if(r.ok)openRecordings();else alert('删除失败')};list.appendChild(li)})}catch(e){empty.textContent='录像列表加载失败：'+e.message}}
$('#recordings-close').onclick=()=>recordingsDialog.hidden=true;recordingsDialog.onclick=e=>{if(e.target===recordingsDialog)recordingsDialog.hidden=true};document.querySelector('#tab-history')?.addEventListener('click',e=>{e.preventDefault();e.stopImmediatePropagation();openRecordings()},true);
