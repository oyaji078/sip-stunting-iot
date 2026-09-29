document.querySelector('.menu-toggle')?.addEventListener('click',()=>document.querySelector('.sidebar')?.classList.toggle('open'));
document.addEventListener('click',e=>{if(e.target.closest('.main')&&!e.target.closest('.menu-toggle'))document.querySelector('.sidebar')?.classList.remove('open')});
const chart=document.querySelector('#categoryChart');
if(chart&&window.Chart){const rows=JSON.parse(chart.dataset.values||'[]');const colors={'Sangat Pendek':'#9f2632','Pendek/Stunting':'#e36d48','Normal':'#238a70','Tinggi':'#4a8eb0'};new Chart(chart,{type:'doughnut',data:{labels:rows.map(x=>x.category),datasets:[{data:rows.map(x=>x.total),backgroundColor:rows.map(x=>colors[x.category]||'#ccc'),borderWidth:0}]},options:{cutout:'70%',plugins:{legend:{position:'bottom',labels:{boxWidth:10,usePointStyle:true}}}}})}
document.querySelector('#voidModal')?.addEventListener('show.bs.modal',e=>{e.target.querySelector('[name=id]').value=e.relatedTarget.dataset.id});
document.querySelector('#rejectModal')?.addEventListener('show.bs.modal',e=>{e.target.querySelector('[name=id]').value=e.relatedTarget.dataset.id});

// Tempel RFID: minta tempel kartu di alat, lalu isi form dari scan terbaru.
(function(){
  const modalEl=document.getElementById('rfidModal');
  if(!modalEl||!window.bootstrap)return;
  const modal=new bootstrap.Modal(modalEl);
  const statusEl=modalEl.querySelector('[data-rfid-status]');
  const iconEl=modalEl.querySelector('[data-rfid-icon]');
  const scanUrl=modalEl.dataset.scanUrl;
  let timer=null,baseId=null,mode=null,target=null;
  const setStatus=(html,cls)=>{statusEl.className='mb-1'+(cls?' '+cls:'');statusEl.innerHTML=html;};
  const stop=()=>{if(timer){clearInterval(timer);timer=null;}iconEl&&iconEl.classList.remove('scanning');};
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
    btn.addEventListener('click',()=>{
      mode=btn.dataset.rfidTrigger;target=btn.dataset.rfidTarget;baseId=null;
      setStatus('Silakan tempelkan kartu RFID pada alat…');
      iconEl&&iconEl.classList.add('scanning');
      modal.show();poll();timer=setInterval(poll,1500);
    });
  });
  modalEl.addEventListener('hidden.bs.modal',stop);
})();

