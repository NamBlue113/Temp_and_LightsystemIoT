#pragma once
// Trang điều khiển do chính ESP32 phục vụ (http://smartroom.local/ hoặc http://<IP>/).
// Thiết kế ưu tiên điện thoại. Mọi lệnh /api/* cần mật khẩu (header X-Auth).
#include <Arduino.h>

const char MANIFEST_JSON[] PROGMEM = R"rawliteral({"name":"Smartroom","short_name":"Smartroom","start_url":"/","display":"standalone","background_color":"#0f1419","theme_color":"#0f1419","icons":[{"src":"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'%3E%3Crect width='64' height='64' rx='14' fill='%233b9eff'/%3E%3Cpath d='M12 32 32 14l20 18v18H38V38H26v12H12z' fill='white'/%3E%3C/svg%3E","sizes":"any","type":"image/svg+xml"}]})rawliteral";

const char INDEX_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="vi"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0f1419">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="mobile-web-app-capable" content="yes">
<link rel="manifest" href="/manifest.json">
<title>Smartroom</title>
<style>
:root{--bg:#0f1419;--card:#1a212b;--line:#2a3441;--tx:#e6edf3;--mut:#8b98a5;--ac:#3b9eff;--ok:#3ecf8e;--bad:#f0616d;--warn:#f5a524}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0 auto;max-width:560px;padding:14px 14px calc(24px + env(safe-area-inset-bottom));background:var(--bg);color:var(--tx);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
h1{font-size:19px;margin:0}
.top{display:flex;justify-content:space-between;align-items:center;margin-bottom:12px}
.sub{color:var(--mut);font-size:13px}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px;margin-bottom:12px}
.card h2{font-size:12px;color:var(--mut);text-transform:uppercase;letter-spacing:.06em;margin:0 0 10px;display:flex;justify-content:space-between;align-items:center}
.grid3{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;text-align:center}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.big{font-size:30px;font-weight:650;line-height:1.1}
.unit{font-size:13px;color:var(--mut);margin-left:2px}
.lab{font-size:12px;color:var(--mut);margin-bottom:2px}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
.row>button{flex:1;min-width:70px}
button{background:#243040;color:var(--tx);border:1px solid var(--line);border-radius:10px;padding:12px 10px;font:inherit;cursor:pointer;min-height:44px}
button:active{filter:brightness(1.25)}
button.on{background:var(--ac);border-color:var(--ac);color:#fff}
button.sm{padding:8px 10px;min-height:36px;font-size:13px}
input[type=number],input[type=time],input[type=password]{background:#0f1419;color:var(--tx);border:1px solid var(--line);border-radius:10px;padding:10px;font:inherit;width:100%;min-height:44px}
input[type=range]{width:100%;margin:12px 0 4px}
input[type=checkbox]{width:22px;height:22px}
.msg{font-size:13px;color:var(--mut);min-height:18px;margin-top:6px}
.tag{font-size:12px;padding:3px 9px;border-radius:99px;background:#243040;color:var(--mut);text-transform:none;letter-spacing:0;font-weight:500}
.tag.bad{background:#4a1f26;color:var(--bad)}
.tag.ok{background:#173a2b;color:var(--ok)}
.tag.warn{background:#4a3410;color:var(--warn)}
.sl{display:grid;grid-template-columns:auto 1fr 1fr;gap:8px;align-items:center;margin-bottom:8px}
canvas{width:100%;height:120px;display:block}
.ch{margin-bottom:12px}
.ch .lab{display:flex;justify-content:space-between}
.axis{display:flex;justify-content:space-between;font-size:11px;color:var(--mut);margin-top:2px}
details summary{cursor:pointer;color:var(--mut);font-size:13px;padding:4px 0}
.ov{position:fixed;inset:0;background:var(--bg);display:flex;align-items:center;justify-content:center;padding:20px;z-index:9}
.ov[hidden]{display:none}
.ov .card{width:100%;max-width:360px}
</style></head><body>

<div id="login" class="ov" hidden>
  <div class="card">
    <h1 style="margin-bottom:6px">Smartroom</h1>
    <div class="sub" style="margin-bottom:12px">Nhập mật khẩu để điều khiển</div>
    <input type="password" id="pw" autocomplete="current-password" placeholder="Mật khẩu" onkeydown="if(event.key==='Enter')doLogin()">
    <button class="on" style="width:100%;margin-top:10px" onclick="doLogin()">Đăng nhập</button>
    <div class="msg" id="loginMsg" style="color:var(--bad)"></div>
  </div>
</div>

<div class="top">
  <div><h1>Smartroom</h1><div class="sub"><span id="time">--</span> <span id="fw"></span></div></div>
  <span id="net" class="tag">...</span>
</div>

<div class="card">
  <div class="grid3">
    <div><div class="lab">Nhiệt độ</div><span class="big" id="t">--</span><span class="unit">°C</span></div>
    <div><div class="lab">Độ ẩm</div><span class="big" id="h">--</span><span class="unit">%</span></div>
    <div><div class="lab">Ánh sáng</div><span class="big" id="lv">--</span><div class="sub" id="dk"></div></div>
  </div>
  <div class="msg" id="sensorMsg"></div>
</div>

<div class="card">
  <h2>Quạt <span id="fanTag" class="tag"></span></h2>
  <div class="row">
    <button id="fAuto" onclick="fan({mode:'auto'})">Auto</button>
    <button id="fOn" onclick="fan({mode:'manual',percent:100})">Bật</button>
    <button id="fOff" onclick="fan({mode:'manual',percent:0})">Tắt</button>
  </div>
  <details><summary>Chạy theo % tốc độ</summary>
    <input type="range" id="fSlider" min="30" max="100" step="5" value="70" oninput="$('fPct').textContent=this.value+'%'" onchange="fan({mode:'manual',percent:+this.value})">
    <div class="sub">Tốc độ: <b id="fPct">70%</b></div>
  </details>
</div>

<div class="card">
  <h2>Đèn <span id="ledTag" class="tag"></span></h2>
  <div class="row">
    <button id="lAuto" onclick="led({mode:'auto'})">Auto</button>
    <button id="lOn" onclick="led({mode:'manual',on:true})">Bật</button>
    <button id="lOff" onclick="led({mode:'manual',on:false})">Tắt</button>
  </div>
  <div class="msg" id="ledWhy"></div>
</div>

<div class="card">
  <h2>Biểu đồ 24 giờ <button class="sm" onclick="loadHist()">Làm mới</button></h2>
  <div class="ch"><div class="lab"><span>Nhiệt độ (°C)</span><b id="cT"></b></div><canvas id="chT"></canvas></div>
  <div class="ch"><div class="lab"><span>Độ ẩm (%)</span><b id="cH"></b></div><canvas id="chH"></canvas></div>
  <div class="ch"><div class="lab"><span>Ánh sáng</span><b id="cL"></b></div><canvas id="chL"></canvas></div>
  <div class="axis"><span id="axA"></span><span id="axB"></span></div>
  <div class="msg" id="histMsg">Lịch sử lưu trong ESP32, mất khi khởi động lại.</div>
</div>

<div class="card">
  <h2>Lịch hẹn giờ đèn</h2>
  <div id="slots"></div>
  <button class="on" style="width:100%" onclick="saveSched()">Lưu lịch</button>
  <div class="msg" id="schedMsg">Đèn Auto chỉ bật khi đang trong khung giờ VÀ phòng tối. Hỗ trợ qua nửa đêm (22:00 → 06:00).</div>
</div>

<div class="card">
  <h2>Ngưỡng ánh sáng</h2>
  <div class="grid2">
    <div><div class="lab">Tối khi ≤</div><input type="number" id="lD" step="10"></div>
    <div><div class="lab">Sáng khi ≥</div><input type="number" id="lB" step="10"></div>
  </div>
  <div class="row" style="margin-top:10px">
    <button class="sm" onclick="$('lD').value=cur.light.level">Mức hiện tại → TỐI</button>
    <button class="sm" onclick="$('lB').value=cur.light.level">Mức hiện tại → SÁNG</button>
  </div>
  <button class="on" style="width:100%;margin-top:10px" onclick="saveLight()">Lưu ngưỡng ánh sáng</button>
  <div class="msg" id="lightMsg">Che cảm biến rồi bấm "→ TỐI"; để phòng sáng rồi bấm "→ SÁNG". Mức hiện tại: <span id="lvNow">--</span></div>
</div>

<div class="card">
  <h2>Ngưỡng quạt tự động</h2>
  <div class="grid2">
    <div><div class="lab">Nhiệt độ bật (°C)</div><input type="number" id="tOn" step="0.5"></div>
    <div><div class="lab">Nhiệt độ tắt (°C)</div><input type="number" id="tOff" step="0.5"></div>
    <div><div class="lab">Độ ẩm bật (%)</div><input type="number" id="hOn" step="1"></div>
    <div><div class="lab">Độ ẩm tắt (%)</div><input type="number" id="hOff" step="1"></div>
  </div>
  <button class="on" style="width:100%;margin-top:10px" onclick="saveCfg()">Lưu ngưỡng quạt</button>
  <div class="msg" id="cfgMsg"></div>
</div>

<div class="card">
  <details><summary>Chế độ thử (giả lập cảm biến)</summary>
    <div class="grid2" style="margin-top:8px">
      <div><div class="lab">Nhiệt độ giả lập (°C)</div><input type="number" id="sT" value="33" step="0.5"></div>
      <div><div class="lab">Độ ẩm giả lập (%)</div><input type="number" id="sH" value="60" step="1"></div>
    </div>
    <div class="row" style="margin-top:10px"><button onclick="sim(true)">Giả lập</button><button onclick="sim(false)">Tắt giả lập</button></div>
    <div class="msg" id="simMsg"></div>
  </details>
  <div style="margin-top:8px"><button class="sm" onclick="logout()">Đăng xuất</button></div>
</div>

<script>
const $=i=>document.getElementById(i);
let PW='';try{PW=localStorage.getItem('pw')||''}catch(e){}
let cur=null,cfgDone=false,lightDone=false,timer=null;

async function api(p,b){
  const o={headers:{'X-Auth':PW}};
  if(b){o.method='POST';o.headers['Content-Type']='application/json';o.body=JSON.stringify(b);}
  const r=await fetch(p,o);
  if(r.status===401||r.status===429){
    const j=await r.json().catch(()=>({}));
    showLogin(r.status===429?'Thử sai quá nhiều lần, đợi 1 phút':(PW?'Sai mật khẩu':''));
    throw new Error('auth');
  }
  return r.json();
}
function showLogin(m){$('login').hidden=false;$('loginMsg').textContent=m||'';}
function doLogin(){
  PW=$('pw').value;try{localStorage.setItem('pw',PW)}catch(e){}
  $('login').hidden=true;cfgDone=false;lightDone=false;start();
}
function logout(){PW='';try{localStorage.removeItem('pw')}catch(e){}location.reload();}
function mark(ids,on){ids.forEach(i=>$(i).classList.toggle('on',i===on));}

async function refresh(){
  try{
    const s=await api('/api/status');cur=s;
    $('time').textContent=s.time.local;$('fw').textContent='· v'+s.fw;
    $('net').textContent=s.wifi.connected?('Wi-Fi '+s.wifi.rssi+' dBm'):'Mất Wi-Fi';
    $('net').className='tag '+(s.wifi.connected?'ok':'bad');
    const se=s.sensor;
    $('t').textContent=se.temperature==null?'--':se.temperature.toFixed(1);
    $('h').textContent=se.humidity==null?'--':se.humidity.toFixed(0);
    $('sensorMsg').textContent=se.error?'Lỗi cảm biến DHT11 – quạt tự động tạm dừng':(se.sim?'Đang giả lập cảm biến':'');
    const li=s.light;
    $('lv').textContent=li.level;$('lvNow').textContent=li.level;
    $('dk').textContent=li.dark?'phòng tối':'phòng sáng';
    const f=s.fan;
    $('fanTag').textContent=(f.mode==='auto'?'AUTO':'THỦ CÔNG')+' · '+f.percent+'%';
    $('fanTag').className='tag '+(f.percent>0?'ok':'');
    mark(['fAuto','fOn','fOff'],f.mode==='auto'?'fAuto':(f.percent===0?'fOff':'fOn'));
    const l=s.led;
    $('ledTag').textContent=(l.mode==='auto'?'AUTO':'THỦ CÔNG')+' · '+(l.on?'đang sáng':'đang tắt');
    $('ledTag').className='tag '+(l.on?'warn':'');
    mark(['lAuto','lOn','lOff'],l.mode==='auto'?'lAuto':(l.on?'lOn':'lOff'));
    $('ledWhy').textContent=l.mode==='auto'?(l.on?'Đang trong khung giờ và phòng tối.':(li.dark?'Phòng tối nhưng chưa tới khung giờ.':'Phòng còn sáng nên chưa bật.')):'';
    if(!cfgDone){
      $('tOn').value=s.config.temp_on;$('tOff').value=s.config.temp_off;
      $('hOn').value=s.config.hum_on;$('hOff').value=s.config.hum_off;cfgDone=true;
    }
    if(!lightDone){$('lD').value=li.dark_level;$('lB').value=li.bright_level;lightDone=true;}
  }catch(e){if(e.message!=='auth'){$('net').textContent='Mất kết nối';$('net').className='tag bad';}}
}
async function fan(b){try{await api('/api/fan',b);refresh();}catch(e){}}
async function led(b){try{await api('/api/led',b);refresh();}catch(e){}}
async function saveCfg(){
  try{const r=await api('/api/config',{temp_on:+$('tOn').value,temp_off:+$('tOff').value,hum_on:+$('hOn').value,hum_off:+$('hOff').value});
  $('cfgMsg').textContent=r.ok?'Đã lưu.':('Lỗi: '+r.error);}catch(e){}
}
async function saveLight(){
  try{const r=await api('/api/light',{dark:+$('lD').value,bright:+$('lB').value});
  $('lightMsg').textContent=r.ok?'Đã lưu ngưỡng ánh sáng.':('Lỗi: '+r.error);refresh();}catch(e){}
}
async function sim(on){
  try{const r=await api('/api/sim',on?{temp:+$('sT').value,hum:+$('sH').value}:{off:true});
  $('simMsg').textContent=r.ok?(on?'Đang giả lập cảm biến (không ghi vào biểu đồ).':'Đã tắt giả lập.'):('Lỗi: '+r.error);refresh();}catch(e){}
}
function buildSlots(list){
  const box=$('slots');box.innerHTML='';
  for(let i=0;i<6;i++){
    const s=list[i]||{enabled:false,start:'00:00',end:'00:00'};
    const d=document.createElement('div');d.className='sl';
    d.innerHTML='<input type="checkbox" id="se'+i+'"'+(s.enabled?' checked':'')+'><input type="time" id="ss'+i+'" value="'+s.start+'"><input type="time" id="sf'+i+'" value="'+s.end+'">';
    box.appendChild(d);
  }
}
async function loadSched(){try{const r=await api('/api/schedule');buildSlots(r.slots);}catch(e){}}
async function saveSched(){
  const slots=[];
  for(let i=0;i<6;i++)slots.push({enabled:$('se'+i).checked,start:$('ss'+i).value||'00:00',end:$('sf'+i).value||'00:00'});
  try{const r=await api('/api/schedule',{slots:slots});
  $('schedMsg').textContent=r.ok?'Đã lưu lịch.':('Lỗi: '+r.error);refresh();}catch(e){}
}

/* ---- Biểu đồ ---- */
function chart(id,arr,color,fixMin,fixMax,dec){
  const c=$(id),dpr=window.devicePixelRatio||1,w=c.clientWidth,h=c.clientHeight;
  c.width=w*dpr;c.height=h*dpr;
  const g=c.getContext('2d');g.setTransform(dpr,0,0,dpr,0,0);g.clearRect(0,0,w,h);
  g.font='11px system-ui';g.fillStyle='#8b98a5';
  if(arr.length<2){g.fillText('Chưa đủ dữ liệu (mỗi 5 phút ghi 1 điểm)',8,h/2);return;}
  let mn=fixMin!=null?fixMin:Math.min(...arr),mx=fixMax!=null?fixMax:Math.max(...arr);
  if(mx-mn<1){mx+=.5;mn-=.5;}
  const padL=34,padR=6,padT=8,padB=6,W=w-padL-padR,H=h-padT-padB;
  g.strokeStyle='#2a3441';g.lineWidth=1;
  for(let i=0;i<=2;i++){
    const y=padT+H*i/2;g.beginPath();g.moveTo(padL,y);g.lineTo(w-padR,y);g.stroke();
    g.fillText((mx-(mx-mn)*i/2).toFixed(dec),2,y+4);
  }
  g.strokeStyle=color;g.lineWidth=2;g.lineJoin='round';g.beginPath();
  arr.forEach((v,i)=>{const x=padL+W*i/(arr.length-1),y=padT+H*(1-(v-mn)/(mx-mn));i?g.lineTo(x,y):g.moveTo(x,y);});
  g.stroke();
}
function hhmm(ts){const d=new Date(ts*1000);return d.toLocaleTimeString('vi-VN',{hour:'2-digit',minute:'2-digit'});}
async function loadHist(){
  try{
    const r=await api('/api/history');
    if(!r.n){chart('chT',[]);chart('chH',[]);chart('chL',[]);$('histMsg').textContent='Chưa có dữ liệu. ESP32 ghi 1 điểm mỗi 5 phút sau khi có giờ hợp lệ.';return;}
    chart('chT',r.t,'#f0616d',null,null,1);chart('chH',r.h,'#3b9eff',null,null,0);chart('chL',r.l,'#f5a524',0,4095,0);
    $('cT').textContent=r.t[r.t.length-1].toFixed(1)+'°C';$('cH').textContent=r.h[r.h.length-1]+'%';$('cL').textContent=r.l[r.l.length-1];
    $('axA').textContent=hhmm(r.end-(r.n-1)*r.interval);$('axB').textContent=hhmm(r.end);
    $('histMsg').textContent=r.n+' điểm, mỗi '+(r.interval/60)+' phút. Lưu trong ESP32, mất khi khởi động lại.';
  }catch(e){}
}

function start(){
  if(timer)clearInterval(timer);
  loadSched();refresh();loadHist();
  timer=setInterval(()=>{if(!document.hidden)refresh();},2000);
  setInterval(()=>{if(!document.hidden)loadHist();},60000);
}
if(!PW)showLogin('');else start();
document.addEventListener('visibilitychange',()=>{if(!document.hidden&&PW)refresh();});
</script></body></html>)rawliteral";
