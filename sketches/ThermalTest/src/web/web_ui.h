#pragma once
#include <Arduino.h>

// Single-page dark UI in the cineink visual language (black bg, system font,
// uppercase letter-spaced labels, .btn/.line/.danger tokens). Polls /api/state
// every 2 s and, once on load, hands the device the phone's LOCAL wall-clock via
// POST /api/time. Served from PROGMEM by web_server.cpp::handleRoot().

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>ThermalTest</title>
<style>
body{font-family:-apple-system,BlinkMacSystemFont,system-ui,'Helvetica Neue',Arial,sans-serif;
 font-size:14px;max-width:440px;margin:24px auto;padding:0 16px;background:#000;color:#f2f2f2}
h1{font-size:1.6em;letter-spacing:4px;margin:0 0 4px;text-align:center;color:#fff}
.clock{text-align:center;color:#9a9a9a;letter-spacing:2px;margin-bottom:20px;
 font-variant-numeric:tabular-nums}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:24px}
.card{border:1px solid #cccccc;padding:12px 6px;text-align:center}
.card .lbl{font-size:0.8em;color:#cfcfcf;text-transform:uppercase;letter-spacing:1px}
.card .val{font-size:1.7em;margin-top:6px;font-variant-numeric:tabular-nums}
.card.off .val{color:#666}
.sec{margin-bottom:16px}
a.btn{display:block;width:100%;box-sizing:border-box;padding:11px;background:#f2f2f2;color:#000;
 text-align:center;text-decoration:none;letter-spacing:2px;margin-bottom:10px}
a.btn:active{transform:translateY(1px)}
.btn-row{display:flex;gap:10px}.btn-row>a.btn{flex:1;margin:0}
.line{background:#000;color:#cfcfcf;border:1px solid #cccccc}
.danger{background:#000;color:#f66;border:1px solid #f66}
.dbg{margin-top:28px;padding-top:12px;border-top:1px solid #cccccc;font-size:0.9em;color:#cfcfcf;
 text-align:center;line-height:1.6}
</style></head><body>
<h1>THERMAL</h1>
<div class="clock" id="clock">--:--:--</div>
<div class="grid">
 <div class="card" id="c0"><div class="lbl">PSU</div><div class="val" id="t0">--</div></div>
 <div class="card" id="c1"><div class="lbl">BODY</div><div class="val" id="t1">--</div></div>
</div>
<div class="sec">
 <a class="btn" href="/log">DOWNLOAD LOG</a>
 <div class="btn-row">
  <a class="btn line" href="/log.1">LOG.BAK</a>
  <a class="btn danger" href="#" onclick="clr();return false">CLEAR LOG</a>
 </div>
</div>
<div class="dbg" id="dbg">connecting...</div>
<script>
function sync(){fetch('/api/time',{method:'POST',
 headers:{'Content-Type':'application/x-www-form-urlencoded'},
 body:'epoch='+(Math.floor(Date.now()/1000)-new Date().getTimezoneOffset()*60)}).catch(function(){});}
function poll(){fetch('/api/state').then(function(r){return r.json();}).then(function(s){
 document.getElementById('clock').textContent=s.time;
 for(var i=0;i<2;i++){var v=s.temp[i];
  document.getElementById('t'+i).textContent=(v==null?'—':v.toFixed(1)+'°');
  document.getElementById('c'+i).className='card'+(v==null?' off':'');}
 document.getElementById('dbg').innerHTML='clients '+s.clients+' &middot; heap '+s.heap+
  ' &middot; log '+s.log+' B &middot; clock '+(s.clock?'set':'unset');
}).catch(function(){}).finally(function(){setTimeout(poll,2000);});}
function clr(){if(confirm('Clear log?'))fetch('/clearlog',{method:'POST'}).then(function(){});}
sync();poll();
</script></body></html>)HTML";
