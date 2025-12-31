#ifndef HTML1_H
#define HTML1_H

// --- HTML CONTENT ---
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESPDSP Control</title>
  <style>
    body { font-family: Arial, sans-serif; background-color: #ffffff; color: #000000; margin: 0; padding: 20px; }
    h1 { border-bottom: 2px solid #000; padding-bottom: 10px; }
    h2 { margin-top: 30px; background: #eee; padding: 5px; }
    .section { margin-bottom: 20px; padding: 10px; border: 1px solid #ddd; }
    .row { display: flex; align-items: center; margin-bottom: 10px; flex-wrap: wrap; }
    label { margin-right: 10px; font-weight: bold; min-width: 100px; }
    input[type=text], input[type=number] { padding: 5px; margin-right: 10px; border: 1px solid #000; }
    button { background: #000; color: #fff; padding: 8px 15px; border: none; cursor: pointer; }
    button:hover { background: #444; }
    .slider-container { width: 100%; display: flex; align-items: center; margin: 5px 0; }
    input[type=range] { flex-grow: 1; margin: 0 10px; }
    .switch { position: relative; display: inline-block; width: 40px; height: 20px; margin-right: 10px; }
    .switch input { opacity: 0; width: 0; height: 0; }
    .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; transition: .4s; }
    .slider:before { position: absolute; content: ""; height: 16px; width: 16px; left: 2px; bottom: 2px; background-color: white; transition: .4s; }
    input:checked + .slider { background-color: #000; }
    input:checked + .slider:before { transform: translateX(20px); }
    .eq-band { display: flex; flex-direction: column; align-items: center; width: 9%; }
    .eq-container { display: flex; justify-content: space-between; }
  </style>
</head>
<body>
  <h1>ESPDSP Interface</h1>
  <div class="section">
    <h2>1. Configuration</h2>
    <div class="row">
      <label>BT Name:</label>
      <input type="text" id="btName" value="ESPDSP">
      <button onclick="saveConfig('bt')">Apply</button>
    </div>
    <div class="row">
      <label>WiFi SSID:</label>
      <input type="text" id="ssid" value="ESPDSP">
    </div>
    <div class="row">
      <label>WiFi Pass:</label>
      <input type="password" id="pass" value="ESPDSP">
      <button onclick="saveConfig('wifi')">Apply</button>
    </div>
  </div>

  <div class="section">
    <h2>2. DSPout</h2>
    <div class="row">
      <label>Preset:</label>
      <select id="presetSelect">
        <option value="0">User 1</option>
        <option value="1">User 2</option>
        <option value="2">User 3</option>
        <option value="3">User 4</option>
        <option value="4">User 5</option>
      </select>
      <button onclick="loadPreset()">Choose</button>
      <button onclick="savePreset()">Save Current</button>
    </div>
    <div class="row">
      <label class="switch"><input type="checkbox" id="stereoExp"><span class="slider"></span></label> Stereo Exp
      <label class="switch" style="margin-left:20px"><input type="checkbox" id="subsonic"><span class="slider"></span></label> Subsonic
      <label class="switch" style="margin-left:20px"><input type="checkbox" id="eqEnable"><span class="slider"></span></label> Enable EQ
      <button style="margin-left:auto" onclick="applyDSP()">Apply & Save</button>
    </div>
    <div class="slider-container">
      <label>Gain:</label>
      <input type="range" id="mainGain" min="0" max="200" value="100">
      <span id="gainVal">100%</span>
    </div>
    <h3>10-Band Equalizer</h3>
    <div class="eq-container">
       <div id="eqBands"></div>
    </div>
    <br>
    <button onclick="applyDSP()">Apply & Save EQ</button>
  </div>

  <div class="section">
    <h2>3. Signal Generator</h2>
    <div class="row">
      <label class="switch"><input type="checkbox" id="genActive"><span class="slider"></span></label> Gen Mode
      <select id="sigType" style="margin-left: 10px;">
        <option value="0">Sine</option>
        <option value="1">White Noise</option>
        <option value="2">Pink Noise</option>
        <option value="3">Sweep</option>
      </select>
      <button onclick="applyGen()">Apply</button>
    </div>
    <div class="row">
      <label>F-Start (Hz):</label><input type="number" id="fStart" value="440">
      <label>F-End (Hz):</label><input type="number" id="fEnd" value="440">
    </div>
    <div class="row">
      <label>Period (s):</label><input type="number" id="period" value="10">
      <button onclick="applyGen()">Apply Params</button>
    </div>
  </div>

<script>
  const freqs = [32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000];
  let html = '<div style="display:flex; justify-content:space-between;">';
  freqs.forEach((f, i) => {
    html += `<div class="eq-band"><input type="range" orient="vertical" id="eq${i}" min="-12" max="12" value="0" style="-webkit-appearance: slider-vertical; height: 100px;"><span>${f}</span></div>`;
  });
  html += '</div>';
  document.getElementById('eqBands').innerHTML = html;

  document.getElementById('mainGain').oninput = function() { document.getElementById('gainVal').innerText = this.value + '%'; }

  function sendData(url, data) {
    fetch(url, { method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(data) })
    .then(res => console.log('Sent', data));
  }

  function saveConfig(type) {
    const data = { type: type };
    if(type === 'bt') data.name = document.getElementById('btName').value;
    if(type === 'wifi') { data.ssid = document.getElementById('ssid').value; data.pass = document.getElementById('pass').value; }
    sendData('/api/config', data);
  }

  function applyDSP() {
    const eqVals = [];
    for(let i=0; i<10; i++) eqVals.push(document.getElementById('eq'+i).value);
    const data = {
      stereo: document.getElementById('stereoExp').checked,
      subsonic: document.getElementById('subsonic').checked,
      eqEnable: document.getElementById('eqEnable').checked,
      gain: document.getElementById('mainGain').value,
      eq: eqVals
    };
    sendData('/api/dsp', data);
  }

  function applyGen() {
    const data = {
      active: document.getElementById('genActive').checked,
      type: document.getElementById('sigType').value,
      fStart: document.getElementById('fStart').value,
      fEnd: document.getElementById('fEnd').value,
      period: document.getElementById('period').value
    };
    sendData('/api/gen', data);
  }

function loadPreset() {
      let idx = document.getElementById('presetSelect').value;
      fetch('/api/preset?id='+idx)
      .then(res => {
          if(!res.ok) throw new Error("Empty");
          return res.json();
      })
      .then(data => {
          // Update Switches
          document.getElementById('stereoExp').checked = data.stereo;
          document.getElementById('subsonic').checked = data.subsonic;
          
          // [FIX] Update Sliders for Gain and EQ
          // Note: You must ensure your save logic included "eq" array in the JSON!
          if(data.gain) {
             document.getElementById('mainGain').value = data.gain;
             document.getElementById('gainVal').innerText = data.gain + '%';
          }
          
          if(data.eq && Array.isArray(data.eq)) {
              for(let i=0; i<10; i++) {
                  let el = document.getElementById('eq'+i);
                  if(el) el.value = data.eq[i];
              }
          }
      })
      .catch(e => alert("Preset empty or load error"));
  }

  function savePreset() {
      let idx = document.getElementById('presetSelect').value;

      // 1. Collect all current values manually
      const eqVals = [];
      for(let i=0; i<10; i++) eqVals.push(document.getElementById('eq'+i).value);

      const currentData = {
          id: idx, // The preset ID to save to
          stereo: document.getElementById('stereoExp').checked,
          subsonic: document.getElementById('subsonic').checked,
          eqEnable: document.getElementById('eqEnable').checked,
          gain: document.getElementById('mainGain').value,
          eq: eqVals
      };

      // 2. Send EVERYTHING to the save endpoint.
      // This ensures what you see on screen is exactly what gets saved.
      // (Note: You already updated web_server.h to handle this rich JSON data!)
      sendData('/api/savePreset', currentData);
  }
</script>
</body>
</html>
)rawliteral";

#endif
