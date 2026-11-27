#pragma once
// Trang giám sát + điều khiển do chính ESP32 phục vụ (http://smartroom.local/ hoặc http://<IP>/).
// Dashboard tối, ưu tiên desktop; tablet 1 cột; mobile xếp 3 biểu đồ theo chiều dọc. Mọi lệnh /api/* cần mật khẩu (header X-Auth).
// Font JetBrains Mono / Inter tải từ Google Fonts khi có Internet; không có thì dùng font hệ thống.
#include <Arduino.h>

const char MANIFEST_JSON[] PROGMEM = R"rawliteral({"name":"Smartroom","short_name":"Smartroom","start_url":"/","display":"standalone","background_color":"#0A0E14","theme_color":"#0A0E14","icons":[{"src":"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'%3E%3Crect width='64' height='64' rx='8' fill='%2300E5A0'/%3E%3Cpath d='M12 32 32 14l20 18v18H38V38H26v12H12z' fill='%230A0E14'/%3E%3C/svg%3E","sizes":"any","type":"image/svg+xml"}]})rawliteral";

const char INDEX_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="vi"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0A0E14">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="mobile-web-app-capable" content="yes">
<link rel="manifest" href="/manifest.json">
<link rel="preconnect" href="https://fonts.googleapis.com"><link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600&family=JetBrains+Mono:wght@400;500;700&display=swap" rel="stylesheet">
<title>Smartroom</title>
<style>
:root{--bg:#0A0E14;--line:#1F2937;--tx:#E5E9F0;--mut:#8A94A6;--ac:#00E5A0;--bad:#FF4757;--warn:#FFA502;--r:8px;
--mono:"JetBrains Mono",ui-monospace,SFMono-Regular,Consolas,monospace;--sans:Inter,system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;background:var(--bg);color:var(--tx);font:14px/1.45 var(--sans);padding-bottom:env(safe-area-inset-bottom)}
.num,.mono{font-family:var(--mono);font-variant-numeric:tabular-nums}
svg.ic{width:16px;height:16px;fill:none;stroke:currentColor;stroke-width:2;stroke-linecap:round;stroke-linejoin:round;flex:none}
header{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:12px 20px;border-bottom:1px solid var(--line)}
header h1{font-size:14px;font-weight:600;letter-spacing:.08em;margin:0;text-transform:uppercase}
header h1 span{color:var(--mut);font-weight:400;margin-left:8px;letter-spacing:0;text-transform:none}
.conn{display:flex;align-items:center;gap:8px;font-size:12px;color:var(--mut)}
.dot{width:8px;height:8px;border-radius:50%;background:var(--bad);flex:none}
.dot.ok{background:var(--ac)}
.layout{display:grid;grid-template-columns:3fr 2fr;min-height:calc(100vh - 50px)}
.left{border-right:1px solid var(--line)}
.kgrid{display:grid;grid-template-columns:1fr 1fr}
.kpi{padding:18px 20px;border-bottom:1px solid var(--line);min-width:0}
.kpi:nth-child(odd){border-right:1px solid var(--line)}
.kh{display:flex;align-items:center;gap:8px;color:var(--mut);font-size:11px;letter-spacing:.08em;text-transform:uppercase}
.kh .al{margin-left:auto;color:var(--bad);display:none}
.kv{margin:8px 0 2px;display:flex;align-items:baseline;gap:6px}
.big{font-size:56px;font-weight:700;line-height:1;transition:color .2s}
.mid{font-size:48px}
.u{font-family:var(--mono);color:var(--mut);font-size:16px}
.kt{display:flex;align-items:center;gap:6px;font-family:var(--mono);font-size:12px;color:var(--mut);min-height:18px}
.kf{margin-top:8px;font-size:12px;color:var(--mut)}
.kpi.bad .big{color:var(--bad)}.kpi.bad .kh .al{display:block}.kpi.bad .kh{color:var(--bad)}
.kpi.warn .big{color:var(--warn)}
.chg{animation:chg .2s}@keyframes chg{from{opacity:.35}to{opacity:1}}
.sec{padding:16px 20px;border-bottom:1px solid var(--line)}
.sec h2{margin:0 0 10px;font-size:11px;letter-spacing:.08em;text-transform:uppercase;color:var(--mut);font-weight:500;display:flex;align-items:center;gap:8px}
.sec h2 .sp{flex:1}
.charts{display:grid;grid-template-columns:repeat(3,1fr);gap:14px}
.ch .cl{display:flex;justify-content:space-between;font-size:11px;color:var(--mut);margin-bottom:4px}
.ch .cl b{font-family:var(--mono);color:var(--tx);font-weight:500}
canvas{width:100%;height:130px;display:block}
.axis{display:flex;justify-content:space-between;font-family:var(--mono);font-size:10px;color:var(--mut);margin-top:6px}
.seg{display:flex;border:1px solid var(--line);border-radius:var(--r);overflow:hidden}
.seg button{flex:1;border:0;border-right:1px solid var(--line);border-radius:0;min-height:40px}
.seg button:last-child{border-right:0}
button{background:transparent;color:var(--tx);border:1px solid var(--line);border-radius:var(--r);padding:8px 12px;font:inherit;cursor:pointer;min-height:40px}
button:hover{border-color:#334155}button:active{filter:brightness(1.3)}
button.on{background:var(--ac);border-color:var(--ac);color:#0A0E14;font-weight:600}
button.sm{min-height:32px;padding:4px 10px;font-size:12px}
button.pri{background:var(--ac);border-color:var(--ac);color:#0A0E14;font-weight:600;width:100%;margin-top:10px}
.rng button{min-height:28px;padding:2px 10px;font-size:12px;border-radius:6px;white-space:nowrap}
input[type=number],input[type=time],input[type=password]{background:#0D131B;color:var(--tx);border:1px solid var(--line);border-radius:var(--r);padding:8px 10px;font:inherit;font-family:var(--mono);width:100%;min-height:40px}
input[type=range]{width:100%;margin:10px 0 2px;accent-color:var(--ac)}
input[type=checkbox]{width:20px;height:20px;accent-color:var(--ac)}
.g2{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.lab{font-size:11px;color:var(--mut);margin-bottom:3px}
.msg{font-size:12px;color:var(--mut);margin-top:8px;min-height:16px}
.tag{font-family:var(--mono);font-size:11px;padding:2px 8px;border-radius:6px;border:1px solid var(--line);color:var(--mut);text-transform:none;letter-spacing:0}
.tag.ok{color:var(--ac);border-color:#0b5c44}.tag.bad{color:var(--bad);border-color:#6b1f28}.tag.warn{color:var(--warn);border-color:#6b4710}
.rows{display:grid;gap:0}
.row{display:flex;justify-content:space-between;align-items:center;gap:10px;padding:8px 0;border-bottom:1px solid var(--line);font-size:13px}
.row:last-child{border-bottom:0}.row .k{color:var(--mut)}.row .v{font-family:var(--mono);text-align:right;display:flex;align-items:center;gap:8px}
.sl{display:grid;grid-template-columns:auto 1fr 1fr;gap:8px;align-items:center;margin-bottom:8px}
details{border-bottom:1px solid var(--line)}details>summary{cursor:pointer;list-style:none;padding:14px 20px;font-size:11px;letter-spacing:.08em;text-transform:uppercase;color:var(--mut);display:flex;justify-content:space-between}
details>summary::-webkit-details-marker{display:none}details>summary::after{content:"+";font-family:var(--mono)}details[open]>summary::after{content:"−"}
details>.in{padding:0 20px 16px}
.ov{position:fixed;inset:0;background:var(--bg);display:flex;align-items:center;justify-content:center;padding:20px;z-index:9}.ov[hidden]{display:none}
.ov .box{width:100%;max-width:340px;border:1px solid var(--line);border-radius:var(--r);padding:20px}
.ov h1{font-size:14px;letter-spacing:.08em;text-transform:uppercase;margin:0 0 4px}
@media(max-width:1024px){.layout{grid-template-columns:1fr;align-content:start}.left{border-right:0}}
@media(max-width:640px){.charts{grid-template-columns:1fr}.big{font-size:44px}.mid{font-size:40px}.kpi{padding:14px}header{padding:10px 14px}.sec{padding:14px}details>summary{padding:14px}details>.in{padding:0 14px 14px}}
</style></head><body>

<svg width="0" height="0" style="position:absolute" aria-hidden="true"><defs>
<symbol id="i-th" viewBox="0 0 24 24"><path d="M14 4v10.54a4 4 0 1 1-4 0V4a2 2 0 0 1 4 0Z"/></symbol>
<symbol id="i-dr" viewBox="0 0 24 24"><path d="M12 22a7 7 0 0 0 7-7c0-2-1-3.9-3-5.5s-3.5-4-4-6.5c-.5 2.5-2 4.9-4 6.5C6 11.1 5 13 5 15a7 7 0 0 0 7 7z"/></symbol>
<symbol id="i-su" viewBox="0 0 24 24"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.93 4.93l1.41 1.41M17.66 17.66l1.41 1.41M2 12h2M20 12h2M6.34 17.66l-1.41 1.41M19.07 4.93l-1.41 1.41"/></symbol>
<symbol id="i-fa" viewBox="0 0 24 24"><path d="M10.83 16.38a6.08 6.08 0 0 1-8.62-7l5.41 1.45a6.08 6.08 0 0 1 7-8.62l-1.45 5.41a6.08 6.08 0 0 1 8.62 7l-5.41-1.45a6.08 6.08 0 0 1-7 8.62l1.45-5.41Z"/><path d="M12 12v.01"/></symbol>
<symbol id="i-la" viewBox="0 0 24 24"><path d="M15 14c.2-1 .7-1.7 1.5-2.5 1-.9 1.5-2.2 1.5-3.5A6 6 0 0 0 6 8c0 1 .2 2.2 1.5 3.5.7.7 1.3 1.5 1.5 2.5"/><path d="M9 18h6M10 22h4"/></symbol>
<symbol id="i-al" viewBox="0 0 24 24"><path d="m21.73 18-8-14a2 2 0 0 0-3.48 0l-8 14A2 2 0 0 0 4 21h16a2 2 0 0 0 1.73-3"/><path d="M12 9v4M12 17h.01"/></symbol>
<symbol id="i-up" viewBox="0 0 24 24"><path d="M7 7h10v10M7 17 17 7"/></symbol>
<symbol id="i-dn" viewBox="0 0 24 24"><path d="M7 7l10 10M17 7v10H7"/></symbol>
<symbol id="i-rt" viewBox="0 0 24 24"><path d="M5 12h14M12 5l7 7-7 7"/></symbol>
</defs></svg>

<div id="login" class="ov" hidden>
  <div class="box">
    <h1>Smartroom</h1>
    <div class="msg" style="margin:0 0 12px">Nhập mật khẩu để xem và điều khiển</div>
    <input type="password" id="pw" autocomplete="current-password" placeholder="Mật khẩu" onkeydown="if(event.key==='Enter')doLogin()">
    <button class="pri" onclick="doLogin()">Đăng nhập</button>
    <div class="msg" id="loginMsg" style="color:var(--bad)"></div>
  </div>
</div>

<header>
  <h1>Smartroom<span>Giám sát phòng</span></h1>
  <div class="conn"><span>Cập nhật lần cuối: <span class="mono" id="last">--:--:--</span></span><span class="dot" id="dot"></span><span id="connTx">Chờ dữ liệu</span></div>
</header>

<div class="layout">
<div class="left">
  <div class="kgrid">
    <div class="kpi" id="kT">
      <div class="kh"><svg class="ic"><use href="#i-th"/></svg>Nhiệt độ<svg class="ic al"><use href="#i-al"/></svg></div>
      <div class="kv"><span class="num big" id="t">--</span><span class="u">°C</span></div>
      <div class="kt" id="tTr"></div>
      <div class="kf" id="tF">Bật quạt khi ≥ -- °C, tắt khi ≤ -- °C</div>
    </div>
    <div class="kpi" id="kH">
      <div class="kh"><svg class="ic"><use href="#i-dr"/></svg>Độ ẩm<svg class="ic al"><use href="#i-al"/></svg></div>
      <div class="kv"><span class="num big mid" id="h">--</span><span class="u">%</span></div>
      <div class="kt" id="hTr"></div>
      <div class="kf" id="hF">Bật quạt khi ≥ -- %, tắt khi ≤ -- %</div>
    </div>
    <div class="kpi" id="kL">
      <div class="kh"><svg class="ic"><use href="#i-su"/></svg>Ánh sáng</div>
      <div class="kv"><span class="num big mid" id="lv">--</span><span class="u">/ 4095</span></div>
      <div class="kt" id="lTr"></div>
      <div class="kf" id="lF">Tối khi ≤ -- · Sáng khi ≥ --</div>
    </div>
    <div class="kpi" id="kF">
      <div class="kh"><svg class="ic"><use href="#i-fa"/></svg>Quạt<span class="tag" id="fanTag" style="margin-left:auto"></span></div>
      <div class="kv"><span class="num big mid" id="fp">--</span><span class="u">%</span></div>
      <div class="seg" style="margin-top:8px">
        <button id="fAuto" onclick="fan({mode:'auto'})">Auto</button>
        <button id="fOn" onclick="fan({mode:'manual',percent:100})">Bật</button>
        <button id="fOff" onclick="fan({mode:'manual',percent:0})">Tắt</button>
      </div>
      <div class="kf" id="fF">Auto: chạy 100 % khi vượt ngưỡng, tắt khi xuống ngưỡng tắt</div>
    </div>
  </div>

  <div class="sec charts-sec">
    <h2>Biểu đồ <span class="sp"></span>
      <span class="seg rng"><button id="rLive" class="on" onclick="setRange('live')">10 phút</button><button id="r24" onclick="setRange('24h')">24 giờ</button></span><button class="sm" id="csvBtn" onclick="exportCsv()">Xuất CSV</button></h2>
    <div class="charts">
      <div class="ch"><div class="cl"><span>Nhiệt độ °C</span><b id="cT"></b></div><canvas id="chT"></canvas></div>
      <div class="ch"><div class="cl"><span>Độ ẩm %</span><b id="cH"></b></div><canvas id="chH"></canvas></div>
      <div class="ch"><div class="cl"><span>Ánh sáng</span><b id="cL"></b></div><canvas id="chL"></canvas></div>
    </div>
    <div class="axis"><span id="axA"></span><span id="axB"></span></div>
    <div class="msg" id="histMsg">Đường đỏ nét đứt là ngưỡng. Biểu đồ 10 phút ghi từ lúc mở trang; biểu đồ 24 giờ lưu trong bộ nhớ của ESP32. File CSV chứa tối đa 30 ngày.</div>
  </div>
</div>

<div class="right">
  <div class="sec">
    <h2><svg class="ic"><use href="#i-la"/></svg>Đèn chiếu sáng <span class="sp"></span><span class="tag" id="ledTag"></span></h2>
    <div class="seg">
      <button id="lAuto" onclick="led({mode:'auto'})">Auto</button>
      <button id="lOn" onclick="led({mode:'manual',on:true})">Bật</button>
      <button id="lOff" onclick="led({mode:'manual',on:false})">Tắt</button>
    </div>
    <div class="msg" id="ledWhy">Auto: chỉ bật khi đang trong khung giờ VÀ phòng tối.</div>
  </div>

  <div class="sec">
    <h2>Thiết bị <span class="sp"></span><span class="tag" id="devTag">--</span></h2>
    <div class="rows">
      <div class="row"><span class="k">Wi-Fi</span><span class="v"><svg id="bars" class="ic" viewBox="0 0 24 24" style="width:18px;height:18px"></svg><span id="rssi">--</span></span></div>
      <div class="row"><span class="k">Mạng đang dùng</span><span class="v"><span id="ssid">--</span><button class="sm" onclick="changeWifi()">Đổi Wi-Fi</button></span></div>
      <div class="row"><span class="k">Địa chỉ IP</span><span class="v" id="ip">--</span></div>
      <div class="row"><span class="k">Firmware</span><span class="v" id="fw">--</span></div>
      <div class="row"><span class="k">Thời gian chạy</span><span class="v" id="up">--</span></div>
      <div class="row"><span class="k">Giờ thiết bị</span><span class="v" id="dtime">--</span></div>
      <div class="row"><span class="k">Cảm biến DHT11</span><span class="v" id="dht">--</span></div>
      <div class="row"><span class="k">Đồng hồ RTC</span><span class="v" id="rtc">--</span></div>
      <div class="row"><span class="k">LED báo hiệu</span><span class="v" id="sled">--</span></div>
    </div>
  </div>

  <details>
    <summary>Lịch hẹn giờ đèn</summary>
    <div class="in">
      <div id="slots"></div>
      <button class="pri" onclick="saveSched()" style="margin-top:0">Lưu lịch</button>
      <div class="msg" id="schedMsg">Hỗ trợ khung qua nửa đêm (22:00 → 06:00). Đèn Auto chỉ bật khi trong khung giờ và phòng tối.</div>
    </div>
  </details>
  <details>
    <summary>Ngưỡng quạt tự động</summary>
    <div class="in">
      <div class="g2">
        <div><div class="lab">Nhiệt độ bật (°C)</div><input type="number" id="tOn" step="0.5"></div>
        <div><div class="lab">Nhiệt độ tắt (°C)</div><input type="number" id="tOff" step="0.5"></div>
        <div><div class="lab">Độ ẩm bật (%)</div><input type="number" id="hOn" step="1"></div>
        <div><div class="lab">Độ ẩm tắt (%)</div><input type="number" id="hOff" step="1"></div>
      </div>
      <button class="pri" onclick="saveCfg()">Lưu ngưỡng quạt</button>
      <div class="msg" id="cfgMsg">Quạt bật khi nhiệt độ HOẶC độ ẩm chạm ngưỡng bật, chỉ tắt khi cả hai xuống ngưỡng tắt.</div>
    </div>
  </details>
  <details>
    <summary>Ngưỡng ánh sáng</summary>
    <div class="in">
      <div class="g2">
        <div><div class="lab">Tối khi ≤</div><input type="number" id="lD" step="10"></div>
        <div><div class="lab">Sáng khi ≥</div><input type="number" id="lB" step="10"></div>
      </div>
      <div class="g2" style="margin-top:10px">
        <button class="sm" onclick="$('lD').value=cur.light.level">Mức hiện tại → TỐI</button>
        <button class="sm" onclick="$('lB').value=cur.light.level">Mức hiện tại → SÁNG</button>
      </div>
      <button class="pri" onclick="saveLight()">Lưu ngưỡng ánh sáng</button>
      <div class="msg" id="lightMsg">Che cảm biến rồi bấm "→ TỐI"; để phòng sáng rồi bấm "→ SÁNG".</div>
    </div>
  </details>
  <details>
    <summary>Chế độ thử (giả lập cảm biến)</summary>
    <div class="in">
      <div class="g2">
        <div><div class="lab">Nhiệt độ giả lập (°C)</div><input type="number" id="sT" value="33" step="0.5"></div>
        <div><div class="lab">Độ ẩm giả lập (%)</div><input type="number" id="sH" value="60" step="1"></div>
      </div>
      <div class="g2" style="margin-top:10px"><button onclick="sim(true)">Giả lập</button><button onclick="sim(false)">Tắt giả lập</button></div>
      <div class="msg" id="simMsg">Dữ liệu giả lập không ghi vào biểu đồ 24 giờ.</div>
      <button class="sm" onclick="logout()" style="margin-top:6px">Đăng xuất</button>
    </div>
  </details>
</div>
</div>

<script>
const $=i=>document.getElementById(i);
let PW='';try{PW=localStorage.getItem('pw')||''}catch(e){}
let cur=null,hist=null,range='live',cfgDone=false,lightDone=false,timer=null,lastOk=0;
const BUF=[];   /* {ts,t,h,l} mỗi ~5 giây, giữ 16 phút */
const PAT={normal:'Bình thường',ota:'Đang cập nhật OTA',dht_error:'Lỗi DHT11',rtc_fault:'Lỗi RTC / chưa có giờ',over_threshold:'Vượt ngưỡng',wifi_connecting:'Đang nối Wi-Fi',backend_lost:'Mất kết nối Firebase'};

/*TRANSPORT-BEGIN*/
async function api(p,b){
  const o={headers:{'X-Auth':PW}};
  if(b){o.method='POST';o.headers['Content-Type']='application/json';o.body=JSON.stringify(b);}
  const r=await fetch(p,o);
  if(r.status===401||r.status===429){
    showLogin(r.status===429?'Thử sai quá nhiều lần, đợi 1 phút':(PW?'Sai mật khẩu':''));
    throw new Error('auth');
  }
  return r.json();
}
function showLogin(m){$('login').hidden=false;$('loginMsg').textContent=m||'';}
function doLogin(){PW=$('pw').value;try{localStorage.setItem('pw',PW)}catch(e){}$('login').hidden=true;cfgDone=false;lightDone=false;start();}
function logout(){PW='';try{localStorage.removeItem('pw')}catch(e){}location.reload();}
/*TRANSPORT-END*/
function mark(ids,on){ids.forEach(i=>$(i).classList.toggle('on',i===on));}
function setNum(el,txt){if(el.textContent!==txt){el.textContent=txt;el.classList.remove('chg');void el.offsetWidth;el.classList.add('chg');}}
const pad=n=>String(n).padStart(2,'0');
const clock=d=>pad(d.getHours())+':'+pad(d.getMinutes())+':'+pad(d.getSeconds());
function fmtUp(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return (d?d+'n ':'')+pad(h)+':'+pad(m)+':'+pad(s%60);}

/* ---- xu hướng so với ~5 phút trước ---- */
function prior(key){
  const now=Date.now();
  for(let i=BUF.length-1;i>=0;i--)if(now-BUF[i].ts>=300000&&BUF[i][key]!=null)return BUF[i][key];
  if(hist&&hist.n>=2){const a=key==='t'?hist.t:key==='h'?hist.h:hist.l;return a[a.length-2];}
  return null;
}
function trendHtml(key,val,th,dec){
  const p=prior(key);
  if(val==null||p==null)return '<svg class="ic"><use href="#i-rt"/></svg>chưa đủ dữ liệu 5 phút';
  const d=val-p;
  const ic=Math.abs(d)<th?'i-rt':(d>0?'i-up':'i-dn');
  const sign=d>0?'+':'';
  return '<svg class="ic"><use href="#'+ic+'"/></svg>'+sign+d.toFixed(dec)+' so với 5 phút trước';
}

/* ---- cập nhật giao diện ---- */
async function refresh(){
  try{
    const s=await api('/api/status');cur=s;lastOk=Date.now();
    const se=s.sensor,li=s.light,f=s.fan,l=s.led,c=s.config;
    const tv=(se.temperature==null)?null:se.temperature,hv=(se.humidity==null)?null:se.humidity;
    setNum($('t'),tv==null?'--':tv.toFixed(1));
    setNum($('h'),hv==null?'--':hv.toFixed(0));
    setNum($('lv'),String(li.level));
    setNum($('fp'),String(f.percent));
    $('kT').classList.toggle('bad',!!f.temp_hot);$('kH').classList.toggle('bad',!!f.hum_high);
    $('kT').classList.toggle('warn',!!se.error);$('kH').classList.toggle('warn',!!se.error);
    $('tF').textContent=se.error?'Lỗi cảm biến DHT11 – quạt tự động tạm dừng':(f.temp_hot?'VƯỢT NGƯỠNG – ':'')+'Bật quạt khi ≥ '+c.temp_on+' °C, tắt khi ≤ '+c.temp_off+' °C';
    $('hF').textContent=se.error?'Lỗi cảm biến DHT11 – quạt tự động tạm dừng':(f.hum_high?'VƯỢT NGƯỠNG – ':'')+'Bật quạt khi ≥ '+c.hum_on+' %, tắt khi ≤ '+c.hum_off+' %';
    $('lF').textContent='Phòng '+(li.dark?'TỐI':'SÁNG')+' · tối khi ≤ '+li.dark_level+' · sáng khi ≥ '+li.bright_level;
    $('fanTag').textContent=f.mode==='auto'?'AUTO':'THỦ CÔNG';$('fanTag').className='tag '+(f.percent>0?'ok':'');
    $('fF').textContent=f.mode==='auto'?'Auto: chạy 100 % khi vượt ngưỡng, tắt khi xuống ngưỡng tắt':'Thủ công: không tự tắt cho tới khi bấm Auto';
    mark(['fAuto','fOn','fOff'],f.mode==='auto'?'fAuto':(f.percent===0?'fOff':'fOn'));
    $('ledTag').textContent=(l.mode==='auto'?'AUTO':'THỦ CÔNG')+' · '+(l.on?'SÁNG':'TẮT');$('ledTag').className='tag '+(l.on?'warn':'');
    mark(['lAuto','lOn','lOff'],l.mode==='auto'?'lAuto':(l.on?'lOn':'lOff'));
    $('ledWhy').textContent=l.mode==='auto'?(l.on?'Đang trong khung giờ và phòng tối.':(li.dark?'Phòng tối nhưng chưa tới khung giờ.':'Phòng còn sáng nên chưa bật.')):'Thủ công: không phụ thuộc khung giờ và ánh sáng.';
    /* thiết bị */
    const w=s.wifi;
    $('devTag').textContent='ONLINE';$('devTag').className='tag ok';
    $('rssi').textContent=w.connected?(w.rssi+' dBm'):(w.portal?'chưa có mạng':'mất kết nối');
    $('ssid').textContent=w.ssid||(w.portal?w.setup_ssid:'--');
    const nb=!w.connected?0:(w.rssi>-55?4:w.rssi>-65?3:w.rssi>-75?2:w.rssi>-85?1:0);
    let bars='';for(let i=0;i<4;i++){const h=5+i*4;bars+='<rect x="'+(3+i*5)+'" y="'+(20-h)+'" width="3" height="'+h+'" rx="1" fill="currentColor" opacity="'+(i<nb?1:.25)+'" stroke="none"/>';}
    $('bars').innerHTML=bars;
    $('ip').textContent=w.ip||'--';$('fw').textContent='v'+s.fw;$('up').textContent=fmtUp(s.uptime_s);$('dtime').textContent=s.time.local;
    $('dht').innerHTML=se.error?'<span class="tag bad">LỖI</span>':(se.sim?'<span class="tag warn">GIẢ LẬP</span>':'<span class="tag ok">OK</span>');
    $('rtc').innerHTML=!s.time.rtc_found?'<span class="tag bad">KHÔNG THẤY</span>':'<span class="tag ok">OK</span> '+(s.time.rtc_temp!=null?s.time.rtc_temp.toFixed(1)+' °C':'');
    $('sled').textContent=PAT[s.status_led]||s.status_led;
    if(!cfgDone){$('tOn').value=c.temp_on;$('tOff').value=c.temp_off;$('hOn').value=c.hum_on;$('hOff').value=c.hum_off;cfgDone=true;}
    if(!lightDone){$('lD').value=li.dark_level;$('lB').value=li.bright_level;lightDone=true;}
    /* bộ đệm cho biểu đồ 10 phút + xu hướng */
    const now=Date.now();
    if(!BUF.length||now-BUF[BUF.length-1].ts>=4500){BUF.push({ts:now,t:tv,h:hv,l:li.level});while(BUF.length&&now-BUF[0].ts>960000)BUF.shift();}
    $('tTr').innerHTML=trendHtml('t',tv,0.2,1);$('hTr').innerHTML=trendHtml('h',hv,1,0);$('lTr').innerHTML=trendHtml('l',li.level,30,0);
    drawCharts();
  }catch(e){if(e.message!=='auth'){$('devTag').textContent='OFFLINE';$('devTag').className='tag bad';}}
  tickConn();
}
function tickConn(){
  const ok=lastOk&&Date.now()-lastOk<15000;
  $('dot').className='dot'+(ok?' ok':'');
  $('connTx').textContent=ok?'Đã kết nối':(lastOk?'Mất kết nối':'Chờ dữ liệu');
  if(lastOk)$('last').textContent=clock(new Date(lastOk));
}

/* ---- điều khiển ---- */
async function changeWifi(){
  if(location.hostname==='192.168.4.1'){location.href='/';return;}
  if(!cur||!confirm('Phát Wi-Fi cài đặt để đổi mạng cho thiết bị?'))return;
  try{
    const r=await api('/api/wifi/portal',{});
    alert(r.ok?'Đã phát Wi-Fi "'+cur.wifi.setup_ssid+'". Nối điện thoại vào mạng đó rồi mở http://192.168.4.1/':'Lỗi: '+r.error);
  }catch(e){}
}
async function fan(b){try{await api('/api/fan',b);refresh();}catch(e){}}
async function led(b){try{await api('/api/led',b);refresh();}catch(e){}}
async function saveCfg(){try{const r=await api('/api/config',{temp_on:+$('tOn').value,temp_off:+$('tOff').value,hum_on:+$('hOn').value,hum_off:+$('hOff').value});$('cfgMsg').textContent=r.ok?'Đã lưu.':('Lỗi: '+r.error);refresh();}catch(e){}}
async function saveLight(){try{const r=await api('/api/light',{dark:+$('lD').value,bright:+$('lB').value});$('lightMsg').textContent=r.ok?'Đã lưu ngưỡng ánh sáng.':('Lỗi: '+r.error);refresh();}catch(e){}}
async function sim(on){try{const r=await api('/api/sim',on?{temp:+$('sT').value,hum:+$('sH').value}:{off:true});$('simMsg').textContent=r.ok?(on?'Đang giả lập cảm biến.':'Đã tắt giả lập.'):('Lỗi: '+r.error);refresh();}catch(e){}}
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
  try{const r=await api('/api/schedule',{slots:slots});$('schedMsg').textContent=r.ok?'Đã lưu lịch.':('Lỗi: '+r.error);refresh();}catch(e){}
}

/* ---- biểu đồ ---- */
function draw(id,pts,x0,x1,lines,dec,gap){
  const c=$(id),dpr=window.devicePixelRatio||1,w=c.clientWidth,h=c.clientHeight;
  if(!w)return;
  c.width=w*dpr;c.height=h*dpr;
  const g=c.getContext('2d');g.setTransform(dpr,0,0,dpr,0,0);g.clearRect(0,0,w,h);
  g.font='10px "JetBrains Mono",monospace';g.fillStyle='#8A94A6';
  if(pts.length<2){g.fillText('Chưa đủ dữ liệu',6,h/2);return;}
  let ys=pts.map(p=>p.y).concat(lines.map(l=>l.v));
  let mn=Math.min(...ys),mx=Math.max(...ys);if(mx-mn<1){mx+=.5;mn-=.5;}
  const sp=(mx-mn)*.08;mn-=sp;mx+=sp;
  const pl=38,pr=4,pt=6,pb=4,W=w-pl-pr,H=h-pt-pb;
  const X=x=>pl+W*(x-x0)/(x1-x0),Y=y=>pt+H*(1-(y-mn)/(mx-mn));
  g.strokeStyle='#1F2937';g.lineWidth=1;
  for(let i=0;i<=2;i++){const y=pt+H*i/2;g.beginPath();g.moveTo(pl,y);g.lineTo(w-pr,y);g.stroke();g.fillText((mx-(mx-mn)*i/2).toFixed(dec),0,y+3);}
  lines.forEach(l=>{g.strokeStyle=l.color;g.setLineDash([4,3]);g.beginPath();g.moveTo(pl,Y(l.v));g.lineTo(w-pr,Y(l.v));g.stroke();g.setLineDash([]);});
  g.strokeStyle='#00E5A0';g.lineWidth=1.8;g.lineJoin='round';g.beginPath();
  pts.forEach((p,i)=>{i&&p.x-pts[i-1].x<=gap?g.lineTo(X(p.x),Y(p.y)):g.moveTo(X(p.x),Y(p.y));});g.stroke();
  g.fillStyle='#00E5A0';pts.forEach((p,i)=>{const a=i&&p.x-pts[i-1].x<=gap,b=i<pts.length-1&&pts[i+1].x-p.x<=gap;if(!a&&!b)g.fillRect(X(p.x)-1.5,Y(p.y)-1.5,3,3);});
}
function drawCharts(){
  if(!cur)return;
  const c=cur.config,li=cur.light;
  const L={t:[{v:c.temp_on,color:'#FF4757'}],h:[{v:c.hum_on,color:'#FF4757'}],l:[{v:li.dark_level,color:'#FFA502'}]};
  let P={t:[],h:[],l:[]},x0,x1,gap=30000;
  if(range==='live'){
    const now=Date.now();x0=now-600000;x1=now;
    BUF.filter(b=>b.ts>=x0).forEach(b=>{if(b.t!=null)P.t.push({x:b.ts,y:b.t});if(b.h!=null)P.h.push({x:b.ts,y:b.h});P.l.push({x:b.ts,y:b.l});});
    $('axA').textContent='-10 phút';$('axB').textContent='bây giờ';
  }else if(hist&&hist.n){
    x0=hist.ts[0];x1=Math.max(hist.ts[hist.n-1],x0+1);gap=hist.interval*2.5;
    hist.ts.forEach((x,i)=>{P.t.push({x:x,y:hist.t[i]});P.h.push({x:x,y:hist.h[i]});P.l.push({x:x,y:hist.l[i]});});
    const hh=ts=>{const d=new Date(ts*1000);return pad(d.getHours())+':'+pad(d.getMinutes());};
    $('axA').textContent=hh(x0);$('axB').textContent=hh(hist.ts[hist.n-1]);
  }else{x0=0;x1=1;$('axA').textContent='';$('axB').textContent='';}
  draw('chT',P.t,x0,x1,L.t,1,gap);draw('chH',P.h,x0,x1,L.h,0,gap);draw('chL',P.l,x0,x1,L.l,0,gap);
  const last=a=>a.length?a[a.length-1].y:null;
  $('cT').textContent=last(P.t)==null?'':last(P.t).toFixed(1)+' °C';$('cH').textContent=last(P.h)==null?'':last(P.h).toFixed(0)+' %';$('cL').textContent=last(P.l)==null?'':last(P.l);
}
function setRange(r){range=r;mark(['rLive','r24'],r==='live'?'rLive':'r24');drawCharts();}
async function loadHist(){try{hist=await api('/api/history');drawCharts();}catch(e){}}
function saveFile(blob,name){
  const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download=name;
  document.body.appendChild(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(a.href),10000);
}
/*CSV-BEGIN*/
async function exportCsv(){
  try{
    const r=await fetch('/api/history.csv',{headers:{'X-Auth':PW}});
    if(!r.ok){$('histMsg').textContent='Không xuất được CSV (mã '+r.status+').';return;}
    saveFile(await r.blob(),'smartroom-history.csv');
  }catch(e){$('histMsg').textContent='Không xuất được CSV.';}
}
/*CSV-END*/

function start(){
  if(timer)clearInterval(timer);
  loadSched();refresh();loadHist();
  timer=setInterval(()=>{if(!document.hidden)refresh();},5000);
  setInterval(()=>{if(!document.hidden)loadHist();},60000);
  setInterval(tickConn,1000);
}
window.addEventListener('resize',drawCharts);
document.addEventListener('visibilitychange',()=>{if(!document.hidden&&PW)refresh();});
/*BOOT-BEGIN*/if(!PW)showLogin('');else start();/*BOOT-END*/
</script></body></html>)rawliteral";
