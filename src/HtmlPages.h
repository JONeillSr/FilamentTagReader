#pragma once
#include <pgmspace.h>

/**
 * @file    HtmlPages.h
 * @brief   Project dashboard page for the Filament Tag Reader / Writer.
 *
 * Single-page dashboard. It polls /api/filament once per second and renders the
 * decoded record, including the ABGR-derived color shown as a filled circle so
 * the actual filament color is visible at a glance. Served from PROGMEM via the
 * sync WebServer that WebService owns (we register the route through
 * WebService::routes()).
 */

const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Filament Tag Reader</title>
<style>
 body{font-family:system-ui,Arial,sans-serif;margin:0;background:#0f1419;color:#e6e6e6}
 header{background:#1b2430;padding:16px 20px;border-bottom:1px solid #2a3645;display:flex;justify-content:space-between;align-items:center}
 h1{margin:0;font-size:18px}.status{font-size:13px;color:#8aa0b4;margin-top:4px}
 a.btn{color:#5fd3a0;text-decoration:none;border:1px solid #2a3645;padding:8px 14px;border-radius:6px;font-size:14px}
 .wrap{padding:20px;max-width:680px;margin:0 auto}
 .card{background:#161e29;border:1px solid #2a3645;border-radius:10px;padding:20px;margin-top:16px}
 .top{display:flex;align-items:center;gap:18px}
 .swatch{width:72px;height:72px;border-radius:50%;border:2px solid #2a3645;flex:0 0 auto;box-shadow:0 0 12px rgba(0,0,0,.4)}
 .ttl{font-size:24px;font-weight:700;margin:0}
 .sub{color:#8aa0b4;font-size:14px;margin-top:4px}
 .grid{display:grid;grid-template-columns:1fr 1fr;gap:10px 20px;margin-top:18px}
 .k{color:#8aa0b4;font-size:13px}.v{font-size:15px;font-weight:600}
 .v.mono{font-family:ui-monospace,Consolas,monospace;color:#cdd6e0}
 .empty{color:#6b7d90;padding:30px;text-align:center}
 .hex{font-family:ui-monospace,Consolas,monospace}
 .inp{width:100%;background:#0f1419;color:#e6e6e6;border:1px solid #2a3645;border-radius:6px;padding:9px;font-size:14px;box-sizing:border-box}
 input[type=color].inp{height:40px;padding:3px;cursor:pointer}
 .btn.go{background:#1f7a52;border-color:#2a9168;color:#eafff5;cursor:pointer;width:100%}
 .btn.go:hover{background:#248a5d}
 button.btn{cursor:pointer;background:transparent;color:#5fd3a0;border:1px solid #2a3645;padding:8px 14px;border-radius:6px;font-size:14px;font-family:inherit}
 button.btn.active{background:#2563eb;border-color:#2563eb;color:#fff}
 .ok{color:#5fd3a0}.bad{color:#e06b6b}.warn{color:#e0b85f}
 .fmtBadge{display:inline-block;margin-left:8px;padding:2px 9px;border-radius:11px;background:#1f3a52;color:#7fd3ff;font-size:12px;font-weight:600;vertical-align:middle}
 .conv{margin-top:16px;padding-top:14px;border-top:1px solid #2a3645}
 .convRow{display:flex;align-items:center;gap:10px;margin-bottom:10px}
 .convSwatch{width:22px;height:22px;border-radius:5px;border:1px solid #2a3645;background:#1030d0;display:inline-block}
</style></head><body>
<header>
 <div><h1>Filament Tag Reader</h1><div class="status" id="status">Connecting...</div></div>
 <div style="display:flex;gap:8px;align-items:center">
  <button class="btn" id="modeBtn" onclick="toggleMode()">Mode: read</button>
  <a class="btn" href="/filaments">Filament tables &rarr;</a>
  <a class="btn" href="/tigertag">TigerTag IDs &rarr;</a>
  <a class="btn" href="/status">Device status &rarr;</a>
 </div>
</header>
<div class="wrap">
 <!-- Write panel: only shown in write mode. Lets you pick material/color/weight
      and queue a write; the actual write happens on the next tag tap. -->
 <div id="writePanel" class="card" style="display:none">
  <h2 style="margin:0 0 14px;font-size:17px">Write a spool tag
   <span id="writeFmt" class="fmtBadge"></span></h2>
  <div class="grid">
   <div><div class="k">Material</div>
    <select id="wType" class="inp"></select></div>
   <div><div class="k">Weight</div>
    <select id="wWeight" class="inp"></select></div>
   <div><div class="k">Color</div>
    <input type="color" id="wColor" class="inp" value="#1030d0" oninput="cvFromPicker()"></div>
   <div style="align-self:end">
    <button class="btn go" onclick="queueWrite()">Write tag</button></div>
  </div>
  <div id="writeMsg" class="sub" style="margin-top:12px"></div>

  <!-- Color converter: type into any one box (HEX, RGB, or ABGR) and the other
       two update live. The swatch previews it, and "Use" sets the write color.
       Formats store color differently (Anycubic ABGR, OpenSpool hex, TigerTag
       RGBA), so this helps translate between them. -->
  <div class="conv">
   <div class="convRow">
    <span class="convSwatch" id="cvSwatch"></span>
    <strong style="font-size:13px">Color converter</strong>
    <button class="btn" style="margin-left:auto;padding:5px 12px" onclick="cvUse()">Use</button>
   </div>
   <div class="grid">
    <div><div class="k">HEX</div>
     <input id="cvHex" class="inp hex" placeholder="#1030D0" oninput="cvFrom('hex')"></div>
    <div><div class="k">RGB</div>
     <input id="cvRgb" class="inp" placeholder="16, 48, 208" oninput="cvFrom('rgb')"></div>
    <div><div class="k">ABGR (hex)</div>
     <input id="cvAbgr" class="inp hex" placeholder="FFD03010" oninput="cvFrom('abgr')"></div>
    <div><div class="k">RGBA (hex)</div>
     <input id="cvRgba" class="inp hex" placeholder="1030D0FF" oninput="cvFrom('rgba')"></div>
   </div>
  </div>
 </div>
 <div id="content"><div class="card"><div class="empty">Waiting for a tag&hellip; place a spool on the reader.</div></div></div>
</div>
<script>
function row(k,v,mono){return '<div><div class="k">'+k+'</div><div class="v'+(mono?' mono':'')+'">'+v+'</div></div>';}
let curMode='read', lastReadCount=-1, lastEventMsg='';

async function loadMaterials(){
 try{
  const r=await fetch('/api/materials');const d=await r.json();
  const ty=document.getElementById('wType');const wt=document.getElementById('wWeight');
  ty.innerHTML='';wt.innerHTML='';
  d.materials.forEach(m=>{const o=document.createElement('option');o.value=m.name;o.textContent=m.name;ty.appendChild(o);});
  d.weights.forEach(w=>{const o=document.createElement('option');o.value=w.label;o.textContent=w.label;wt.appendChild(o);});
  if(d.fallback){document.getElementById('writeMsg').innerHTML='<span class="warn">Using built-in fallback table (filaments.json missing or invalid).</span>';}
 }catch(e){}
}
async function toggleMode(){
 const next=curMode==='write'?'read':'write';
 const fd=new URLSearchParams();fd.set('mode',next);
 await fetch('/api/mode',{method:'POST',body:fd});
 curMode=next;applyMode();
}
function applyMode(){
 const b=document.getElementById('modeBtn');
 b.textContent='Mode: '+curMode;
 b.classList.toggle('active',curMode==='write');
 document.getElementById('writePanel').style.display=(curMode==='write')?'block':'none';
 // In write mode the last-read card is hidden to avoid confusion with the
 // spool being written; it returns when you switch back to read mode.
 document.getElementById('content').style.display=(curMode==='write')?'none':'block';
}
async function queueWrite(){
 const fd=new URLSearchParams();
 fd.set('type',document.getElementById('wType').value);
 fd.set('color',document.getElementById('wColor').value);
 fd.set('weight',document.getElementById('wWeight').value);
 const m=document.getElementById('writeMsg');
 try{
  const r=await fetch('/api/write',{method:'POST',body:fd});
  if(r.ok){m.innerHTML='<span class="ok">Queued. Tap a tag on the reader to write it.</span>';}
  else{m.innerHTML='<span class="bad">'+(await r.text())+'</span>';}
 }catch(e){m.innerHTML='<span class="bad">Request failed.</span>';}
}
async function refresh(){
 try{
  const r=await fetch('/api/filament');const d=await r.json();
  document.getElementById('status').textContent=
    'Connected - reader '+(d.readerOk?'OK':'OFFLINE')+' - reads '+d.readCount
    +(d.pending?' - write pending':'');
  if(d.mode&&d.mode!==curMode){curMode=d.mode;applyMode();}
  var fb=document.getElementById('writeFmt');
  if(fb)fb.textContent=d.writeFormat||'';
  // Don't rebuild the read card while in write mode (it's hidden anyway).
  if(curMode==='write') return;
  const c=document.getElementById('content');
  if(!d.hasData){
   c.innerHTML='<div class="card"><div class="empty">Waiting for a tag&hellip; place a spool on the reader.</div></div>';
   return;
  }
  const f=d.filament;
  let h='<div class="card"><div class="sub" style="margin:0 0 10px;text-transform:uppercase;letter-spacing:.5px;font-size:11px">Last read</div><div class="top">';
  h+='<div class="swatch" style="background:'+f.colorHex+'"></div>';
  h+='<div><p class="ttl">'+(f.type||'?')+'</p>';
  h+='<div class="sub">'+(f.brand||'')+' &middot; <span class="hex">'+f.colorHex+'</span></div></div>';
  h+='</div><div class="grid">';
  h+=row('SKU',f.sku||'-',true);
  h+=row('UID',f.uid||'-',true);
  h+=row('Nozzle temp',f.extruderMinC+'&ndash;'+f.extruderMaxC+' &deg;C');
  h+=row('Bed temp',f.bedMinC+'&ndash;'+f.bedMaxC+' &deg;C');
  h+=row('Diameter',f.diameterMm.toFixed(2)+' mm');
  h+=row('Format',f.format||'-');
  h+=row('Color ABGR','A:'+f.colorA+' B:'+f.colorB+' G:'+f.colorG+' R:'+f.colorR);
  h+='</div></div>';
  c.innerHTML=h;
 }catch(e){document.getElementById('status').textContent='Connection lost...';}
}
// ---- Color converter: type in any box, the other two follow ----------------
function cvClamp(n){return Math.max(0,Math.min(255,n|0));}
function cvRender(r,g,b){
 var hex='#'+[r,g,b].map(function(x){return cvClamp(x).toString(16).padStart(2,'0');}).join('').toUpperCase();
 var rgb=cvClamp(r)+', '+cvClamp(g)+', '+cvClamp(b);
 // ABGR byte order (Anycubic): A, B, G, R - alpha fixed opaque (FF).
 var abgr=['FF',cvClamp(b),cvClamp(g),cvClamp(r)].map(function(x){return (typeof x==='string'?x:x.toString(16).padStart(2,'0'));}).join('').toUpperCase();
 // RGBA byte order (TigerTag): R, G, B, A - alpha fixed opaque (FF).
 var rgba=[cvClamp(r),cvClamp(g),cvClamp(b),'FF'].map(function(x){return (typeof x==='string'?x:x.toString(16).padStart(2,'0'));}).join('').toUpperCase();
 document.getElementById('cvSwatch').style.background=hex;
 return {hex:hex,rgb:rgb,abgr:abgr,rgba:rgba};
}
function cvSet(o,except){
 if(except!=='hex')  document.getElementById('cvHex').value=o.hex;
 if(except!=='rgb')  document.getElementById('cvRgb').value=o.rgb;
 if(except!=='abgr') document.getElementById('cvAbgr').value=o.abgr;
 if(except!=='rgba') document.getElementById('cvRgba').value=o.rgba;
}
function cvFrom(src){
 var r=0,g=0,b=0,ok=false;
 if(src==='hex'){
  var h=document.getElementById('cvHex').value.replace(/[^0-9a-fA-F]/g,'');
  if(h.length>=6){r=parseInt(h.substr(0,2),16);g=parseInt(h.substr(2,2),16);b=parseInt(h.substr(4,2),16);ok=true;}
 }else if(src==='rgb'){
  var m=document.getElementById('cvRgb').value.match(/\d+/g);
  if(m&&m.length>=3){r=+m[0];g=+m[1];b=+m[2];ok=true;}
 }else if(src==='abgr'){
  var a=document.getElementById('cvAbgr').value.replace(/[^0-9a-fA-F]/g,'');
  if(a.length>=8){b=parseInt(a.substr(2,2),16);g=parseInt(a.substr(4,2),16);r=parseInt(a.substr(6,2),16);ok=true;}
  else if(a.length>=6){b=parseInt(a.substr(0,2),16);g=parseInt(a.substr(2,2),16);r=parseInt(a.substr(4,2),16);ok=true;}
 }else if(src==='rgba'){
  var q=document.getElementById('cvRgba').value.replace(/[^0-9a-fA-F]/g,'');
  if(q.length>=6){r=parseInt(q.substr(0,2),16);g=parseInt(q.substr(2,2),16);b=parseInt(q.substr(4,2),16);ok=true;}
 }
 if(!ok)return;
 cvSet(cvRender(r,g,b),src);
}
function cvUse(){
 var h=document.getElementById('cvHex').value.replace(/[^0-9a-fA-F]/g,'');
 if(h.length>=6){document.getElementById('wColor').value='#'+h.substr(0,6);}
}
// When the color picker changes, update all converter boxes to match.
function cvFromPicker(){
 var v=document.getElementById('wColor').value.replace('#','');
 if(v.length>=6)cvSet(cvRender(parseInt(v.substr(0,2),16),parseInt(v.substr(2,2),16),parseInt(v.substr(4,2),16)),'');
}
// Seed the converter from the current write color picker.
(function(){var v=document.getElementById('wColor').value.replace('#','');
 if(v.length>=6)cvSet(cvRender(parseInt(v.substr(0,2),16),parseInt(v.substr(2,2),16),parseInt(v.substr(4,2),16)),'');})();

loadMaterials();applyMode();
setInterval(refresh,1000);refresh();
</script>
<!-- Reusable device footer (links to /status, /webserial, /update, + project
     links). Served by the WebService module; see addFooterLink() in main. -->
<script src="/footer.js"></script>
</body></html>
)HTML";

// ---------------------------------------------------------------------------
// Filament table editor: shows the raw filaments.json and lets you edit + save
// it. Saving posts to /api/db, which validates before persisting (a bad edit is
// rejected and the previous table kept). Self-contained; no external assets.
// ---------------------------------------------------------------------------
const char FILAMENTS_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Filament tables</title>
<style>
 body{font-family:system-ui,Arial,sans-serif;margin:0;background:#0f1419;color:#e6e6e6}
 header{background:#1b2430;padding:16px 20px;border-bottom:1px solid #2a3645;display:flex;justify-content:space-between;align-items:center}
 h1{margin:0;font-size:18px}
 a.btn,button.btn{color:#5fd3a0;text-decoration:none;border:1px solid #2a3645;padding:8px 14px;border-radius:6px;font-size:14px;background:transparent;cursor:pointer}
 .wrap{padding:20px;max-width:820px;margin:0 auto}
 textarea{width:100%;height:60vh;background:#0b0f14;color:#cdd6e0;border:1px solid #2a3645;border-radius:8px;padding:14px;font-family:ui-monospace,Consolas,monospace;font-size:13px;box-sizing:border-box;line-height:1.45}
 .bar{display:flex;gap:10px;align-items:center;margin-top:12px}
 .save{background:#1f7a52;border-color:#2a9168;color:#eafff5}
 .msg{font-size:14px}.ok{color:#5fd3a0}.bad{color:#e06b6b}
 .hint{color:#8aa0b4;font-size:13px;margin:8px 0 0}
</style></head><body>
<header>
 <h1>Filament tables</h1>
 <a class="btn" href="/">&larr; Dashboard</a>
</header>
<div class="wrap">
 <p class="hint">Edit the filament definitions below. Saving validates the JSON first &mdash; if it is invalid, the previous table is kept and nothing is overwritten.</p>
 <textarea id="json" spellcheck="false"></textarea>
 <div class="bar">
  <button class="btn save" onclick="save()">Save</button>
  <button class="btn" onclick="load()">Reload</button>
  <span class="msg" id="msg"></span>
 </div>
</div>
<script>
async function load(){
 const m=document.getElementById('msg');m.textContent='';
 try{const r=await fetch('/api/db');document.getElementById('json').value=await r.text();}
 catch(e){m.innerHTML='<span class="bad">Failed to load.</span>';}
}
async function save(){
 const m=document.getElementById('msg');m.textContent='Saving...';
 const fd=new URLSearchParams();fd.set('json',document.getElementById('json').value);
 try{
  const r=await fetch('/api/db',{method:'POST',body:fd});
  if(r.ok){m.innerHTML='<span class="ok">Saved.</span>';}
  else{m.innerHTML='<span class="bad">'+(await r.text())+'</span>';}
 }catch(e){m.innerHTML='<span class="bad">Request failed.</span>';}
}
load();
</script>
<script src="/footer.js"></script>
</body></html>
)HTML";

// ---------------------------------------------------------------------------
// TigerTag ID database editor. Same validated-save pattern as the filament
// editor, pointed at /api/ttdb (tigertag_ids.json). Brand/material/aspect name
// -> numeric ID maps plus the local-naming alias layers.
// ---------------------------------------------------------------------------
const char TIGERTAG_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>TigerTag IDs</title>
<style>
 body{font-family:system-ui,Arial,sans-serif;margin:0;background:#0f1419;color:#e6e6e6}
 header{background:#1b2430;padding:16px 20px;border-bottom:1px solid #2a3645;display:flex;justify-content:space-between;align-items:center}
 h1{margin:0;font-size:18px}
 a.btn,button.btn{color:#5fd3a0;text-decoration:none;border:1px solid #2a3645;padding:8px 14px;border-radius:6px;font-size:14px;background:transparent;cursor:pointer}
 .wrap{padding:20px;max-width:820px;margin:0 auto}
 textarea{width:100%;height:60vh;background:#0b0f14;color:#cdd6e0;border:1px solid #2a3645;border-radius:8px;padding:14px;font-family:ui-monospace,Consolas,monospace;font-size:13px;box-sizing:border-box;line-height:1.45}
 .bar{display:flex;gap:10px;align-items:center;margin-top:12px}
 .save{background:#1f7a52;border-color:#2a9168;color:#eafff5}
 .msg{font-size:14px}.ok{color:#5fd3a0}.bad{color:#e06b6b}
 .hint{color:#8aa0b4;font-size:13px;margin:8px 0 0}
</style></head><body>
<header>
 <h1>TigerTag IDs</h1>
 <a class="btn" href="/">&larr; Dashboard</a>
</header>
<div class="wrap">
 <p class="hint">Edit the TigerTag ID database (brand / material / aspect &rarr; numeric IDs, plus naming aliases). Saving validates the JSON first &mdash; if it is invalid, the previous table is kept. This file is larger than the filament table; give it a moment to load.</p>
 <textarea id="json" spellcheck="false"></textarea>
 <div class="bar">
  <button class="btn save" onclick="save()">Save</button>
  <button class="btn" onclick="load()">Reload</button>
  <span class="msg" id="msg"></span>
 </div>
</div>
<script>
async function load(){
 const m=document.getElementById('msg');m.textContent='';
 try{const r=await fetch('/api/ttdb');document.getElementById('json').value=await r.text();}
 catch(e){m.innerHTML='<span class="bad">Failed to load.</span>';}
}
async function save(){
 const m=document.getElementById('msg');m.textContent='Saving...';
 const fd=new URLSearchParams();fd.set('json',document.getElementById('json').value);
 try{
  const r=await fetch('/api/ttdb',{method:'POST',body:fd});
  if(r.ok){m.innerHTML='<span class="ok">Saved.</span>';}
  else{m.innerHTML='<span class="bad">'+(await r.text())+'</span>';}
 }catch(e){m.innerHTML='<span class="bad">Request failed.</span>';}
}
load();
</script>
<script src="/footer.js"></script>
</body></html>
)HTML";
