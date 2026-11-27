#pragma once
// Trang cài đặt Wi-Fi, chỉ phục vụ cho thiết bị đang nối vào Wi-Fi riêng của ESP32.
// Không tải tài nguyên ngoài vì lúc này chưa có Internet.
#include <Arduino.h>

const char WIFI_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="vi"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0A0E14">
<title>Cài đặt Wi-Fi</title>
<style>
:root{--bg:#0A0E14;--line:#1F2937;--tx:#E5E9F0;--mut:#8A94A6;--ac:#00E5A0;--bad:#FF4757;--r:8px;--mono:ui-monospace,SFMono-Regular,Consolas,monospace}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0 auto;max-width:440px;padding:16px;background:var(--bg);color:var(--tx);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
h1{font-size:14px;letter-spacing:.08em;text-transform:uppercase;margin:4px 0}
h2{font-size:11px;letter-spacing:.08em;text-transform:uppercase;color:var(--mut);font-weight:500;margin:20px 0 8px;display:flex;justify-content:space-between;align-items:center}
.mut{color:var(--mut);font-size:13px}
button,input{font:inherit;border-radius:var(--r);border:1px solid var(--line);min-height:44px;width:100%}
button{background:transparent;color:var(--tx);padding:8px 12px;cursor:pointer}
input{background:#0D131B;color:var(--tx);padding:8px 10px;margin-bottom:10px}
input[type=checkbox]{width:18px;height:18px;min-height:0;margin:0;accent-color:var(--ac)}
.net{display:flex;justify-content:space-between;gap:10px;margin-bottom:6px;text-align:left}
.net span:first-child{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.net span:last-child{color:var(--mut);font:12px var(--mono);white-space:nowrap}
.net.on{border-color:var(--ac)}
.pri{background:var(--ac);border-color:var(--ac);color:#0A0E14;font-weight:600}
.sm{width:auto;min-height:30px;padding:2px 10px;font-size:12px}
label.chk{display:flex;align-items:center;gap:8px;margin:0 0 12px;font-size:13px;color:var(--mut)}
#res{margin-top:12px;padding:12px;border:1px solid var(--line);border-radius:var(--r);display:none;word-break:break-word}
#res.ok{border-color:var(--ac)}#res.bad{border-color:var(--bad)}
a{color:var(--ac)}
</style></head><body>
<h1>Smartroom · Cài đặt Wi-Fi</h1>
<div class="mut" id="now">Đang đọc trạng thái...</div>

<h2>Mạng xung quanh <button class="sm" onclick="scan(true)">Quét lại</button></h2>
<div id="list" class="mut">Đang quét...</div>

<h2>Kết nối</h2>
<input id="ssid" placeholder="Tên mạng (chọn ở trên hoặc tự nhập)" maxlength="32" autocapitalize="off" autocorrect="off">
<input id="pw" type="password" placeholder="Mật khẩu Wi-Fi (để trống nếu mạng mở)" maxlength="64" autocapitalize="off" autocorrect="off">
<label class="chk"><input type="checkbox" onchange="$('pw').type=this.checked?'text':'password'">Hiện mật khẩu</label>
<button class="pri" id="go" onclick="join()">Kết nối</button>
<button id="later" onclick="keep()" style="margin-top:8px">Lưu để dùng sau</button>
<div class="mut" style="margin-top:6px">"Lưu để dùng sau" dành cho mạng đang tắt, ví dụ hotspot của chính điện thoại này.</div>
<div id="res"></div>

<h2>Mạng đã nhớ</h2>
<div id="saved" class="mut">--</div>

<h2>Khi chưa có mạng</h2>
<div class="mut"><a href="/dashboard">Mở bảng điều khiển</a> để xem số liệu, điều khiển quạt và đèn (cần mật khẩu web). Chỉ dùng được Wi-Fi 2,4 GHz.</div>

<script>
const $=i=>document.getElementById(i);
let waiting=false,lost=0,savedKey=null;
async function get(u,o){const r=await fetch(u,o);return r.json();}
function result(t,c){const e=$('res');e.style.display='block';e.className=c||'';e.textContent=t;}
async function info(){
  try{
    const s=await get('/api/wifi/info');lost=0;
    $('now').textContent=s.connected?'Đang dùng mạng "'+s.current+'" ('+s.ip+')':'Chưa kết nối mạng nào';
    showSaved(s.saved);
    if(!waiting)return;
    if(s.state==='ok'){waiting=false;$('go').disabled=false;result('Đã kết nối "'+s.target+'". Địa chỉ mới: http://'+s.ip+'/ — hãy nối điện thoại vào cùng mạng đó rồi mở địa chỉ này. Wi-Fi riêng sẽ tắt sau khoảng 30 giây.','ok');}
    else if(s.state==='fail'){waiting=false;$('go').disabled=false;result('Không kết nối được "'+s.target+'". Kiểm tra mật khẩu và băng tần 2,4 GHz rồi thử lại.','bad');}
  }catch(e){
    if(waiting&&++lost>=5)result('Mất liên lạc với thiết bị. Xem địa chỉ IP trên màn LCD; nếu thấy lại Wi-Fi Smartroom-Setup thì nối vào và thử lại.');
  }
}
function showSaved(list){
  const key=JSON.stringify(list);if(key===savedKey)return;savedKey=key;
  const box=$('saved');box.textContent='';
  if(!list.length){box.textContent='Chưa nhớ mạng nào.';return;}
  list.forEach(n=>{
    const row=document.createElement('div');row.className='net';row.style.alignItems='center';
    const a=document.createElement('span'),x=document.createElement('button');
    a.textContent=n;x.className='sm';x.textContent='Xóa';
    x.onclick=async()=>{if(!confirm('Quên mạng "'+n+'"?'))return;await get('/api/wifi/forget',post({ssid:n}));info();};
    row.append(a,x);box.appendChild(row);
  });
}
const post=b=>({method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});
async function keep(){
  const ssid=$('ssid').value;
  if(!ssid){result('Hãy chọn hoặc nhập tên mạng.','bad');return;}
  try{
    const r=await get('/api/wifi/save',post({ssid:ssid,password:$('pw').value}));
    if(!r.ok){result('Lỗi: '+r.error,'bad');return;}
    result('Đã lưu "'+ssid+'" (chưa kiểm tra mật khẩu). Hãy ngắt điện thoại khỏi Smartroom-Setup rồi bật mạng đó; thiết bị sẽ tự kết nối trong khoảng 1 phút.','ok');
    info();
  }catch(e){result('Không gửi được yêu cầu, thử lại.','bad');}
}
async function scan(fresh){
  try{
    const r=await get('/api/wifi/scan'+(fresh?'?new=1':''));
    if(r.scanning){$('list').textContent='Đang quét...';setTimeout(()=>scan(false),1500);return;}
    const box=$('list');box.textContent='';
    if(!r.list.length){box.textContent='Không thấy mạng nào. Bấm "Quét lại" hoặc tự nhập tên mạng.';return;}
    r.list.forEach(n=>{
      const b=document.createElement('button');b.className='net';
      const a=document.createElement('span'),c=document.createElement('span');
      a.textContent=n.ssid;c.textContent=(n.open?'mở':'khóa')+' · '+n.rssi+' dBm';
      b.append(a,c);
      b.onclick=()=>{$('ssid').value=n.ssid;document.querySelectorAll('.net').forEach(x=>x.classList.remove('on'));b.classList.add('on');$('pw').focus();};
      box.appendChild(b);
    });
  }catch(e){$('list').textContent='Không quét được. Bấm "Quét lại".';}
}
async function join(){
  const ssid=$('ssid').value;
  if(!ssid){result('Hãy chọn hoặc nhập tên mạng.','bad');return;}
  $('go').disabled=true;result('Đang kết nối "'+ssid+'"... (tối đa khoảng 15 giây)');
  try{
    const r=await get('/api/wifi/connect',post({ssid:ssid,password:$('pw').value}));
    if(!r.ok){$('go').disabled=false;result('Lỗi: '+r.error,'bad');return;}
    waiting=true;lost=0;
  }catch(e){$('go').disabled=false;result('Không gửi được yêu cầu, thử lại.','bad');}
}
info();scan(false);setInterval(info,2000);
</script></body></html>)rawliteral";
