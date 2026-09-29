document.querySelector('.menu-toggle')?.addEventListener('click',()=>document.querySelector('.sidebar')?.classList.toggle('open'));
document.addEventListener('click',e=>{if(e.target.closest('.main')&&!e.target.closest('.menu-toggle'))document.querySelector('.sidebar')?.classList.remove('open')});
const chart=document.querySelector('#categoryChart');
if(chart&&window.Chart){const rows=JSON.parse(chart.dataset.values||'[]');const colors={'Sangat Pendek':'#9f2632','Pendek/Stunting':'#e36d48','Normal':'#238a70','Tinggi':'#4a8eb0'};new Chart(chart,{type:'doughnut',data:{labels:rows.map(x=>x.category),datasets:[{data:rows.map(x=>x.total),backgroundColor:rows.map(x=>colors[x.category]||'#ccc'),borderWidth:0}]},options:{cutout:'70%',plugins:{legend:{position:'bottom',labels:{boxWidth:10,usePointStyle:true}}}}})}
document.querySelector('#voidModal')?.addEventListener('show.bs.modal',e=>{e.target.querySelector('[name=id]').value=e.relatedTarget.dataset.id});

