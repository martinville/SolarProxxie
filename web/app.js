'use strict';
const nav=document.querySelector('#nav');
const panels=[...document.querySelectorAll('[data-page-panel]')];
const validPages=new Set(panels.map(panel=>panel.dataset.pagePanel));

function iconPage(page,push=true){
  if(!validPages.has(page))page='overview';
  document.querySelectorAll('#nav [data-page]').forEach(button=>{
    const active=button.dataset.page===page;
    button.classList.toggle('active',active);
    button.setAttribute('aria-current',active?'page':'false');
  });
  panels.forEach(panel=>{
    const active=panel.dataset.pagePanel===page;
    panel.hidden=!active;
    panel.classList.toggle('active',active);
  });
  if(push)history.pushState({page},'',page==='overview'?'./':`#${page}`);
  document.querySelector('main').focus?.({preventScroll:true});
  scrollTo({top:0,behavior:matchMedia('(prefers-reduced-motion: reduce)').matches?'auto':'smooth'});
}

nav.addEventListener('click',event=>{
  const button=event.target.closest('[data-page]');
  if(button)iconPage(button.dataset.page);
});
document.addEventListener('click',event=>{
  const button=event.target.closest('[data-open-page]');
  if(button)iconPage(button.dataset.openPage);
});
addEventListener('popstate',()=>iconPage(location.hash.slice(1)||'overview',false));

iconPage(location.hash.slice(1)||'overview',false);
