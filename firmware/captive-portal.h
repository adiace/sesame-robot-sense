#pragma once

#include <Arduino.h>

// ======================================================================
// --- WEB INTERFACE HTML ---
// ======================================================================

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Robot AP Controller</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { 
      font-family: 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; 
      text-align: center; 
      background-color: #121212; 
      color: #e0e0e0; 
      touch-action: manipulation; 
      margin: 0;
      padding: 20px;
    }
    h2 { 
      margin-top: 10px;
      margin-bottom: 20px; 
      color: #ffffff;
      font-size: 28px;
    }
    .grid { 
      display: grid; 
      grid-template-columns: repeat(2, 1fr); 
      gap: 15px; 
      max-width: 400px; 
      margin: 0 auto; 
      padding: 10px; 
    }
    .dpad-container { 
      display: flex; 
      justify-content: center; 
      margin: 20px 0; 
    }
    .dpad { 
      display: grid; 
      grid-template-columns: repeat(3, 90px); 
      grid-template-rows: repeat(2, 90px); 
      gap: 12px; 
    }
    .dpad button { 
      font-size: 35px; 
      background-color: #333333; 
      border: 2px solid #555555; 
      box-shadow: 0 4px #111111;
    }
    button { 
      background-color: #333333; 
      border: none; 
      color: #e0e0e0; 
      padding: 15px; 
      font-size: 18px; 
      border-radius: 12px; 
      cursor: pointer; 
      user-select: none; 
      box-shadow: 0 4px #111111;
      transition: all 0.1s;
    }
    button:active { 
      box-shadow: 0 2px #111111;
      transform: translateY(2px); 
    }
    .btn-pose { 
      background-color: #2c3e50; 
      box-shadow: 0 4px #1a252f; 
    }
    .btn-pose:active { 
      background-color: #34495e; 
      box-shadow: 0 2px #1a252f; 
    }
    .btn-stop-all { 
      background-color: #d00000; 
      width: 95%; 
      max-width: 400px; 
      margin: 30px auto; 
      display: block; 
      font-weight: bold; 
      font-size: 24px; 
      padding: 20px; 
      border-radius: 15px; 
      box-shadow: 0 6px #800000;
      border: 2px solid #ff4d4d;
      color: white;
    }
    .btn-stop-all:active { 
      box-shadow: 0 2px #800000; 
      transform: translateY(4px); 
    }
    .btn-settings { 
      background-color: #444444; 
      box-shadow: 0 4px #222222; 
      margin-top: 10px;
      padding: 10px 25px;
    }
    .spacer { visibility: hidden; }
    .settings-panel { 
      display: none; 
      position: fixed; 
      top: 0; 
      left: 0; 
      width: 100%; 
      height: 100%; 
      background: rgba(0,0,0,0.85); 
      z-index: 100; 
      backdrop-filter: blur(5px); 
    }
    .settings-content { 
      background: #1e1e1e; 
      border: 1px solid #444444; 
      max-width: 300px; 
      margin: 50px auto; 
      padding: 25px; 
      border-radius: 20px; 
      text-align: left; 
      box-shadow: 0 10px 30px rgba(0,0,0,0.5); 
    }
    .settings-content h3 { color: #ffffff; margin-top: 0; }
    .settings-content label { display: block; margin-top: 15px; font-weight: bold; color: #cccccc; }
    .settings-content input { 
      width: 100%; 
      padding: 10px; 
      margin-top: 5px; 
      background: #333333; 
      color: #fff; 
      border: 1px solid #555555; 
      border-radius: 8px; 
      box-sizing: border-box;
    }
    .btn-save { background-color: #27ae60; box-shadow: 0 4px #1e8449; width: 100%; margin-top: 25px; color: white; }
    .btn-close { background-color: #c0392b; box-shadow: 0 4px #922b21; width: 100%; margin-top: 12px; color: white; }
  </style>
</head>
<body>
  <h2>Sesame Controller</h2>
  
  <div class="dpad-container">
    <div class="dpad">
      <div class="spacer"></div>
      <button onmousedown="move('forward')" onmouseup="stop()" ontouchstart="move('forward')" ontouchend="stop()">&#9650;</button>
      <div class="spacer"></div>
      
      <button onmousedown="move('left')" onmouseup="stop()" ontouchstart="move('left')" ontouchend="stop()">&#9664;</button>
      <button onmousedown="move('backward')" onmouseup="stop()" ontouchstart="move('backward')" ontouchend="stop()">&#9660;</button>
      <button onmousedown="move('right')" onmouseup="stop()" ontouchstart="move('right')" ontouchend="stop()">&#9654;</button>
    </div>
  </div>

  <div class="grid">
    <button class="btn-pose" onclick="pose('rest')">Rest</button>
    <button class="btn-pose" onclick="pose('stand')">Stand</button>
    <button class="btn-pose" onclick="pose('wave')">Wave</button>
    <button class="btn-pose" onclick="pose('dance')">Dance</button>
    <button class="btn-pose" onclick="pose('swim')">Swim</button>
    <button class="btn-pose" onclick="pose('point')">Point</button>
    <button class="btn-pose" onclick="pose('pushup')">Pushup</button>
    <button class="btn-pose" onclick="pose('bow')">Bow</button>
    <button class="btn-pose" onclick="pose('cute')">Cute</button>
    <button class="btn-pose" onclick="pose('freaky')">Freaky</button>
    <button class="btn-pose" onclick="pose('worm')">Worm</button>
    <button class="btn-pose" onclick="pose('shake')">Shake</button>
    <button class="btn-pose" onclick="pose('shrug')">Shrug</button>
    <button class="btn-pose" onclick="pose('dead')">Dead</button>
    <button class="btn-pose" onclick="pose('crab')">Crab</button>
  </div>

  <button class="btn-stop-all" onclick="stop()">STOP ALL</button>
  <button class="btn-settings" onclick="openSettings()">Settings</button>

  <div id="settingsPanel" class="settings-panel">
    <div class="settings-content">
      <h3>Settings</h3>
      <label>Frame Delay (ms):</label>
      <input type="number" id="frameDelay">
      <label>Walk Cycles:</label>
      <input type="number" id="walkCycles">
      <label>Motor Current Delay (ms):</label>
      <input type="number" id="motorCurrentDelay">
      <label>Face FPS:</label>
      <input type="number" id="faceFps">
      <button class="btn-save" onclick="saveSettings()">Save Settings</button>
      <button class="btn-close" onclick="closeSettings()">Close</button>
    </div>
  </div>

<script>
function move(dir) { fetch('/cmd?go=' + dir).catch(console.log); }
function stop() { fetch('/cmd?stop=1').catch(console.log); }
function pose(name) { fetch('/cmd?pose=' + name).catch(console.log); }
function openSettings() {
  fetch('/getSettings').then(r => r.json()).then(data => {
    document.getElementById('frameDelay').value = data.frameDelay;
    document.getElementById('walkCycles').value = data.walkCycles;
    document.getElementById('motorCurrentDelay').value = data.motorCurrentDelay;
    document.getElementById('faceFps').value = data.faceFps;
    document.getElementById('settingsPanel').style.display = 'block';
  });
}
function closeSettings() { document.getElementById('settingsPanel').style.display = 'none'; }
function saveSettings() {
  const fd = document.getElementById('frameDelay').value;
  const wc = document.getElementById('walkCycles').value;
  const mcd = document.getElementById('motorCurrentDelay').value;
  const fps = document.getElementById('faceFps').value;
  fetch(`/setSettings?frameDelay=${fd}&walkCycles=${wc}&motorCurrentDelay=${mcd}&faceFps=${fps}`).then(() => closeSettings());
}
</script>
</body>
</html>
)rawliteral";
