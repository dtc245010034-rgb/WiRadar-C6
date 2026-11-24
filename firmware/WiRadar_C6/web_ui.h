// Web pages served by the ESP32-C6 (no external resources: works on the setup AP without internet)
#pragma once

static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>WiRadar-C6</title>
<style>
:root{--bg:#0b100d;--card:#121a15;--line:#223028;--text:#e8f1ec;--mute:#8aa095;--ok:#17e07a;--bad:#ff4a3c;--warn:#ffc700;--blue:#4aa8ff;--org:#ff8c1e}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;background:var(--bg);color:var(--text);font:16px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;padding:0 16px env(safe-area-inset-bottom)}
header{display:flex;align-items:center;justify-content:space-between;gap:8px;padding:14px 0 8px}
h1{font-size:19px;margin:0;letter-spacing:.2px}
.pill{font-size:12px;padding:3px 10px;border-radius:99px;border:1px solid var(--line);color:var(--mute);white-space:nowrap;max-width:55%;overflow:hidden;text-overflow:ellipsis}
.pill.on{color:var(--ok);border-color:#1d5a3a}.pill.off{color:var(--bad);border-color:#6a2420}
nav{display:grid;grid-template-columns:repeat(4,1fr);gap:6px;margin:6px 0 14px}
nav button{font:inherit;font-size:14px;padding:10px 0;border-radius:10px;background:var(--card);color:var(--mute);border:1px solid var(--line);cursor:pointer}
nav button.act{color:var(--text);border-color:var(--ok);background:#10231a}
button:focus-visible{outline:2px solid var(--warn);outline-offset:2px}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px;margin-bottom:12px}
canvas{display:block;width:100%;border-radius:10px;background:#050806}
#rad{aspect-ratio:1/1}#gr{aspect-ratio:2/1}
.status{font-size:26px;font-weight:700;margin:10px 0 2px}
.s-ok{color:var(--ok)}.s-bad{color:var(--bad)}.s-warn{color:var(--warn)}.s-mute{color:var(--mute)}
.bar{height:14px;border:1px solid var(--line);border-radius:8px;overflow:hidden;position:relative;margin-top:8px}
.bar i{display:block;height:100%;width:0;background:var(--ok);transition:width .12s}
.bar b{position:absolute;top:0;bottom:0;width:2px;background:var(--warn)}
.row{display:flex;justify-content:space-between;gap:12px;padding:8px 0;border-bottom:1px solid var(--line);font-size:15px}
.row:last-child{border:0}.row span:first-child{color:var(--mute)}.row span:last-child{text-align:right;word-break:break-all}
.big{font-size:54px;font-weight:700;line-height:1.05}
.unit{font-size:22px;color:var(--mute);margin-left:4px}
.lbl{font-size:13px;color:var(--mute);text-transform:uppercase;letter-spacing:.08em}
.gauge{height:10px;border-radius:6px;background:#0a110d;border:1px solid var(--line);margin-top:10px;overflow:hidden}
.gauge i{display:block;height:100%;width:0;transition:width .3s}
.btn{font:inherit;font-weight:600;padding:12px 16px;border-radius:10px;border:0;background:var(--ok);color:#04150b;cursor:pointer;display:inline-block;text-decoration:none}
.btn.sec{background:transparent;color:var(--text);border:1px solid var(--line)}
.btn.bad{background:transparent;color:var(--bad);border:1px solid #6a2420}
.acts{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px}
.hint{font-size:13px;color:var(--mute);margin:8px 0 0}
.hide{display:none}
</style></head><body>
<header><h1>WiRadar-C6</h1><span class="pill" id="net">connecting...</span></header>
<nav id="tabs"><button data-t="radar" class="act">Radar</button><button data-t="graph">Graph</button><button data-t="clim">Climate</button><button data-t="wifi">Wi-Fi</button></nav>

<section id="t-radar">
  <div class="card"><canvas id="rad"></canvas>
    <div class="status s-mute" id="st">...</div>
    <div class="hint" id="stsub"></div>
    <div class="bar"><i id="lv"></i><b id="th"></b></div>
    <div class="acts"><button class="btn sec" id="recal">Recalibrate</button></div>
    <p class="hint">Ripples show how strong the motion is, not where it is. Keep the area clear during calibration.</p>
  </div>
</section>

<section id="t-graph" class="hide">
  <div class="card"><div class="lbl">Motion level, last 30 s</div><canvas id="gr"></canvas>
    <div class="row"><span>Now</span><span id="gnow">-</span></div>
    <div class="row"><span>Yellow line</span><span>detection threshold</span></div>
  </div>
</section>

<section id="t-clim" class="hide">
  <div class="card"><div class="lbl">Temperature</div><div><span class="big" id="tv" style="color:var(--org)">--</span><span class="unit">&deg;C</span></div><div class="gauge"><i id="tg" style="background:var(--org)"></i></div></div>
  <div class="card"><div class="lbl">Humidity</div><div><span class="big" id="hv" style="color:var(--blue)">--</span><span class="unit">%</span></div><div class="gauge"><i id="hg" style="background:var(--blue)"></i></div></div>
  <p class="hint" id="nosens">No DHT11 detected yet. Connect the sensor to GPIO1 and it will appear here.</p>
</section>

<section id="t-wifi" class="hide">
  <div class="card">
    <div class="row"><span>Mode</span><span id="w-mode">-</span></div>
    <div class="row"><span>Network</span><span id="w-ssid">-</span></div>
    <div class="row"><span>IP address</span><span id="w-ip">-</span></div>
    <div class="row"><span>Address</span><span>http://wiradar.local</span></div>
    <div class="row"><span>Signal</span><span id="w-rssi">-</span></div>
    <div class="row"><span>CSI packets</span><span id="w-pps">-</span></div>
    <div class="row"><span>Uptime</span><span id="w-up">-</span></div>
    <div class="acts"><a class="btn" href="/setup">Set up Wi-Fi</a><button class="btn bad" id="forget">Forget saved Wi-Fi</button></div>
    <p class="hint">You can also hold the button on the device for 5 seconds to start the setup network again.</p>
  </div>
</section>

<script>
const $=id=>document.getElementById(id);
let S=null,hist=[],rip=[],sweep=0,lastRip=0,fails=0;
const T0=performance.now();
document.querySelectorAll('#tabs button').forEach(b=>b.onclick=()=>{
  document.querySelectorAll('#tabs button').forEach(x=>x.classList.toggle('act',x===b));
  ['radar','graph','clim','wifi'].forEach(t=>$('t-'+t).classList.toggle('hide',t!==b.dataset.t));
});
$('recal').onclick=()=>fetch('/api/recal',{method:'POST'});
$('forget').onclick=()=>{if(confirm('Forget the saved Wi-Fi? The device will keep running in setup mode.'))fetch('/api/forget',{method:'POST'}).then(()=>alert('Saved Wi-Fi removed.'))};

async function poll(){
  try{
    const r=await fetch('/api/state',{cache:'no-store'});S=await r.json();fails=0;
    const now=performance.now();hist.push([now,S.ratio]);
    while(hist.length&&hist[0][0]<now-30000)hist.shift();
    ui();
  }catch(e){fails++;if(fails>3){const p=$('net');p.textContent='no connection';p.className='pill off'}}
  setTimeout(poll,150);
}
function ui(){
  const p=$('net');p.textContent=(S.net==='AP'?'Setup AP: ':'Wi-Fi: ')+S.ssid;p.className='pill on';
  const st=$('st'),sub=$('stsub');
  if(!S.data){st.textContent='NO CSI DATA';st.className='status s-warn';sub.textContent=S.net==='AP'?'Waiting for a phone to connect to the setup network...':'Waiting for packets from the router...'}
  else if(S.cal){st.textContent='CALIBRATING';st.className='status s-warn';sub.textContent='Keep the area clear ('+S.calLeft+' s)'}
  else if(S.detect){st.textContent='MOTION DETECTED';st.className='status s-bad';sub.textContent='Strength '+Math.round(S.level*100)+'%'}
  else{st.textContent='AREA CLEAR';st.className='status s-ok';sub.textContent='x'+S.ratio.toFixed(2)+' of baseline'}
  $('lv').style.width=(S.level*100)+'%';$('lv').style.background=S.detect?'var(--bad)':'var(--ok)';
  $('th').style.left=(0.8/3*100)+'%';
  $('gnow').textContent='x'+S.ratio.toFixed(2);
  // climate
  const has=S.temp!==null&&S.hum!==null;$('nosens').classList.toggle('hide',has);
  $('tv').textContent=S.temp===null?'--':S.temp.toFixed(1);$('hv').textContent=S.hum===null?'--':S.hum.toFixed(0);
  $('tg').style.width=S.temp===null?'0':Math.max(0,Math.min(100,S.temp/50*100))+'%';
  $('hg').style.width=S.hum===null?'0':Math.max(0,Math.min(100,S.hum))+'%';
  // wifi
  $('w-mode').textContent=S.net==='AP'?'Setup access point ('+S.sta+' phone'+(S.sta===1?'':'s')+')':'Connected to router';
  $('w-ssid').textContent=S.ssid;$('w-ip').textContent=S.ip;
  $('w-rssi').textContent=S.data?S.rssi+' dBm':'-';$('w-pps').textContent=S.pps+' / s';
  const u=S.up,h=Math.floor(u/3600),m=Math.floor(u%3600/60),s=u%60;$('w-up').textContent=(h?h+' h ':'')+m+' min '+s+' s';
}

// ---------- radar ----------
function fit(c){const d=window.devicePixelRatio||1,w=c.clientWidth,h=c.clientHeight;if(c.width!==Math.round(w*d)){c.width=Math.round(w*d);c.height=Math.round(h*d)}return d}
function radar(ts){
  requestAnimationFrame(radar);
  const c0=$('rad');if(!c0.clientWidth||!c0.clientHeight)return;
  const c=c0,d=fit(c),g=c.getContext('2d'),W=c.width,H=c.height,cx=W/2,cy=H/2,R=Math.min(W,H)/2-6*d;
  if(R<=0)return;
  g.clearRect(0,0,W,H);
  const det=S&&S.detect&&!S.cal&&S.data,lvl=S?S.level:0;
  const col=det?'255,74,60':'23,224,122';
  const bg=g.createRadialGradient(cx,cy,0,cx,cy,R);bg.addColorStop(0,det?'#3a0e0c':'#06281a');bg.addColorStop(1,'#040806');
  g.fillStyle=bg;g.beginPath();g.arc(cx,cy,R,0,7);g.fill();
  g.strokeStyle='rgb('+col+')';g.lineWidth=(det?3:1.5)*d;g.beginPath();g.arc(cx,cy,R,0,7);g.stroke();
  g.strokeStyle='rgba('+col+',.28)';g.lineWidth=d;
  for(const f of[1/3,2/3]){g.beginPath();g.arc(cx,cy,R*f,0,7);g.stroke()}
  g.beginPath();g.moveTo(cx-R,cy);g.lineTo(cx+R,cy);g.moveTo(cx,cy-R);g.lineTo(cx,cy+R);g.stroke();
  sweep=(sweep+2.2)%360;
  for(let k=36;k>=0;k--){const a0=(sweep-k*1.7)*Math.PI/180;g.fillStyle='rgba('+col+','+(0.4*Math.pow(1-k/37,2))+')';
    g.beginPath();g.moveTo(cx,cy);g.arc(cx,cy,R,a0,a0+0.032);g.closePath();g.fill()}
  const a=sweep*Math.PI/180;g.strokeStyle='rgb('+col+')';g.lineWidth=2.5*d;g.beginPath();g.moveTo(cx,cy);g.lineTo(cx+R*Math.cos(a),cy+R*Math.sin(a));g.stroke();
  const now=ts/1000;
  if(det&&now-lastRip>0.7-0.45*lvl){lastRip=now;rip.push(now)}
  rip=rip.filter(t=>now-t<1.6);
  for(const t of rip){const age=(now-t)/1.6;g.strokeStyle='rgba(255,90,70,'+(0.9*Math.pow(1-age,1.5))+')';g.lineWidth=(3*(1-age)+1)*d;g.beginPath();g.arc(cx,cy,R*age,0,7);g.stroke()}
  if(det){const gr=R*(0.16+0.34*lvl)*(1+0.07*Math.sin(now*9)),rg=g.createRadialGradient(cx,cy,0,cx,cy,gr);
    rg.addColorStop(0,'rgba(255,200,180,.95)');rg.addColorStop(.35,'rgba(255,90,60,.65)');rg.addColorStop(1,'rgba(255,40,20,0)');
    g.fillStyle=rg;g.beginPath();g.arc(cx,cy,gr,0,7);g.fill()}
  if(S&&(S.cal||!S.data)){g.fillStyle='rgba(0,0,0,.78)';const bw=R*1.3,bh=R*.42;g.fillRect(cx-bw/2,cy-bh/2,bw,bh);
    g.strokeStyle='#ffc700';g.lineWidth=2*d;g.strokeRect(cx-bw/2,cy-bh/2,bw,bh);g.fillStyle='#ffc700';g.textAlign='center';
    g.font='bold '+(R*.09)+'px system-ui';g.fillText(S.data?'CALIBRATING... '+S.calLeft+'s':'NO CSI DATA',cx,cy+R*.03);
    g.font=(R*.065)+'px system-ui';g.fillText(S.data?'keep the area clear':'waiting for packets',cx,cy+R*.15)}
}

// ---------- graph ----------
function graph(){
  setTimeout(graph,100);
  const c0=$('gr');if(!c0.clientWidth||!c0.clientHeight)return;
  const c=c0,d=fit(c),g=c.getContext('2d'),W=c.width,H=c.height;g.clearRect(0,0,W,H);
  const ymax=4.5,Y=r=>H-6*d-(H-12*d)*Math.max(0,Math.min(ymax,r))/ymax,now=performance.now();
  g.lineWidth=d;g.setLineDash([4*d,4*d]);g.strokeStyle='#ffc700';g.beginPath();g.moveTo(0,Y(1.8));g.lineTo(W,Y(1.8));g.stroke();
  g.setLineDash([2*d,4*d]);g.strokeStyle='#4a5f54';g.beginPath();g.moveTo(0,Y(1));g.lineTo(W,Y(1));g.stroke();g.setLineDash([]);
  g.lineWidth=2.2*d;g.lineJoin='round';
  for(let i=1;i<hist.length;i++){const x0=W-(now-hist[i-1][0])/30000*W,x1=W-(now-hist[i][0])/30000*W;
    g.strokeStyle=hist[i][1]>1.8?'#ff4a3c':'#17e07a';g.beginPath();g.moveTo(x0,Y(hist[i-1][1]));g.lineTo(x1,Y(hist[i][1]));g.stroke()}
}
requestAnimationFrame(radar);graph();poll();
</script></body></html>)rawliteral";

static const char SETUP_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>WiRadar-C6 Wi-Fi setup</title>
<style>
:root{--bg:#0b100d;--card:#121a15;--line:#223028;--text:#e8f1ec;--mute:#8aa095;--ok:#17e07a;--bad:#ff4a3c}
*{box-sizing:border-box}body{margin:0;padding:16px;background:var(--bg);color:var(--text);font:16px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;max-width:520px;margin:0 auto}
h1{font-size:20px;margin:6px 0 4px}p{color:var(--mute);margin:4px 0 14px;font-size:14px}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px;margin-bottom:12px}
label{display:block;font-size:13px;color:var(--mute);margin:10px 0 4px}
input,select{width:100%;font:inherit;padding:12px;border-radius:10px;border:1px solid var(--line);background:#0a110d;color:var(--text)}
button,a.b{font:inherit;font-weight:600;padding:12px 16px;border-radius:10px;border:0;background:var(--ok);color:#04150b;width:100%;margin-top:14px;cursor:pointer;display:block;text-align:center;text-decoration:none}
button.s,a.s{background:transparent;color:var(--text);border:1px solid var(--line)}
#msg{margin-top:12px;font-size:14px}.err{color:var(--bad)}.ok{color:var(--ok)}
</style></head><body>
<h1>WiRadar-C6 Wi-Fi setup</h1>
<p>Choose your home Wi-Fi (2.4 GHz). The device will restart and join it. Then connect your phone to the same Wi-Fi and open the address shown on the device screen, or <b>http://wiradar.local</b>.</p>
<div class="card">
  <label for="net">Network</label><select id="net"><option>Scanning...</option></select>
  <label for="ssid">or type the name</label><input id="ssid" autocapitalize="off" autocomplete="off" placeholder="Wi-Fi name">
  <label for="pass">Password</label><input id="pass" type="password" autocomplete="off" placeholder="(leave empty for open network)">
  <button id="go">Save and connect</button>
  <button class="s" id="rescan" type="button">Scan again</button>
  <div id="msg"></div>
</div>
<a class="b s" href="/">Open dashboard (test mode)</a>
<script>
const $=id=>document.getElementById(id);
async function scan(){
  $('net').innerHTML='<option>Scanning...</option>';
  for(let i=0;i<12;i++){
    try{const r=await(await fetch('/api/scan',{cache:'no-store'})).json();
      if(!r.scanning){$('net').innerHTML='<option value="">-- choose --</option>'+r.nets.map(n=>'<option value="'+n.ssid.replace(/"/g,'&quot;')+'">'+n.ssid.replace(/</g,'&lt;')+'  ('+n.rssi+' dBm'+(n.open?', open':'')+')</option>').join('');return}
    }catch(e){}
    await new Promise(r=>setTimeout(r,1200));
  }
  $('net').innerHTML='<option value="">No networks found</option>';
}
$('net').onchange=()=>{if($('net').value)$('ssid').value=$('net').value};
$('rescan').onclick=scan;
$('go').onclick=async()=>{
  const ssid=$('ssid').value.trim(),pass=$('pass').value,m=$('msg');
  if(!ssid){m.className='err';m.textContent='Please choose or type a network name.';return}
  if(pass&&pass.length<8){m.className='err';m.textContent='Password must be at least 8 characters.';return}
  m.className='';m.textContent='Saving...';
  try{const b=new URLSearchParams({ssid,pass});const r=await fetch('/api/save',{method:'POST',body:b});const j=await r.json();
    if(j.ok){m.className='ok';m.textContent='Saved. The device is restarting. Reconnect your phone to "'+ssid+'" and open http://wiradar.local (or the IP on the device screen).'}
    else{m.className='err';m.textContent=j.error||'Could not save.'}
  }catch(e){m.className='ok';m.textContent='Saved. The device is restarting. Reconnect your phone to "'+ssid+'" and open http://wiradar.local (or the IP on the device screen).'}
};
scan();
</script></body></html>)rawliteral";
