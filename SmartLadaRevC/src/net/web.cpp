#include "web.h"
#include <WebServer.h>
#include "wifinet.h"
#include "zigbee.h"
#include "../config/config.h"
#include "../fx/effects.h"
#include "../channels/channels.h"
#include "../power/power.h"
#include "radio.h"
#include "../display/display.h"
#include <WiFi.h>
#include <Preferences.h>
#include <Update.h>
#include <LittleFS.h>
#include "../log/eventlog.h"
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
:root{--fg:#f2f2f2;--dim:#9a9a9a;--line:#ccc}
*{box-sizing:border-box}
body{font-family:-apple-system,BlinkMacSystemFont,system-ui,'Helvetica Neue',Helvetica,Arial,sans-serif;
font-size:15px;max-width:460px;margin:24px auto 48px;padding:0 16px;background:#000;color:var(--fg)}
/* the device's own screen, echoed: a pixel face and its uppercase spacing */
.oled{font-family:ui-monospace,'SF Mono',Menlo,Consolas,monospace;letter-spacing:2px;
text-transform:uppercase}
h1{font-size:1.7em;margin:0 0 4px;text-align:center}
.sub{text-align:center;color:var(--dim);font-size:.85em;margin-bottom:22px}
.hdr{font-size:1em;padding-bottom:7px;border-bottom:1px solid var(--line);margin:26px 0 14px;
color:var(--fg)}
label{display:block;font-size:.85em;color:#cfcfcf;text-transform:uppercase;letter-spacing:1px;
margin-bottom:4px}
.row{display:flex;align-items:center;gap:12px;margin-bottom:6px}
.row .nm{flex:0 0 88px;font-size:.85em;text-transform:uppercase;letter-spacing:1px;color:#cfcfcf}
.row .vl{flex:0 0 56px;text-align:right;color:var(--dim);font-variant-numeric:tabular-nums;
font-family:ui-monospace,Menlo,monospace}
/* big, grabbable sliders: 44 px of touch height and a fat thumb */
input[type=range]{flex:1;min-width:0;-webkit-appearance:none;appearance:none;height:44px;
background:transparent;margin:0}
input[type=range]::-webkit-slider-runnable-track{height:6px;background:#333;border-radius:3px}
input[type=range]::-moz-range-track{height:6px;background:#333;border-radius:3px}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:30px;height:30px;
border-radius:50%;background:var(--fg);margin-top:-12px;border:none}
input[type=range]::-moz-range-thumb{width:30px;height:30px;border-radius:50%;background:var(--fg);
border:none}
input[type=text],input[type=password],select{width:100%;padding:11px;background:#000;color:#fff;
border:1px solid var(--line);font-family:inherit;font-size:1em}
select:focus,input:focus{outline:none;border-color:#fff}
button{width:100%;padding:13px;background:var(--fg);color:#000;border:none;letter-spacing:2px;
cursor:pointer;margin-top:8px;font-family:inherit;font-size:1em}
button:active{transform:translateY(1px)}
.line{background:#000;color:#cfcfcf;border:1px solid var(--line)}
.danger{background:#000;color:#f66;border:1px solid #f66}
.sw{flex:0 0 66px;height:38px;display:flex;align-items:center;justify-content:center;
border:1px solid #666;color:#8f8f8f;font-size:.8em;letter-spacing:1px;cursor:pointer;
user-select:none}
.sw.on{background:var(--fg);color:#000;border-color:var(--fg)}
.ok{color:#5f5}.warn{color:#fb0}.err{color:#f66}
.dbg{margin-top:26px;padding-top:12px;border-top:1px solid var(--line);color:#cfcfcf;
font-size:.85em;line-height:1.75;font-family:ui-monospace,Menlo,monospace;
word-break:break-all}
.dbg b{color:#fff;font-weight:normal}
.note{color:var(--dim);font-size:.85em;margin:6px 0 0}
.field{margin-bottom:14px}
.btnlink{text-decoration:none;display:block}
</style></head><body>
<h1 class="oled">SmartLADA</h1><div class="sub" id="sub">connecting...</div>

<div class="hdr oled">Mode</div>
<select id="mode"></select>

<div class="hdr oled">Master</div>
<div id="master"></div>
<div class="note">Acts as a ceiling: at 100% nothing is held back, lower it and the whole
picture comes down while the lamps keep their balance.</div>

<div class="hdr oled">Lamps</div><div id="lamps"></div>

<div class="hdr oled" id="timhdr">Timings</div><div id="tim"></div>

<div class="hdr oled">Lamp setup</div><div id="setup"></div>

<div class="hdr oled">Display</div><div id="disp"></div>

<div class="hdr oled">Access point</div>
<div class="field"><label>Name</label><input type="text" id="apssid"></div>
<div class="field"><label>Password (min 8)</label><input type="text" id="appass"></div>
<button onclick="saveAp()">SAVE ACCESS POINT</button>
<div class="note">Saving restarts the access point - you will have to reconnect.</div>

<div class="hdr oled">Radio</div>
<div class="note" id="radionote"></div>
<button class="line" onclick="toZigbee()">SWITCH TO ZIGBEE</button>
<button class="line" onclick="post('/api/wifireset')">WI-FI RESET</button>

<div class="hdr oled">Log</div>
<div class="row"><span class="nm">Size</span><span class="vl" id="logsz"></span></div>
<a class="btnlink" href="/log" download><button class="line">DOWNLOAD LOG</button></a>
<a class="btnlink" href="/log.1" download><button class="line">DOWNLOAD PREVIOUS</button></a>
<button class="line" onclick="post('/clearlog')">CLEAR LOG</button>

<div class="hdr oled">Firmware over Wi-Fi</div>
<form method="POST" action="/firmware" enctype="multipart/form-data">
<input type="file" name="fw" accept=".bin" style="width:100%;padding:10px 0">
<button type="submit">UPLOAD AND REBOOT</button></form>
<div class="note">Pick the compiled .bin. The board writes it to the spare slot and reboots.</div>

<div class="hdr oled">Maintenance</div>
<div class="row"><span class="nm">Force out</span><span class="sw" id="forcesw"></span>
<span class="vl"></span></div>
<button class="danger" onclick="post('/api/reboot')">REBOOT</button>
<button class="danger" onclick="if(confirm('Erase all settings?'))post('/api/factory')">FACTORY RESET</button>

<div class="dbg" id="dbg"></div>

<script>
var S=null,hold=0,pend={};
function post(u){fetch(u,{method:'POST'}).then(()=>setTimeout(load,300))}
// Send at most every 120 ms while dragging; the label follows the finger immediately.
function send(k,v){pend[k]=v;if(send.t)return;send.t=setTimeout(function(){
  var q=Object.keys(pend).map(k=>k+'='+pend[k]).join('&');pend={};send.t=0;
  fetch('/api/set?'+q,{method:'POST'});},120);}
function esc(s){return String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
function pc(v){return Math.round(v*100/255)+'%'}
function fmtP(p){return p.t==2?(p.v?'On':'Off'):(p.t==0?(p.v/1000).toFixed(1)+'s':p.v)}
// A slider that reports live: oninput repaints the number and queues a throttled write.
function sl(nm,key,min,max,step,val,txt){
  return '<div class="row"><span class="nm">'+esc(nm)+'</span>'+
   '<input type="range" min="'+min+'" max="'+max+'" step="'+step+'" value="'+val+'" '+
   'oninput="live(this,\''+key+'\')" onpointerdown="hold=1" onpointerup="hold=0" '+
   'ontouchstart="hold=1" ontouchend="hold=0">'+
   '<span class="vl">'+esc(txt)+'</span></div>';
}
function live(el,key){
  var v=+el.value,t=el.parentNode.querySelector('.vl');
  if(key=='master'||key.slice(0,4)=='bri:')t.textContent=pc(v);
  else if(key.slice(0,2)=='p:'){var p=S.params[+key.slice(2)];t.textContent=fmtP({t:p.t,v:v});}
  else if(key=='gamma')t.textContent=(v/10).toFixed(1);
  else if(key=='soft')t.textContent=v+'ms';
  else if(key=='pwm')t.textContent=(v/1000).toFixed(1)+'k';
  else if(key=='disp')t.textContent=pc(v);
  else if(key=='dim'||key=='off')t.textContent=v?v+'s':'off';
  else t.textContent=v+'%';
  if(key.slice(0,4)=='bri:')send('ch',key.slice(4)),send('bri',v);
  else if(key.slice(0,2)=='p:')send('p',key.slice(2)),send('v',v);
  else send(key,v);
}
function render(){
  var s=S;
  document.getElementById('sub').innerHTML='v'+esc(s.ver)+' &middot; '+esc(s.radio)+' &middot; '+
    (s.p12?'<span class="ok">12V</span>':'<span class="err">no 12V</span>');
  var m=document.getElementById('mode');
  if(m.options.length!=s.modes.length){
    m.innerHTML=s.modes.map((n,i)=>'<option value="'+i+'">'+esc(n)+'</option>').join('');
    m.onchange=()=>{fetch('/api/set?mode='+m.value,{method:'POST'}).then(()=>setTimeout(load,250))};
  }
  m.value=s.mode;
  document.getElementById('master').innerHTML=sl('Master','master',0,255,1,s.master,pc(s.master));
  var h='';
  for(var i=0;i<4;i++){var l=s.lamps[i];
    h+='<div class="row"><span class="nm">'+esc(l.n)+'</span>'+
      '<span class="sw'+(l.on?' on':'')+'" onclick="post(\'/api/set?ch='+i+'&on='+(l.on?0:1)+'\')">'+
      (l.on?'ON':'OFF')+'</span>'+
      '<input type="range" min="0" max="255" value="'+l.b+'" oninput="live(this,\'bri:'+i+'\')" '+
      'onpointerdown="hold=1" onpointerup="hold=0"><span class="vl">'+pc(l.b)+'</span></div>';}
  document.getElementById('lamps').innerHTML=h;

  document.getElementById('timhdr').textContent=s.mode?('Timings - '+s.modes[s.mode]):'Timings';
  h=s.params.length?'':'<div class="note">No timings in this mode.</div>';
  s.params.forEach(function(p,i){
    if(p.t==2)h+='<div class="row"><span class="nm">'+esc(p.n)+'</span>'+
      '<span class="sw'+(p.v?' on':'')+'" onclick="post(\'/api/set?p='+i+'&v='+(p.v?0:1)+'\')">'+
      (p.v?'ON':'OFF')+'</span><span class="vl"></span></div>';
    else h+=sl(p.n,'p:'+i,p.mn,p.mx,p.st,p.v,fmtP(p));
  });
  document.getElementById('tim').innerHTML=h;

  var c=s.calib;
  h =sl('Gamma','gamma',10,30,1,c.g,(c.g/10).toFixed(1));
  h+=sl('Soft start','soft',0,3000,50,c.s,c.s+'ms');
  h+=sl('Min level','min',0,50,1,c.mn,c.mn+'%');
  h+=sl('Max level','max',50,100,1,c.mx,c.mx+'%');
  h+=sl('PWM','pwm',100,30000,100,c.f,(c.f/1000).toFixed(1)+'k');
  document.getElementById('setup').innerHTML=h;

  var d=s.disp;
  h =sl('Contrast','disp',3,255,1,d.b,pc(d.b));
  h+=sl('Dim after','dim',0,120,5,d.dim,d.dim?d.dim+'s':'off');
  h+=sl('Off after','off',0,3600,30,d.off,d.off?d.off+'s':'off');
  document.getElementById('disp').innerHTML=h;

  document.getElementById('logsz').textContent=s.log+' B';
  var f=document.getElementById('forcesw');
  f.className='sw'+(s.force?' on':'');f.textContent=s.force?'ON':'OFF';
  f.onclick=()=>post('/api/set?force='+(s.force?0:1));

  if(document.activeElement.id!='apssid')document.getElementById('apssid').value=s.wifi.ap;
  if(document.activeElement.id!='appass')document.getElementById('appass').value=s.wifi.appass;
  document.getElementById('radionote').textContent=
    'The board has one 2.4 GHz radio. Switching to Zigbee stops Wi-Fi and reboots: '+
    'this page will disconnect and the lamp returns to the app.';

  document.getElementById('dbg').innerHTML=
   'fw <b>'+esc(s.ver)+'</b> hw <b>'+esc(s.hw)+'</b><br>'+
   'radio <b>'+esc(s.radio)+'</b> &middot; wifi <b>'+esc(s.wifi.st)+'</b> '+esc(s.wifi.ip)+
   (s.wifi.rssi?' <b>'+s.wifi.rssi+'</b> dBm':'')+' clients <b>'+s.wifi.cli+'</b><br>'+
   'ap <b>'+esc(s.wifi.ap)+'</b> / <b>'+esc(s.wifi.appass)+'</b> ch <b>'+s.wifi.ch+'</b><br>'+
   'sta <b>'+(esc(s.wifi.sta)||'-')+'</b> mac <b>'+esc(s.mac)+'</b><br>'+
   'zigbee <b>'+(s.zb?'joined':'not joined')+'</b>'+
   (s.zb?' pan <b>0x'+s.pan.toString(16)+'</b> ch <b>'+s.chan+'</b> addr <b>0x'+
     s.addr.toString(16)+'</b> lqi <b>'+s.lqi+'</b> rssi <b>'+s.zrssi+'</b>':'')+
   ' fix <b>'+(s.fix?'on':'OFF')+'</b><br>'+
   'vbus <b>'+(s.vbus/1000).toFixed(2)+'V</b> pg <b>'+(s.pg?'12V':'no')+'</b> gate <b>'+
   (s.p12?'open':'closed')+'</b> force <b>'+(s.force?'on':'off')+'</b><br>'+
   'temp <b>'+s.temp.toFixed(1)+'C</b> heap <b>'+s.heap+'</b> min <b>'+s.minheap+'</b><br>'+
   'up <b>'+Math.floor(s.up/1000)+'s</b> boot <b>#'+s.boot+'</b> rst <b>'+esc(s.rst)+
   '</b> log <b>'+s.log+'</b> B<br>'+
   'mode <b>'+s.mode+'</b> master <b>'+s.master+'</b> lampOn <b>0x'+s.on.toString(16)+
   '</b> bri <b>'+s.lamps.map(l=>l.b).join(',')+'</b>';
}
function saveAp(){
  var a=encodeURIComponent(document.getElementById('apssid').value);
  var b=encodeURIComponent(document.getElementById('appass').value);
  if(b.length<8){alert('Password must be at least 8 characters');return}
  fetch('/api/ap?ssid='+a+'&pass='+b,{method:'POST'});
  document.getElementById('sub').textContent='access point restarting...';
}
function toZigbee(){
  if(!confirm('Switch the radio to Zigbee?\n\nWi-Fi stops and the board reboots: this page '+
              'will disconnect. Use the OLED menu to come back.'))return;
  fetch('/api/radio?m=zigbee',{method:'POST'});
  document.getElementById('sub').textContent='switching to zigbee, disconnecting...';
}
function load(){if(hold)return;fetch('/api/state').then(r=>r.json()).then(j=>{S=j;render()}).catch(()=>{})}
load();setInterval(load,2000);
</script></body></html>)HTML";

// ---- state ----
// One payload with everything the page shows, diagnostics included: the footer is meant to be
// readable off a phone in the car, without a serial cable.
static void handleState() {
  String j; j.reserve(2000);
  j += "{\"ver\":\"" FW_VERSION "\",\"hw\":\"" HW_REV "\"";
  j += ",\"mode\":" + String(config::s.mode);
  j += ",\"master\":" + String(config::s.master);
  j += ",\"on\":" + String(config::s.lampOn);
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
  j += ",\"disp\":{\"b\":" + String(config::s.dispBri) + ",\"dim\":" + String(config::s.dimSec);
  j += ",\"off\":" + String(config::s.offSec) + "}";

  bool zbUp = zb::connected();                    // false when the stack was not started
  uint8_t lqi = 0; int8_t zrssi = 0; uint16_t par = 0;
  if (zbUp) zb::parentLink(lqi, zrssi, par);
  j += ",\"zb\":" + String(zbUp ? 1 : 0);
  j += ",\"pan\":" + String(zbUp ? zb::panId() : 0);
  j += ",\"chan\":" + String(zbUp ? zb::channel() : 0);
  j += ",\"addr\":" + String(zbUp ? zb::shortAddr() : 0);
  j += ",\"lqi\":" + String(lqi) + ",\"zrssi\":" + String(zrssi);
  j += ",\"fix\":" + String(zb::colorFixActive() ? 1 : 0);
  j += ",\"radio\":\""; j += radio::name(radio::mode()); j += "\"";

  j += ",\"p12\":" + String(power::present12V() ? 1 : 0);
  j += ",\"pg\":" + String(power::good() ? 1 : 0);
  j += ",\"force\":" + String(power::forced() ? 1 : 0);
  j += ",\"vbus\":" + String(power::vbusMv());
  j += ",\"rst\":\""; j += power::resetReason(); j += "\"";
  j += ",\"temp\":" + String(temperatureRead(), 1);
  j += ",\"heap\":" + String((unsigned)ESP.getFreeHeap());
  j += ",\"minheap\":" + String((unsigned)ESP.getMinFreeHeap());
  j += ",\"up\":" + String(millis()) + ",\"boot\":" + String(g_bootCount);
  j += ",\"mac\":\""; j += WiFi.macAddress(); j += "\"";
  j += ",\"log\":" + String((unsigned)evlog::size());

  const char* st = "off";
  switch (wifinet::state()) {
    case wifinet::CONNECTING: st = "connecting"; break;
    case wifinet::ONLINE:     st = "online";     break;
    case wifinet::AP:         st = "ap";         break;
    default: break;
  }
  j += ",\"wifi\":{\"st\":\""; j += st; j += "\",\"ip\":\"";
  j += wifinet::ip().toString(); j += "\",\"ap\":\"";
  j += wifinet::apSsid(); j += "\",\"appass\":\"";
  j += wifinet::apPass(); j += "\",\"sta\":\"";
  j += wifinet::staSsid(); j += "\",\"cli\":" + String(wifinet::apClients());
  j += ",\"ch\":" + String((int)WiFi.channel());
  j += ",\"rssi\":" + String(wifinet::rssi()) + "}}";
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
  if ((v = argNum("disp"))  >= 0) { config::s.dispBri = constrain(v, 3, 255);
                                    display::setBrightness(config::s.dispBri); }
  if ((v = argNum("dim"))   >= 0) config::s.dimSec = constrain(v, 0, 120);
  if ((v = argNum("off"))   >= 0) config::s.offSec = constrain(v, 0, 3600);
  if ((v = argNum("force")) >= 0) power::setForce(v != 0);
  server.send(200, "application/json", "{\"ok\":1}");   // config::tick() persists the settle
}

void begin() {
  if (s_running) return;
  server.on("/", HTTP_GET, [] {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html", PAGE);
  });
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/set", HTTP_POST, handleSet);
  server.on("/api/ap", HTTP_POST, [] {          // this device's own access point identity
    String ss = server.arg("ssid"), pw = server.arg("pass");
    server.send(200, "application/json", "{\"ok\":1}");   // answer first: this drops the AP
    wifinet::setAp(ss.c_str(), pw.c_str());
  });
  server.on("/api/wifireset", HTTP_POST, [] {
    server.send(200, "application/json", "{\"ok\":1}"); wifinet::resetWifi();
  });
  // One-way from here: the page cannot come back over a radio it just handed away.
  server.on("/api/radio", HTTP_POST, [] {
    bool toZb = (server.arg("m") == "zigbee");
    server.send(200, "application/json", "{\"ok\":1}");
    delay(200);
    radio::setMode(toZb ? radio::ZIGBEE : radio::WIFI);
  });
  // ---- log: download as plain text, so a bench session can be handed over as a file ----
  server.on("/log", HTTP_GET, [] {
    if (!evlog::ready()) { server.send(503, "text/plain", "log unavailable"); return; }
    File f = LittleFS.open("/log.txt", "r");
    if (!f) { server.send(404, "text/plain", "no log yet"); return; }
    server.sendHeader("Content-Disposition", "attachment; filename=smartlada-log.txt");
    server.streamFile(f, "text/plain");
    f.close();
  });
  server.on("/log.1", HTTP_GET, [] {
    File f = evlog::ready() ? LittleFS.open("/log.bak.txt", "r") : File();
    if (!f) { server.send(404, "text/plain", "no rotated log"); return; }
    server.sendHeader("Content-Disposition", "attachment; filename=smartlada-log-prev.txt");
    server.streamFile(f, "text/plain");
    f.close();
  });
  server.on("/clearlog", HTTP_POST, [] {
    if (evlog::ready()) { LittleFS.remove("/log.txt"); LittleFS.remove("/log.bak.txt"); }
    LOGI("log", "cleared from the web");
    server.send(200, "application/json", "{\"ok\":1}");
  });

  // ---- firmware over Wi-Fi: same image as the BLE path, no cable, no OTA boot mode ----
  server.on("/firmware", HTTP_POST,
    [] {   // completion: answer, then reboot into whatever we just wrote
      bool ok = !Update.hasError();
      server.sendHeader("Connection", "close");
      server.send(ok ? 200 : 500, "text/plain", ok ? "OK, rebooting" : Update.errorString());
      LOGI("ota", "wifi update %s", ok ? "ok" : "FAILED");
      if (ok) { delay(400); ESP.restart(); }
    },
    [] {   // upload: stream straight into the inactive slot
      HTTPUpload& up = server.upload();
      if (up.status == UPLOAD_FILE_START) {
        LOGI("ota", "wifi update start: %s", up.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) LOGE("ota", "begin failed: %s", Update.errorString());
      } else if (up.status == UPLOAD_FILE_WRITE) {
        if (Update.write(up.buf, up.currentSize) != up.currentSize)
          LOGE("ota", "write failed: %s", Update.errorString());
      } else if (up.status == UPLOAD_FILE_END) {
        if (!Update.end(true)) LOGE("ota", "end failed: %s", Update.errorString());
        else LOGI("ota", "received %u bytes", (unsigned)up.totalSize);
      }
    });

  server.on("/api/factory", HTTP_POST, [] {
    server.send(200, "application/json", "{\"ok\":1}");
    delay(200);
    Preferences p; p.begin("smartlada", false); p.clear(); p.end();
    ESP.restart();
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
