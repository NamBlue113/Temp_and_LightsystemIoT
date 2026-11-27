"""Sinh trang điều khiển từ xa docs/index.html từ giao diện trong include/web_page.h.

Chạy lại mỗi khi sửa giao diện:   python tools/build_remote.py
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
def read(*parts):
    return io.open(os.path.join(ROOT, *parts), encoding='utf-8').read().replace('\r\n', '\n')


def swap(text, old, new):
    if text.count(old) != 1:
        sys.exit('build_remote: khong tim thay dung 1 lan: %r' % old[:70])
    return text.replace(old, new)


def block(text, name, new):
    pattern = r'/\*%s-BEGIN\*/.*?/\*%s-END\*/' % (name, name)
    text, n = re.subn(pattern, lambda _: new, text, flags=re.S)
    if n != 1:
        sys.exit('build_remote: thieu khoi %s' % name)
    return text


def build():
    header = read('include', 'web_page.h')
    m = re.search(r'INDEX_HTML\[\] PROGMEM = R"rawliteral\((.*?)\)rawliteral"', header, re.S)
    if not m:
        sys.exit('build_remote: khong doc duoc INDEX_HTML')
    html = m.group(1)
    cfg = dict(re.findall(r'^#define\s+(FIREBASE_\w+)\s+"([^"]*)"', read('include', 'firebase_config.h'), re.M))
    transport = read('tools', 'remote_transport.js').strip()
    for name in ('FIREBASE_API_KEY', 'FIREBASE_DB_HOST', 'FIREBASE_DEVICE_ID'):
        transport = swap(transport, '__%s__' % name, cfg[name])

    html = swap(html, '<link rel="manifest" href="/manifest.json">\n', '')
    html = swap(html, '<title>Smartroom</title>', '<title>Smartroom từ xa</title>')
    html = swap(html, 'input[type=number],input[type=time],input[type=password]{',
                'input[type=email],input[type=number],input[type=time],input[type=password]{')
    html = swap(html, '<div class="msg" style="margin:0 0 12px">Nhập mật khẩu để xem và điều khiển</div>',
                '<div class="msg" style="margin:0 0 12px">Điều khiển từ xa · đăng nhập bằng tài khoản Firebase dành cho trang web</div>\n'
                '    <input type="email" id="user" placeholder="Email" autocomplete="username" autocapitalize="off" autocorrect="off" style="margin-bottom:8px">')
    html = swap(html, '<button class="sm" onclick="changeWifi()">Đổi Wi-Fi</button>', '')
    html, n = re.subn(r'<details>\s*<summary>Chế độ thử \(giả lập cảm biến\)</summary>.*?</details>',
                      '<details>\n    <summary>Tài khoản</summary>\n    <div class="in">\n'
                      '      <button class="sm" onclick="logout()">Đăng xuất khỏi trình duyệt này</button>\n    </div>\n  </details>',
                      html, flags=re.S)
    if n != 1:
        sys.exit('build_remote: khong tim thay muc che do thu')
    html = swap(html, 'File CSV chứa tối đa 30 ngày.', 'Khi điều khiển từ xa, file CSV chứa 24 giờ gần nhất.')

    html = block(html, 'TRANSPORT', transport)
    html = block(html, 'CSV', '''async function exportCsv(){
  try{
    const d=await api('/api/history'),p2=n=>String(n).padStart(2,'0');
    let s='thoi_gian,epoch,nhiet_do_c,do_am_pct,anh_sang\\r\\n';
    d.ts.forEach((ts,i)=>{
      const x=new Date(ts*1000);
      s+=x.getFullYear()+'-'+p2(x.getMonth()+1)+'-'+p2(x.getDate())+' '+clock(x)+','+ts+','+d.t[i]+','+d.h[i]+','+d.l[i]+'\\r\\n';
    });
    saveFile(new Blob([s],{type:'text/csv'}),'smartroom-history.csv');
  }catch(e){$('histMsg').textContent='Không xuất được CSV: '+e.message;}
}''')
    html = block(html, 'BOOT', "if(!CFG)showLogin('');else{connect();begin();}")
    html = swap(html, "document.addEventListener('visibilitychange',()=>{if(!document.hidden&&PW)refresh();});",
                "document.addEventListener('visibilitychange',()=>{if(!document.hidden&&CFG)refresh();});")

    out = os.path.join(ROOT, 'docs', 'index.html')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    io.open(out, 'w', encoding='utf-8', newline='\n').write(html + '\n')
    print('da tao', out, len(html), 'ky tu')


if __name__ == '__main__':
    build()
