document.querySelector('.menu-toggle')?.addEventListener('click',()=>document.querySelector('.sidebar')?.classList.toggle('open'));
document.addEventListener('click',e=>{if(e.target.closest('.main')&&!e.target.closest('.menu-toggle'))document.querySelector('.sidebar')?.classList.remove('open')});
const chart=document.querySelector('#categoryChart');
if(chart&&window.Chart){const rows=JSON.parse(chart.dataset.values||'[]');const colors={'Sangat Pendek':'#9f2632','Pendek/Stunting':'#e36d48','Normal':'#238a70','Tinggi':'#4a8eb0'};new Chart(chart,{type:'doughnut',data:{labels:rows.map(x=>x.category),datasets:[{data:rows.map(x=>x.total),backgroundColor:rows.map(x=>colors[x.category]||'#ccc'),borderWidth:0}]},options:{cutout:'70%',plugins:{legend:{position:'bottom',labels:{boxWidth:10,usePointStyle:true}}}}})}
document.querySelector('#voidModal')?.addEventListener('show.bs.modal',e=>{e.target.querySelector('[name=id]').value=e.relatedTarget.dataset.id});
document.querySelector('#deleteChildModal')?.addEventListener('show.bs.modal',e=>{
  const m=e.target,d=e.relatedTarget.dataset,n=parseInt(d.count||'0',10);
  m.querySelector('[name=id]').value=d.id;
  m.querySelector('[data-delete-name]').textContent=d.name;
  const box=m.querySelector('[data-delete-history]');
  box.classList.toggle('d-none',n<1);
  m.querySelector('[data-delete-empty]').classList.toggle('d-none',n>0);
  m.querySelector('[data-delete-count]').textContent=n;
  m.querySelector('[data-delete-submit]').textContent=n>0?'Nonaktifkan':'Ya, hapus';
});
document.querySelector('#rejectModal')?.addEventListener('show.bs.modal',e=>{e.target.querySelector('[name=id]').value=e.relatedTarget.dataset.id});
document.querySelectorAll('[data-auto-show="1"]').forEach(el=>bootstrap.Modal.getOrCreateInstance(el).show());

// Monitor alat IoT: sinkronkan status perangkat dan sensor tanpa reload.
(function(){
  const root=document.querySelector('[data-device-live]');
  if(!root)return;
  const url=root.dataset.statusUrl;
  const sensorText=value=>value===true?'Normal':value===false?'Gangguan':'Belum ada data';
  const sensorClass=value=>value===true?'ok':value===false?'error':'unknown';
  async function refreshDevices(){
    try{
      const response=await fetch(url,{headers:{Accept:'application/json'},cache:'no-store'});
      if(!response.ok)throw new Error('HTTP '+response.status);
      const payload=await response.json();
      (payload.devices||[]).forEach(device=>{
        const card=root.querySelector('[data-device-id="'+device.id+'"]');
        if(!card)return;
        const status=card.querySelector('[data-device-status]');
        status.textContent=device.status;
        status.className='status status-'+device.status_color;
        card.querySelector('[data-device-firmware]').textContent=device.firmware;
        card.querySelector('[data-device-rssi]').textContent=device.rssi;
        card.querySelector('[data-device-last]').textContent=device.last_seen;
        card.querySelector('[data-device-uptime]').textContent=device.uptime;
        Object.entries(device.sensors||{}).forEach(([key,value])=>{
          if(key==='uptime_seconds')return;
          const item=card.querySelector('[data-sensor="'+key+'"] .sensor-state');
          if(!item)return;
          item.textContent=sensorText(value);
          item.className='sensor-state sensor-'+sensorClass(value);
        });
      });
      root.classList.remove('live-error');
    }catch(error){root.classList.add('live-error');}
  }
  refreshDevices();
  setInterval(refreshDevices,5000);
})();

// Grafik perkembangan dipisahkan per balita.
(function(){
  if(!window.Chart)return;
  const growth=document.querySelector('#growthChart');
  if(growth){
    const rows=JSON.parse(growth.dataset.values||'[]');
    new Chart(growth,{type:'line',data:{labels:rows.map(x=>x.date),datasets:[
      {label:'Berat (kg)',data:rows.map(x=>x.weight),borderColor:'#176b5b',backgroundColor:'#176b5b22',yAxisID:'weight',tension:.28,pointRadius:4},
      {label:'Tinggi (cm)',data:rows.map(x=>x.height),borderColor:'#4a8eb0',backgroundColor:'#4a8eb022',yAxisID:'height',tension:.28,pointRadius:4}
    ]},options:{maintainAspectRatio:false,interaction:{mode:'index',intersect:false},scales:{weight:{position:'left',title:{display:true,text:'kg'}},height:{position:'right',grid:{drawOnChartArea:false},title:{display:true,text:'cm'}}}}});
  }
  const zchart=document.querySelector('#zscoreChart');
  if(zchart){
    const rows=JSON.parse(zchart.dataset.values||'[]'),line=value=>rows.map(()=>value);
    new Chart(zchart,{type:'line',data:{labels:rows.map(x=>x.date),datasets:[
      {label:'Z-score',data:rows.map(x=>x.z),borderColor:'#e36d48',backgroundColor:'#e36d4820',fill:true,tension:.28,pointRadius:4},
      {label:'Batas -2 SD',data:line(-2),borderColor:'#e79030',borderDash:[6,5],pointRadius:0,borderWidth:1.5},
      {label:'Batas -3 SD',data:line(-3),borderColor:'#a23f45',borderDash:[6,5],pointRadius:0,borderWidth:1.5}
    ]},options:{maintainAspectRatio:false,interaction:{mode:'index',intersect:false},scales:{y:{suggestedMin:-4,suggestedMax:4,title:{display:true,text:'Standar deviasi (SD)'}}}}});
  }
})();

