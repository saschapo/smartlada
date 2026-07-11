#pragma once
#include <pgmspace.h>

// Single-page bench UI. Fully self-contained: inline CSS/JS, no CDN.
// Channel names/keys are duplicated in the JS constant CH — when changing the
// channel map in config.cpp, update here too.

namespace web {

static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SmartLada — PWM bench</title>
<style>
body{font-family:system-ui,sans-serif;margin:0 auto;padding:12px;background:#1b1e23;color:#e8e8e8;max-width:760px}
h1{font-size:1.3em;margin:8px 0}
.card{background:#262b33;border-radius:10px;padding:12px;margin:10px 0}
.card h2{font-size:1.05em;margin:0 0 4px}
.sub{color:#9aa3af;font-size:.85em;margin-bottom:8px}
.row{display:flex;align-items:center;gap:10px;margin:6px 0;flex-wrap:wrap}
input[type=range]{flex:1;min-width:160px}
input[type=number],textarea{background:#1b1e23;color:#e8e8e8;border:1px solid #444;border-radius:6px;padding:4px 6px}
input[type=number]{width:76px}
label{font-size:.85em;color:#c3cad4}
.cal{display:flex;gap:12px;flex-wrap:wrap;margin-top:6px}
.cal div{display:flex;flex-direction:column}
button{background:#3b82f6;border:0;color:#fff;padding:8px 14px;border-radius:8px;cursor:pointer;margin:4px 6px 0 0}
button.warn{background:#dc2626}
#status{font-size:.85em;color:#9aa3af;margin:8px 0}
#msg{color:#4ade80;font-size:.9em;min-height:1.2em}
textarea{width:100%;height:180px;font-family:monospace;font-size:.8em;box-sizing:border-box}
.capon{color:#fbbf24}.capoff{color:#f87171}
</style></head><body>
<h1>SmartLada — PWM bench (ESP8266)</h1>
<div id="status">loading…</div>
<div id="channels"></div>
<div class="card">
 <h2>Global parameters</h2>
 <div class="cal">
  <div><label>PWM, Hz (100–1000)</label><input type="number" id="freq" min="100" max="1000"></div>
  <div><label>Cap max duty, % (0–100)</label><input type="number" id="cap" min="0" max="100"></div>
 </div>
 <button onclick="apply()">Apply</button>
 <button class="warn" onclick="removeCap()">Remove cap (100%)</button>
</div>
<div class="card">
 <h2>Config export / import</h2>
 <textarea id="json" spellcheck="false"></textarea>
 <button onclick="exportJson()">Export JSON</button>
 <button onclick="importJson()">Import JSON</button>
</div>
<div id="msg"></div>
<script>
const CH=[{key:'stop',name:'Stop',w:21},{key:'reverse',name:'Reverse',w:21},
          {key:'turn',name:'Turn',w:21},{key:'marker',name:'Marker',w:5}];
const $=id=>document.getElementById(id);
let cfg=null;
function msg(t){$('msg').textContent=t;setTimeout(()=>{if($('msg').textContent===t)$('msg').textContent='';},3000);}
function render(){
 let h='';
 CH.forEach((c,i)=>{const o=cfg.channels[c.key];
  h+=`<div class="card"><h2>${c.name}</h2><div class="sub">CH${i} · ${c.key} · ${c.w} W · GPIO${o.gpio}</div>
  <div class="row"><input type="range" id="sl${i}" min="0" max="100" value="0" oninput="onSlider(${i},this.value)">
  <input type="number" id="pv${i}" min="0" max="100" value="0" onchange="onSlider(${i},this.value)"> %</div>
  <div class="cal">
   <div><label>gamma (1.8–2.6)</label><input type="number" id="g${i}" step="0.05" min="1.8" max="2.6" value="${o.gamma}"></div>
   <div><label>min_duty (0–1023)</label><input type="number" id="mn${i}" min="0" max="1023" value="${o.min_duty}"></div>
   <div><label>max_duty (0–1023)</label><input type="number" id="mx${i}" min="0" max="1023" value="${o.max_duty}"></div>
   <div><label>soft_start, ms (0–1000)</label><input type="number" id="ss${i}" min="0" max="1000" value="${o.soft_start_ms}"></div>
  </div></div>`;});
 $('channels').innerHTML=h;
 $('freq').value=cfg.pwm_freq_hz;$('cap').value=cfg.max_duty_cap_pct;
}
const pend={};
function onSlider(i,v){
 v=Math.max(0,Math.min(100,+v||0));
 $('sl'+i).value=v;$('pv'+i).value=v;
 sendPct(i,v);
}
async function sendPct(i,v){
 if(i in pend){pend[i]=v;return;}   // request in flight — remember the latest value
 pend[i]=v;
 while(i in pend){
  const val=pend[i];
  try{await fetch('/set?ch='+i+'&pct='+val,{method:'POST'});}catch(e){msg('no connection');}
  if(pend[i]===val)delete pend[i];
 }
}
function collect(){
 cfg.pwm_freq_hz=+$('freq').value;cfg.max_duty_cap_pct=+$('cap').value;
 CH.forEach((c,i)=>{const o=cfg.channels[c.key];
  o.gamma=+$('g'+i).value;o.min_duty=+$('mn'+i).value;
  o.max_duty=+$('mx'+i).value;o.soft_start_ms=+$('ss'+i).value;});
}
async function apply(){
 collect();
 const r=await fetch('/config',{method:'POST',body:JSON.stringify(cfg)}).catch(()=>null);
 msg(r&&r.ok?'Applied':'Apply error');
 await reload();
}
function removeCap(){
 if(confirm('Remove cap and allow 100% power? The transition goes through soft-start.')){
  $('cap').value=100;apply();
 }
}
async function exportJson(){
 $('json').value=await(await fetch('/config.json')).text();
 $('json').select();
}
async function importJson(){
 const r=await fetch('/config',{method:'POST',body:$('json').value}).catch(()=>null);
 msg(r&&r.ok?'Imported':'Import error');
 await reload();
}
async function reload(){
 cfg=await(await fetch('/config.json')).json();
 render();
 await refreshStatus(true);
}
async function refreshStatus(withSliders){
 try{
  const s=await(await fetch('/status')).json();
  const cap=s.max_duty_cap_pct;
  $('status').innerHTML=`MAC ${s.mac} · heap ${s.heap} · PWM ${s.pwm_freq_hz} Hz · cap: `+
   (cap<100?`<span class="capon">${cap}% active</span>`:`<span class="capoff">off (100%)</span>`);
  if(withSliders)s.pct.forEach((p,i)=>{$('sl'+i).value=p;$('pv'+i).value=p;});
 }catch(e){$('status').textContent='no connection to device';}
}
reload();
setInterval(()=>refreshStatus(false),5000);
</script></body></html>
)rawliteral";

}  // namespace web
