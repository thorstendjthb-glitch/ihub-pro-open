// webui_html.h — Grow-Dashboard (dependency-frei: SVG-Gauge + Canvas-Charts)
#pragma once

static const char DASHBOARD_HTML[] = R"HTML(<!doctype html><html lang="de"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>iHub-Pro · Grow</title>
<script src="/i18n.js"></script>
<style>
:root{--bg:#0b0f0c;--c1:#141a16;--c2:#1a211c;--bd:#2a322c;--fg:#e6edf3;--mut:#8b9bb0;--grn:#3fb950;--grn2:#2ea043;--blu:#388bfd;--amb:#f5a524;--red:#e5484d;--vio:#a371f7}
*{box-sizing:border-box;margin:0;padding:0}
/* MarsHydro-Look: anthrazit mit grünem Glow oben + unten (wie die Steckdosenleiste) */
body{background:radial-gradient(1100px 520px at 50% -8%,#16331f 0%,transparent 55%),radial-gradient(1000px 480px at 50% 108%,#103021 0%,transparent 60%),var(--bg);color:var(--fg);font:14px/1.5 system-ui,Segoe UI,Roboto,sans-serif;padding:14px;max-width:1180px;margin:auto}
a{color:var(--grn);text-decoration:none}
h1{font-size:19px;font-weight:700;letter-spacing:.2px}
h3{font-size:12px;text-transform:uppercase;letter-spacing:.08em;color:#7fb98e;margin-bottom:10px;font-weight:700}
.top{display:flex;align-items:center;gap:12px;flex-wrap:wrap;margin-bottom:14px}
.top .sp{flex:1}
.pill{background:var(--c2);border:1px solid var(--bd);border-radius:999px;padding:6px 12px;font-size:13px;color:var(--mut)}
select,button,input{font:inherit;color:var(--fg)}
select,input{background:var(--c1);border:1px solid var(--bd);border-radius:8px;padding:7px 9px;color-scheme:dark}
/* Logo + Cannabis-Blatt-Deko */
.logoleaf{width:30px;height:30px;color:var(--grn);flex:none;filter:drop-shadow(0 0 5px rgba(57,230,57,.55))}
h1 .g{color:var(--grn)}
h1 .sub{font-size:12px;color:var(--mut);font-weight:500;letter-spacing:0}
.byline{font-size:10px;color:var(--grn);font-weight:700;letter-spacing:.14em;text-transform:uppercase}
.bgleaf{position:fixed;right:-90px;top:40px;width:440px;height:440px;color:#1b3a23;opacity:.16;z-index:-1;transform:rotate(18deg);pointer-events:none}
@media(max-width:760px){.bgleaf{width:280px;height:280px;right:-70px}}
.btn{background:var(--grn2);border:0;border-radius:8px;padding:8px 14px;font-weight:600;cursor:pointer}
.btn:hover{background:var(--grn)}
.btn.gray{background:#283142}
.card{background:linear-gradient(180deg,var(--c2),var(--c1));border:1px solid var(--bd);border-radius:16px;padding:16px;margin-bottom:14px;box-shadow:0 8px 24px rgba(0,0,0,.38),inset 0 1px 0 rgba(120,220,150,.05)}
.growrow{display:grid;grid-template-columns:1fr 1fr;gap:14px;margin-bottom:14px}
@media(max-width:760px){.growrow{grid-template-columns:1fr}}
.grid{display:grid;gap:14px}
.g2{grid-template-columns:300px 1fr}
@media(max-width:760px){.g2{grid-template-columns:1fr}}
#chambers{grid-template-columns:1fr 1fr}
@media(max-width:760px){#chambers{grid-template-columns:1fr}}
.ccard{background:linear-gradient(180deg,var(--c2),var(--c1));border:1px solid var(--bd);border-radius:14px;padding:14px}
.ccard.off{opacity:.65}
.tiles{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:10px}
.tile{background:var(--c1);border:1px solid var(--bd);border-radius:12px;padding:12px}
.tile .v{font-size:24px;font-weight:700}
.tile .u{font-size:12px;color:var(--mut);margin-left:3px}
.tile .l{font-size:11px;color:var(--mut);text-transform:uppercase;letter-spacing:.05em}
.tile .rg{font-size:10px;color:var(--mut);margin-top:3px}
.alarm{background:linear-gradient(90deg,#3a0d10,#2a0c0e);border:1px solid #5b1a1f;color:#ff9aa0;border-radius:12px;padding:12px 16px;margin-bottom:14px;font-weight:600}
.info{background:linear-gradient(90deg,#0c2a3a,#0a2230);border:1px solid #15556b;color:#7fd6ff;border-radius:12px;padding:12px 16px;margin-bottom:14px;font-weight:600}
.hide{display:none}
/* Grow timeline */
.gh{display:grid;grid-template-columns:1fr 280px;gap:18px;align-items:center}
@media(max-width:760px){.gh{grid-template-columns:1fr}}
.gday{font-size:40px;font-weight:800;line-height:1}
.gsub{color:var(--mut);margin-top:4px}
.bar{height:14px;border-radius:999px;background:#0c1018;border:1px solid var(--bd);overflow:hidden;margin:14px 0 8px}
.bar>div{height:100%;background:linear-gradient(90deg,#2ea043,#39e639);border-radius:999px;transition:width .6s;box-shadow:0 0 8px rgba(57,230,57,.5)}
.kv{display:flex;justify-content:space-between;color:var(--mut);font-size:12px}
.big{font-size:15px;color:var(--fg);font-weight:600}
/* calendar */
.cal .calhead,.cal .calgrid{display:grid;grid-template-columns:repeat(7,1fr);gap:3px}
.cal .calhead span{font-size:10px;color:var(--mut);text-align:center}
.cal .calgrid span{text-align:center;font-size:12px;padding:5px 0;border-radius:7px;color:var(--mut)}
.cal .cd{color:var(--fg)}
.cal .today{background:#2ea043;color:#fff;font-weight:700}
.cal .harvest{background:var(--grn2);color:#fff;font-weight:700;box-shadow:0 0 0 2px rgba(63,185,80,.3)}
.calmon{font-size:13px;font-weight:600;margin-bottom:8px;text-align:center}
/* charts */
.charts{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:14px}
.ch{background:var(--c1);border:1px solid var(--bd);border-radius:12px;padding:10px}
.ch .ct{font-size:12px;color:var(--mut);display:flex;justify-content:space-between;margin-bottom:6px}
.ch canvas{width:100%;height:90px;display:block}
.dot{display:inline-block;width:8px;height:8px;border-radius:2px;margin-right:4px;vertical-align:middle}
/* relays */
.relgrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}
/* Steckdosenleisten-Optik: 2×5-Raster aus dunklen, geriffelten Slots mit grüner LED */
.strip{background:linear-gradient(180deg,#23282b,#15191b);border:1px solid rgba(57,230,57,.28);border-radius:20px;
 padding:16px;box-shadow:0 0 30px rgba(57,230,57,.22),0 0 60px rgba(57,230,57,.1),0 10px 30px rgba(0,0,0,.5),inset 0 1px 0 rgba(255,255,255,.06)}
.outletgrid{display:grid;grid-template-columns:1fr 1fr;grid-template-rows:repeat(5,1fr);grid-auto-flow:column;gap:14px}
.outlet{position:relative;cursor:pointer;border-radius:16px;padding:13px 14px;min-height:104px;
 background:repeating-linear-gradient(125deg,#1b201e 0 7px,#171c1a 7px 14px);
 border:1px solid #2b332d;box-shadow:inset 0 1px 0 rgba(255,255,255,.05),inset 0 0 22px rgba(0,0,0,.5),0 3px 10px rgba(0,0,0,.4);
 transition:.15s;display:flex;flex-direction:column}
.outlet:hover{border-color:var(--grn);box-shadow:0 0 0 1px var(--grn) inset,0 0 18px rgba(63,185,80,.25)}
.outlet .onum{font-size:11px;color:var(--mut)}
.outlet .olbl{font-size:10.5px;letter-spacing:.07em;color:#cdd8d0;text-transform:uppercase;font-weight:700}
.outlet .oval{font-size:21px;font-weight:800;margin-top:auto;color:#e6edf3}
.outlet .ou{font-size:11px;color:var(--mut);font-weight:500}
.outlet .oled{position:absolute;top:14px;right:14px;width:26px;height:5px;border-radius:3px;background:#243a29}
.outlet.on .oled{background:#39e639;box-shadow:0 0 9px #39e639,0 0 4px #39e639}
.outlet .ogear{position:absolute;bottom:11px;right:13px;font-size:11px;color:var(--mut)}
.rel{background:var(--c1);border:1px solid var(--bd);border-radius:12px;padding:12px;cursor:pointer;position:relative;transition:.15s}
.rel.on{border-color:var(--grn);box-shadow:0 0 0 1px var(--grn) inset,0 4px 16px rgba(63,185,80,.18)}
.rel .rn{font-weight:600}.rel .rs{font-size:12px;color:var(--mut)}
.rel.on .rs{color:var(--grn)}
.badge{position:absolute;top:9px;right:9px;font-size:9px;padding:2px 6px;border-radius:6px;background:#283142;color:var(--mut);letter-spacing:.05em}
.badge.man{background:#3a2a0c;color:var(--amb);cursor:pointer}
.light{background:var(--c1);border:1px solid var(--bd);border-radius:12px;padding:12px;margin-bottom:10px}
.light input[type=range]{width:100%}
.gset{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
.ch{cursor:pointer;transition:.15s}.ch:hover{border-color:var(--grn)}
.modal{position:fixed;inset:0;background:rgba(0,0,0,.65);display:flex;align-items:center;justify-content:center;z-index:50;padding:16px}
.modal.hide{display:none}
.mbox{background:linear-gradient(180deg,var(--c2),var(--c1));border:1px solid var(--bd);border-radius:16px;padding:18px;width:100%;max-width:860px}
.mhead{display:flex;justify-content:space-between;align-items:center;font-size:16px;font-weight:600;margin-bottom:12px}
.mx{cursor:pointer;color:var(--mut);font-size:20px}
.mbox canvas{width:100%;height:300px;display:block}
.mstat{display:flex;gap:18px;flex-wrap:wrap;color:var(--mut);font-size:13px;margin-top:10px}
.mxa{display:flex;justify-content:space-between;color:var(--mut);font-size:11px;margin-top:4px}
</style></head><body>

<svg width="0" height="0" style="position:absolute"><symbol id="leaf" viewBox="-56 -58 112 62"><g fill="currentColor">
 <path d="M0 0 Q-3.6 -29 0 -54 Q3.6 -29 0 0Z"/>
 <path d="M0 0 Q-3.4 -26 0 -47 Q3.4 -26 0 0Z" transform="rotate(31)"/>
 <path d="M0 0 Q-3.4 -26 0 -47 Q3.4 -26 0 0Z" transform="rotate(-31)"/>
 <path d="M0 0 Q-3 -19 0 -35 Q3 -19 0 0Z" transform="rotate(60)"/>
 <path d="M0 0 Q-3 -19 0 -35 Q3 -19 0 0Z" transform="rotate(-60)"/>
 <path d="M0 0 Q-2.4 -12 0 -22 Q2.4 -12 0 0Z" transform="rotate(85)"/>
 <path d="M0 0 Q-2.4 -12 0 -22 Q2.4 -12 0 0Z" transform="rotate(-85)"/>
 <rect x="-1" y="0" width="2" height="9" rx="1"/>
</g></symbol></svg>
<svg class="bgleaf"><use href="#leaf"/></svg>

<div class="top">
  <svg class="logoleaf"><use href="#leaf"/></svg>
  <div style="line-height:1.12">
   <h1>MARS<span class="g">HYDRO</span> <span class="sub">iHub-Pro</span></h1>
   <span class="byline">by THB</span>
  </div>
  <span class="pill" id="clock">—</span>
  <span class="sp"></span>
  <a class="pill" href="/settings">⚙ Einstellungen</a>
</div>

<div class="alarm hide" id="alarm"></div>
<div class="alarm hide" id="autooff"></div>
<div class="info hide" id="acb"></div>

<div id="grows" class="growrow"></div>

<div class="card">
  <h3>Kammern</h3>
  <div id="chambers" class="grid"></div>
</div>

<div class="card">
  <h3>Sensoren (global)</h3>
  <div class="tiles" id="klima"></div>
</div>

<div class="card" id="watercard" style="display:none">
  <h3>💧 Wasserwerte (RDWC) <span id="waterstale" class="pill" style="display:none;padding:3px 9px;background:#3a2a0d;border-color:#5b4a1a;color:#ffd08a">⚠ veraltet</span></h3>
  <div class="tiles" id="water"></div>
  <small style="color:var(--mut)">Quelle: 8-in-1-Tester über Home Assistant. Zielkorridore (RDWC): pH 5,8–6,2 · Wassertemp 18–20 °C · ORP 350–450 mV · EC je Phase. Kachel/Chart anklicken → 7-Tage- bzw. ganzer-Grow-Verlauf.</small>
</div>

<div class="card" id="wchcard" style="display:none">
  <h3>💧 Wasser-Verlauf</h3>
  <div class="charts" id="watercharts"></div>
</div>

<div class="card">
  <h3>Verlauf (Live-Sitzung)</h3>
  <div class="charts" id="charts"></div>
</div>

<div class="card">
  <h3>Energie</h3>
  <div class="tiles" id="power"></div>
</div>

<div class="card">
  <h3>Geräte &amp; Steckdosen</h3>
  <div class="strip"><div class="outletgrid" id="devices"></div></div>
</div>

<div class="card" id="alogcard" style="display:none">
  <h3>🔔 Alarm-Protokoll</h3>
  <div id="alog" style="font-size:13px;color:var(--mut)"></div>
</div>

<div id="devmodal" class="modal hide" onclick="if(event.target===this)closeDev()">
 <div class="mbox" style="max-width:460px">
  <div class="mhead"><span id="dvtitle"></span><span class="mx" onclick="closeDev()">✕</span></div>
  <div id="dvbody"></div>
 </div>
</div>

<div id="cfm" class="modal hide" onclick="if(event.target===this)cfmNo()">
 <div class="mbox" style="max-width:400px">
  <div class="mhead">🔒 Änderung bestätigen</div>
  <div id="cfmtext" style="color:var(--mut);margin-bottom:16px"></div>
  <div style="display:flex;gap:10px;justify-content:flex-end">
   <button class="btn gray" onclick="cfmNo()">Abbrechen</button>
   <button class="btn" onclick="cfmYes()">Ja, ändern</button></div>
 </div>
</div>


<div id="modal" class="modal hide" onclick="if(event.target===this)closeHist()">
 <div class="mbox">
  <div class="mhead"><span id="mtitle"></span><span class="mx" onclick="closeHist()">✕</span></div>
  <canvas id="mcanvas"></canvas>
  <div class="mstat" id="mstat"></div>
  <div class="mxa" id="mxa"></div>
 </div>
</div>

<script>
// API-Schutz: gespeicherten Token automatisch an /api-Aufrufe anhängen; bei 401 einmal nachfragen.
(()=>{const _f=window.fetch.bind(window);window.fetch=(u,o)=>{try{const k=localStorage.getItem('apikey');if(k&&typeof u==='string'&&u.startsWith('/api'))u+=(u.includes('?')?'&':'?')+'key='+encodeURIComponent(k);}catch(e){}
 return _f(u,o).then(r=>{if(r.status===401&&!window._keyAsk){window._keyAsk=1;const t=prompt('API-Schutz aktiv — Token eingeben (siehe Einstellungen):');if(t){localStorage.setItem('apikey',t.trim());location.reload();}}return r;});};})();
const PH=["Seeds","Stecklinge","Wuchs","Blüte","Automatics","Trocknen","Aus (Automatik deaktiviert)"];
const RN=["Light 1","Light 2","Humidifier","De-Humidifier","Watering","Fan","Inline Fan","Heating","Device 1","Device 2"];
// Gerät → gekoppelte Strom-Steckdose (Relais-Index, wie Gerätebeschriftung).
const DREL={l0:0,l1:1,humid:2,clip:5,fan:6}, DEVREL=Object.values(DREL);
// Anzeige-Reihenfolge: linke Spalte oben→unten 1–5, rechte Spalte 6–10
// (2 Spalten, spaltenweise gefüllt via grid-auto-flow:column → Reihenfolge 0..9).
const SLOTORDER=[0,1,2,3,4,5,6,7,8,9];
const DAY=86400, H={aT:[],aR:[],aV:[],bT:[],bR:[],bV:[],co2:[],power:[],
 wph:[],worp:[],wtemp:[],wec:[],wtds:[],wsal:[],wsg:[]}, CAP=240;
let phaseInit=false;
const $=id=>document.getElementById(id);

function push(s){const A=(s.chambers&&s.chambers[0])||{},B=(s.chambers&&s.chambers[1])||{};
 H.aT.push(A.valid?A.temp:null);H.aR.push(A.valid?A.rh:null);H.aV.push(A.valid?A.vpd:null);
 H.bT.push(B.valid?B.temp:null);H.bR.push(B.valid?B.rh:null);H.bV.push(B.valid?B.vpd:null);
 H.co2.push(s.co2_ok?s.co2:null);H.power.push(s.pw_ok?s.power:null);
 const w=s.water||{};
 H.wph.push(w.ph_ok?w.ph:null);H.worp.push(w.orp_ok?w.orp:null);H.wtemp.push(w.temp_ok?w.temp:null);
 H.wec.push(w.ec_ok?w.ec:null);H.wtds.push(w.tds_ok?w.tds:null);H.wsal.push(w.sal_ok?w.sal:null);H.wsg.push(w.sg_ok?w.sg:null);
 for(const k in H) if(H[k].length>CAP) H[k].shift();}

function chart(cid,series,unit){
 const cv=$(cid); if(!cv) return; const dpr=2;
 const W=cv.width=cv.clientWidth*dpr, Hh=cv.height=cv.clientHeight*dpr, ctx=cv.getContext('2d');
 ctx.clearRect(0,0,W,Hh);
 let all=[].concat(...series.map(s=>s.d)).filter(v=>v!=null&&!isNaN(v));
 if(all.length<2) return;
 let mn=Math.min(...all),mx=Math.max(...all); if(mn===mx){mn-=1;mx+=1;} let pd=(mx-mn)*0.12; mn-=pd;mx+=pd;
 // grid
 ctx.strokeStyle='rgba(255,255,255,.05)';ctx.lineWidth=1;
 for(let i=0;i<=2;i++){let y=Hh*i/2;ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(W,y);ctx.stroke();}
 series.forEach(s=>{
  const n=s.d.length; ctx.beginPath();ctx.lineWidth=2.5;ctx.strokeStyle=s.c;ctx.lineJoin='round';
  let started=false;
  s.d.forEach((v,i)=>{if(v==null||isNaN(v)){return;} const x=n<2?0:i/(n-1)*W, y=Hh-(v-mn)/(mx-mn)*Hh;
   if(!started){ctx.moveTo(x,y);started=true;}else ctx.lineTo(x,y);});
  ctx.stroke();
  // last point dot
  for(let i=n-1;i>=0;i--){if(s.d[i]!=null&&!isNaN(s.d[i])){const x=W,y=Hh-(s.d[i]-mn)/(mx-mn)*Hh;ctx.fillStyle=s.c;ctx.beginPath();ctx.arc(x-3,y,3.5,0,7);ctx.fill();break;}}
 });
}
function lastv(a){for(let i=a.length-1;i>=0;i--)if(a[i]!=null&&!isNaN(a[i]))return a[i];return null;}
function fmt(v,d){return v==null?'—':(+v).toFixed(d);}
// Ampel-Einfärbung: grün=optimal (lo..hi), gelb=grenzwertig (±my drumrum), rot=schlecht.
const C_OK='#3fb950',C_WARN='#f5a524',C_BAD='#e5484d';
function clr(v,lo,hi,my){if(v==null)return'';if(v>=lo&&v<=hi)return C_OK;if(v>=lo-my&&v<=hi+my)return C_WARN;return C_BAD;}
// CO₂: <250 kaputt(Sensor/zu wenig), 350–1500 optimal, bis 2200 grenzwertig, drüber schlecht.
function co2col(v){if(v==null)return'';if(v<250)return C_BAD;if(v<350)return C_WARN;if(v<=1500)return C_OK;if(v<=2200)return C_WARN;return C_BAD;}

// Serien je Kammer (A kräftig, B heller). m = History-Kanal (0..8).
const CHARTDEFS=[
 {id:'cT',t:'Temperatur A/B',s:[{d:H.aT,c:'#f5a524',n:'°C A',m:0},{d:H.bT,c:'#ffd9a0',n:'°C B',m:3}]},
 {id:'cR',t:'Feuchte A/B',s:[{d:H.aR,c:'#388bfd',n:'% A',m:1},{d:H.bR,c:'#a9ccff',n:'% B',m:4}]},
 {id:'cV',t:'VPD A/B',s:[{d:H.aV,c:'#3fb950',n:'kPa A',m:2},{d:H.bV,c:'#9ee2ab',n:'kPa B',m:5}]},
 {id:'cC',t:'CO₂',s:[{d:H.co2,c:'#a371f7',n:'ppm',m:6}]},
 {id:'cP',t:'Leistung',s:[{d:H.power,c:'#e5484d',n:'W',m:7}]},
];
function renderCharts(){
 if(!$('cT')){
  $('charts').innerHTML=CHARTDEFS.map(d=>`<div class="ch" title="Langzeit-Verlauf (7 Tage)"><div class="ct"><span>${d.t} 📈</span><span class="cv"></span></div><canvas id="${d.id}"></canvas></div>`).join('');
  CHARTDEFS.forEach(d=>{$(d.id).parentElement.onclick=()=>openHist(d.t,d.s);});
 }
 CHARTDEFS.forEach(d=>{
  $(d.id).parentElement.querySelector('.cv').innerHTML=d.s.map(x=>`<span class="dot" style="background:${x.c}"></span>${fmt(lastv(x.d),x.n=='ppm'?0:1)} ${x.n}`).join('  ');
  chart(d.id,d.s);
 });
}
// Wasser-Verlauf: ein Mini-Chart je Wert, m = History-Kanal (9..15). Klick → 7-Tage/Langzeit-Modal.
const WCHARTDEFS=[
 {id:'wcP',t:'pH',c:'#3fb950',n:'pH',k:'wph',m:9,dec:2},
 {id:'wcO',t:'ORP',c:'#a371f7',n:'mV',k:'worp',m:10,dec:0},
 {id:'wcT',t:'Wassertemp',c:'#f5a524',n:'°C',k:'wtemp',m:11,dec:1},
 {id:'wcE',t:'EC',c:'#388bfd',n:'mS',k:'wec',m:12,dec:2},
 {id:'wcD',t:'TDS',c:'#e5484d',n:'ppm',k:'wtds',m:13,dec:0},
 {id:'wcS',t:'Salinität',c:'#56d4dd',n:'ppm',k:'wsal',m:14,dec:0},
 {id:'wcG',t:'Dichte',c:'#ffd9a0',n:'SG',k:'wsg',m:15,dec:3},
];
function renderWaterCharts(s){
 const w=s.water, has=w&&(w.ph_ok||w.ec_ok||w.orp_ok||w.temp_ok);
 $('wchcard').style.display=has?'':'none';
 if(!has)return;
 if(!$('wcP')){
  $('watercharts').innerHTML=WCHARTDEFS.map(d=>`<div class="ch" title="Verlauf (Klick = 7 Tage / ganzer Grow)"><div class="ct"><span>${d.t} 📈</span><span class="cv"></span></div><canvas id="${d.id}"></canvas></div>`).join('');
  WCHARTDEFS.forEach(d=>{$(d.id).parentElement.onclick=()=>openHist(d.t+' 💧',[{c:d.c,n:d.n,m:d.m}]);});
 }
 WCHARTDEFS.forEach(d=>{
  const ser=[{d:H[d.k],c:d.c,n:d.n}];
  $(d.id).parentElement.querySelector('.cv').innerHTML=`<span class="dot" style="background:${d.c}"></span>${fmt(lastv(H[d.k]),d.dec)} ${d.n}`;
  chart(d.id,ser);
 });
}
let _hTitle='',_hSeries=null,_hLong=false;
async function openHist(title,series,long){
 _hTitle=title;_hSeries=series;_hLong=!!long;
 const sw=(on,lbl)=>`<a href="#" onclick="setHistRange(${on});return false" style="color:${(_hLong===on)?'#3fb950;text-decoration:underline':'#8b9bb0'}">${lbl}</a>`;
 $('mtitle').innerHTML=title+' <span style="font-size:13px;font-weight:400;margin-left:10px">'+sw(false,'7 Tage')+' · '+sw(true,'ganzer Grow')+'</span>';
 $('modal').className='modal';$('mstat').textContent='lädt …';$('mxa').textContent='';
 const cx=$('mcanvas').getContext('2d');cx.clearRect(0,0,$('mcanvas').width,$('mcanvas').height);
 let metas=[];
 for(const s of series){try{const r=await(await fetch('/api/history?m='+s.m+(_hLong?'&long=1':''),{cache:'no-store'})).json();
  metas.push({c:s.c,n:s.n,interval:r.interval,newest:r.newest,n:r.n,vals:r.v.map(x=>x==null?null:x/r.div)});}catch(e){}}
 bigChart(metas);
}
function setHistRange(long){if(_hSeries)openHist(_hTitle,_hSeries,long);}
function closeHist(){$('modal').className='modal hide';}
function bigChart(metas){
 const cv=$('mcanvas'),dpr=2,W=cv.width=cv.clientWidth*dpr,Hh=cv.height=cv.clientHeight*dpr,ctx=cv.getContext('2d');
 ctx.clearRect(0,0,W,Hh);
 let all=[];metas.forEach(m=>m.vals&&m.vals.forEach(v=>{if(v!=null&&!isNaN(v))all.push(v);}));
 if(all.length<2){ctx.fillStyle='#8b9bb0';ctx.font='26px system-ui';ctx.fillText('Sammle noch Daten … (1 Punkt / 5 min)',24,Hh/2);$('mstat').textContent='';$('mxa').textContent='';return;}
 let mn=Math.min(...all),mx=Math.max(...all);if(mn===mx){mn-=1;mx+=1;}let pd=(mx-mn)*0.12;mn-=pd;mx+=pd;
 ctx.strokeStyle='rgba(255,255,255,.06)';ctx.fillStyle='#8b9bb0';ctx.font='18px system-ui';ctx.lineWidth=1;
 for(let i=0;i<=4;i++){let y=Hh*i/4;ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(W,y);ctx.stroke();ctx.fillText((mx-(mx-mn)*i/4).toFixed(1),6,y+17);}
 metas.forEach(m=>{const n=m.vals.length;ctx.beginPath();ctx.lineWidth=3;ctx.strokeStyle=m.c;ctx.lineJoin='round';let st=false;
  m.vals.forEach((v,i)=>{if(v==null||isNaN(v)){st=false;return;}const x=n<2?0:i/(n-1)*W,y=Hh-(v-mn)/(mx-mn)*Hh;st?ctx.lineTo(x,y):ctx.moveTo(x,y);st=true;});ctx.stroke();});
 $('mstat').innerHTML=metas.map(m=>{let vv=m.vals.filter(v=>v!=null&&!isNaN(v));if(!vv.length)return'';
  return `<span><span class="dot" style="background:${m.c}"></span>${m.n}: jetzt <b style="color:#e6edf3">${vv[vv.length-1].toFixed(1)}</b> · min ${Math.min(...vv).toFixed(1)} · max ${Math.max(...vv).toFixed(1)}</span>`;}).join('');
 const me=metas[0],old=me.newest-(me.n-1)*me.interval,f=t=>new Date(t*1000).toLocaleString('de-DE',{day:'2-digit',month:'2-digit',hour:'2-digit',minute:'2-digit'});
 $('mxa').innerHTML=`<span>${f(old)}</span><span>${f((old+me.newest)/2)}</span><span>${f(me.newest)}</span>`;
}

// VPD radial gauge
function pt(deg,r){const a=deg*Math.PI/180;return[100+r*Math.cos(a),100-r*Math.sin(a)];}
function arc(d0,d1,r){const[x0,y0]=pt(d0,r),[x1,y1]=pt(d1,r);return`M${x0.toFixed(1)} ${y0.toFixed(1)} A${r} ${r} 0 0 1 ${x1.toFixed(1)} ${y1.toFixed(1)}`;}
// Grünes Idealband (lo..hi) je Kammer (vom Server). Übergang orange ±0,35, Extreme rot. Skala 0..2.
function gaugeZones(lo,hi){const m=0.35,oLo=Math.max(0,lo-m),oHi=Math.min(2,hi+m);let z=[];if(oLo>0)z.push([0,oLo,C_BAD]);z.push([oLo,lo,C_WARN],[lo,hi,C_OK],[hi,oHi,C_WARN]);if(oHi<2)z.push([oHi,2,C_BAD]);return z;}
function drawGauge(el,v,lo,hi){
 if(!el)return;let s='';
 gaugeZones(lo,hi).forEach(z=>{const a0=180-z[0]/2*180,a1=180-z[1]/2*180;s+=`<path d="${arc(a0,a1,80)}" stroke="${z[2]}" stroke-width="14" fill="none" stroke-linecap="round"/>`;});
 const f=Math.max(0,Math.min(1,(v||0)/2)),deg=180-f*180,[x,y]=pt(deg,72);
 s+=`<line x1="100" y1="100" x2="${x.toFixed(1)}" y2="${y.toFixed(1)}" stroke="#e6edf3" stroke-width="3" stroke-linecap="round"/><circle cx="100" cy="100" r="6" fill="#e6edf3"/>`;
 el.innerHTML=s;
}
function vpdLabel(v,lo,hi){if(v==null)return'kein Sensor';let z;if(v<lo-0.35)z='viel zu feucht';else if(v<lo)z='zu feucht';else if(v<=hi)z='ideal';else if(v<=hi+0.35)z='zu trocken';else z='viel zu trocken';return 'kPa · '+z;}

// Eine Kammer-Karte (Profil + VPD-Gauge + Temp/Feuchte/Abluft + Sensor + Alarm)
const SRCN=["Sensor-Bus","Fan-Bus","BLE TP357"];
function chamberCard(ch,c){
 const nm=ch?'B':'A', off=c.phase==6;
 const day=off?'—':(c.is_day?'☀ Tag':'🌙 Nacht');
 const src=SRCN[c.src]||'?';
 const po=PH.map((n,i)=>`<option value="${i}" ${c.phase==i?'selected':''}>${n}</option>`).join('');
 const tcol=c.valid?clr(c.temp,c.tlo,c.thi,2):'';
 const rcol=c.valid?clr(c.rh,c.rlo,c.rhi,10):'';
 const al=[];if(c.alarm_temp)al.push('Temperatur');if(c.alarm_mold)al.push('Feuchte/Schimmel');if(c.alarm_light)al.push('💡 Lampe?');
 // Licht-An/Aus-Info (nur wenn Tag/Nacht-Zyklus) + gemessener Lux-Wert (Lampen-Kontrolle)
 let li='';if(!off&&c.loff>=0)li=c.is_day?('🌙 Licht aus '+hhmm(c.loff)):('☀ Licht an '+hhmm(c.lon));
 if(c.is_day&&c.lux>0)li+=' · '+Math.round(c.lux)+' lx'+(c.alarm_light?' ⚠':'');
 return `<div class="ccard ${off?'off':''}">
   <div style="display:flex;align-items:center;gap:8px;margin-bottom:8px">
     <b style="font-size:15px">Kammer ${nm}</b>
     ${c.auto===false?'<span class="pill" style="padding:3px 9px;background:#3a0d10;border-color:#5b1a1f;color:#ff9aa0">⛔ Automatik AUS</span>':''}
     <span class="pill" style="padding:3px 9px">${day}</span>
     ${li?`<span class="pill" style="padding:3px 9px">${li}</span>`:''}
     <span class="sp" style="flex:1"></span>
     <select onchange="confirmPhase(${ch},this)">${po}</select>
   </div>
   <div style="display:grid;grid-template-columns:140px 1fr;gap:10px;align-items:center">
     <div style="text-align:center">
       <svg id="g${ch}" viewBox="0 0 200 120" style="width:100%"></svg>
       <div style="font-size:23px;font-weight:800;margin-top:-14px">${c.valid?fmt(c.vpd,2):'—'}</div>
       <div style="color:var(--mut);font-size:11px">${vpdLabel(c.valid?c.vpd:null,c.vlo,c.vhi)}</div>
       <div style="color:var(--mut);font-size:10px">ideal ${c.vlo}–${c.vhi} kPa</div>
     </div>
     <div class="tiles" style="grid-template-columns:1fr 1fr">
       <div class="tile"><div class="l">Temperatur</div><div><span class="v" style="color:${tcol||'var(--fg)'}">${c.valid?fmt(c.temp,1):'—'}</span><span class="u">°C</span></div><div class="rg">${c.tlo}–${c.thi} °C</div></div>
       <div class="tile"><div class="l">Feuchte</div><div><span class="v" style="color:${rcol||'var(--fg)'}">${c.valid?fmt(c.rh,1):'—'}</span><span class="u">%</span></div><div class="rg">${c.rlo}–${c.rhi} %</div></div>
       <div class="tile"><div class="l">Abluft</div><div><span class="v">${fmt(c.ifan,0)}</span><span class="u">%</span></div></div>
       <div class="tile"><div class="l">Sensor</div><div><span class="v" style="font-size:15px">${src}</span></div></div>
     </div>
   </div>
   ${al.length?`<div class="alarm" style="margin:10px 0 0;padding:8px 12px">⚠ ${al.join(', ')}</div>`:''}
 </div>`;
}
function renderChambers(s){
 // Nicht neu aufbauen, während der Nutzer ein Dropdown in den Kammer-Karten offen hat
 // (sonst wird das <select> ersetzt und springt zu) → erst nach Auswahl/Wegklick.
 const ae=document.activeElement;
 if(ae&&ae.tagName==='SELECT'&&$('chambers').contains(ae))return;
 const chs=s.chambers||[];
 $('chambers').innerHTML=[0,1].map(ch=>chamberCard(ch,chs[ch]||{phase:6})).join('');
 [0,1].forEach(ch=>{const c=chs[ch]||{};drawGauge($('g'+ch),c.valid?c.vpd:null,c.vlo||0.8,c.vhi||1.2);});
}

function planBar(c){
 if(!c||!c.pl||!c.pst||!c.pst.length)return '';
 const PHC=['#7bd88f','#5fd0c5','#56b6f7','#c084fc','#f0a35e','#c9a26b'];
 const tot=c.pst.reduce((a,s)=>a+s[1],0)||1;
 let segs='';c.pst.forEach(s=>{segs+=`<div title="${PH[s[0]]||'?'} ${s[1]}d" style="width:${(s[1]/tot*100).toFixed(2)}%;background:${PHC[s[0]]||'#888'};height:100%"></div>`;});
 const mark=Math.max(0,Math.min(100,((c.pd||1)-1)/tot*100));
 const next=c.pn>=0?`Nächste: <b>${PH[c.pn]||'?'}</b> in ${c.pni} Tg`:'letzte Phase';
 const aft=c.af?` · 🌗 Autoflower ${c.afon}/${24-c.afon}`:'';
 return `<div style="margin-top:10px"><div class="gsub">Grow-Plan · aktuell <b style="color:${PHC[c.phase]||'#fff'}">${PH[c.phase]||'?'}</b>${aft}</div><div style="display:flex;height:14px;border-radius:7px;overflow:hidden;position:relative;margin:5px 0;background:#222">${segs}<div style="position:absolute;top:-2px;left:${mark.toFixed(2)}%;width:2px;height:18px;background:#fff;box-shadow:0 0 3px #000"></div></div><div class="kv"><span>Plan-Tag ${c.pd}/${c.pt}</span><span>${next}</span></div></div>`;
}
function calendar(ch,nowS,gstart,gdays){
 const now=new Date(nowS*1000), nm='Kammer '+(ch?'B':'A');
 const c=(LS&&LS.chambers&&LS.chambers[ch])||{};
 if(!gstart){
  return `<div class="card" style="margin:0"><h3>🌱 ${nm} · Grow</h3>
   <div class="gset"><label style="color:var(--mut)">Start</label><input type="date" id="gd${ch}">
   <label style="color:var(--mut)">Tage</label><input type="number" id="gn${ch}" value="90" style="width:70px">
   <button class="btn" onclick="setGrow(${ch})">Grow starten</button></div></div>`;
 }
 const start=new Date(gstart*1000), harvest=new Date((gstart+gdays*DAY)*1000);
 const dayX=Math.max(0,Math.floor((nowS-gstart)/DAY))+1;
 const remain=Math.ceil((gstart+gdays*DAY-nowS)/DAY);
 const prog=Math.max(0,Math.min(100,(nowS-gstart)/(gdays*DAY)*100));
 const opt={day:'2-digit',month:'2-digit',year:'numeric'};
 const remTxt = remain>0 ? `Ernte in ~<b class="big">${remain}</b> Tagen` : (remain===0?'Ernte heute! 🎉':`+${-remain} Tage über Plan`);
 return `<div class="card" style="margin:0"><div class="gh">
  <div>
   <div class="gsub">${nm} · Grow-Tag</div>
   <div class="gday">${dayX} <span style="font-size:18px;color:var(--mut)">/ ${gdays}</span></div>
   <div class="bar"><div style="width:${prog.toFixed(1)}%"></div></div>
   <div class="kv"><span>${remTxt}</span></div>
   ${planBar(c)}
   <div style="margin-top:8px"><button class="btn gray" onclick="editGrow(${ch})">Grow ändern</button></div>
  </div>
  <div style="align-self:center;text-align:center;min-width:140px">
   <div class="gsub">Start</div>
   <div class="big" style="font-size:22px">${start.toLocaleDateString('de-DE',opt)}</div>
   <div class="gsub" style="margin-top:14px">ca. Ernte</div>
   <div class="big" style="font-size:22px">${harvest.toLocaleDateString('de-DE',opt)}</div>
  </div></div></div>`;
}
let editing=[false,false];
function buildGrows(){const s=LS,chs=s.chambers||[],nowS=s.now||Math.floor(Date.now()/1000);
 $('grows').innerHTML=[0,1].map(ch=>{const c=chs[ch]||{};
  return editing[ch]?calendar(ch,nowS,0,90):calendar(ch,nowS,c.gstart||0,c.gdays||90);}).join('');}
function editGrow(ch){editing[ch]=true;buildGrows();}
function setGrow(ch){
 const dv=$('gd'+ch).value, dn=+$('gn'+ch).value||90;
 const ep=dv?Math.floor(new Date(dv+'T00:00:00').getTime()/1000):Math.floor(Date.now()/1000);
 const ds=new Date(ep*1000).toLocaleDateString('de-DE');
 askConfirm('Grow <b>Kammer '+(ch?'B':'A')+'</b> mit Start <b>'+ds+'</b> ('+dn+' Tage) speichern?',()=>{
  editing[ch]=false;
  fetch('/api/grow?ch='+ch+'&start='+ep+'&days='+dn,{method:'POST'}).then(()=>setTimeout(load,400));
 });
}
function renderGrows(s){
 // im Bearbeiten-Modus oder bei fokussiertem Feld nicht überschreiben (Datum/Eingaben bleiben)
 if(editing[0]||editing[1])return;
 const ae=document.activeElement; if(ae&&$('grows').contains(ae))return;
 buildGrows();
}

function relTile(i,on,mode){
 return `<div class="rel ${on?'on':''}" onclick="openDev('r${i}')">
  <span class="badge ${mode?'man':''}">${mode?'MAN':'AUTO'}</span>
  <div class="rn">${RN[i]}</div><div class="rs">Steckdose ${i+1} · ${on?'AN':'aus'}</div></div>`;
}
function relay(i,on){fetch('/api/relay?id='+i+'&on='+on,{method:'POST'}).then(()=>setTimeout(load,250));}
function relayAuto(i){fetch('/api/relay?id='+i+'&auto=1',{method:'POST'}).then(()=>setTimeout(load,250));}
// Aktor-Befehle (nur fetch; Anzeige im Modal läuft per oninput inline)
function setLight(ch,v){fetch('/api/lightstep?ch='+ch+'&step='+v,{method:'POST'});}
function setIfan(v){fetch('/api/ifan?pct='+v,{method:'POST'});}
function ifanAuto(){fetch('/api/ifan?auto=1',{method:'POST'});closeDev();}
function setHumid(l){fetch('/api/humidifier?level='+l,{method:'POST'});}
function humidAuto(){fetch('/api/humidifier?auto=1',{method:'POST'});closeDev();}
function sendClip(){const st=$('clipst').value,sw=$('clipsw').value,nat=$('clipnat').checked?1:0;fetch('/api/clipfan?stufe='+st+'&schwenk='+sw+'&natural='+nat,{method:'POST'});}

// ── Geräte-Kacheln + Einstellungs-Popup ──
let LS={};   // letzter Status (für die Modal-Inhalte)
function renderDevices(s){
 if(!$('devmodal').classList.contains('hide')) return;  // Popup offen → Kacheln nicht neu bauen
 const hman=('humid_man'in s)&&s.humid_man>=0, fman=('ifan_man'in s)&&s.ifan_man>=0;
 const L=s.light||[0,0], r=s.relay||[], M=s.rmode||[];
 const C={0:'l0',1:'l1',2:'humid',5:'clip',6:'fan'};   // Steckdose → gekoppeltes Gerät
 // Statuswert (gross) + Einheit je Slot
 const stat=i=>{const k=C[i]; if(!r[i])return ['aus',''];
  if(k==='l0'||k==='l1')return [L[i],'%'];
  if(k==='humid')return hman?['Stufe '+s.humid_man,'']:['Auto',''];
  if(k==='clip')return s.clip_stufe>0?['Stufe '+s.clip_stufe,'']:['an',''];
  if(k==='fan')return 'ifan'in s?[s.ifan,'%'+(fman?' M':'')]:['an',''];
  return ['AN',''];};
 // Reihenfolge wie die Leiste: linke Spalte 1–5, rechte Spalte 6–10 (spaltenweise)
 const SCH=s.rsched||[];   // Zeitplan-Modus je Steckdose (0=aus 1=Tagesplan 2=Zyklus)
 $('devices').innerHTML=SLOTORDER.map(i=>{const k=C[i]||('r'+i), v=stat(i);
  return `<div class="outlet ${r[i]?'on':''}" onclick="openDev('${k}')">
   <div class="onum">${i+1}</div><div class="olbl">${RN[i]}</div>
   <div class="oval">${v[0]}<span class="ou">${v[1]}</span></div>
   <span class="oled"></span><span class="ogear">${SCH[i]?'⏱ ':''}${C[i]?'⚙':(M[i]?'MAN':'')}</span></div>`;}).join('');
}
function openDev(k){
 const s=LS, L=s.light||[0,0], R=s.relay||[], M=s.rmode||[]; let title='', body='';
 // Strom-an/aus-Block für die gekoppelte Steckdose eines Geräts
 const pwr=rel=>`<div style="display:flex;gap:10px;margin-bottom:12px">
   <button class="btn" style="flex:1;${R[rel]?'':'background:#283142'}" onclick="relay(${rel},1);closeDev()">Strom an</button>
   <button class="btn" style="flex:1;${R[rel]?'background:#283142':''}" onclick="relay(${rel},0);closeDev()">Strom aus</button></div>
   ${M[rel]?`<div style="margin-bottom:8px;font-size:11px"><a href="#" style="color:var(--mut)" onclick="relayAuto(${rel});closeDev();return false">↩ Steckdose auf Automatik</a></div>`:''}`;
 if(k[0]==='r'){const i=+k.slice(1), on=(s.relay||[])[i], mode=(s.rmode||[])[i], sch=(s.rsched||[])[i]||0;
  title='🔌 '+RN[i]+' · Steckdose '+(i+1);
  body=`<div class="light">
    <div style="display:flex;gap:10px;margin-bottom:12px">
     <button class="btn" style="flex:1;${on?'':'background:#283142'}" onclick="relay(${i},1);closeDev()">Einschalten</button>
     <button class="btn" style="flex:1;${on?'background:#283142':''}" onclick="relay(${i},0);closeDev()">Ausschalten</button></div>
    <div style="color:var(--mut);font-size:12px;margin-bottom:8px">Status: <b style="color:var(--fg)">${on?'AN':'aus'}</b> · Modus: <b style="color:var(--fg)">${mode?'MANUELL (Override)':'AUTO (Regelung)'}</b>${sch?` · Zeitplan: <b style="color:var(--fg)">⏱ ${sch==1?'Tagesplan':'Zyklus'}</b>`:''}</div>
    ${sch?'<div style="color:var(--mut);font-size:11px;margin-bottom:8px">Zeitplan in den <a href="/settings">Einstellungen</a> · Priorität: Manuell &gt; Zeitplan &gt; Regelung</div>':''}
    ${mode?`<button class="btn gray" onclick="relayAuto(${i});closeDev()">↩ Auf Automatik zurück</button>`:'<div style="color:var(--mut);font-size:11px">Wird von der Klimaregelung gesteuert.</div>'}
   </div>`;
 } else if(k==='l0'||k==='l1'){const ch=k==='l0'?0:1, on=(s.relay||[])[ch], mode=(s.rmode||[])[ch];
  title='💡 Licht '+(ch+1);
  body=`<div class="light">
    <div style="display:flex;gap:10px;margin-bottom:12px">
     <button class="btn" style="flex:1;${on?'':'background:#283142'}" onclick="relay(${ch},1);closeDev()">Einschalten</button>
     <button class="btn" style="flex:1;${on?'background:#283142':''}" onclick="relay(${ch},0);closeDev()">Ausschalten</button></div>
    <b>Helligkeit (Stufe 0–10)</b> <span style="float:right;color:var(--mut)" id="dvv">Stufe ${(s.lstep||[])[ch]||0}</span>
    <input type="range" min="0" max="10" step="1" value="${(s.lstep||[])[ch]||0}" oninput="$('dvv').textContent='Stufe '+this.value;setLight(${ch},this.value)">
    <div style="margin-top:8px;color:var(--mut);font-size:12px">Strom: <b style="color:var(--fg)">${on?'AN':'aus'}</b>${mode?`  ·  <a href="#" onclick="relayAuto(${ch});closeDev();return false">↩ Steckdose auf Automatik</a>`:''}</div>
   </div>`;
 } else if(k==='fan'){const fman=('ifan_man'in s)&&s.ifan_man>=0; title='🌀 Abluft (Lüfter)';
  body=`<div class="light">${pwr(DREL.fan)}<b>Drehzahl</b> <span style="float:right;color:var(--mut)" id="dvv">${'ifan'in s?s.ifan:0}% · ${fman?'Manuell':'Auto'}</span>
   <input type="range" min="0" max="100" value="${'ifan'in s?s.ifan:0}" oninput="$('dvv').textContent=this.value+'% · Manuell';setIfan(this.value)">
   <div style="margin-top:10px"><button class="btn gray" onclick="ifanAuto()">↩ Auto (Regelung)</button></div></div>`;
 } else if(k==='humid'){const hman=('humid_man'in s)&&s.humid_man>=0; title='💧 Luftbefeuchter';
  body=`<div class="light">${pwr(DREL.humid)}<b>Nebelstufe</b> <span style="float:right;color:var(--mut)" id="dvv">${hman?('Stufe '+s.humid_man):'Auto (VPD)'}</span>
   <input type="range" min="0" max="4" value="${hman?s.humid_man:0}" oninput="$('dvv').textContent='Stufe '+this.value;setHumid(this.value)">
   <div style="margin-top:6px;color:var(--mut);font-size:11px">0 = aus · 1–4 = Nebelstufe</div>
   <div style="margin-top:8px"><button class="btn gray" onclick="humidAuto()">↩ Auto (VPD-Regelung)</button></div></div>`;
 } else if(k==='clip'){const sw=s.clip_schwenk||0, st=s.clip_stufe||0; title='🪭 Clip-Fan';
  body=`<div class="light">${pwr(DREL.clip)}<b>Stufe</b> <span style="float:right;color:var(--mut)"><span id="clipstv">${st}</span>/10</span>
   <input id="clipst" type="range" min="0" max="10" value="${st}" oninput="$('clipstv').textContent=this.value" onchange="sendClip()">
   <div class="row" style="margin-top:10px;display:flex;gap:14px;align-items:center;flex-wrap:wrap">
    <label style="color:var(--mut)">Schwenk <select id="clipsw" onchange="sendClip()">
     <option value="0" ${sw==0?'selected':''}>aus</option><option value="5" ${sw==5?'selected':''}>45°</option><option value="10" ${sw==10?'selected':''}>90°</option></select></label>
    <label style="color:var(--mut)"><input type="checkbox" id="clipnat" onchange="sendClip()" ${s.clip_nat?'checked':''}> Natural Wind</label>
   </div></div>`;
 }
 $('dvtitle').textContent=title; $('dvbody').innerHTML=body; $('devmodal').className='modal';
}
function closeDev(){$('devmodal').className='modal hide';}
function setPhase(ch,p){fetch('/api/phase?ch='+ch+'&p='+p,{method:'POST'}).then(()=>setTimeout(load,300));}
// Bestätigungsdialog gegen versehentliches Verstellen
let _cfmY=null,_cfmN=null;
function askConfirm(text,onYes,onNo){_cfmY=onYes;_cfmN=onNo||null;$('cfmtext').innerHTML=text;$('cfm').className='modal';}
function cfmYes(){$('cfm').className='modal hide';const f=_cfmY;_cfmY=_cfmN=null;if(f)f();}
function cfmNo(){$('cfm').className='modal hide';const f=_cfmN;_cfmY=_cfmN=null;if(f)f();}
function confirmPhase(ch,sel){const p=+sel.value;
 askConfirm('Profil <b>Kammer '+(ch?'B':'A')+'</b> auf <b>'+PH[p]+'</b> ändern?',
  ()=>setPhase(ch,p),
  ()=>{sel.value=(LS.chambers&&LS.chambers[ch]?LS.chambers[ch].phase:0);});}
function ackAlarm(){fetch('/api/ack',{method:'POST'}).then(()=>setTimeout(load,300));}
const hhmm=m=>String(Math.floor(m/60)).padStart(2,'0')+':'+String(m%60).padStart(2,'0');

let _alogT=0;
async function renderAlarmLog(now){
 if(now-_alogT<20000)return; _alogT=now;
 let ev;try{ev=await(await fetch('/api/alarmlog')).json();}catch(e){return;}
 const card=$('alogcard');
 if(!ev||!ev.length){if(card)card.style.display='none';return;}
 const TN={temp:'🌡 Temperatur',mold:'💧 Feuchte/Schimmel',co2:'CO₂',sensor:'⚠ Sensor-Ausfall',light:'💡 Lampe',water:'💧 Wasserwert'};
 if(card)card.style.display='';
 $('alog').innerHTML=ev.slice(0,12).map(e=>{const d=new Date(e.ts*1000).toLocaleString('de-DE',{day:'2-digit',month:'2-digit',hour:'2-digit',minute:'2-digit'});return `<div style="padding:2px 0">${d} — ${TN[e.type]||e.type}: <b style="color:${e.on?'#f85149':'#3fb950'}">${e.on?'ausgelöst':'normalisiert'}</b></div>`;}).join('');
}
async function load(){
 let s; try{s=await(await fetch('/api/status',{cache:'no-store'})).json();}catch(e){return;}
 LS=s; const D='—';
 // clock
 if(s.now)$('clock').textContent=new Date(s.now*1000).toLocaleString('de-DE',{weekday:'short',day:'2-digit',month:'2-digit',year:'numeric',hour:'2-digit',minute:'2-digit'})+(s.is_day?' ☀':' 🌙');
 // alarm
 const al=[];if(s.alarm_temp)al.push('Temperatur');if(s.alarm_mold)al.push('Feuchte/Schimmel');if(s.alarm_co2)al.push('CO₂');if(s.alarm_sensor)al.push('Sensor-Ausfall');if(s.alarm_light)al.push('💡 Lampe aus/defekt?');if(s.alarm_water)al.push('💧 Wasserwert');
 const ab=$('alarm');if(al.length){ab.className='alarm';ab.innerHTML='⚠ Alarm: '+al.join(', ')+' <button class="btn gray" style="margin-left:10px;padding:5px 12px;font-weight:600" onclick="ackAlarm()">🔕 Quittieren</button>';}else ab.className='alarm hide';
 const acb=$('acb');if(s.ac_demand){acb.className='info';acb.textContent='❄ Klima läuft — '+(['','kühlen','entfeuchten','kühlen + entfeuchten'][s.ac_mode]||'aktiv')+' (Free-Cooling reicht nicht)';}else acb.className='info hide';
 // Warnbanner: Klima-Automatik ausgeschaltet → Profile/Regelung wirkungslos (Vorfall 2026-06-12)
 const ao=$('autooff');const offs=(s.chambers||[]).map((c,i)=>c.auto===false?i:-1).filter(i=>i>=0);
 if(offs.length){ao.className='alarm';ao.innerHTML='⛔ Klima-Automatik AUSGESCHALTET: '+offs.map(i=>'Kammer '+'AB'[i]).join(' + ')+' — Profile, Befeuchter, Abluft & Lichtplan sind wirkungslos!'+offs.map(i=>` <button class="btn gray" style="margin-left:10px;padding:5px 12px;font-weight:600" onclick="chamberAuto(${i})">Kammer ${'AB'[i]} einschalten</button>`).join('');}
 else ao.className='alarm hide';
 // Grow-Timeline pro Kammer (ganz oben)
 renderGrows(s);
 // Kammern (2-Kammer-Ansicht)
 renderChambers(s);
 renderAlarmLog(s.now ? s.now*1000 : Date.now());   // Alarm-Protokoll (gedrosselt)
 // globale Sensoren (+ Bewässerungs-Status)
 const tago=e=>{let d=(s.now||0)-e;if(d<0)d=0;return d<3600?Math.round(d/60)+' min':d<86400?(d/3600).toFixed(1).replace('.',',')+' h':Math.round(d/86400)+' T';};
 const wnx=s.wat_next?new Date(s.wat_next*1000).toLocaleString('de-DE',{weekday:'short',hour:'2-digit',minute:'2-digit'}):'';
 const wrg=[s.wat_last?'zuletzt vor '+tago(s.wat_last):'noch nie gegossen',wnx?'nächste '+wnx:''].filter(x=>x).join(' · ');
 $('klima').innerHTML=[
  ['CO₂',s.co2_ok?fmt(s.co2,0):D,'ppm',s.co2_ok?co2col(s.co2):'','350–1500 ppm'],
  ['PPFD',s.par_ok?fmt(s.ppfd,0):D,'µmol','',''],
  ['Substrat F',s.soil_ok?fmt(s.soil_m,1):D,'%',s.soil_ok?clr(s.soil_m,40,70,10):'','40–70 %'],
  ['Substrat EC',s.soil_ok?fmt(s.soil_ec,2):D,'mS',s.soil_ok?clr(s.soil_ec,0.8,2.5,0.6):'','0,8–2,5 mS'],
  ['Substrat T',s.soilt_ok?fmt(s.soil_t,1):D,'°C',s.soilt_ok?clr(s.soil_t,18,26,3):'','18–26 °C'],
  ['💧 Bewässerung',s.wat_on?'läuft':(s.wat_mode?['','Intervall','Substrat'][s.wat_mode]:'Aus'),'',s.wat_on?C_OK:(s.wat_mode?'':'var(--mut,#8b949e)'),wrg],
  ['DLI',fmt(s.dli,2),'mol',(s.dli_tgt>0&&s.dli>=s.dli_tgt)?C_OK:'',s.dli_tgt>0?('Ziel '+s.dli_tgt+' mol → Drossel'):''],
  ['Dachboden T',s.attic_ok?fmt(s.attic_t,1):D,'°C','',''],
  ['Dachboden F',s.attic_ok?fmt(s.attic_rh,1):D,'%','',''],
  ['Klima',s.ac_demand?(['aus','kühlen','entfeucht.','kühl+ent'][s.ac_mode]||'an'):'aus','',s.ac_demand?C_WARN:C_OK,'']
 ].map(t=>`<div class="tile"><div class="l">${t[0]}</div><div><span class="v" style="color:${t[3]||'var(--fg,#e6edf3)'}">${t[1]}</span><span class="u">${t[2]}</span></div>${t[4]?`<div class="rg">${t[4]}</div>`:''}</div>`).join('');
 // Wasserwerte (RDWC) — nur zeigen, wenn der iHub schon mal Werte von HA empfangen hat.
 // Jede Kachel ist klickbar → 7-Tage-Verlauf (History-Kanäle m=9..15). Spalten:
 // [Name, Wert, Einheit, Ampelfarbe, Bereichstext, History-m, Chartfarbe]
 const w=s.water;
 if(w&&(w.ph_ok||w.ec_ok||w.orp_ok||w.temp_ok)){
  $('watercard').style.display='';
  $('waterstale').style.display=w.fresh?'none':'';
  const wv=(ok,val,dec)=>ok?fmt(val,dec):D;
  $('water').innerHTML=[
   ['pH',wv(w.ph_ok,w.ph,2),'',w.ph_ok?clr(w.ph,5.8,6.2,0.4):'','Ziel 5,8–6,2',9,'#3fb950'],
   ['Wassertemp',wv(w.temp_ok,w.temp,1),'°C',w.temp_ok?clr(w.temp,18,20,2):'','Ziel 18–20 °C',11,'#f5a524'],
   ['ORP',wv(w.orp_ok,w.orp,0),'mV',w.orp_ok?clr(w.orp,350,450,80):'','Ziel 350–450 mV',10,'#a371f7'],
   ['EC',wv(w.ec_ok,w.ec,2),'mS','','phasenabh.',12,'#388bfd'],
   ['TDS',wv(w.tds_ok,w.tds,0),'ppm','','',13,'#e5484d'],
   ['Salinität',wv(w.sal_ok,w.sal,0),'ppm','','',14,'#56d4dd'],
   ['Dichte',wv(w.sg_ok,w.sg,3),'','','SG',15,'#ffd9a0']
  ].map(t=>`<div class="tile" style="cursor:pointer" title="7-Tage-Verlauf" onclick="openHist('${t[0]} 💧',[{c:'${t[7]}',n:'${t[2]||t[0]}',m:${t[6]}}])"><div class="l">${t[0]} 📈</div><div><span class="v" style="color:${t[3]||'var(--fg,#e6edf3)'}">${t[1]}</span><span class="u">${t[2]}</span></div>${t[4]?`<div class="rg">${t[4]}</div>`:''}</div>`).join('');
 }else{$('watercard').style.display='none';}
 // power
 $('power').innerHTML=[
  ['Leistung',s.pw_ok?fmt(s.power,0):D,'W','',''],
  ['Spannung',s.pw_ok?fmt(s.voltage,1):D,'V',s.pw_ok?clr(s.voltage,220,245,10):'','220–245 V'],
  ['Strom',s.pw_ok?fmt(s.current,2):D,'A','',''],
  ['Energie',s.pw_ok?fmt(s.energy,2):D,'kWh','','']
 ].map(t=>`<div class="tile"><div class="l">${t[0]}</div><div><span class="v" style="color:${t[3]||'var(--fg,#e6edf3)'}">${t[1]}</span><span class="u">${t[2]}</span></div>${t[4]?`<div class="rg">${t[4]}</div>`:''}</div>`).join('');
 // charts
 push(s);renderCharts();renderWaterCharts(s);
 // Geräte + Steckdosen als Kacheln (Optionen im Popup beim Klick)
 LS=s; renderDevices(s);
}
// Kammer-Automatik EINschalten (Banner-Button). Abschalten gibt es im Dashboard bewusst
// NICHT — das geht nur per API mit confirm=1 (Schutz vor versehentlichem Deaktivieren).
async function chamberAuto(ch){await fetch('/api/chamber?ch='+ch+'&auto=1',{method:'POST'});setTimeout(load,400);}
load();setInterval(load,3000);
window.addEventListener('resize',renderCharts);
</script></body></html>)HTML";