// Sinkronisasi data antartab/perangkat. Reload ditunda saat operator sedang
// mengetik atau modal terbuka agar input tidak hilang.
(function(){
  const url=document.body.dataset.liveUrl;
  if(!url)return;
  let version=null,pending=false;
  const busy=()=>document.querySelector('.modal.show')||['INPUT','TEXTAREA','SELECT'].includes(document.activeElement?.tagName);
  async function poll(){
    try{
      const response=await fetch(url,{headers:{Accept:'application/json'},cache:'no-store'});
      if(!response.ok)return;
      const next=(await response.json()).version;
      if(version===null)version=next;
      else if(next&&next!==version){version=next;pending=true;}
      if(pending&&!busy())location.reload();
    }catch(error){/* coba lagi pada interval berikutnya */}
  }
  poll();
  setInterval(poll,3000);
})();

// Tempel RFID: minta tempel kartu di alat, lalu isi form dari scan terbaru.
(function(){
  const modalEl=document.getElementById('rfidModal');
  if(!modalEl||!window.bootstrap)return;
  const modal=new bootstrap.Modal(modalEl);
  const statusEl=modalEl.querySelector('[data-rfid-status]');
  const iconEl=modalEl.querySelector('[data-rfid-icon]');
  const scanUrl=modalEl.dataset.scanUrl;
  const modeUrl=modalEl.dataset.modeUrl;
  const csrf=modalEl.dataset.csrf;
  let timer=null,baseId=null,mode=null,target=null,captureActive=false,returnModalEl=null;
  const setStatus=(html,cls)=>{statusEl.className='mb-1'+(cls?' '+cls:'');statusEl.innerHTML=html;};
  // Beri tahu alat: minta tempel kartu (register/select) atau batalkan (off).
  async function setDeviceMode(m){
    try{
      const body=new URLSearchParams({csrf:csrf,mode:m});
      const r=await fetch(modeUrl,{method:'POST',body:body,headers:{'Accept':'application/json'}});
      return await r.json();
    }catch(e){return null;}
  }
  const stop=()=>{
    if(timer){clearInterval(timer);timer=null;}
    iconEl&&iconEl.classList.remove('scanning');
    if(captureActive){captureActive=false;setDeviceMode('off');}
  };
  async function poll(){
    try{
      const d=await(await fetch(scanUrl,{headers:{'Accept':'application/json'}})).json();
      if(baseId===null){baseId=d.id||0;return;}
      if(d.id&&d.id>baseId&&d.age<=30){stop();handle(d);}
    }catch(e){/* jaringan: coba lagi tick berikutnya */}
  }
  function handle(d){
    const uid=(d.uid||'').toString();
    if(mode==='balita'){
      const input=document.querySelector(target);
      if(input)input.value=uid;
      if(d.child_id)setStatus('Kartu <code>'+uid+'</code> sudah dipakai <b>'+(d.child_name||'balita lain')+'</b>. Periksa kembali.','text-warning');
      else setStatus('UID <code>'+uid+'</code> terisi. Lengkapi data lalu simpan.','text-success');
      setTimeout(()=>modal.hide(),1400);
    }else if(mode==='pemeriksaan'){
      if(d.child_id&&d.child_active){
        const sel=document.querySelector(target);
        if(sel)sel.value=String(d.child_id);
        setStatus('<b>'+(d.child_name||'Balita')+'</b> dipilih.','text-success');
        setTimeout(()=>modal.hide(),1200);
      }else{
        setStatus('UID <code>'+uid+'</code> belum terdaftar/aktif. Daftarkan dulu di menu Data Balita.','text-danger');
      }
    }
  }
  document.querySelectorAll('[data-rfid-trigger]').forEach(btn=>{
    btn.addEventListener('click',async()=>{
      mode=btn.dataset.rfidTrigger;target=btn.dataset.rfidTarget;baseId=null;
      setStatus('Menghubungi alat…');
      iconEl&&iconEl.classList.add('scanning');
      const parentEl=btn.closest('.modal.show');
      returnModalEl=parentEl||null;
      if(parentEl){
        await new Promise(resolve=>{
          parentEl.addEventListener('hidden.bs.modal',resolve,{once:true});
          bootstrap.Modal.getOrCreateInstance(parentEl).hide();
        });
      }
      modal.show();
      await poll(); // ambil titik awal scan sebelum meminta kartu
      const res=await setDeviceMode(mode==='balita'?'register':'select');
      captureActive=!!(res&&res.ok);
      if(!res||!res.ok){setStatus('Gagal meminta alat. Muat ulang halaman lalu coba lagi.','text-danger');iconEl&&iconEl.classList.remove('scanning');return;}
      if(res.devices_online<1)setStatus('Tidak ada alat online saat ini. Pastikan alat menyala dan terhubung internet, lalu tempelkan kartu.','text-warning');
      else setStatus('Alat meminta kartu. Tempelkan kartu RFID pada alat (60 detik)…');
      timer=setInterval(poll,700);
    });
  });
  modalEl.addEventListener('hidden.bs.modal',()=>{
    stop();
    if(returnModalEl){
      const parentEl=returnModalEl;
      returnModalEl=null;
      bootstrap.Modal.getOrCreateInstance(parentEl).show();
    }
  });
})();

