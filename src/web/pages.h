#pragma once
#include <Arduino.h>

// All web UI is embedded as PROGMEM — no CDN/external assets, works fully
// offline on the local WiFi (AP or STA). Polling (REST), not WebSockets:
// simpler/more robust for a monitoring+debug tool where ~1s latency is fine.

namespace Pages {

// Shared compact style, inlined into every page's <style> block.
static const char kStyle[] PROGMEM = R"CSS(
body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:12px}
h1{font-size:1.1rem;margin:0 0 10px}
nav{margin-bottom:14px}
nav a{color:#9cf;margin-right:14px;text-decoration:none;font-size:.9rem}
nav a.active{color:#fff;font-weight:bold}
.card{background:#1b1b1b;border:1px solid #333;border-radius:8px;padding:10px 14px;margin-bottom:10px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:8px}
.stat{font-size:1.4rem;font-weight:bold}
.label{font-size:.75rem;color:#999;text-transform:uppercase}
.warn{color:#f5a623}
.bad{color:#e5484d}
.good{color:#3ecf8e}
table{width:100%;border-collapse:collapse;font-size:.8rem}
th,td{border-bottom:1px solid #2a2a2a;padding:4px 6px;text-align:left}
input,select,button{background:#222;color:#eee;border:1px solid #444;border-radius:4px;padding:6px 8px;margin:4px 0}
button{cursor:pointer;background:#2d6cdf;border-color:#2d6cdf}
button.secondary{background:#333;border-color:#444}
label{display:block;font-size:.8rem;margin-top:10px;color:#ccc}
canvas{width:100%;height:80px;background:#141414;border-radius:4px}
small{color:#888}
)CSS";

static const char kNav[] PROGMEM = R"HTML(
<nav>
<a href="/" id="nav-dash">Dashboard</a>
<a href="/can" id="nav-can">CAN Debug</a>
<a href="/logs" id="nav-logs">Logs</a>
<a href="/settings" id="nav-settings">Settings</a>
<a href="/update" id="nav-update">Firmware Update</a>
</nav>
)HTML";

static const char kDashboard[] PROGMEM = R"HTML(<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>BMZ Monitor</title><style>%STYLE%</style></head><body>
%NAV%
<h1>Battery status</h1>
<div class="card" id="protoNote"><small>Decoded via this pack's own CAN protocol (community-reverse-engineered, not an official datasheet) &mdash; each battery module appears as its own node below. See <a href="/can">CAN Debug</a> to check raw frames.</small></div>
<div class="card grid" id="stats">
  <div><div class="label">Bus</div><div class="stat" id="bus">--</div></div>
  <div><div class="label">Frames decoded</div><div class="stat" id="framesSeen">--</div></div>
</div>
<div id="nodesContainer"></div>
<div class="card">
  <div class="label">Pack Voltage (history, primary node)</div>
  <canvas id="chartV"></canvas>
</div>
<div class="card">
  <div class="label">Pack Power W, +charge/-discharge (history, primary node)</div>
  <canvas id="chartPower"></canvas>
</div>
<div class="card">
  <div class="label">SOC % (history, primary node)</div>
  <canvas id="chartSoc"></canvas>
</div>
<script>
document.getElementById('nav-dash').className='active';
function fmt(v,d,unit){return (v===null||v===undefined)?'--':v.toFixed(d)+unit;}

function drawSeries(canvas, series, minV, maxV, color){
  const ctx=canvas.getContext('2d');
  const w=canvas.width=canvas.clientWidth, h=canvas.height=canvas.clientHeight;
  ctx.clearRect(0,0,w,h);
  if(series.length<2) return;
  if(minV===undefined){minV=Math.min(...series);maxV=Math.max(...series);}
  if(maxV-minV<0.001){maxV=minV+1;}
  ctx.strokeStyle=color; ctx.lineWidth=1.5; ctx.beginPath();
  series.forEach((v,idx)=>{
    const x=idx/(series.length-1)*w;
    const y=h-((v-minV)/(maxV-minV))*h;
    idx===0?ctx.moveTo(x,y):ctx.lineTo(x,y);
  });
  ctx.stroke();
}

function renderNode(n){
  const cellRow=(n.cellVoltage||[]).map(v=>v.toFixed(3)).join(' ');
  const tempRow=(n.boardTemp||[]).map(t=>t.toFixed(1)).join(' / ');
  const cellTempRow=(n.cellTemp||[]).join(' ');
  return '<div class="card">'+
    '<div class="label">Battery node '+n.id+' '+
      '<span class="'+(n.heartbeat?'good':'bad')+'">'+(n.heartbeat?'alive':'SILENT')+'</span></div>'+
    '<div class="grid">'+
      '<div><div class="label">Pack V</div><div class="stat">'+fmt(n.packVoltage,3,' V')+'</div></div>'+
      '<div><div class="label">Power</div><div class="stat">'+fmt(n.packPowerW,0,' W')+'</div></div>'+
      '<div><div class="label">SOC (byte4/byte5)</div><div class="stat">'+
        (n.socByte4>=0?n.socByte4:'--')+' / '+(n.socByte5>=0?n.socByte5:'--')+' %</div></div>'+
      '<div><div class="label">Min/Max cell</div><div class="stat">'+fmt(n.minCellVoltage,3,'')+'/'+fmt(n.maxCellVoltage,3,' V')+'</div></div>'+
      '<div><div class="label">Relay</div><div class="stat '+(n.relayClosed?'good':'warn')+'">'+(n.relayClosed?'closed':'open')+'</div></div>'+
      '<div><div class="label">Faults</div><div class="stat '+(n.faultBits?'bad':'good')+'">0x'+(n.faultBits||0).toString(16)+'</div></div>'+
    '</div>'+
    (cellRow?'<small>Cells (V): '+cellRow+'</small><br>':'')+
    (tempRow?'<small>Board temp (C): '+tempRow+'</small><br>':'')+
    (cellTempRow?'<small>Cell temp (C): '+cellTempRow+'</small><br>':'')+
    '<small>Current limit: '+fmt(n.currentLimit,1,' A')+
      (n.balanceBitmask!==undefined?'  Balancing: 0x'+n.balanceBitmask.toString(16):'')+'</small>'+
  '</div>';
}

async function refreshStatus(){
  try{
    const r=await fetch('/api/status'); const s=await r.json();
    const bus=document.getElementById('bus');
    bus.textContent=s.canAlive?'alive':'SILENT';
    bus.className='stat '+(s.canAlive?'good':'bad');
    document.getElementById('framesSeen').textContent=s.framesSeen;
    const container=document.getElementById('nodesContainer');
    container.innerHTML=(s.nodes&&s.nodes.length)
      ? s.nodes.map(renderNode).join('')
      : '<div class="card"><small>No battery node frames decoded yet.</small></div>';
  }catch(e){}
}

async function refreshHistory(){
  try{
    const r=await fetch('/api/history'); const j=await r.json(); const h=j.samples;
    drawSeries(document.getElementById('chartV'), h.map(x=>x.v), undefined, undefined, '#3ecf8e');
    drawSeries(document.getElementById('chartPower'), h.map(x=>x.p), undefined, undefined, '#f5a623');
    drawSeries(document.getElementById('chartSoc'), h.map(x=>x.soc), 0, 100, '#9cf');
  }catch(e){}
}

refreshStatus(); refreshHistory();
setInterval(refreshStatus, 2000);
setInterval(refreshHistory, 30000);
</script>
</body></html>)HTML";

static const char kCanDebug[] PROGMEM = R"HTML(<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CAN Debug</title><style>%STYLE%</style></head><body>
%NAV%
<h1>Raw CAN frames</h1>
<div class="card">
  <label>Filter by ID (hex, comma separated, blank = all)
    <input id="filter" placeholder="351,355,356">
  </label>
  <button id="toggle">Pause</button>
  <button class="secondary" id="clear">Clear</button>
  <br><small id="counters"></small>
  <br><small id="busState"></small>
</div>
<div class="card">
<table><thead><tr><th>t (ms)</th><th>dir</th><th>id</th><th>dlc</th><th>data</th></tr></thead>
<tbody id="rows"></tbody></table>
</div>
<script>
document.getElementById('nav-can').className='active';
let since=0, paused=false, rows=[];
const MAX_ROWS=250;

function hex(n,w){return n.toString(16).padStart(w,'0');}

function matchesFilter(id){
  const f=document.getElementById('filter').value.trim();
  if(!f) return true;
  return f.split(',').map(s=>parseInt(s.trim(),16)).includes(id);
}

async function poll(){
  if(!paused){
    try{
      const r=await fetch('/api/can/frames?since='+since);
      const j=await r.json();
      since=j.seq;
      for(const f of j.frames){
        if(!matchesFilter(f.id)) continue;
        rows.push(f);
      }
      while(rows.length>MAX_ROWS) rows.shift();
      render();
      document.getElementById('counters').textContent=
        'rx='+j.rxCount+' tx='+j.txCount+' rxErr='+j.rxErrorCount+' txErr='+j.txErrorCount;
      const bs=document.getElementById('busState');
      bs.textContent='CAN bus state: '+j.busState;
      bs.className=(j.busState==='RUNNING')?'good':(j.busState==='BUS_OFF'?'bad':'warn');
    }catch(e){}
  }
  setTimeout(poll, 400);
}

function render(){
  const tbody=document.getElementById('rows');
  tbody.innerHTML='';
  for(let idx=rows.length-1; idx>=0; idx--){
    const f=rows[idx];
    const tr=document.createElement('tr');
    const dataHex=(f.data.match(/.{1,2}/g)||[]).join(' ');
    tr.innerHTML='<td>'+f.t+'</td><td>'+(f.rx?'RX':'TX')+'</td><td>0x'+hex(f.id,3)+
      '</td><td>'+f.dlc+'</td><td>'+dataHex+'</td>';
    tbody.appendChild(tr);
  }
}

document.getElementById('toggle').onclick=function(){
  paused=!paused; this.textContent=paused?'Resume':'Pause';
};
document.getElementById('clear').onclick=function(){ rows=[]; render(); };

poll();
</script>
</body></html>)HTML";

static const char kLogs[] PROGMEM = R"HTML(<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Logs</title><style>%STYLE%
#log{background:#141414;border-radius:4px;padding:8px;font-family:ui-monospace,monospace;font-size:.78rem;
  height:60vh;overflow-y:auto;white-space:pre-wrap;word-break:break-all}
</style></head><body>
%NAV%
<h1>Device logs</h1>
<div class="card">
  <button id="toggle">Pause</button>
  <button class="secondary" id="clear">Clear</button>
  <label><input type="checkbox" id="autoscroll" checked style="width:auto"> Auto-scroll</label>
</div>
<div class="card"><div id="log"></div></div>
<script>
document.getElementById('nav-logs').className='active';
let since=0, paused=false, lines=[];
const MAX_LINES=500;

async function poll(){
  if(!paused){
    try{
      const r=await fetch('/api/logs?since='+since);
      const j=await r.json();
      since=j.seq;
      for(const l of j.lines) lines.push(l);
      while(lines.length>MAX_LINES) lines.shift();
      render();
    }catch(e){}
  }
  setTimeout(poll, 500);
}

function render(){
  const el=document.getElementById('log');
  el.textContent=lines.map(l=>'['+l.t+'] '+l.m).join('\n');
  if(document.getElementById('autoscroll').checked) el.scrollTop=el.scrollHeight;
}

document.getElementById('toggle').onclick=function(){
  paused=!paused; this.textContent=paused?'Resume':'Pause';
};
document.getElementById('clear').onclick=function(){ lines=[]; render(); };

poll();
</script>
</body></html>)HTML";

static const char kSettings[] PROGMEM = R"HTML(<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Settings</title><style>%STYLE%</style></head><body>
%NAV%
<h1>Settings</h1>
<div class="card">
  <div class="label">WiFi</div>
  <label>SSID<input id="ssid"></label>
  <label>Password<input id="pass" type="password"></label>
</div>
<div class="card">
  <div class="label">CAN bus</div>
  <label>Bitrate
    <select id="bitrate">
      <option value="125000">125 kbit/s</option>
      <option value="250000">250 kbit/s</option>
      <option value="500000" selected>500 kbit/s (confirmed CAN-bus BMS profile)</option>
      <option value="1000000">1 Mbit/s</option>
    </select>
  </label>
  <small>Bitrate change requires a restart to take effect.</small>
</div>
<div class="card">
  <div class="label">Keep-alive (experimental)</div>
  <small class="warn">No officially published spec for this battery's keep-alive requirement &mdash;
  only that it shuts down ~10s without something from a GX/inverter. Defaults OFF.
  Enable and watch <a href="/can">CAN Debug</a> for the battery's reaction before trusting readings.</small>
  <label><input type="checkbox" id="kaEnabled" style="width:auto"> Enable keep-alive transmission</label>
  <label>CAN ID (hex)<input id="kaId" placeholder="305"></label>
  <label>Interval (ms)<input id="kaInterval" type="number"></label>
  <label>Payload bytes (hex, space separated, 0-8 bytes)<input id="kaPayload" placeholder="00 00 00 00 00 00 00 00"></label>
</div>
<div class="card">
  <div class="label">Admin password (optional, protects Settings &amp; Firmware Update)</div>
  <label>Password (blank = no password)<input id="adminPass" type="password"></label>
</div>
<button id="save">Save</button>
<button class="secondary" id="restart">Restart device</button>
<div id="msg"></div>
<script>
document.getElementById('nav-settings').className='active';

function bytesToHex(arr){return arr.map(b=>b.toString(16).padStart(2,'0')).join(' ');}
function hexToBytes(str){
  return str.trim().split(/\s+/).filter(x=>x.length).map(x=>parseInt(x,16)&0xff);
}

async function load(){
  const r=await fetch('/api/settings'); const s=await r.json();
  document.getElementById('ssid').value=s.wifiSsid;
  document.getElementById('bitrate').value=s.canBitrate;
  document.getElementById('kaEnabled').checked=s.keepaliveEnabled;
  document.getElementById('kaId').value=s.keepaliveCanId.toString(16);
  document.getElementById('kaInterval').value=s.keepaliveIntervalMs;
  document.getElementById('kaPayload').value=bytesToHex(s.keepalivePayload);
}

document.getElementById('save').onclick=async function(){
  const body={
    wifiSsid: document.getElementById('ssid').value,
    wifiPassword: document.getElementById('pass').value,
    canBitrate: parseInt(document.getElementById('bitrate').value,10),
    keepaliveEnabled: document.getElementById('kaEnabled').checked,
    keepaliveCanId: parseInt(document.getElementById('kaId').value||'305',16),
    keepaliveIntervalMs: parseInt(document.getElementById('kaInterval').value||'500',10),
    keepalivePayload: hexToBytes(document.getElementById('kaPayload').value),
    adminPassword: document.getElementById('adminPass').value
  };
  const r=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
  document.getElementById('msg').textContent = r.ok ? 'Saved.' : 'Save failed.';
};

document.getElementById('restart').onclick=async function(){
  if(!confirm('Restart the device now?')) return;
  await fetch('/api/restart',{method:'POST'});
  document.getElementById('msg').textContent='Restarting...';
};

load();
</script>
</body></html>)HTML";

static const char kUpdate[] PROGMEM = R"HTML(<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Firmware Update</title><style>%STYLE%</style></head><body>
%NAV%
<h1>Firmware update (FOTA)</h1>
<div class="card">
  <small>Upload a new firmware .bin built from this project. The device will apply it and reboot.
  Do not power off during the upload/flash.</small>
  <br><br>
  <input type="file" id="file" accept=".bin">
  <br><br>
  <button id="go">Upload &amp; Flash</button>
  <div class="card" id="progressWrap" style="display:none">
    <div class="label">Progress</div>
    <div id="progressText">0%</div>
  </div>
  <div id="msg"></div>
</div>
<script>
document.getElementById('nav-update').className='active';
document.getElementById('go').onclick=function(){
  const f=document.getElementById('file').files[0];
  if(!f){ alert('Choose a .bin file first'); return; }
  const xhr=new XMLHttpRequest();
  const fd=new FormData(); fd.append('firmware', f);
  document.getElementById('progressWrap').style.display='block';
  xhr.upload.onprogress=function(e){
    if(e.lengthComputable){
      const pct=Math.round(e.loaded/e.total*100);
      document.getElementById('progressText').textContent=pct+'%';
    }
  };
  xhr.onload=function(){
    document.getElementById('msg').textContent =
      xhr.status===200 ? 'Flashed OK. Rebooting...' : ('Failed: '+xhr.responseText);
  };
  xhr.onerror=function(){ document.getElementById('msg').textContent='Upload error.'; };
  xhr.open('POST','/update'); xhr.send(fd);
};
</script>
</body></html>)HTML";

} // namespace Pages
