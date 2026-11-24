// The typing page a TV's QR code opens: GET /chat/<code>. Plain HTML, no build step, no sign-in: the party code in
// the address is the only credential (see webChat in logic.js). `code` has been checked as [A-Z0-9]{6}.
export const chatPage = (code) => `<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="robots" content="noindex">
<title>Watch party chat</title>
<style>
 body{margin:0;font:18px/1.4 system-ui,sans-serif;background:#16141d;color:#eee;display:flex;justify-content:center}
 main{width:100%;max-width:460px;padding:24px 18px}
 h1{font-size:24px;margin:0 0 4px} p{margin:0 0 18px;color:#aaa}
 label{display:block;margin:14px 0 6px;font-size:14px;color:#aaa}
 input{width:100%;box-sizing:border-box;font:inherit;padding:14px;border-radius:12px;border:1px solid #444;background:#24212d;color:#fff}
 button{width:100%;margin-top:18px;padding:15px;font:inherit;font-weight:700;border:0;border-radius:12px;background:#7b61ff;color:#fff}
 button:disabled{opacity:.5} #msg{margin-top:14px;min-height:1.4em;font-size:16px} .ok{color:#7ddc8a} .bad{color:#ff8a80}
</style></head><body><main>
<h1>Watch party chat</h1>
<p>Party ${code}. Your message shows in the chat on the TV and on every device in the party.</p>
<form id="f" autocomplete="off">
 <label for="t">Message</label>
 <input id="t" maxlength="300" placeholder="Type a message" autofocus>
 <label for="n">Your name (optional)</label>
 <input id="n" maxlength="40" placeholder="Phone">
 <button id="b" type="submit">Send</button>
</form>
<div id="msg" role="status"></div>
<script>
 var code=${JSON.stringify(code)},f=document.getElementById("f"),t=document.getElementById("t"),n=document.getElementById("n"),
  b=document.getElementById("b"),m=document.getElementById("msg");
 try{n.value=localStorage.getItem("chatName")||""}catch(e){}
 function say(s,ok){m.textContent=s;m.className=ok?"ok":"bad"}
 f.addEventListener("submit",function(e){
  e.preventDefault();
  var text=t.value.trim();
  if(!text){say("Type a message first.",false);return}
  var name=n.value.trim()||"Phone";
  try{localStorage.setItem("chatName",n.value.trim())}catch(e){}
  b.disabled=true;
  fetch("/chat/"+code,{method:"POST",headers:{"content-type":"application/json"},body:JSON.stringify({text:text,name:name})})
   .then(function(r){return r.json().catch(function(){return{}}).then(function(j){return{s:r.status,j:j}})})
   .then(function(x){
    if(x.s===200){say("Sent.",true);t.value="";t.focus()}
    else if(x.s===404)say("This watch party has ended.",false);
    else if(x.s===429)say("A little slower, try again.",false);
    else say("Could not send ("+x.s+").",false);
   })
   .catch(function(){say("No connection. Try again.",false)})
   .then(function(){b.disabled=false});
 });
</script></main></body></html>`;
