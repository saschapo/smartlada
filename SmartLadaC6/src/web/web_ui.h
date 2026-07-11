#pragma once
#include <pgmspace.h>

// Single-page UI for the C6 bench. Fully self-contained: inline CSS/JS, no CDN.
// Design language — dark "instrument" theme (after the cineink layout): uppercase
// headers with a border, monochrome + status tokens, full-width buttons.
// Calibration is shared across all channels (identical lamps). CH/MODES are
// duplicated from config.cpp/anim.h — when changing there, update here too.

namespace web {

static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>SmartLada C6</title>
<style>
body{font-family:-apple-system,BlinkMacSystemFont,system-ui,'Helvetica Neue',Arial,sans-serif;font-size:14px;max-width:440px;margin:22px auto;padding:0 16px;background:#000;color:#f2f2f2}
h1{font-size:1.7em;letter-spacing:4px;margin:0 0 2px;color:#fff;text-align:center}
.sub{text-align:center;color:#8f8f8f;font-size:.78em;letter-spacing:1px;margin-bottom:16px}
#status{font-size:.78em;color:#cfcfcf;line-height:1.7;border:1px solid #333;padding:8px 11px;margin-bottom:22px}
#status b{color:#fff;font-weight:600}
.section{margin-bottom:24px}
.section-header{font-size:.8em;font-weight:700;color:#fff;text-transform:uppercase;letter-spacing:2px;padding-bottom:6px;border-bottom:1px solid #444;margin-bottom:12px;display:flex;justify-content:space-between;align-items:baseline}
.section-header .hn{font-weight:400;letter-spacing:.5px;color:#7f7f7f;font-size:.8em}
label{display:block;font-size:.7em;text-transform:uppercase;letter-spacing:.5px;color:#8f8f8f;margin-bottom:4px}
.row{display:flex;align-items:center;gap:12px;margin:6px 0}
input[type=range]{flex:1;min-width:0;accent-color:#f2f2f2;height:28px}
.val{width:3.4em;text-align:right;font-variant-numeric:tabular-nums;color:#fff;font-size:1.05em}
input[type=number]{width:100%;padding:7px;background:#000;color:#fff;border:1px solid #555;box-sizing:border-box;font-family:inherit;font-size:1em}
input[type=number]:focus{outline:none;border-color:#fff}
button{font-family:inherit;font-size:.9em;width:100%;padding:11px;background:#f2f2f2;color:#000;border:none;letter-spacing:1.5px;cursor:pointer;box-sizing:border-box}
button:active{transform:translateY(1px)}
button.line{background:#000;color:#cfcfcf;border:1px solid #555}
button.danger{background:#000;color:#f66;border:1px solid #f66}
.btn-row{display:flex;gap:10px}
.modes{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px}
.modes button{padding:13px 2px;letter-spacing:1px;font-size:.8em}
.mode-off{background:#000;color:#cfcfcf;border:1px solid #555}
.mode-on{background:#f2f2f2;color:#000;border:1px solid #f2f2f2}
.card{border:1px solid #2a2a2a;padding:10px 12px;margin-bottom:10px}
.card h3{margin:0;font-size:1em;color:#fff;font-weight:600}
.meta{color:#8f8f8f;font-size:.74em;letter-spacing:.5px;margin:2px 0 6px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:8px 12px}
.disabled{opacity:.35;pointer-events:none}
.flash{animation:fl 1s ease}
@keyframes fl{0%{box-shadow:0 0 0 2px #5f5}100%{box-shadow:none}}
#msg{min-height:1.2em;font-size:.85em;margin-top:4px}
.ok{color:#5f5}.warn{color:#fb0}.err{color:#f66}
textarea{width:100%;height:130px;background:#000;color:#cfcfcf;border:1px solid #555;font-family:monospace;font-size:.8em;box-sizing:border-box}
</style></head><body>
<h1>SMARTLADA</h1>
<div class="sub">C6 · <span id="ssid">—</span> · <span id="fw">—</span></div>
<div id="status">loading…</div>

<div class="section" id="msec">
 <div class="section-header">MASTER</div>
 <div class="row"><input type="range" id="master" min="0" max="100" value="0" oninput="onMaster(this.value)"><span class="val"><span id="masterv">0</span>%</span></div>
 <div class="btn-row"><button onclick="masterOn()">ON 100%</button><button class="line" onclick="masterOff()">OFF</button></div>
</div>

<div class="section">
 <div class="section-header">MODE <span class="hn">PWM dynamics test</span></div>
 <div class="modes" id="modes"></div>
</div>

<div class="section">
 <div class="section-header">CHANNELS</div>
 <div id="channels"></div>
</div>

<div class="section">
 <div class="section-header">CALIBRATION <span class="hn">shared for all lamps</span></div>
 <div class="grid">
  <div><label>gamma (1–2.5)</label><input type="number" id="g" step="0.05" min="1" max="2.5"></div>
  <div><label>soft_start, ms</label><input type="number" id="ss" min="0" max="1000"></div>
  <div><label>min_duty (0–1023)</label><input type="number" id="mn" min="0" max="1023"></div>
  <div><label>max_duty (0–1023)</label><input type="number" id="mx" min="0" max="1023"></div>
  <div><label>PWM, Hz (10–30000)</label><input type="number" id="freq" min="10" max="30000"></div>
  <div><label>Cap max duty, %</label><input type="number" id="cap" min="0" max="100"></div>
 </div>
 <div class="btn-row" style="margin-top:12px"><button onclick="apply()">APPLY</button><button class="danger" onclick="removeCap()">REMOVE CAP</button></div>
</div>

<div class="section">
 <div class="section-header">MODE TIMINGS <span class="hn">ms, 100–10000</span></div>
 <div class="grid">
  <div><label>CHASE (run), ms</label><input type="number" id="tchase" min="100" max="10000"></div>
  <div><label>SEQ (fill), ms</label><input type="number" id="tseq" min="100" max="10000"></div>
  <div><label>PULSE (breathe), ms</label><input type="number" id="tpulse" min="100" max="10000"></div>
  <div><label>ALT (even/odd), ms</label><input type="number" id="talt" min="100" max="10000"></div>
 </div>
 <div class="btn-row" style="margin-top:12px"><button onclick="apply()">APPLY</button></div>
</div>

<div class="section">
 <div class="section-header">CONFIG JSON</div>
 <textarea id="json" spellcheck="false"></textarea>
 <div class="btn-row" style="margin-top:8px"><button class="line" onclick="exportJson()">EXPORT</button><button class="line" onclick="importJson()">IMPORT</button></div>
</div>
<div id="msg"></div>

<script>
const CH=[{key:'stop',name:'Stop',w:10,gpio:0},{key:'reverse',name:'Reverse',w:10,gpio:1},
          {key:'turn',name:'Turn',w:10,gpio:2},{key:'marker',name:'Marker',w:10,gpio:3}];
const MODES=['MANUAL','TURN','CHASE','SEQ','PULSE','ALT','DRIVE'];
const $=id=>document.getElementById(id);
let cfg=null,curMode=0;
function msg(t,cls){const m=$('msg');m.className=cls||'ok';m.textContent=t;setTimeout(()=>{if(m.textContent===t)m.textContent='';},3000);}
function flash(el){el.classList.remove('flash');void el.offsetWidth;el.classList.add('flash');}

const pend={};
async function post(key,url){
 if(key in pend){pend[key]=url;return;}
 pend[key]=url;
 while(key in pend){const u=pend[key];try{await fetch(u,{method:'POST'});}catch(e){msg('no connection','err');}if(pend[key]===u)delete pend[key];}
}
function renderModes(){
 $('modes').innerHTML=MODES.map((m,i)=>`<button id="mode${i}" class="mode-off" onclick="onMode(${i})">${m}</button>`).join('');
}
function render(){
 $('channels').innerHTML=CH.map((c,i)=>`<div class="card"><h3>${c.name}</h3><div class="meta">CH${i} · ${c.key} · ${c.w} W · GPIO${c.gpio}</div>
  <div class="row"><input type="range" id="sl${i}" min="0" max="100" value="0" oninput="onSlider(${i},this.value)"><span class="val"><span id="pl${i}">0</span>%</span></div></div>`).join('');
 fillCalib();
}
function fillCalib(){$('freq').value=cfg.pwm_freq_hz;$('cap').value=cfg.max_duty_cap_pct;$('g').value=cfg.gamma;$('mn').value=cfg.min_duty;$('mx').value=cfg.max_duty;$('ss').value=cfg.soft_start_ms;$('tchase').value=cfg.chase_ms;$('tseq').value=cfg.seq_ms;$('tpulse').value=cfg.pulse_ms;$('talt').value=cfg.alt_ms;}
function onSlider(i,v){v=Math.max(0,Math.min(100,+v||0));$('sl'+i).value=v;$('pl'+i).textContent=v;post('ch'+i,'/set?ch='+i+'&pct='+v);setModeUI(0);}
function onMaster(v){v=Math.max(0,Math.min(100,+v||0));$('master').value=v;$('masterv').textContent=v;CH.forEach((c,i)=>{$('sl'+i).value=v;$('pl'+i).textContent=v;});post('all','/all?pct='+v);setModeUI(0);}
function masterOn(){onMaster(100);}
function masterOff(){onMaster(0);}
function onMode(id){post('mode','/mode?id='+id);setModeUI(id);}
function setModeUI(id){
 curMode=id;
 MODES.forEach((m,i)=>{const b=$('mode'+i);if(b)b.className=(i===id)?'mode-on':'mode-off';});
 const manual=(id===0);
 $('msec').classList.toggle('disabled',!manual);
 $('channels').classList.toggle('disabled',!manual);
}
function collect(){cfg.pwm_freq_hz=+$('freq').value;cfg.max_duty_cap_pct=+$('cap').value;cfg.gamma=+$('g').value;cfg.min_duty=+$('mn').value;cfg.max_duty=+$('mx').value;cfg.soft_start_ms=+$('ss').value;cfg.chase_ms=+$('tchase').value;cfg.seq_ms=+$('tseq').value;cfg.pulse_ms=+$('tpulse').value;cfg.alt_ms=+$('talt').value;}
async function apply(){
 collect();
 let ok=false;
 try{const r=await fetch('/config',{method:'POST',body:JSON.stringify(cfg)});if(r.ok){cfg=await r.json();fillCalib();ok=true;}}catch(e){}
 msg(ok?'Applied':'Apply error',ok?'ok':'err');
 if(ok)flash($('g'));
 refreshStatus(false);
}
function removeCap(){if(confirm('Remove cap and allow 100%?')){$('cap').value=100;apply();}}
async function exportJson(){$('json').value=await(await fetch('/config.json')).text();$('json').select();}
async function importJson(){
 let ok=false;
 try{const r=await fetch('/config',{method:'POST',body:$('json').value});if(r.ok){cfg=await r.json();fillCalib();ok=true;}}catch(e){}
 msg(ok?'Imported':'Import error',ok?'ok':'err');
 refreshStatus(false);
}
async function reload(){cfg=await(await fetch('/config.json')).json();render();refreshStatus(true);}
async function refreshStatus(withSliders){
 try{const s=await(await fetch('/status')).json();
  $('ssid').textContent=s.ssid;$('fw').textContent='fw '+s.fw;
  const cap=s.max_duty_cap_pct;
  $('status').innerHTML=`clients <b>${s.clients}</b> · heap <b>${s.heap}</b> · PWM <b>${s.pwm_freq_hz}</b> Hz · `+
   `cap <b class="${cap<100?'warn':'ok'}">${cap<100?cap+'%':'off'}</b> · mode <b>${MODES[s.mode]||s.mode}</b>`;
  setModeUI(s.mode);
  if(withSliders&&s.mode===0)s.pct.forEach((p,i)=>{if($('sl'+i)){$('sl'+i).value=p;$('pl'+i).textContent=p;}});
 }catch(e){$('status').textContent='no connection to device';}
}
renderModes();reload();
setInterval(()=>refreshStatus(false),5000);
</script></body></html>
)rawliteral";

}  // namespace web
