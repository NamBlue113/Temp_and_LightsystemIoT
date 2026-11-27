/* Lớp truyền thông của bản điều khiển từ xa: thay cho các lệnh HTTP của trang trên ESP32.
   Trình duyệt đăng nhập Firebase rồi đọc trạng thái / gửi lệnh qua Realtime Database (REST);
   ESP32 dùng cùng cơ sở dữ liệu bằng tài khoản riêng. */
const FB={key:'__FIREBASE_API_KEY__',db:'https://__FIREBASE_DB_HOST__',dev:'__FIREBASE_DEVICE_ID__'};
const BASE=FB.db+'/devices/'+FB.dev,STALE_MS=35000;
let CFG=null,idToken='',expAt=0,es=null,retryT=null,lastState=null,stateAt=0,started=false,synced=false;
try{CFG=JSON.parse(localStorage.getItem('sr-fb'))}catch(e){}

function showLogin(m){
  $('login').hidden=false;$('loginMsg').textContent=m||'';
  if(CFG)$('user').value=CFG.email;
}
function saveCfgLocal(){try{localStorage.setItem('sr-fb',JSON.stringify(CFG))}catch(e){}}
async function post(url,type,body){
  const r=await fetch(url,{method:'POST',headers:{'Content-Type':type},body:body});
  let d={};try{d=await r.json()}catch(e){}
  return {code:r.status,d:d};
}
async function doLogin(){
  const email=$('user').value.trim(),pass=$('pw').value;
  if(!email||!pass){$('loginMsg').textContent='Hãy nhập email và mật khẩu.';return;}
  $('loginMsg').textContent='Đang đăng nhập...';
  let r;
  try{
    r=await post('https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key='+FB.key,
      'application/json',JSON.stringify({email:email,password:pass,returnSecureToken:true}));
  }catch(e){$('loginMsg').textContent='Không kết nối được. Kiểm tra mạng.';return;}
  if(r.code!==200){
    $('loginMsg').textContent=/TOO_MANY/.test((r.d.error||{}).message||'')?'Thử sai quá nhiều lần, đợi một lúc.':'Sai email hoặc mật khẩu.';
    return;
  }
  /* chỉ lưu mã làm mới phiên, không lưu mật khẩu */
  CFG={email:email,refresh:r.d.refreshToken};saveCfgLocal();
  idToken=r.d.idToken;expAt=Date.now()+(+r.d.expiresIn)*1000;
  $('pw').value='';$('login').hidden=true;$('loginMsg').textContent='';
  cfgDone=false;lightDone=false;synced=false;
  connect();begin();
}
function logout(){try{localStorage.removeItem('sr-fb')}catch(e){}location.reload();}
function begin(){if(!started){started=true;start();}else refresh();}

async function token(force){
  if(!CFG)throw new Error('auth');
  if(!force&&idToken&&Date.now()<expAt-60000)return idToken;
  const r=await post('https://securetoken.googleapis.com/v1/token?key='+FB.key,'application/x-www-form-urlencoded',
    'grant_type=refresh_token&refresh_token='+encodeURIComponent(CFG.refresh));
  if(r.code>=400&&r.code<500){
    try{localStorage.removeItem('sr-fb')}catch(e){}
    CFG=null;idToken='';disconnect();showLogin('Phiên đăng nhập đã hết, hãy đăng nhập lại.');
    throw new Error('auth');
  }
  if(r.code!==200)throw new Error('offline');
  idToken=r.d.id_token;expAt=Date.now()+(+r.d.expires_in)*1000;CFG.refresh=r.d.refresh_token;saveCfgLocal();
  return idToken;
}

/* ---- nhận trạng thái tức thời (Server-Sent Events) ---- */
function disconnect(){clearTimeout(retryT);if(es){es.close();es=null;}}
function retry(force){
  disconnect();
  retryT=setTimeout(()=>connect(force),3000);
}
async function connect(force){
  disconnect();
  let t;
  try{t=await token(force)}catch(e){if(e.message!=='auth')retry(force);return;}
  disconnect();
  const s=new EventSource(BASE+'/state.json?auth='+t);es=s;
  let first=true;
  const apply=ev=>{
    if(s!==es)return;
    const m=JSON.parse(ev.data),initial=first;first=false;
    if(m.path!=='/'||!m.data)return;
    lastState=m.data;
    /* bản đầu tiên có thể là dữ liệu cũ; các bản sau là dữ liệu vừa được gửi */
    if(!initial||Math.abs(Date.now()-m.data.at)<STALE_MS)stateAt=Date.now();
    refresh();
    if(isOnline()&&!synced){synced=true;loadSched();loadHist();}
  };
  s.addEventListener('put',apply);
  s.addEventListener('auth_revoked',()=>{if(s===es)retry(true);});
  s.addEventListener('cancel',()=>{if(s===es){disconnect();showLogin('Tài khoản này không có quyền xem thiết bị.');}});
  s.onerror=()=>{if(s===es)retry(false);};
}
function isOnline(){
  const ok=!!lastState&&Date.now()-stateAt<STALE_MS;
  if(!ok)synced=false;
  return ok;
}

/* ---- gửi lệnh và chờ phản hồi ---- */
const wait=ms=>new Promise(r=>setTimeout(r,ms));
async function send(op,body){
  if(!isOnline())return {ok:false,error:'Thiết bị đang ngoại tuyến'};
  const id=Date.now().toString(36)+Math.random().toString(36).slice(2,8);
  try{
    const q='.json?auth='+await token();
    const w=await fetch(BASE+'/cmd/'+id+q+'&print=silent',{method:'PUT',headers:{'Content-Type':'application/json'},
      body:JSON.stringify({op:op,body:JSON.stringify(body||{}),at:{'.sv':'timestamp'}})});
    if(w.status===401||w.status===403)return {ok:false,error:'Tài khoản này không có quyền điều khiển'};
    if(!w.ok)return {ok:false,error:'Không gửi được lệnh (mã '+w.status+')'};
    const end=Date.now()+10000;
    while(Date.now()<end){
      await wait(400);
      const r=await fetch(BASE+'/reply/'+id+q);
      if(!r.ok)continue;
      const text=await r.json();
      if(text){
        fetch(BASE+'/reply/'+id+q+'&print=silent',{method:'DELETE'}).catch(()=>{});
        return JSON.parse(text);
      }
    }
    /* quá hạn: gỡ lệnh để thiết bị không chạy muộn */
    fetch(BASE+'/cmd/'+id+q+'&print=silent',{method:'DELETE'}).catch(()=>{});
    return {ok:false,error:'Thiết bị không phản hồi'};
  }catch(e){return {ok:false,error:'Mất kết nối mạng'};}
}

async function api(p,b){
  if(!CFG)throw new Error('auth');
  if(p==='/api/status'){
    if(!isOnline())throw new Error('offline');
    return lastState;
  }
  const op={'/api/fan':'fan','/api/led':'led','/api/config':'config','/api/light':'light','/api/history':'history',
    '/api/schedule':b?'schedule_set':'schedule_get'}[p];
  if(!op)return {ok:false,error:'Chức năng này chỉ dùng được khi ở nhà'};
  const r=await send(op,b);
  if(!b&&!r.ok)throw new Error(r.error);          /* lệnh đọc thất bại: không ghi đè giao diện */
  if((op==='fan'||op==='led')&&!r.ok)alert(r.error);
  return r;
}
