#include "web.h"
#include <WebServer.h>
#include "wifinet.h"
#include "zigbee.h"
#include "../config/config.h"
#include "../fx/effects.h"
#include "../channels/channels.h"
#include "../power/power.h"
#include "../version.h"

extern uint32_t g_bootCount;   // defined in the .ino (RTC-persisted boot counter)

namespace web {

static WebServer  server(80);
static bool       s_running = false;

static const char* LAMP_NAME[4] = {"Turn", "Marker", "Reverse", "Stop"};

// ---- page (dark view, same visual language as the cineink device pages) ----
static const char PAGE[] PROGMEM = R"HTML(<!doctype html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SmartLada</title><style>
body{font-family:-apple-system,BlinkMacSystemFont,system-ui,'Helvetica Neue',Helvetica,Arial,sans-serif;
font-size:14px;max-width:440px;margin:28px auto;padding:0 16px;background:#000;color:#f2f2f2}
h1{font-size:1.9em;letter-spacing:3px;margin:0 0 6px;color:#fff;text-align:center}
.sub{text-align:center;color:#9a9a9a;font-size:.9em;margin-bottom:24px}
.section-header{font-size:1.1em;text-transform:uppercase;letter-spacing:2px;padding-bottom:8px;
border-bottom:1px solid #ccc;margin:26px 0 12px}
label{display:block;text-transform:uppercase;letter-spacing:1px;color:#cfcfcf;margin-bottom:3px}
.row{display:flex;align-items:center;gap:10px;margin-bottom:12px}
.row .nm{flex:0 0 92px;text-transform:uppercase;letter-spacing:1px;color:#cfcfcf}
.row .vl{flex:0 0 54px;text-align:right;color:#9a9a9a;font-variant-numeric:tabular-nums}
input[type=range]{flex:1;min-width:0;accent-color:#f2f2f2;background:transparent}
input[type=text],input[type=password],select{width:100%;padding:8px;background:#000;color:#fff;
border:1px solid #ccc;box-sizing:border-box;font-family:inherit;font-size:1em}
select:focus,input:focus{outline:none;border-color:#fff}
button{width:100%;padding:11px;background:#f2f2f2;color:#000;border:none;letter-spacing:2px;
cursor:pointer;margin-top:6px;font-family:inherit;font-size:1em}
button:active{transform:translateY(1px)}
.line{background:#000;color:#cfcfcf;border:1px solid #ccc}
.danger{background:#000;color:#f66;border:1px solid #f66}
.sw{flex:0 0 58px;padding:6px 0;text-align:center;border:1px solid #666;color:#8f8f8f;
letter-spacing:1px;cursor:pointer;user-select:none;font-size:.82em}
.sw.on{background:#f2f2f2;color:#000;border-color:#f2f2f2}
.ok{color:#5f5}.warn{color:#fb0}.err{color:#f66}
.debug{margin-top:28px;padding-top:12px;border-top:1px solid #ccc;color:#cfcfcf;line-height:1.7;
font-size:.92em}.debug b{color:#fff;font-weight:normal}
.field{margin-bottom:12px}.grid{display:grid;grid-template-columns:1fr 1fr;gap:0 12px}
</style></head><body>
<h1>SMARTLADA</h1><div class="sub" id="sub">connecting...</div>

<div class="section-header">Mode</div>
<select id="mode"></select>

<div class="section-header">Effect brightness</div>
<div class="row"><span class="nm">Master</span>
<input type="range" id="master" min="0" max="255"><span class="vl" id="masterv"></span></div>

<div class="section-header">Lamps</div><div id="lamps"></div>

<div class="section-header" id="timhdr">Timings</div><div id="tim"></div>

<div class="section-header">Lamp setup</div><div id="setup"></div>

<div class="section-header">Wi-Fi</div>
<div class="field"><label>Network</label><input type="text" id="ssid" placeholder="SSID"></div>
<div class="field"><label>Password</label><input type="password" id="pass"></div>
<button onclick="saveWifi()">CONNECT</button>
<button class="line" onclick="post('/api/forget')">FORGET NETWORK</button>

<div class="debug" id="dbg"></div>
<button class="danger" style="margin-top:16px" onclick="post('/api/reboot')">REBOOT</button>

<script>
var S=null,busy=0;
function post(u){busy=1;fetch(u,{method:'POST'}).then(()=>{busy=0;load()})}
function set(q){busy=1;fetch('/api/set?'+q,{method:'POST'}).then(()=>{busy=0;load()})}
function esc(s){return String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
function fmt(p){
  if(p.t==2)return p.v?'On':'Off';
  if(p.t==0)return (p.v/1000).toFixed(1)+'s';
  return p.v;
}
function row(nm,inner,vid,vtx){
  return '<div class="row"><span class="nm">'+esc(nm)+'</span>'+inner+
         '<span class="vl" id="'+vid+'">'+esc(vtx)+'</span></div>';
}
function render(){
  var s=S;
  document.getElementById('sub').innerHTML='v'+esc(s.ver)+' &middot; '+
    (s.zb?'<span class="ok">zigbee</span>':'<span class="warn">no zigbee</span>')+' &middot; '+
    (s.p12?'<span class="ok">12V</span>':'<span class="err">no 12V</span>');
  var m=document.getElementById('mode');
  if(m.options.length!=s.modes.length){
    m.innerHTML=s.modes.map((n,i)=>'<option value="'+i+'">'+esc(n)+'</option>').join('');
    m.onchange=()=>set('mode='+m.value);
  }
  m.value=s.mode;
  var mv=document.getElementById('master');
  if(document.activeElement!==mv)mv.value=s.master;
  document.getElementById('masterv').textContent=Math.round(s.master*100/255)+'%';
  mv.onchange=()=>set('master='+mv.value);

  var h='';
  for(var i=0;i<4;i++){var l=s.lamps[i];
    h+='<div class="row"><span class="nm">'+esc(l.n)+'</span>'+
       '<span class="sw'+(l.on?' on':'')+'" onclick="set(\'ch='+i+'&on='+(l.on?0:1)+'\')">'+
       (l.on?'ON':'OFF')+'</span>'+
       '<input type="range" min="0" max="255" value="'+l.b+'" onchange="set(\'ch='+i+'&bri=\'+this.value)">'+
       '<span class="vl">'+Math.round(l.b*100/255)+'%</span></div>';}
  document.getElementById('lamps').innerHTML=h;

  document.getElementById('timhdr').textContent=s.mode?('Timings - '+s.modes[s.mode]):'Timings';
  h='';
  if(!s.params.length)h='<div style="color:#9a9a9a">No timings in this mode.</div>';
  s.params.forEach(function(p,i){
    if(p.t==2)h+='<div class="row"><span class="nm">'+esc(p.n)+'</span>'+
      '<span class="sw'+(p.v?' on':'')+'" onclick="set(\'p='+i+'&v='+(p.v?0:1)+'\')">'+
      (p.v?'ON':'OFF')+'</span><span class="vl"></span></div>';
    else h+='<div class="row"><span class="nm">'+esc(p.n)+'</span>'+
      '<input type="range" min="'+p.mn+'" max="'+p.mx+'" step="'+p.st+'" value="'+p.v+
      '" onchange="set(\'p='+i+'&v=\'+this.value)"><span class="vl">'+esc(fmt(p))+'</span></div>';
  });
  document.getElementById('tim').innerHTML=h;

  var c=s.calib;
  h =row('Gamma','<input type="range" min="10" max="30" value="'+c.g+'" onchange="set(\'gamma=\'+this.value)">','g',(c.g/10).toFixed(1));
  h+=row('Soft start','<input type="range" min="0" max="3000" step="50" value="'+c.s+'" onchange="set(\'soft=\'+this.value)">','s',c.s+'ms');
  h+=row('Min level','<input type="range" min="0" max="50" value="'+c.mn+'" onchange="set(\'min=\'+this.value)">','mn',c.mn+'%');
  h+=row('Max level','<input type="range" min="50" max="100" value="'+c.mx+'" onchange="set(\'max=\'+this.value)">','mx',c.mx+'%');
  h+=row('PWM','<input type="range" min="100" max="30000" step="100" value="'+c.f+'" onchange="set(\'pwm=\'+this.value)">','f',(c.f/1000).toFixed(1)+'k');
  document.getElementById('setup').innerHTML=h;

  document.getElementById('ssid').placeholder=s.wifi.ssid||'SSID';
  document.getElementById('dbg').innerHTML=
    'wifi <b>'+esc(s.wifi.st)+'</b> '+esc(s.wifi.ip)+(s.wifi.rssi?' ('+s.wifi.rssi+' dBm)':'')+'<br>'+
    'zigbee <b>'+(s.zb?('pan 0x'+s.pan.toString(16)+' ch '+s.chan):'offline')+'</b><br>'+
    'vbus <b>'+(s.vbus/1000).toFixed(1)+'V</b> &middot; temp <b>'+s.temp.toFixed(1)+'C</b> &middot; up <b>'+
    Math.floor(s.up/1000)+'s</b> &middot; boot <b>#'+s.boot+'</b>';
}
function load(){if(busy)return;fetch('/api/state').then(r=>r.json()).then(j=>{S=j;render()}).catch(()=>{})}
function saveWifi(){
  var a=encodeURIComponent(document.getElementById('ssid').value);
  var b=encodeURIComponent(document.getElementById('pass').value);
  if(!a)return;
  busy=1;fetch('/api/wifi?ssid='+a+'&pass='+b,{method:'POST'}).then(()=>{busy=0});
  document.getElementById('sub').textContent='reconnecting to '+decodeURIComponent(a)+'...';
}
load();setInterval(load,2000);
</script></body></html>)HTML";

// ---- state ----
static void handleState() {
  String j; j.reserve(1400);
  j += "{\"ver\":\"" FW_VERSION "\"";
  j += ",\"mode\":" + String(config::s.mode);
  j += ",\"master\":" + String(config::s.master);
  j += ",\"modes\":[\"Static\"";
  for (uint8_t i = 0; i < fx::COUNT; i++) { j += ",\""; j += fx::EFFECTS[i].name; j += "\""; }
  j += "],\"lamps\":[";
  for (uint8_t i = 0; i < 4; i++) {
    if (i) j += ",";
    j += "{\"n\":\""; j += LAMP_NAME[i]; j += "\",\"on\":";
    j += String((config::s.lampOn >> i) & 1);
    j += ",\"b\":" + String(config::s.staticBri[i]) + "}";
  }
  j += "],\"params\":[";
  if (config::s.mode && config::s.mode <= fx::COUNT) {
    const fx::Effect& e = fx::EFFECTS[config::s.mode - 1];
    for (uint8_t i = 0; i < e.nparams; i++) {
      const fx::Param& p = e.params[i];
      if (i) j += ",";
      j += "{\"n\":\""; j += p.name; j += "\",\"t\":" + String((int)p.type);
      j += ",\"v\":" + String(p.value) + ",\"mn\":" + String(p.min);
      j += ",\"mx\":" + String(p.max) + ",\"st\":" + String(p.step) + "}";
    }
  }
  j += "],\"calib\":{\"g\":" + String(config::s.gammaX10) + ",\"s\":" + String(config::s.softMs);
  j += ",\"mn\":" + String(config::s.minLvl) + ",\"mx\":" + String(config::s.maxLvl);
  j += ",\"f\":" + String(config::s.pwmFreq) + "}";
  j += ",\"zb\":" + String(zb::connected() ? 1 : 0);
  j += ",\"pan\":" + String(zb::panId()) + ",\"chan\":" + String(zb::channel());
  j += ",\"p12\":" + String(power::present12V() ? 1 : 0);
  j += ",\"vbus\":" + String(power::vbusMv()) + ",\"temp\":" + String(temperatureRead(), 1);
  j += ",\"up\":" + String(millis()) + ",\"boot\":" + String(g_bootCount);
  const char* st = "off";
  switch (wifinet::state()) {
    case wifinet::CONNECTING: st = "connecting"; break;
    case wifinet::ONLINE:     st = "online";     break;
    case wifinet::AP:         st = "ap";         break;
    default: break;
  }
  j += ",\"wifi\":{\"st\":\""; j += st; j += "\",\"ip\":\"";
  j += wifinet::ip().toString(); j += "\",\"ssid\":\"";
  j += (wifinet::state() == wifinet::AP) ? wifinet::apSsid() : wifinet::staSsid();
  j += "\",\"rssi\":" + String(wifinet::rssi()) + "}}";
  server.send(200, "application/json", j);
}

static long argNum(const char* k, long def = -1) {
  return server.hasArg(k) ? server.arg(k).toInt() : def;
}

static void handleSet() {
  long v;
  if ((v = argNum("mode")) >= 0 && v <= fx::COUNT) config::s.mode = (uint8_t)v;
  if ((v = argNum("master")) >= 0) config::s.master = (uint8_t)constrain(v, 0, 255);

  long ch = argNum("ch");
  if (ch >= 0 && ch < 4) {
    if ((v = argNum("on")) >= 0) {
      if (v) config::s.lampOn |= (1 << ch); else config::s.lampOn &= ~(1 << ch);
    }
    if ((v = argNum("bri")) >= 0) config::s.staticBri[ch] = (uint8_t)constrain(v, 0, 255);
  }

  long pi = argNum("p");
  if (pi >= 0 && server.hasArg("v") && config::s.mode && config::s.mode <= fx::COUNT) {
    fx::Effect& e = fx::EFFECTS[config::s.mode - 1];
    if (e.params && pi < e.nparams) {
      fx::Param& p = e.params[pi];
      p.value = constrain(server.arg("v").toInt(), p.min, p.max);
      fx::saveParams();
    }
  }

  bool calib = false;
  if ((v = argNum("gamma")) >= 0) { config::s.gammaX10 = constrain(v, 10, 30); calib = true; }
  if ((v = argNum("min"))   >= 0) { config::s.minLvl   = constrain(v, 0, 50);  calib = true; }
  if ((v = argNum("max"))   >= 0) { config::s.maxLvl   = constrain(v, 50, 100);calib = true; }
  if (calib) channels::setCalib(config::s.gammaX10, config::s.minLvl, config::s.maxLvl);
  if ((v = argNum("soft"))  >= 0) { config::s.softMs = constrain(v, 0, 3000);
                                    channels::setSoftMs(config::s.softMs); }
  if ((v = argNum("pwm"))   >= 0) { config::s.pwmFreq = constrain(v, 100, 30000);
                                    channels::setFreq(config::s.pwmFreq); }
  server.send(200, "application/json", "{\"ok\":1}");   // config::tick() persists the settle
}

static void handleWifi() {
  if (!server.hasArg("ssid")) { server.send(400, "application/json", "{\"ok\":0}"); return; }
  String ssid = server.arg("ssid"), pass = server.arg("pass");
  server.send(200, "application/json", "{\"ok\":1}");
  wifinet::setCreds(ssid.c_str(), pass.c_str());   // answer first: this drops the link
}

void begin() {
  if (s_running) return;
  server.on("/", HTTP_GET, [] {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html", PAGE);
  });
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/set", HTTP_POST, handleSet);
  server.on("/api/wifi", HTTP_POST, handleWifi);
  server.on("/api/forget", HTTP_POST, [] {
    server.send(200, "application/json", "{\"ok\":1}"); wifinet::forget();
  });
  server.on("/api/reboot", HTTP_POST, [] {
    server.send(200, "application/json", "{\"ok\":1}"); delay(200); ESP.restart();
  });
  // Captive-portal style: anything unknown lands on the page, so joining the provisioning
  // AP and opening any address gets you the UI.
  server.onNotFound([] {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html", PAGE);
  });
  server.begin();
  s_running = true;
  Serial.println("[web] server on :80");
}

void update(uint32_t) { if (s_running) server.handleClient(); }
bool running() { return s_running; }

}  // namespace web
