#pragma once

namespace mp4 {

const char kIndexHtml[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MP4 Player</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;background:#1a1a2e;color:#e0e0e0;font-size:16px;line-height:1.5}
.hdr{background:#16213e;padding:12px 16px;display:flex;justify-content:space-between;align-items:center;border-bottom:2px solid #0f3460}
.hdr h1{color:#e94560;font-size:20px}
.tabs{display:flex;gap:6px}
.tab{background:#0f3460;color:#ccc;border:none;padding:8px 14px;border-radius:8px;cursor:pointer;font-size:13px;font-weight:bold}
.tab.act{background:#e94560;color:#fff}
.cnt{padding:16px;max-width:600px;margin:0 auto}
.vw{display:none}.vw.act{display:block}
.st-bar{background:#16213e;border-radius:12px;padding:14px 16px;margin-bottom:16px;text-align:center}
.st-txt{font-size:13px;color:#aaa}.st-file{font-size:18px;font-weight:bold;color:#e94560;margin-top:4px;word-break:break-all}
.ctrls{display:flex;justify-content:center;gap:12px;margin-bottom:16px}
.cb{background:#0f3460;color:#fff;border:none;width:52px;height:52px;border-radius:50%;cursor:pointer;font-size:22px;display:flex;align-items:center;justify-content:center;transition:background .15s}
.cb:active{transform:scale(.93);background:#e94560}
.cb.pl{background:#e94560;width:60px;height:60px;font-size:26px}
.cb.pl:active{background:#c0392b}
.pl-item{background:#16213e;border-radius:10px;padding:12px 16px;margin:8px 0;display:flex;align-items:center;cursor:pointer;transition:background .1s}
.pl-item:active{background:#0f3460}
.pl-item.cur{border-left:4px solid #e94560}
.pl-item .nm{flex:1;word-break:break-all;font-size:15px}
.pl-item .ic{color:#e94560;font-size:18px;margin-left:8px}
.sec-title{font-size:13px;color:#888;text-transform:uppercase;letter-spacing:1px;margin:20px 0 8px;padding-left:4px}
.nr{display:flex;align-items:center;gap:8px;margin-bottom:12px;flex-wrap:wrap}
.nb{background:#0f3460;color:#e0e0e0;border:none;padding:8px 14px;border-radius:8px;cursor:pointer;font-size:13px;font-weight:bold;white-space:nowrap}
.nb:active{background:#e94560}
.nb.up{background:#28a745}.nb.up:active{background:#218838}
.nb.nf{background:#fd7e14}.nb.nf:active{background:#e8650e}
.pt{color:#aaa;font-size:13px;flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.ua{border:2px dashed #0f3460;border-radius:12px;padding:24px 16px;text-align:center;margin-bottom:16px;display:none;background:#16213e}
.ua.sh{display:block}.ua.dg{border-color:#28a745;background:#1a2e1a}
.ua p{color:#aaa;margin-bottom:12px;font-size:14px}
.ub{background:#0f3460;color:#fff;border:none;padding:12px 24px;border-radius:10px;cursor:pointer;font-size:15px;font-weight:bold}
.pw{display:none;margin-top:12px;align-items:center;gap:8px}.pw.sh{display:flex}
.pbg{flex:1;background:#0f3460;border-radius:8px;overflow:hidden;height:20px}
.pb{height:100%;background:linear-gradient(90deg,#28a745,#20c997);border-radius:8px;transition:width .1s;display:flex;align-items:center;justify-content:center;min-width:32px}
.ppct{color:#fff;font-size:11px;font-weight:bold}
.us{margin-top:8px;font-size:13px;color:#28a745}
.fi{background:#16213e;border-radius:10px;padding:12px 16px;margin:8px 0;cursor:pointer;display:flex;align-items:center;transition:background .1s}
.fi:active{background:#0f3460}
.fi.dr{border-left:3px solid #fd7e14}
.fn{flex:1;word-break:break-all;font-size:15px}
.fs{color:#aaa;font-size:12px;margin-left:8px;white-space:nowrap}
.fic{margin-right:8px;font-size:14px}
.ov{display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,.7);z-index:100}
.ov.sh{display:flex;justify-content:center;align-items:center;padding:16px}
.dlg{background:#16213e;padding:24px;border-radius:16px;width:100%;max-width:320px;text-align:center}
.dlg h3{color:#e0e0e0;margin-bottom:20px;font-size:17px;word-break:break-all}
.dbs{display:flex;flex-direction:column;gap:10px}
.db{padding:13px;border:none;border-radius:10px;cursor:pointer;font-size:15px;font-weight:bold;color:#fff}
.db:active{transform:scale(.98)}
.db-dl{background:#4dabf7}.db-del{background:#e94560}.db-ren{background:#fd7e14}
.db-pl{background:#28a745}.db-x{background:#495057;color:#ccc}.db-sv{background:#28a745}
.di{width:100%;padding:12px;font-size:15px;border:2px solid #0f3460;border-radius:8px;margin-bottom:16px;background:#1a1a2e;color:#e0e0e0}
.di:focus{outline:none;border-color:#e94560}
.sr{display:flex;justify-content:space-between;padding:10px 0;border-bottom:1px solid #0f3460;text-align:left}
.sr:last-child{border-bottom:none}
.sl{color:#aaa;font-size:13px}.sv{color:#e0e0e0;font-weight:bold;font-size:13px}
.ld{display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(26,26,46,.9);z-index:200;justify-content:center;align-items:center;flex-direction:column}
.ld.sh{display:flex}
.sp{width:40px;height:40px;border:4px solid #0f3460;border-top:4px solid #e94560;border-radius:50%;animation:spin 1s linear infinite}
@keyframes spin{0%{transform:rotate(0)}100%{transform:rotate(360deg)}}
.lt{color:#aaa;margin-top:12px;font-size:14px}
.em{text-align:center;color:#666;padding:32px;font-size:14px}
.cb-x{background:#ff6b6b;color:#fff;border:none;width:22px;height:22px;border-radius:50%;cursor:pointer;font-size:12px;font-weight:bold;line-height:1;padding:0}
</style>
</head>
<body>
<div class="hdr">
<h1>MP4 Player</h1>
<div class="tabs">
<button class="tab act" onclick="showTab('player')">Player</button>
<button class="tab" onclick="showTab('files')">Files</button>
<button class="tab" onclick="showSettings()">Info</button>
</div>
</div>
<div class="cnt">
<div id="vPlayer" class="vw act">
<div class="st-bar">
<div class="st-txt" id="stTxt">Loading...</div>
<div class="st-file" id="stFile">-</div>
</div>
<div class="ctrls">
<button class="cb" onclick="api('/api/prev')">&#x23EE;</button>
<button class="cb" onclick="api('/api/stop')">&#x23F9;</button>
<button class="cb pl" onclick="api('/api/play')">&#x25B6;</button>
<button class="cb" onclick="api('/api/next')">&#x23ED;</button>
</div>
<div class="sec-title">Playlist</div>
<div id="playlist"></div>
</div>
<div id="vFiles" class="vw">
<div class="nr">
<button class="nb" id="backBtn" onclick="goBack()">&#x2190; Back</button>
<span class="pt" id="pathTxt">/</span>
<button class="nb nf" onclick="showNF()">+ Folder</button>
<button class="nb up" onclick="toggleUp()">Upload</button>
</div>
<div class="ua" id="upArea">
<p>Drop files here or tap to select</p>
<input type="file" id="fInput" multiple style="display:none">
<button class="ub" onclick="document.getElementById('fInput').click()">Choose Files</button>
<div class="pw" id="pWrap">
<div class="pbg"><div class="pb" id="pBar"><span class="ppct" id="pPct">0%</span></div></div>
<button class="cb-x" id="pCancel">&#x2715;</button>
</div>
<div class="us" id="upSt"></div>
</div>
<div id="fileList"></div>
</div>
</div>
<div class="ov" id="fileDlg" onclick="if(event.target===this)closeDlg('fileDlg')">
<div class="dlg">
<h3 id="fdName"></h3>
<div class="dbs">
<button class="db db-pl" id="fdPlay" onclick="playFromDlg()">&#x25B6; Play</button>
<button class="db db-dl" onclick="dlFile()">Download</button>
<button class="db db-ren" onclick="showRen()">Rename</button>
<button class="db db-del" onclick="delItem()">Delete</button>
<button class="db db-x" onclick="closeDlg('fileDlg')">Cancel</button>
</div>
</div>
</div>
<div class="ov" id="folderDlg" onclick="if(event.target===this)closeDlg('folderDlg')">
<div class="dlg">
<h3 id="foDlgName"></h3>
<div class="dbs">
<button class="db db-ren" onclick="showRen()">Rename</button>
<button class="db db-del" onclick="delItem()">Delete</button>
<button class="db db-x" onclick="closeDlg('folderDlg')">Cancel</button>
</div>
</div>
</div>
<div class="ov" id="renDlg" onclick="if(event.target===this)closeDlg('renDlg')">
<div class="dlg">
<h3>Rename</h3>
<input type="text" class="di" id="renIn">
<div class="dbs">
<button class="db db-sv" onclick="doRename()">Save</button>
<button class="db db-x" onclick="closeDlg('renDlg')">Cancel</button>
</div>
</div>
</div>
<div class="ov" id="nfDlg" onclick="if(event.target===this)closeDlg('nfDlg')">
<div class="dlg">
<h3>New Folder</h3>
<input type="text" class="di" id="nfIn" placeholder="Folder name">
<div class="dbs">
<button class="db db-sv" onclick="doNF()">Create</button>
<button class="db db-x" onclick="closeDlg('nfDlg')">Cancel</button>
</div>
</div>
</div>
<div class="ov" id="setDlg" onclick="if(event.target===this)closeDlg('setDlg')">
<div class="dlg">
<h3>Storage Info</h3>
<div class="sr"><span class="sl">Total</span><span class="sv" id="sdTot">-</span></div>
<div class="sr"><span class="sl">Used</span><span class="sv" id="sdUsd">-</span></div>
<div class="sr"><span class="sl">Free</span><span class="sv" id="sdFre">-</span></div>
<div class="dbs" style="margin-top:16px">
<button class="db db-x" onclick="closeDlg('setDlg')">Close</button>
</div>
</div>
</div>
<div class="ld" id="loading">
<div class="sp"></div>
<div class="lt">Loading...</div>
</div>
<script>
var cp='/',selP='',selN='',selD=false,curXhr=null,upCancelled=false;
function $(id){return document.getElementById(id)}
function fmt(b){
if(b<1024)return b+' B';
if(b<1048576)return(b/1024).toFixed(1)+' KB';
if(b<1073741824)return(b/1048576).toFixed(1)+' MB';
return(b/1073741824).toFixed(2)+' GB';
}
function showTab(t){
var tabs=document.querySelectorAll('.tab');
tabs[0].classList.toggle('act',t==='player');
tabs[1].classList.toggle('act',t==='files');
$('vPlayer').classList.toggle('act',t==='player');
$('vFiles').classList.toggle('act',t==='files');
if(t==='player'){loadSt();loadPL();}
if(t==='files')loadDir(cp);
}
function closeDlg(id){$(id).classList.remove('sh')}
function showSettings(){
$('setDlg').classList.add('sh');
fetch('/api/storage').then(function(r){return r.json()}).then(function(d){
$('sdTot').textContent=fmt(d.total);
$('sdUsd').textContent=fmt(d.used);
$('sdFre').textContent=fmt(d.free);
}).catch(function(){});
}
function api(url){
return fetch(url,{method:'POST'}).then(function(r){return r.json()}).then(function(d){
setTimeout(loadSt,300);return d;
}).catch(function(){});
}
function loadSt(){
fetch('/api/status').then(function(r){return r.json()}).then(function(d){
$('stTxt').textContent=d.playing?'Playing...':'Stopped';
$('stFile').textContent=d.file||'-';
$('stTxt').style.color=d.playing?'#28a745':'#aaa';
var items=document.querySelectorAll('.pl-item');
for(var i=0;i<items.length;i++){
items[i].classList.toggle('cur',items[i].dataset.idx==d.index&&d.playing);
}
}).catch(function(){$('stTxt').textContent='Connection error';$('stTxt').style.color='#e94560'});
}
function loadPL(){
fetch('/api/playlist').then(function(r){return r.json()}).then(function(list){
var h='';
if(list.length===0)h='<div class="em">No MP4 files found on SD card</div>';
for(var i=0;i<list.length;i++){
h+='<div class="pl-item" data-idx="'+i+'" onclick="playIdx('+i+')">';
h+='<span class="nm">'+esc(list[i])+'</span>';
h+='<span class="ic">&#x25B6;</span></div>';
}
$('playlist').innerHTML=h;
loadSt();
}).catch(function(){$('playlist').innerHTML='<div class="em">Failed to load playlist</div>'});
}
function playIdx(i){
fetch('/api/play?index='+i,{method:'POST'}).then(function(r){return r.json()}).then(function(){
setTimeout(loadSt,500);
});
}
function esc(s){
var d=document.createElement('div');d.textContent=s;return d.innerHTML;
}
function loadDir(path){
cp=path;
$('pathTxt').textContent=path;
$('backBtn').style.display=path==='/'?'none':'inline-block';
fetch('/api/files?path='+encodeURIComponent(path)).then(function(r){return r.json()}).then(function(files){
files.sort(function(a,b){
if(a.dir!==b.dir)return a.dir?-1:1;
return a.name.localeCompare(b.name);
});
var h='';
if(files.length===0)h='<div class="em">Empty directory</div>';
for(var i=0;i<files.length;i++){
var f=files[i];
if(f.dir){
h+='<div class="fi dr" onclick="onDir(\''+escAttr(f.name)+'\')" oncontextmenu="event.preventDefault();showFoDlg(\''+escAttr(f.name)+'\')">';
h+='<span class="fic">&#x1F4C1;</span><span class="fn">'+esc(f.name)+'/</span></div>';
}else{
h+='<div class="fi" onclick="showFiDlg(\''+escAttr(f.name)+'\','+f.size+')">';
h+='<span class="fic">&#x1F4C4;</span><span class="fn">'+esc(f.name)+'</span>';
h+='<span class="fs">'+fmt(f.size)+'</span></div>';
}
}
$('fileList').innerHTML=h;
}).catch(function(){$('fileList').innerHTML='<div class="em">Failed to load files</div>'});
}
function escAttr(s){return s.replace(/'/g,"\\'").replace(/"/g,'&quot;')}
function onDir(name){loadDir(cp+(cp.endsWith('/')?'':'/')+name)}
function goBack(){
if(cp==='/') return;
var p=cp.replace(/\/$/,'');
var i=p.lastIndexOf('/');
loadDir(i<=0?'/':p.substring(0,i));
}
function showFiDlg(name,size){
selN=name;selD=false;
selP=cp+(cp.endsWith('/')?'':'/')+name;
$('fdName').textContent=name+' ('+fmt(size)+')';
$('fdPlay').style.display=name.toLowerCase().endsWith('.mp4')?'block':'none';
$('fileDlg').classList.add('sh');
}
function showFoDlg(name){
selN=name;selD=true;
selP=cp+(cp.endsWith('/')?'':'/')+name;
$('foDlgName').textContent=name+'/';
$('folderDlg').classList.add('sh');
}
function playFromDlg(){
closeDlg('fileDlg');
fetch('/api/play?file='+encodeURIComponent(selN),{method:'POST'}).then(function(r){return r.json()}).then(function(d){
if(d.ok){showTab('player');}
else{alert('Cannot play: file not in playlist (only root .mp4 files are playable)');}
});
}
function dlFile(){
closeDlg('fileDlg');
var a=document.createElement('a');
a.href='/api/download?path='+encodeURIComponent(selP);
a.download=selN;a.click();
}
function showRen(){
closeDlg('fileDlg');closeDlg('folderDlg');
$('renIn').value=selN;
$('renDlg').classList.add('sh');
setTimeout(function(){
var inp=$('renIn');inp.focus();
if(!selD){var dot=selN.lastIndexOf('.');if(dot>0)inp.setSelectionRange(0,dot);else inp.select();}
else inp.select();
},100);
}
function doRename(){
var nn=$('renIn').value.trim();
if(!nn||nn===selN){closeDlg('renDlg');return;}
fetch('/api/rename?path='+encodeURIComponent(selP)+'&name='+encodeURIComponent(nn),{method:'POST'})
.then(function(r){return r.json()}).then(function(d){
closeDlg('renDlg');
if(d.ok)loadDir(cp);else alert('Rename failed');
});
}
function delItem(){
var msg=selD?'Delete folder "'+selN+'" and all contents?':'Delete "'+selN+'"?';
if(!confirm(msg))return;
closeDlg('fileDlg');closeDlg('folderDlg');
fetch('/api/delete?path='+encodeURIComponent(selP),{method:'POST'})
.then(function(r){return r.json()}).then(function(d){
if(d.ok)loadDir(cp);else alert('Delete failed');
});
}
function showNF(){
$('nfIn').value='';
$('nfDlg').classList.add('sh');
setTimeout(function(){$('nfIn').focus()},100);
}
function doNF(){
var nm=$('nfIn').value.trim();
if(!nm){closeDlg('nfDlg');return;}
fetch('/api/mkdir?path='+encodeURIComponent(cp)+'&name='+encodeURIComponent(nm),{method:'POST'})
.then(function(r){return r.json()}).then(function(d){
closeDlg('nfDlg');
if(d.ok)loadDir(cp);else alert('Failed to create folder');
});
}
function toggleUp(){
var a=$('upArea');
a.classList.toggle('sh');
}
var upArea=$('upArea');
upArea.addEventListener('dragover',function(e){e.preventDefault();upArea.classList.add('dg')});
upArea.addEventListener('dragleave',function(e){e.preventDefault();upArea.classList.remove('dg')});
upArea.addEventListener('drop',function(e){e.preventDefault();upArea.classList.remove('dg');doUpload(e.dataTransfer.files)});
$('fInput').addEventListener('change',function(e){doUpload(e.target.files);e.target.value=''});
$('pCancel').addEventListener('click',function(){if(curXhr){upCancelled=true;curXhr.abort()}});
function uploadOne(file,path){
return new Promise(function(resolve,reject){
var xhr=new XMLHttpRequest();
curXhr=xhr;
xhr.upload.addEventListener('progress',function(e){
if(e.lengthComputable){
var pct=Math.round(e.loaded/e.total*100);
$('pBar').style.width=pct+'%';
$('pPct').textContent=pct+'%';
}
});
xhr.addEventListener('load',function(){curXhr=null;xhr.status===200?resolve():reject(new Error('HTTP '+xhr.status))});
xhr.addEventListener('error',function(){curXhr=null;reject(new Error('Network error'))});
xhr.addEventListener('abort',function(){curXhr=null;reject(new Error('Cancelled'))});
xhr.open('POST','/api/upload?path='+encodeURIComponent(path)+'&filename='+encodeURIComponent(file.name));
xhr.send(file);
});
}
async function doUpload(files){
var st=$('upSt');
st.textContent='';st.style.color='#28a745';
$('pWrap').classList.remove('sh');
upCancelled=false;
var uploaded=0;
for(var i=0;i<files.length;i++){
if(upCancelled){st.textContent='Upload cancelled';st.style.color='#aaa';break;}
var f=files[i];
st.style.color='#28a745';
st.textContent=f.name+' uploading...';
$('pBar').style.width='0%';$('pPct').textContent='0%';
$('pWrap').classList.add('sh');
try{
await uploadOne(f,cp);
$('pBar').style.width='100%';$('pPct').textContent='100%';
st.textContent=f.name+' uploaded';
uploaded++;
}catch(err){
if(upCancelled){st.textContent='Upload cancelled';st.style.color='#aaa';break;}
st.textContent='Error: '+err.message;st.style.color='#e94560';
}
}
$('pWrap').classList.remove('sh');
if(uploaded>0)setTimeout(function(){loadDir(cp)},500);
}
loadSt();loadPL();
setInterval(loadSt,3000);
</script>
</body>
</html>
)rawliteral";

}  // namespace mp4
