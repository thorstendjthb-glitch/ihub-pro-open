// settings_html.h — Settings-Seite (clean/professionell), lädt/speichert via API.
#pragma once

static const char SETTINGS_HTML[] = R"HTML(<!doctype html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>iHub-Pro · Einstellungen</title>
<script>try{if(localStorage.getItem('ihub_lang')==='en'){var s=document.createElement('script');s.src='/i18n.js?v=3';s.defer=true;document.head.appendChild(s);}}catch(e){}</script>
<style>
:root{--bg:#0d1117;--card:#161b22;--bd:#21262d;--fg:#e6edf3;--mut:#8b949e;--grn:#3fb950;--acc:#2ea043}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--fg);font:14px/1.5 system-ui,sans-serif;padding:16px;max-width:1200px;margin:auto}
h1{font-size:18px;font-weight:600;margin-bottom:4px}
a{color:var(--grn);text-decoration:none}
.sec{font-size:13px;color:var(--mut);text-transform:uppercase;letter-spacing:.05em;margin:18px 0 8px}
.card{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:14px;margin-bottom:12px}
table{width:100%;border-collapse:collapse;font-size:13px}
th,td{padding:6px 6px;text-align:center;border-bottom:1px solid var(--bd)}
th{color:var(--mut);font-weight:500}
td:first-child,th:first-child{text-align:left;color:var(--fg);font-weight:500}
input{background:#0d1117;border:1px solid var(--bd);color:var(--fg);border-radius:6px;padding:5px;width:58px;text-align:center}
input.wide{width:100%;text-align:left}
input[type=time]{width:96px}
button{background:var(--acc);color:#fff;border:none;border-radius:8px;padding:10px 18px;font-weight:600;cursor:pointer;font-size:14px}
button:hover{background:var(--grn)}
.row{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin:6px 0}
.row label{min-width:130px;color:var(--mut)}
.ok{color:var(--grn);margin-left:10px}
small{color:var(--mut)}
</style></head><body>
<h1>⚙ Einstellungen <a href="/" style="font-size:14px;font-weight:400">← Dashboard</a></h1>

<div class="sec">System &amp; Netzwerk</div>
<div class="card">
<div class="row"><label>Sprache / Language</label><select id="sy_lang" style="width:140px" onchange="setLang(this.value)"><option value="de">Deutsch</option><option value="en">English</option></select> <small>Stellt die Sprache des WebUI um (pro Browser gespeichert).</small></div>
<div class="row"><label>Hostname (mDNS)</label><input id="sy_host" style="width:140px;text-align:left"> <small>Gerät erreichbar als http://&lt;name&gt;.local/ — wirkt nach Reboot</small></div>
<div class="row"><label>Zeitzone</label><select id="sy_tz" style="width:230px"><option value="CET-1CEST,M3.5.0,M10.5.0/3">Mitteleuropa – Berlin/Wien/Zürich (CET/CEST)</option><option value="GMT0BST,M3.5.0/1,M10.5.0">Westeuropa – London/Lissabon (GMT/BST)</option><option value="EET-2EEST,M3.5.0/3,M10.5.0/4">Osteuropa – Athen/Helsinki (EET/EEST)</option><option value="MSK-3">Moskau (MSK, ohne Sommerzeit)</option><option value="UTC0">UTC (ohne Sommerzeit)</option><option value="EST5EDT,M3.2.0,M11.1.0">USA Ost – New York (ET)</option><option value="PST8PDT,M3.2.0,M11.1.0">USA West – Los Angeles (PT)</option></select> <small>Ortszeit für Licht-/Bewässerungs-/Steckdosen-Zeitpläne. Wirkt sofort, inkl. autom. Sommerzeit</small></div>
<div class="row"><label>Hotspot-Passwort</label><input id="sy_appw" readonly style="width:170px;text-align:left"> <small>WLAN „iHub" für Notzugriff ohne Heimnetz — pro Gerät zufällig (nicht mehr universell)</small></div>
<div class="row"><label>API-Schutz</label><input type="checkbox" id="sy_prot" disabled> <small>Sobald ein <b>Token</b> gesetzt ist, brauchen ALLE Steuer-/Settings-/Diagnose-Endpunkte <code>?key=&lt;Token&gt;</code> (automatisch erzwungen). Ohne Token bleibt alles offen. Dashboard fragt bei Bedarf einmalig nach dem Token.</small></div>
</div>

<div class="sec">Sicherheit &amp; Login</div>
<div class="card">
<div class="row"><label>Benutzername</label><input id="au_user" style="width:160px;text-align:left" value="admin"></div>
<div class="row"><label>Neues Passwort</label><input id="au_pass" type="password" style="width:200px" placeholder="leer lassen = unverändert"></div>
<div class="row"><label>Passwort wiederholen</label><input id="au_pass2" type="password" style="width:200px"></div>
<div class="row"><label>Login deaktivieren</label><input type="checkbox" id="au_clear"> <small>Entfernt das Passwort — WebUI wieder ohne Anmeldung erreichbar.</small></div>
<div class="row"><label>Login-Status</label><span id="au_stat" style="color:#9ab"></span> &nbsp;<button type="button" style="background:#283142" onclick="fetch('/api/logout',{method:'POST'}).then(()=>location.href='/login')">Abmelden</button></div>
<hr style="border:0;border-top:1px solid #2c3744;margin:12px 0">
<div class="row"><label>API-Token</label><input id="mq_token" style="width:230px;text-align:left" placeholder="leer lassen = unverändert"></div>
<div class="row"><label></label><small>Für Home Assistant / Skripte / Headless-OTA: <code>?key=&lt;Token&gt;</code> oder Header <code>Authorization: Bearer &lt;Token&gt;</code>. Login per Browser läuft über das Passwort oben.</small></div>
<div class="row"><label>Token entfernen</label><input type="checkbox" id="mq_token_clear"> <small id="tk_stat"></small></div>
</div>

<div class="sec">Grow-Profile — Sollwerte je Profil</div>
<div class="card" style="overflow-x:auto">
<table id="sp"><thead><tr>
<th>Profil</th><th>Licht h</th><th>Licht an</th><th>Rampe min</th><th>VPD</th><th>VPD ±</th>
<th>T Tag</th><th>T Nacht</th><th>T ±</th><th>rH Tag</th><th>rH Nacht</th>
<th>CO₂</th><th>CO₂ Tag</th><th>Licht %</th><th>DLI-Ziel</th><th>PPFD-Regel.</th><th>PPFD-Ziel</th>
<th>⚠ T-Max</th><th>⚠ rH-Max</th><th>⚠ T-Min</th><th>⚠ CO₂-Max</th>
</tr></thead><tbody></tbody></table>
<small>Rampe = Sonnenauf-/untergang-Dauer (0 = aus). „Automatics" fährt durchgehend (20/4). DLI-Ziel in mol/m²/Tag (0 = aus; braucht PAR-Sensor — bei Erreichen wird das Licht auf die DLI-Drossel gedimmt, die Photoperiode bleibt). Alarm-Spalten: T-Max/T-Min in °C, rH-Max in %, CO₂-Max in ppm — jeweils 0 = Alarm aus.</small>
</div>

<div class="sec">Alarme & Sicherheit — global</div>
<div class="card">
<div class="row"><label>Buzzer aktiv</label><input type="checkbox" id="g_buz"> <small>akustischer Alarm an/aus</small></div>
<div class="row"><label>Alarm-Wiederholung</label><input id="g_rep"> <small>Sekunden zwischen Buzzer-Tönen bei Daueralarm</small></div>
<div class="row"><label>Lüfter Mindestlaufzeit</label><input id="g_fan"> <small>Sekunden — Anti-Takt-Sperre Abluft</small></div>
<div class="row"><label>Übertemp → Lampen drosseln</label><input type="checkbox" id="g_ldim"> <small>bei Übertemp-Alarm auf 50%</small></div>
<div class="row"><label>Übertemp → Abluft erzwingen</label><input type="checkbox" id="g_lfan"></div>
<div class="row"><label>Sensor-Ausfall-Alarm</label><input type="checkbox" id="g_sens"> <small>Alarm wenn Klimasensor nicht antwortet</small></div>
<div class="row"><label>Abluft Grundlast %</label><input id="g_fb" style="width:70px"> <small>RS-485-Inline-Fan Dauerzug (Geruch/Aktivkohle)</small></div>
<div class="row"><label>Abluft Maximal %</label><input id="g_fm" style="width:70px"> <small>Drehzahl bei Hitze/Schimmel</small></div>
<div class="row"><label>Klimaanlage-Verzögerung min</label><input id="g_acd" style="width:70px"> <small>Abluft läuft so lange erfolglos gegen Kühl-/Trocknungsbedarf (Außenluft zu warm/feucht) → erst dann AC anfordern. 0 = sofort</small></div>
<div class="row"><label>CO₂-Frischluft-Baseline ppm</label><input id="g_co2b" style="width:70px"> <small>CO₂ = Sensor-Messanteil + Baseline. Frischluft-Kalibrierung: bei frischer Außenluft soll der Wert ~420 zeigen. Default 420</small></div>
<div class="row"><label>Klimaanlage in Kammer</label><select id="g_acch" style="width:90px"><option value="0">Kammer A</option><option value="1">Kammer B</option><option value="-1">keine AC</option></select> <small>Nur diese Kammer fordert die Klima an und drosselt bei AC-Betrieb ihre Abluft</small></div>
<div class="row"><label>PAR-/PPFD-Sensor in Kammer</label><select id="g_parch" style="width:90px"><option value="0">Kammer A</option><option value="1">Kammer B</option></select> <small>Nur dort greifen PPFD-Lichtregelung + DLI-Drossel; die andere Kammer nutzt feste %-Helligkeit</small></div>
</div>

<div class="sec">Beleuchtung — 0-10V Polarität</div>
<div class="card">
<div class="row"><label>Licht 1 invertiert</label><input type="checkbox" id="inv0"> <small>an = 0V hell / 10V aus</small></div>
<div class="row"><label>Licht 2 invertiert</label><input type="checkbox" id="inv1"> <small>an = 0V hell / 10V aus</small></div>
<div class="row"><label>DLI-Drossel %</label><input id="g_dlif"> <small>Helligkeit nach Erreichen des DLI-Ziels (Spalte „DLI-Ziel" oben); Photoperiode läuft weiter</small></div>
<div class="row"><label>Lampen-Ausfall-Lux</label><input id="g_lfl" style="width:70px"> <small>Mindest-Lux am TH3in1-Lichtsensor, wenn Licht laut Plan AN sein soll — sonst Lampen-Ausfall-Alarm. 0 = aus</small></div>
</div>

<div class="sec">Kammern — Profil &amp; Klimasensor je Kammer</div>
<div class="card" style="overflow-x:auto">
<table id="cham"><thead><tr><th>Kammer</th><th>Profil</th><th>Klimasensor</th><th>BLE-Gerät (nur bei BLE)</th></tr></thead><tbody></tbody></table>
<small>Sensor-Quelle: <b>Sensor-Bus-TH3in1</b> (Haupt-Buchse) · <b>Fan-Bus-TH3in1</b> (Lüfter-Buchse) · <b>BLE</b> = ThermoPro TP357 (Gerät rechts wählen). Jede Kammer regelt eigenständig.</small>
</div>

<div class="sec">Grow-Plan — zeitgesteuerte Phasenabfolge</div>
<div class="card" id="gpcard"></div>
<small>Ist der Plan aktiv und ein Grow-Start gesetzt, wechselt das Profil automatisch nach den Tagen (manuelle Profilwahl pausiert). Nach Plan-Ende bleibt die letzte Phase. Tipp: 1 Woche = 7 Tage. Klima/VPD wird je Profil geregelt.</small>

<div class="sec">Dachboden / Quellluft — Free-Cooling-Referenz</div>
<div class="card">
<div class="row"><label>Sensor-Quelle</label><select id="atsrc" data-csrc="2"></select></div>
<div class="row"><label>BLE-Gerät (bei BLE)</label><select id="atmac" data-cmac="2"></select></div>
<small>Vergleicht die Quellluft mit der Kammer: ist sie kühler bzw. (absolut) trockener → es wird gelüftet, sonst Klimaanlage angefordert. Quelle z.B. ein BLE-TP357 am Dachboden statt des Fan-Bus-Sensors.</small>
</div>

<div class="sec">Geräte-Zuordnung — Steckdose → Kammer &amp; Rolle</div>
<div class="card" style="overflow-x:auto">
<table id="asgT"><thead><tr><th>Steckdose</th><th>Kammer</th><th>Rolle</th></tr></thead><tbody></tbody></table>
<div class="row" style="margin-top:10px"><label>Dimmer 1 (Licht)</label><select id="dimch0"></select></div>
<div class="row"><label>Dimmer 2 (Licht)</label><select id="dimch1"></select></div>
<div class="row"><label>Abluft stufenlos (RS-485)</label><select id="ifanch"></select></div>
<small>Jede Steckdose gehört einer Kammer (A/B) und hat eine Rolle (Licht, Heizung, …). Die Regelung schaltet je Kammer nur ihre eigenen Geräte. „—" = keiner Kammer zugeordnet (nur manuell/MQTT). Der stufenlose Abluft-iFan + MarsHydro-Luftbefeuchter gehören genau der hier gewählten Kammer.</small>
</div>

<div class="sec">Wasserwerte-Alarme (RDWC) — Werte kommen vom Tester über Home Assistant</div>
<div class="card">
<div class="row"><label>Alarme aktiv</label><input type="checkbox" id="w_al_en"> <small>Warnung (Buzzer/Dashboard/HA) bei pH/Wassertemp/ORP außerhalb des Korridors. Default AUS — erst einschalten, wenn echte Nährlösung läuft (sonst Fehlalarm bei Testwasser).</small></div>
<div class="row"><label>pH min / max</label><input id="w_phmin"> <input id="w_phmax"> <small>RDWC-Ziel 5,8–6,2 · 0 = diese Grenze aus</small></div>
<div class="row"><label>Wassertemp max / min</label><input id="w_tmax"> <input id="w_tmin"> <small>°C · Ziel 18–20 · 0 = diese Grenze aus</small></div>
<div class="row"><label>ORP min</label><input id="w_orpmin"> <small>mV · &lt; Wert = anaerob/Pathogen-Risiko (RDWC-Routine 350–450) · 0 = aus</small></div>
</div>

<div class="sec">Bewässerung — Automatik</div>
<div class="card">
<div class="row"><label>Modus</label><select id="w_mode">
<option value="0">Aus (nur manuell)</option>
<option value="1">Intervall</option>
<option value="2">Substratfeuchte</option></select></div>
<div class="row"><label>Gieß-Dauer</label><input id="w_dur"> <small>Sekunden pro Gießvorgang</small></div>
<div class="row"><label>Intervall</label><input id="w_int"> <small>Stunden zwischen Gießvorgängen (Modus Intervall)</small></div>
<div class="row"><label>Start unter</label><input id="w_low"> <small>% Substratfeuchte (Modus Substratfeuchte)</small></div>
<div class="row"><label>Mindestpause</label><input id="w_pause"> <small>Minuten zwischen Gießvorgängen (Modus Substratfeuchte)</small></div>
<div class="row"><label>Nur bei Licht</label><input type="checkbox" id="w_day"> <small>nur während der Lichtphase der zugeordneten Kammer gießen</small></div>
<div class="row"><label>⚠ Not-Abschaltung</label><input id="g_wmax"> <small>Minuten Dauerlauf-Limit (Überflutungsschutz, 0 = aus) — gilt auch für manuelles Gießen</small></div>
<small>Gegossen wird über die Steckdose mit Rolle „Bewässerung" (siehe Geräte-Zuordnung, Standard Steckdose 5). Steht die Steckdose auf MANUELL oder hat sie einen Zeitplan, pausiert die Automatik.</small>
</div>

<div class="sec">Zeitpläne — Steckdosen</div>
<div class="card" style="overflow-x:auto">
<table id="schedT"><thead><tr><th>Steckdose</th><th>Modus</th><th>Ein (Uhrzeit)</th><th>Aus (Uhrzeit)</th><th>Zyklus an (min)</th><th>Zyklus aus (min)</th></tr></thead><tbody></tbody></table>
<small>Tagesplan: täglich Ein/Aus zur Uhrzeit (über Mitternacht erlaubt). Zyklus: x min an / y min aus, Raster ab Mitternacht. Priorität: <b>Manuell &gt; Zeitplan &gt; Klimaregelung/Bewässerung</b> — eine Steckdose mit Zeitplan wird von der Regelung nicht mehr angefasst. Modus „Aus" gibt sie der Regelung zurück.</small>
</div>

<div class="sec">Verlauf — Flash-Sicherung</div>
<div class="card">
<div class="row"><label>Speicher-Intervall</label><input id="g_hist"> <small>Minuten — sichert 7-Tage-Verlauf + DLI-Tagessumme in den Flash (überlebt Reboot/Update). 0 = aus, Standard 60. Kleinere Werte = mehr Flash-Verschleiß.</small></div>
</div>

<div class="sec">MQTT-Broker (Home Assistant)</div>
<div class="card">
<div class="row"><label>Broker-URI</label><input class="wide" id="mq_uri" placeholder="mqtt://homeassistant.local:1883"></div>
<div class="row"><label>Benutzer</label><input class="wide" id="mq_user"></div>
<div class="row"><label>Passwort</label><input class="wide" id="mq_pass" type="password"></div>
</div>

<div class="sec">Sensor-Kalibrierung (Offsets)</div>
<div class="card">
<table><thead><tr><th>Sensor</th><th>Temp-Offset °C</th><th>rH-Offset %</th></tr></thead><tbody id="soffb"></tbody></table>
<small>Additive Korrektur driftender RJ12-Sensoren — wird VOR der VPD-Berechnung angewandt. Bereich ±10 °C / ±20 %. Tipp: gegen ein Referenzthermo-/hygrometer abgleichen.</small>
</div>

<div class="sec">Firmware-Update</div>
<div class="card">
<div class="row"><label>Aktuelle Firmware</label><span id="fw_build" style="color:#9ab">…</span></div>
<div class="row"><label>Neue .bin-Datei</label><input type="file" id="fw_file" accept=".bin" style="display:none" onchange="fwPick()"><button type="button" style="background:#283142" onclick="fw_file.click()">Datei wählen</button> <span id="fw_fname" style="color:#9ab;margin-left:8px">keine ausgewählt</span></div>
<div class="row"><label></label><button type="button" onclick="fwUpload()">Hochladen &amp; Flashen</button> <button type="button" style="background:#283142;margin-left:8px" onclick="fwReboot()">Neustart</button></div>
<div class="row" id="fw_progrow" style="display:none"><label>Fortschritt</label><progress id="fw_prog" max="100" value="0" style="width:220px;vertical-align:middle"></progress> <span id="fw_pct" style="margin-left:8px"></span></div>
<div class="row"><label></label><small>Lädt die <code>.bin</code> (aus <code>.pio/build/ihub/firmware.bin</code>) direkt aufs Gerät; danach startet der iHub neu. Schlägt ein Image fehl, rollt der Boot-Validator automatisch auf die vorherige Version zurück.</small></div>
</div>

<button onclick="save()">Speichern</button>
<button type="button" style="background:#283142;margin-left:8px" onclick="location.href='/'">← Zurück zum Dashboard</button>
<button type="button" style="background:#283142;margin-left:8px" onclick="exportCfg()" title="Alle Einstellungen als JSON sichern (ohne Passwörter/Token)">⬇ Export</button>
<button type="button" style="background:#283142;margin-left:8px" onclick="impfile.click()">⬆ Import</button>
<input type="file" id="impfile" accept=".json,application/json" style="display:none" onchange="importCfg(this)">
<span class="ok" id="ok"></span>

<script>
// API-Schutz: gespeicherten Token automatisch an /api-Aufrufe anhängen; bei 401 einmal nachfragen.
(()=>{const _f=window.fetch.bind(window);window.fetch=(u,o)=>{try{const k=localStorage.getItem('apikey');if(k&&typeof u==='string'&&u.startsWith('/api'))u+=(u.includes('?')?'&':'?')+'key='+encodeURIComponent(k);}catch(e){}
 return _f(u,o).then(r=>{if(r.status===401&&!window._keyAsk){window._keyAsk=1;const t=prompt('API-Schutz aktiv — Token eingeben:');if(t){localStorage.setItem('apikey',t.trim());location.reload();}}return r;});};})();
const PH=["Seeds","Stecklinge","Wuchs","Blüte","Automatics","Trocknen"];
const F=["light_on_h","light_start_h","ramp_min","vpd_target","vpd_deadband","temp_day","temp_night","temp_deadband","rh_day","rh_night","co2_target","co2_only_daylight","light_pct","dli_target","light_mode","ppfd_target","temp_alarm","rh_alarm","temp_min_alarm","co2_max_alarm"];
let data;
// Sollwert-Anzeige: auf max. 2 Nachkommastellen runden + überflüssige Nullen entfernen
// (cJSON liefert z.B. 1.3999999 / 0.6000000 → "1.4" / "0.6").
const fmtSp=v=>{const n=parseFloat(v);return isNaN(n)?v:String(Math.round(n*100)/100);};
async function load(){
 data=await(await fetch('/api/settings')).json();
 const tb=document.querySelector('#sp tbody');tb.innerHTML='';
 data.phases.forEach((p,i)=>{
  let tr=`<tr><td>${PH[i]}</td>`;
  F.forEach(f=>{
   if(f=='co2_only_daylight'||f=='light_mode')tr+=`<td><input type=checkbox data-p=${i} data-f=${f} ${p[f]?'checked':''}></td>`;
   else tr+=`<td><input data-p=${i} data-f=${f} value="${fmtSp(p[f])}"></td>`;
  });
  tb.innerHTML+=tr+'</tr>';
 });
 inv0.checked=data.dimmer.inv0;inv1.checked=data.dimmer.inv1;
 mq_uri.value=data.mqtt.uri;mq_user.value=data.mqtt.user;mq_pass.value=data.mqtt.pass;
 const g=data.global||{};
 g_buz.checked=!!g.buzzer_enable;g_rep.value=g.alarm_repeat_s;g_fan.value=g.fan_min_cycle_s;
 g_ldim.checked=!!g.lockout_dim;g_lfan.checked=!!g.lockout_fan;g_sens.checked=!!g.sensor_alarm_en;
 g_fb.value=g.fan_base;g_fm.value=g.fan_max;
 g_wmax.value=g.water_max_min!=null?g.water_max_min:30;
 g_hist.value=g.hist_save_min!=null?g.hist_save_min:60;
 g_dlif.value=g.dli_floor_pct!=null?g.dli_floor_pct:30;
 g_lfl.value=g.light_fault_lux!=null?g.light_fault_lux:100;
 g_acd.value=g.ac_delay_min!=null?g.ac_delay_min:15;
 g_co2b.value=g.co2_baseline!=null?g.co2_baseline:420;
 g_acch.value=g.ac_chamber!=null?g.ac_chamber:0;
 g_parch.value=g.par_chamber!=null?g.par_chamber:0;
 const so=data.soff||{t:[],rh:[]};const SN=["Kammer A","Kammer B","Dachboden"];
 soffb.innerHTML=SN.map((nm,i)=>`<tr><td>${nm}</td><td><input style="width:70px" data-soft="${i}" value="${(so.t&&so.t[i]!=null?so.t[i]:0)}"></td><td><input style="width:70px" data-sorh="${i}" value="${(so.rh&&so.rh[i]!=null?so.rh[i]:0)}"></td></tr>`).join('');
 sy_host.value=data.mqtt.host||'ihub';sy_prot.checked=!!data.mqtt.protect;
 sy_appw.value=data.mqtt.appw||'(wird bei Hotspot-Start erzeugt)';
 sy_tz.value=data.mqtt.tz||'CET-1CEST,M3.5.0,M10.5.0/3';
 if(sy_tz.selectedIndex<0&&data.mqtt.tz){const o=document.createElement('option');o.value=data.mqtt.tz;o.text='Eigene: '+data.mqtt.tz;sy_tz.add(o);sy_tz.value=data.mqtt.tz;}
 // Login + Token: Secrets werden nie geladen, nur „gesetzt/nicht gesetzt".
 const AU=data.auth||{};au_user.value=AU.user||'admin';
 au_stat.textContent=AU.pass_set?'Passwort gesetzt ✓ (Login aktiv)':'kein Passwort — WebUI offen';
 if(data.mqtt.pass_set)mq_pass.placeholder='•••••• (leer lassen = unverändert)';
 tk_stat.textContent=data.mqtt.token_set?'Token gesetzt ✓':'kein Token';
 sy_lang.value=localStorage.getItem('ihub_lang')||'de';
 try{const st=await(await fetch('/api/status')).json();fw_build.textContent=st.build||'?';}catch(e){fw_build.textContent='?';}
 // ── Wasserwerte-Alarme ── (Werte IMMER auf 2 Nachkommastellen, kein cJSON-Float-Artefakt)
 const f2=(v,def)=>(+(v!=null?v:def)).toFixed(2);
 w_al_en.checked=!!g.water_alarm_en;
 w_phmin.value=f2(g.water_ph_min,5.8);w_phmax.value=f2(g.water_ph_max,6.2);
 w_tmax.value=f2(g.water_temp_max,22);w_tmin.value=f2(g.water_temp_min,0);
 w_orpmin.value=f2(g.water_orp_min,0);
 // ── Bewässerung ──
 const w=data.watering||{};
 w_mode.value=w.mode||0;w_day.checked=!!w.only_day;w_dur.value=w.duration_s!=null?w.duration_s:60;
 w_int.value=w.interval_h!=null?w.interval_h:24;w_low.value=w.moist_low!=null?w.moist_low:40;
 w_pause.value=w.min_pause_min!=null?w.min_pause_min:60;
 // ── Steckdosen-Zeitpläne ──
 const SM=["Aus","Tagesplan","Zyklus"];
 const hm=v=>{v=+v||0;return String(Math.floor(v/60)).padStart(2,'0')+':'+String(v%60).padStart(2,'0');};
 const sch=data.sched||[];
 const tbs=document.querySelector('#schedT tbody');tbs.innerHTML='';
 for(let s=0;s<10;s++){
  const e=sch[s]||{mode:0,on:0,off:0,con:0,coff:0};
  const mo=SM.map((n,i)=>`<option value="${i}" ${e.mode==i?'selected':''}>${n}</option>`).join('');
  tbs.innerHTML+=`<tr><td><span>Steckdose</span> ${s+1}</td><td><select data-sm="${s}">${mo}</select></td><td><input type="time" data-son="${s}" value="${hm(e.on)}"></td><td><input type="time" data-soff="${s}" value="${hm(e.off)}"></td><td><input data-scon="${s}" value="${e.con}"></td><td><input data-scoff="${s}" value="${e.coff}"></td></tr>`;
 }
 // ── 2-Kammer-Zuordnung ──
 const FN=["Licht 1","Licht 2","Luftbefeuchter","Entfeuchter","Bewässerung","Ventilator","Abluft (Inline)","Heizung","Gerät 1 (CO₂)","Gerät 2"];
 const SRC=["Sensor-Bus TH3in1","Fan-Bus TH3in1","BLE (TP357)"];
 const asg=data.asg||{};
 // erkannte BLE-Klimasensoren (TP357) für die MAC-Auswahl
 let ble=[];try{ble=(await(await fetch('/api/ble')).json()).filter(d=>d.th);}catch(e){}
 const PHO=sel=>{let o='';PH.forEach((n,i)=>o+=`<option value="${i}" ${sel==i?'selected':''}>${n}</option>`);return o+`<option value="6" ${sel==6?'selected':''}>Aus</option>`;};
 const KO=ch=>`<option value="-1" ${ch==-1?'selected':''}>—</option><option value="0" ${ch==0?'selected':''}>Kammer A</option><option value="1" ${ch==1?'selected':''}>Kammer B</option>`;
 // Kammern: Profil + Sensor + BLE-Gerät
 const tbc=document.querySelector('#cham tbody');tbc.innerHTML='';
 ["A","B"].forEach((nm,ch)=>{
  const so=SRC.map((s,i)=>`<option value="${i}" ${(asg.csrc&&asg.csrc[ch]==i)?'selected':''}>${s}</option>`).join('');
  const cm=(asg.cmac&&asg.cmac[ch]||'').toUpperCase();
  const mo=`<option value="">—</option>`+ble.map(d=>{const m=d.mac.replace(/:/g,'').toUpperCase();return `<option value="${m}" ${cm==m?'selected':''}>${d.name} (${d.mac}) ${d.temp}°C</option>`;}).join('');
  tbc.innerHTML+=`<tr><td>Kammer ${nm}</td><td><select data-cph="${ch}">${PHO(asg.phase?asg.phase[ch]:6)}</select></td><td><select data-csrc="${ch}">${so}</select></td><td><select data-cmac="${ch}">${mo}</select></td></tr>`;
 });
 // Dachboden/Quellluft (Sensor-Slot 2)
 const ats=asg.csrc?asg.csrc[2]:1, atm=(asg.cmac&&asg.cmac[2]||'').toUpperCase();
 atsrc.innerHTML=SRC.map((s,i)=>`<option value="${i}" ${ats==i?'selected':''}>${s}</option>`).join('');
 atmac.innerHTML=`<option value="">—</option>`+ble.map(d=>{const m=d.mac.replace(/:/g,'').toUpperCase();return `<option value="${m}" ${atm==m?'selected':''}>${d.name} (${d.mac}) ${d.temp}°C</option>`;}).join('');
 // Steckdosen: Kammer + Rolle
 const RO=r=>FN.map((nm,i)=>`<option value="${i}" ${r==i?'selected':''}>${nm}</option>`).join('');
 const tba=document.querySelector('#asgT tbody');tba.innerHTML='';
 for(let s=0;s<10;s++){
  const rc=asg.relch?asg.relch[s]:0,rr=asg.relrole?asg.relrole[s]:s;
  tba.innerHTML+=`<tr><td><span>Steckdose</span> ${s+1}</td><td><select data-relch="${s}">${KO(rc)}</select></td><td><select data-relrole="${s}">${RO(rr)}</select></td></tr>`;
 }
 dimch0.innerHTML=KO(asg.dimch?asg.dimch[0]:0);
 dimch1.innerHTML=KO(asg.dimch?asg.dimch[1]:0);
 ifanch.innerHTML=KO(asg.ifanch!=null?asg.ifanch:0);
 // Grow-Plan je Kammer
 const gpv=data.growplan||{};let gph='';
 for(let c=0;c<2;c++){
  const steps=(gpv.steps&&gpv.steps[c])||[],en=(gpv.en&&gpv.en[c])?'checked':'',start=(gpv.start&&gpv.start[c])||0,today=(gpv.today&&gpv.today[c])||0;
  const af=(gpv.af&&gpv.af[c])?'checked':'',afon=(gpv.afon&&gpv.afon[c]!=null)?gpv.afon[c]:20,afst=(gpv.afst&&gpv.afst[c]!=null)?gpv.afst[c]:6;
  let sstr='';if(start){const d=new Date(start*1000);d.setMinutes(d.getMinutes()-d.getTimezoneOffset());sstr=d.toISOString().slice(0,16);}
  let rows='';
  for(let i=0;i<6;i++){
   const p=(steps[i]&&steps[i].p)||0,dd=(steps[i]&&steps[i].d)||0;
   const opts=PH.map((nm,k)=>`<option value="${k}" ${k==p?'selected':''}>${nm}</option>`).join('');
   rows+=`<tr><td>${i+1}</td><td><select data-gpp="${c}-${i}">${opts}</select></td><td><input style="width:60px" data-gpd="${c}-${i}" value="${dd}"></td></tr>`;
  }
  gph+=`<div style="margin-bottom:14px;${c>0?'border-top:1px solid #444;padding-top:10px':''}"><b>Kammer ${c==0?'A':'B'}</b> &nbsp;<label><input type=checkbox data-gpen="${c}" ${en}> Grow-Plan aktiv</label> ${today?`<span style="color:#6c6">— läuft, Tag ${today}</span>`:''}<div class="row"><label>Grow-Start</label><input type="datetime-local" data-gpstart="${c}" value="${sstr}"> <button type="button" onclick="gpNow(${c})">Jetzt starten</button></div><div class="row"><label>🌗 Autoflower-Modus</label><input type=checkbox data-gpaf="${c}" ${af}> <small>Licht konstant über alle Phasen (kein 12/12-Umschalten)</small></div><div class="row"><label>↳ Licht an / Start</label><input style="width:55px" data-gpafon="${c}" value="${afon}"> h, ab <input style="width:55px" data-gpafst="${c}" value="${afst}"> Uhr</div><table><thead><tr><th>#</th><th>Profil</th><th>Tage</th></tr></thead><tbody>${rows}</tbody></table></div>`;
 }
 gpcard.innerHTML=gph;
}
function gpNow(c){const el=document.querySelector(`[data-gpstart="${c}"]`);const d=new Date();d.setMinutes(d.getMinutes()-d.getTimezoneOffset());el.value=d.toISOString().slice(0,16);}
async function save(){
 document.querySelectorAll('#sp input').forEach(el=>{
  const p=+el.dataset.p,f=el.dataset.f;
  data.phases[p][f]=el.type=='checkbox'?(el.checked?1:0):(parseFloat(el.value)||0);
 });
 data.dimmer={inv0:inv0.checked,inv1:inv1.checked};
 data.mqtt={uri:mq_uri.value,user:mq_user.value,host:sy_host.value.trim(),tz:sy_tz.value,protect:sy_prot.checked?1:0};
 if(mq_pass.value)data.mqtt.pass=mq_pass.value;             // leer = unverändert
 if(mq_token.value.trim())data.mqtt.token=mq_token.value.trim();
 if(mq_token_clear.checked)data.mqtt.token_clear=1;
 // WebUI-Login
 let pwSet=false;
 if(au_clear.checked){data.auth={user:au_user.value.trim(),clear:1};}
 else{data.auth={user:au_user.value.trim()};
  if(au_pass.value){if(au_pass.value!==au_pass2.value){alert('Passwörter stimmen nicht überein');return;}data.auth.pass=au_pass.value;pwSet=true;}}
 data.global={buzzer_enable:g_buz.checked?1:0,alarm_repeat_s:+g_rep.value||60,fan_min_cycle_s:+g_fan.value||60,lockout_dim:g_ldim.checked?1:0,lockout_fan:g_lfan.checked?1:0,sensor_alarm_en:g_sens.checked?1:0,fan_base:+g_fb.value||10,fan_max:+g_fm.value||100,water_max_min:Math.max(0,+g_wmax.value||0),hist_save_min:Math.max(0,+g_hist.value||0),dli_floor_pct:Math.min(100,Math.max(0,+g_dlif.value||30)),light_fault_lux:Math.max(0,+g_lfl.value||0),water_alarm_en:w_al_en.checked?1:0,water_ph_min:+w_phmin.value||0,water_ph_max:+w_phmax.value||0,water_temp_max:+w_tmax.value||0,water_temp_min:+w_tmin.value||0,water_orp_min:Math.round(+w_orpmin.value||0),ac_delay_min:Math.min(255,Math.max(0,+g_acd.value||15)),co2_baseline:Math.min(2000,Math.max(0,+g_co2b.value||420)),ac_chamber:parseInt(g_acch.value),par_chamber:Math.max(0,parseInt(g_parch.value)||0)};
 data.watering={mode:+w_mode.value||0,only_day:w_day.checked?1:0,duration_s:Math.max(0,+w_dur.value||0),interval_h:Math.max(0,+w_int.value||0),moist_low:Math.max(0,+w_low.value||0),min_pause_min:Math.max(0,+w_pause.value||0)};
 data.growplan={en:[],start:[],days:[],steps:[],af:[],afon:[],afst:[]};
 for(let c=0;c<2;c++){
  data.growplan.en[c]=document.querySelector(`[data-gpen="${c}"]`).checked?1:0;
  const sv=document.querySelector(`[data-gpstart="${c}"]`).value;
  data.growplan.start[c]=sv?Math.floor(new Date(sv).getTime()/1000):0;
  data.growplan.af[c]=document.querySelector(`[data-gpaf="${c}"]`).checked?1:0;
  data.growplan.afon[c]=Math.min(24,Math.max(0,+document.querySelector(`[data-gpafon="${c}"]`).value||20));
  data.growplan.afst[c]=Math.min(23,Math.max(0,+document.querySelector(`[data-gpafst="${c}"]`).value||6));
  let tot=0;data.growplan.steps[c]=[];
  for(let i=0;i<6;i++){const dd=Math.max(0,+document.querySelector(`[data-gpd="${c}-${i}"]`).value||0);tot+=dd;data.growplan.steps[c][i]={p:+document.querySelector(`[data-gpp="${c}-${i}"]`).value||0,d:dd};}
  data.growplan.days[c]=tot||90;
 }
 data.soff={t:[],rh:[]};
 for(let i=0;i<3;i++){data.soff.t[i]=+document.querySelector(`[data-soft="${i}"]`).value||0;data.soff.rh[i]=+document.querySelector(`[data-sorh="${i}"]`).value||0;}
 const mins=t=>{const p=(t||'0:0').split(':');return ((+p[0]||0)*60+(+p[1]||0))%1440;};
 data.sched=[];
 for(let s=0;s<10;s++){data.sched[s]={
  mode:+document.querySelector(`[data-sm="${s}"]`).value||0,
  on:mins(document.querySelector(`[data-son="${s}"]`).value),
  off:mins(document.querySelector(`[data-soff="${s}"]`).value),
  con:Math.max(0,+document.querySelector(`[data-scon="${s}"]`).value||0),
  coff:Math.max(0,+document.querySelector(`[data-scoff="${s}"]`).value||0)};}
 const A={relch:[],relrole:[],dimch:[],ifanch:0,phase:[],csrc:[],cmac:[]};
 document.querySelectorAll('[data-relch]').forEach(el=>A.relch[+el.dataset.relch]=parseInt(el.value));
 document.querySelectorAll('[data-relrole]').forEach(el=>A.relrole[+el.dataset.relrole]=parseInt(el.value));
 A.dimch=[parseInt(dimch0.value),parseInt(dimch1.value)];A.ifanch=parseInt(ifanch.value);
 document.querySelectorAll('[data-cph]').forEach(el=>A.phase[+el.dataset.cph]=parseInt(el.value));
 document.querySelectorAll('[data-csrc]').forEach(el=>A.csrc[+el.dataset.csrc]=parseInt(el.value));
 document.querySelectorAll('[data-cmac]').forEach(el=>A.cmac[+el.dataset.cmac]=el.value);
 data.asg=A;delete data.actors;
 await fetch('/api/settings',{method:'POST',body:JSON.stringify(data)});
 if(pwSet){alert('Passwort gesetzt — bitte neu anmelden.');location.href='/login';return;}
 ok.textContent='✓ gespeichert';setTimeout(()=>ok.textContent='',2500);
}
// Konfig-Backup: kompletten /api/settings-Stand als Datei sichern bzw. wiederherstellen.
// Passwörter/Token sind NICHT enthalten (werden nie ausgegeben). WLAN-Zugangsdaten
// und Laufzeitzustand (Grow-Start, manuelle Overrides) ebenfalls nicht.
function exportCfg(){
 const d=new Date().toISOString().slice(0,10);
 const a=document.createElement('a');
 a.href=URL.createObjectURL(new Blob([JSON.stringify(data,null,1)],{type:'application/json'}));
 a.download='ihub-settings-'+d+'.json';a.click();URL.revokeObjectURL(a.href);
 ok.textContent='✓ exportiert';setTimeout(()=>ok.textContent='',2500);
}
async function importCfg(inp){
 const f=inp.files[0];inp.value='';if(!f)return;
 let j;try{j=JSON.parse(await f.text());}catch(e){alert('Keine gültige JSON-Datei');return;}
 if(!j.phases||!j.global){alert('Das sieht nicht nach einem iHub-Settings-Export aus.');return;}
 if(!confirm('Einstellungen aus "'+f.name+'" importieren und auf dem Gerät speichern?'))return;
 const r=await fetch('/api/settings',{method:'POST',body:JSON.stringify(j)});
 if(r.ok){ok.textContent='✓ importiert';setTimeout(()=>location.reload(),900);}
 else alert('Import fehlgeschlagen (HTTP '+r.status+')');
}
// ── Sprache + Firmware-Update ──
function setLang(v){localStorage.setItem('ihub_lang',v);location.reload();}
function keyq(){try{const k=localStorage.getItem('apikey');return k?('?key='+encodeURIComponent(k)):'';}catch(e){return '';}}
function fwReboot(){if(!confirm('Gerät jetzt neu starten?'))return;fetch('/reboot'+keyq(),{method:'POST'}).then(()=>{ok.textContent='Neustart…';});}
function fwPick(){fw_fname.textContent=fw_file.files[0]?fw_file.files[0].name:'keine ausgewählt';}
function fwUpload(){
 const f=fw_file.files[0];if(!f){alert('Bitte zuerst eine .bin-Datei wählen');return;}
 if(!confirm('Firmware „'+f.name+'" ('+((f.size/1024)|0)+' KB) jetzt flashen?'))return;
 fw_progrow.style.display='';fw_prog.value=0;fw_pct.textContent='0%';
 const xhr=new XMLHttpRequest();xhr.open('POST','/ota'+keyq());
 xhr.upload.onprogress=e=>{if(e.lengthComputable){const p=Math.round(e.loaded/e.total*100);fw_prog.value=p;fw_pct.textContent=p+'%';}};
 xhr.onload=()=>{if(xhr.status===200){fw_pct.textContent='✓ OK — Neustart…';setTimeout(()=>location.href='/',15000);}
  else{fw_pct.textContent='Fehler '+xhr.status;alert('Upload fehlgeschlagen (HTTP '+xhr.status+'): '+xhr.responseText);}};
 xhr.onerror=()=>{fw_pct.textContent='Verbindung getrennt — evtl. trotzdem geflasht, warte auf Neustart…';setTimeout(()=>location.href='/',15000);};
 xhr.send(f);
}
load();
</script></body></html>)HTML";
