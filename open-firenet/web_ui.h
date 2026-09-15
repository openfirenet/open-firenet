#pragma once
#include <pgmspace.h>

// Interface Web moderne Open-Firenet pour ESP32-S3
// 100% autonome, sans dépendance externe / CDN (fonctionne hors-ligne)
// Support Multi-Langues natif (Français / Anglais) avec mémorisation locale
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="fr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title id="pageTitle">Open-Firenet — Tableau de Bord</title>
<link rel="icon" type="image/svg+xml" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'%3E%3Cpath d='M48 10A40 40 0 1 0 52 90M52 10A40 40 0 0 1 48 90' fill='none' stroke='%23f97316' stroke-width='7' stroke-linecap='round'/%3E%3Cpath d='M50 24L59 40L53 43L64 58C64 70 58 76 50 76C42 76 36 70 36 58L47 43L41 40Z' fill='%23f97316'/%3E%3Cpath d='M50 52C53 52 56 56 56 61C56 65 53 68 50 68C47 68 44 65 44 61C44 56 47 52 50 52Z' fill='%230c0f17'/%3E%3C/svg%3E">
<style>
:root {
  --bg: #0c0f17;
  --card: #161b26;
  --border: #232a3b;
  --text: #f1f5f9;
  --text-dim: #94a3b8;
  --text-muted: #64748b;
  --primary: #f97316;
  --primary-glow: rgba(249,115,22,0.35);
  --green: #10b981;
  --green-glow: rgba(16,185,129,0.3);
  --amber: #f59e0b;
  --blue: #38bdf8;
  --radius: 14px;
}
* { box-sizing: border-box; margin: 0; padding: 0; }
body {
  font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
  background: var(--bg);
  color: var(--text);
  min-height: 100vh;
  padding-bottom: 40px;
}
header {
  background: rgba(22, 27, 38, 0.85);
  backdrop-filter: blur(12px);
  border-bottom: 1px solid var(--border);
  position: sticky;
  top: 0;
  z-index: 100;
  padding: 12px 20px;
  display: flex;
  align-items: center;
  justify-content: space-between;
}
.brand {
  display: flex;
  align-items: center;
  gap: 10px;
  font-weight: 700;
  font-size: 1.15rem;
}
.brand svg { width: 24px; height: 24px; fill: var(--primary); filter: drop-shadow(0 0 6px var(--primary)); }
.badge {
  font-size: 0.75rem;
  padding: 3px 8px;
  border-radius: 20px;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}
.badge-sub { background: #202738; color: var(--text-dim); }
.badge-live { background: rgba(16,185,129,0.15); color: var(--green); border: 1px solid rgba(16,185,129,0.3); display: flex; align-items: center; gap: 5px; }
.dot { width: 6px; height: 6px; border-radius: 50%; background: currentColor; animation: pulse 2s infinite; }
@keyframes pulse { 0%, 100% { opacity: 1; transform: scale(1); } 50% { opacity: 0.4; transform: scale(0.85); } }

.header-actions { display: flex; gap: 10px; align-items: center; }
.lang-select {
  background: #1e2638;
  color: var(--text);
  border: 1px solid var(--border);
  padding: 6px 28px 6px 10px;
  border-radius: 8px;
  font-weight: 600;
  font-size: 0.85rem;
  cursor: pointer;
  outline: none;
  transition: all 0.2s ease;
  appearance: none;
  -webkit-appearance: none;
  background-image: url("data:image/svg+xml;charset=UTF-8,%3csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='%2394a3b8'%3e%3cpath d='M7 10l5 5 5-5z'/%3e%3c/svg%3e");
  background-repeat: no-repeat;
  background-position: right 6px center;
  background-size: 16px;
}
.lang-select:hover, .lang-select:focus {
  background-color: #2b364e;
  border-color: #4b5878;
}
.lang-select option {
  background: #161b26;
  color: #f1f5f9;
  font-size: 0.9rem;
}
.btn-lock {
  background: transparent;
  color: var(--text-dim);
  border: 1px solid var(--border);
  padding: 6px 12px;
  border-radius: 8px;
  font-weight: 600;
  font-size: 0.8rem;
  cursor: pointer;
  transition: all 0.2s;
  display: flex;
  align-items: center;
  gap: 6px;
}
.btn-lock.armed {
  background: rgba(229,57,53,0.15);
  border-color: var(--primary);
  color: #ff6b6b;
}

main { max-width: 960px; margin: 0 auto; padding: 20px; display: flex; flex-direction: column; gap: 20px; }

/* Hero Card */
.hero-card {
  background: linear-gradient(135deg, #181e2b 0%, #131722 100%);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  padding: 24px;
  display: flex;
  justify-content: space-between;
  align-items: center;
  flex-wrap: wrap;
  gap: 20px;
  box-shadow: 0 8px 24px rgba(0,0,0,0.4);
}
.hero-state { display: flex; align-items: center; gap: 18px; }
.state-icon-box {
  width: 60px;
  height: 60px;
  border-radius: 16px;
  background: #1c2333;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 28px;
  border: 1px solid var(--border);
  position: relative;
}
.state-icon-box.active {
  box-shadow: 0 0 20px var(--primary-glow);
  border-color: rgba(229,57,53,0.5);
}
.hero-labels h2 { font-size: 1.4rem; font-weight: 700; margin-bottom: 4px; }
.hero-labels p { color: var(--text-dim); font-size: 0.9rem; }

.power-btn {
  background: var(--primary);
  color: white;
  border: none;
  padding: 12px 24px;
  border-radius: 10px;
  font-weight: 700;
  font-size: 1rem;
  cursor: pointer;
  box-shadow: 0 4px 14px var(--primary-glow);
  transition: all 0.2s;
  display: flex;
  align-items: center;
  gap: 8px;
}
.power-btn:hover { filter: brightness(1.1); transform: translateY(-1px); }
.power-btn:active { transform: translateY(0); }
.power-btn.btn-off {
  background: #22293a;
  color: var(--text-dim);
  box-shadow: none;
  border: 1px solid var(--border);
}
.power-btn.btn-off:hover { color: var(--text); border-color: #3b455c; }

/* Grid metrics */
.grid-metrics {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 16px;
}
.card {
  background: var(--card);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  padding: 18px;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
  gap: 12px;
}
.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  color: var(--text-dim);
  font-size: 0.85rem;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}
.card-val { font-size: 2.2rem; font-weight: 800; display: flex; align-items: baseline; gap: 4px; }
.card-val .unit { font-size: 1.1rem; color: var(--text-dim); font-weight: 500; }
.card-footer {
  font-size: 0.85rem;
  color: var(--text-muted);
  border-top: 1px solid rgba(255,255,255,0.05);
  padding-top: 10px;
  display: flex;
  justify-content: space-between;
  align-items: center;
}
.card-footer b { color: var(--text); }

.stepper { display: flex; gap: 6px; }
.btn-step {
  background: #202738;
  color: var(--text);
  border: 1px solid var(--border);
  width: 28px;
  height: 28px;
  border-radius: 6px;
  cursor: pointer;
  font-size: 1rem;
  font-weight: bold;
  display: flex;
  align-items: center;
  justify-content: center;
}
.btn-step:hover { background: #2c364c; }

.progress-bar {
  width: 100%;
  height: 6px;
  background: #10141e;
  border-radius: 4px;
  overflow: hidden;
  margin-top: 4px;
}
.progress-fill { height: 100%; background: linear-gradient(90deg, var(--green), var(--amber)); transition: width 0.5s; }

/* Control Deck */
.control-deck {
  background: var(--card);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  padding: 22px;
  display: flex;
  flex-direction: column;
  gap: 20px;
}
.deck-title { font-size: 1.1rem; font-weight: 700; display: flex; align-items: center; gap: 8px; }

/* Mode selector */
.mode-selector {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 12px;
}
.mode-btn {
  background: #1c2230;
  border: 1px solid var(--border);
  border-radius: 12px;
  padding: 14px;
  text-align: center;
  cursor: pointer;
  transition: all 0.2s;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 6px;
}
.mode-btn .m-icon { font-size: 1.3rem; }
.mode-btn .m-title { font-weight: 600; font-size: 0.95rem; }
.mode-btn .m-desc { font-size: 0.75rem; color: var(--text-muted); }
.mode-btn:hover { border-color: #3b455c; color: var(--text); }
.mode-btn.active {
  background: rgba(229,57,53,0.12);
  border-color: var(--primary);
  color: #ff8a80;
  box-shadow: 0 0 12px var(--primary-glow);
}

/* Sliders */
.sliders-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
  gap: 16px;
}
.slider-box {
  background: #1a202c;
  border: 1px solid var(--border);
  border-radius: 12px;
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 12px;
}
.slider-head { display: flex; justify-content: space-between; align-items: baseline; }
.slider-head label { font-size: 0.85rem; font-weight: 600; color: var(--text-dim); }
.slider-head .val { font-size: 1.4rem; font-weight: 800; color: var(--text); }

input[type=range] {
  -webkit-appearance: none;
  width: 100%;
  background: #0f131a;
  height: 8px;
  border-radius: 4px;
  outline: none;
}
input[type=range]::-webkit-slider-thumb {
  -webkit-appearance: none;
  width: 22px;
  height: 22px;
  border-radius: 50%;
  background: var(--primary);
  cursor: pointer;
  box-shadow: 0 0 8px var(--primary-glow);
  border: 2px solid #fff;
}

.btn-apply {
  background: #252d3d;
  color: var(--text);
  border: 1px solid var(--border);
  padding: 8px;
  border-radius: 8px;
  font-weight: 600;
  font-size: 0.85rem;
  cursor: pointer;
  align-self: flex-end;
  transition: all 0.2s;
}
.btn-apply:hover { background: #323d52; border-color: #495775; }

/* Tabs */
.tabs { display: flex; gap: 8px; border-bottom: 1px solid var(--border); padding-bottom: 8px; }
.tab-btn {
  background: transparent;
  color: var(--text-dim);
  border: none;
  padding: 8px 16px;
  font-weight: 600;
  font-size: 0.9rem;
  cursor: pointer;
  border-radius: 8px;
  transition: all 0.2s;
}
.tab-btn:hover { color: var(--text); background: rgba(255,255,255,0.03); }
.tab-btn.active { color: var(--text); background: #202738; }

.tab-content { display: none; }
.tab-content.active { display: block; }
.log-console {
  background: #0a0d14; border: 1px solid var(--border); border-radius: 10px;
  padding: 10px 12px; font-family: ui-monospace, Menlo, Consolas, monospace;
  font-size: 0.78rem; line-height: 1.5; max-height: 60vh; overflow: auto;
  white-space: pre-wrap; word-break: break-all; color: var(--text-muted);
}
.log-console .rx { color: var(--blue); }
.log-console .tx { color: var(--green); }
.log-console .ts { color: var(--text-muted); }

/* Tables */
table { width: 100%; border-collapse: collapse; font-size: 0.85rem; }
th, td { padding: 10px 12px; text-align: left; border-bottom: 1px solid rgba(255,255,255,0.04); }
th { color: var(--text-dim); font-weight: 600; font-size: 0.75rem; text-transform: uppercase; }
tr:hover td { background: rgba(255,255,255,0.02); }

.input-text {
  padding: 8px 12px;
  border-radius: 8px;
  border: 1px solid var(--border);
  background: #12151e;
  color: #fff;
  font-size: 0.9rem;
  width: 100%;
}
.toast {
  position: fixed;
  bottom: 24px;
  right: 24px;
  background: #1e2533;
  color: #fff;
  border: 1px solid var(--border);
  padding: 12px 20px;
  border-radius: 10px;
  box-shadow: 0 8px 24px rgba(0,0,0,0.5);
  display: none;
  font-weight: 600;
  font-size: 0.9rem;
  z-index: 1000;
}
</style>
</head>
<body>
<header>
  <div class="brand">
    <svg viewBox="0 0 100 100" style="width:28px;height:28px;filter:drop-shadow(0 0 6px rgba(249,115,22,0.5));flex-shrink:0"><path d="M48 10A40 40 0 1 0 52 90M52 10A40 40 0 0 1 48 90" fill="none" stroke="var(--primary)" stroke-width="7" stroke-linecap="round"/><path d="M50 24L59 40L53 43L64 58C64 70 58 76 50 76C42 76 36 70 36 58L47 43L41 40Z" fill="var(--primary)"/><path d="M50 52C53 52 56 56 56 61C56 65 53 68 50 68C47 68 44 65 44 61C44 56 47 52 50 52Z" fill="var(--bg)"/></svg>
    <span>Open-Firenet</span>
    <span class="badge badge-sub" id="modelBadge">Poêle</span>
    <span class="badge badge-live"><span class="dot"></span> <span id="connState">En ligne</span></span>
  </div>
  <div class="header-actions">
    <select class="lang-select" id="langSelect" onchange="onSelectLang(this.value)">
      <option value="fr">🇫🇷 Français</option>
      <option value="en">🇬🇧 English</option>
    </select>
  </div>
</header>
<main>
  <div id="apBanner" style="display:none;background:rgba(56,189,248,0.15);border:1px solid var(--blue);border-radius:10px;padding:12px 16px;font-size:0.9rem;align-items:center;gap:12px">
    <span style="font-size:1.5rem">📶</span>
    <div>
      <b id="apBannerTitle">Mode Point d'Accès actif</b>
      <p id="apBannerDesc" style="font-size:0.8rem;color:var(--text-dim)">Sélectionnez votre réseau WiFi ci-dessous pour connecter Open-Firenet à votre box.</p>
    </div>
  </div>

  <!-- Hero Card: Main Stove State -->
  <div class="hero-card">
    <div class="hero-state">
      <div class="state-icon-box" id="stateIconBox">⚪</div>
      <div class="hero-labels">
        <h2 id="mainStateTitle">Veille (Standby)</h2>
        <p id="subStateDesc">Poêle en attente de consigne</p>
      </div>
    </div>
    <div>
      <button class="power-btn btn-off" id="powerBtn" onclick="togglePower()">
        <span>⏻</span> <span id="powerBtnText">Allumer</span>
      </button>
    </div>
  </div>

  <!-- 4 Core Metrics -->
  <div class="grid-metrics">
    <div class="card">
      <div class="card-header">
        <span id="lblRoomHeader">Température Ambiante</span>
        <span>🌡️</span>
      </div>
      <div class="card-val">
        <span id="roomTemp">--</span><span class="unit">°C</span>
      </div>
      <div class="card-footer">
        <span><span id="lblRoomTargetPrefix">Consigne : </span><b id="roomTarget">--</b>°C</span>
        <div class="stepper">
          <button class="btn-step" onclick="stepTarget(-1)">-</button>
          <button class="btn-step" onclick="stepTarget(1)">+</button>
        </div>
      </div>
    </div>

    <div class="card">
      <div class="card-header">
        <span id="lblFlameHeader">Température Chambre Combustion</span>
        <span>🔥</span>
      </div>
      <div class="card-val">
        <span id="flameTemp">--</span><span class="unit">°C</span>
      </div>
      <div class="card-footer">
        <span id="lblFlameSub">Sonde thermocouple</span>
        <span id="flameBadge" style="color:var(--text-muted)">Inactif</span>
      </div>
    </div>

    <div class="card">
      <div class="card-header">
        <span id="lblPowerHeader">Puissance</span>
        <span>⚡</span>
      </div>
      <div class="card-val">
        <span id="stageCur">--</span><span class="unit">%</span>
      </div>
      <div class="card-footer">
        <span><span id="lblModePrefix">Mode : </span><b id="modeText">--</b></span>
        <span><span id="lblStageTgtPrefix">Cible : </span><b id="stageTgt">--</b>%</span>
      </div>
    </div>

    <div class="card">
      <div class="card-header">
        <span id="lblPelletsHeader">Compteurs & Entretien</span>
        <span>📦</span>
      </div>
      <div class="card-val">
        <span id="pelletsTotal">--</span><span class="unit">kg</span>
      </div>
      <div class="card-footer" style="display:block">
        <div style="display:flex;justify-content:space-between;font-size:0.75rem;margin-bottom:2px">
          <span><span id="lblServiceCountPrefix">Service restant : </span><b id="serviceCount">--</b> kg</span>
          <span><b id="pelletHours">--</b><span id="lblPelletHoursSuffix"> h granulés</span></span>
        </div>
        <div class="progress-bar"><div class="progress-fill" id="serviceBar" style="width:100%"></div></div>
      </div>
    </div>
  </div>

  <!-- Control Deck -->
  <div class="control-deck">
    <div class="deck-title">
      <span>🎛️</span> <span id="lblDeckTitle">Commandes & Consignes</span>
    </div>

    <!-- Mode Selector -->
    <div>
      <label id="lblRegulationMode" style="font-size:0.85rem;color:var(--text-dim);margin-bottom:8px;display:block">MODE DE RÉGULATION</label>
      <div class="mode-selector">
        <div class="mode-btn" id="modeBtn2" onclick="setMode(2)">
          <div class="m-icon">🛋️</div>
          <div class="m-title" id="lblModeTitle2">Confort</div>
          <div class="m-desc" id="lblModeDesc2">Sonde de température</div>
        </div>
        <div class="mode-btn" id="modeBtn1" onclick="setMode(1)">
          <div class="m-icon">⏱️</div>
          <div class="m-title" id="lblModeTitle1">Auto</div>
          <div class="m-desc" id="lblModeDesc1">Horaires programmés</div>
        </div>
        <div class="mode-btn" id="modeBtn0" onclick="setMode(0)">
          <div class="m-icon">⚙️</div>
          <div class="m-title" id="lblModeTitle0">Manuel</div>
          <div class="m-desc" id="lblModeDesc0">Puissance fixe (%)</div>
        </div>
      </div>
    </div>

    <!-- Sliders Grid -->
    <div class="sliders-grid">
      <!-- Target Room Temp -->
      <div class="slider-box">
        <div class="slider-head">
          <label id="lblSliderTemp">Consigne Ambiance (Confort)</label>
          <span class="val"><span id="sliderTempVal">20</span> <small style="font-size:0.9rem;color:var(--text-muted)">°C</small></span>
        </div>
        <input type="range" id="tempRange" min="14" max="28" step="1" value="20" oninput="onTempSlider(this.value)">
        <button class="btn-apply" id="btnApplyTemp" onclick="applyRoomTarget()">Appliquer la température</button>
      </div>

      <!-- Target Stage Power -->
      <div class="slider-box">
        <div class="slider-head">
          <label id="lblSliderStage">Puissance (Auto & Manuel)</label>
          <span class="val"><span id="sliderStageVal">70</span> <small style="font-size:0.9rem;color:var(--text-muted)">%</small></span>
        </div>
        <input type="range" id="stageRange" min="30" max="100" step="5" value="70" oninput="onStageSlider(this.value)">
        <button class="btn-apply" id="btnApplyStage" onclick="applyStageTarget()">Appliquer la puissance</button>
      </div>
    </div>
  </div>

  <!-- Tabs Navigation -->
  <div class="tabs">
    <button class="tab-btn active" id="tabBtnTelemetry" onclick="showTab('tab-telemetry')">📊 Télémétrie complète</button>
    <button class="tab-btn" id="tabBtnNetwork" onclick="showTab('tab-network')">📶 Réseau & WiFi</button>
    <button class="tab-btn" id="tabBtnLink" onclick="showTab('tab-link')">⚙️ Liaison CDC</button>
    <button class="tab-btn" id="tabBtnLogs" onclick="showTab('tab-logs')">📜 Logs CDC</button>
  </div>

  <!-- Tab 1: Telemetry -->
  <div class="tab-content active" id="tab-telemetry">
    <div class="card" style="padding:14px">
      <div style="margin-bottom:12px;display:flex;gap:10px">
        <input class="input-text" id="sensorSearch" placeholder="🔍 Filtrer les 53 capteurs..." oninput="filterSensors(this.value)">
      </div>
      <table id="sensorsTable">
        <thead><tr><th id="thSensorName">Capteur</th><th id="thSensorId">Identifiant</th><th id="thSensorVal">Valeur</th></tr></thead>
        <tbody id="sensorsBody"></tbody>
      </table>
    </div>
  </div>

  <!-- Tab 2: Network -->
  <div class="tab-content" id="tab-network">
    <div class="card" style="gap:16px">
      <h3 id="lblNetTitle">Configuration Réseau</h3>
      <div style="display:flex;justify-content:space-between;color:var(--text-dim);font-size:0.9rem;flex-wrap:wrap;gap:8px">
        <span><span id="lblNetModePrefix">Mode : </span><b id="netMode">--</b></span>
        <span>IP : <b id="netIp">--</b></span>
        <span><span id="lblNetRssiPrefix">Signal RSSI : </span><b id="netRssi">--</b> dBm</span>
        <span><span id="lblNetUptimePrefix">Uptime : </span><b id="netUptime">--</b></span>
      </div>
      <div style="display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px">
        <label id="lblJoinWifi" style="font-size:0.9rem;font-weight:600">Rejoindre un réseau WiFi (2.4 GHz) :</label>
        <button class="btn-lang" id="btnScan" style="padding:4px 10px;font-size:0.8rem" onclick="scanWifi()">🔄 Scanner</button>
      </div>
      <div style="display:flex;gap:10px;flex-direction:column">
        <select class="input-text" id="ssidSelect" onchange="onSelectSsid(this.value)">
          <option value="">-- Choisir un réseau détecté --</option>
        </select>
        <div style="display:flex;gap:10px;flex-wrap:wrap">
          <input class="input-text" id="newSsid" placeholder="Nom du réseau (SSID)" style="flex:1;min-width:180px">
          <div style="flex:1;min-width:180px;position:relative;display:flex;align-items:center">
            <input class="input-text" id="newPass" type="password" placeholder="Mot de passe" style="width:100%;padding-right:36px">
            <span style="position:absolute;right:10px;cursor:pointer;user-select:none;font-size:1.1rem" onclick="togglePassView()" title="Afficher/Masquer">👁️</span>
          </div>
        </div>
      </div>
      <div style="display:flex;gap:10px;justify-content:flex-end">
        <button class="btn-lock" id="btnForgetWifi" style="color:#ef4444;border-color:#ef4444" onclick="forgetWifi()">Oublier le WiFi</button>
        <button class="power-btn" id="btnSaveWifi" style="padding:8px 16px;font-size:0.9rem" onclick="saveWifi()">Enregistrer & Redémarrer</button>
      </div>
    </div>
  </div>

  <!-- Tab 3: CDC Link -->
  <div class="tab-content" id="tab-link">
    <div class="card">
      <table id="linkTable">
        <tbody>
          <tr><td id="lblCdcPort" style="color:var(--text-dim)">Liaison série USB</td><td id="cdcState">Opérationnelle (Full Speed)</td></tr>
          <tr><td id="lblCdcAck" style="color:var(--text-dim)">Version poêle acquittée</td><td id="cdcAck">--</td></tr>
          <tr><td id="lblCdcGen" style="color:var(--text-dim)">Génération protocole</td><td id="cdcGen">--</td></tr>
          <tr><td id="lblCdcRev" style="color:var(--text-dim)">Révision poêle courante</td><td id="cdcRev">--</td></tr>
          <tr><td id="lblCdcIn" style="color:var(--text-dim)">Trames reçues poêle (IN)</td><td id="cdcIn">--</td></tr>
          <tr><td id="lblCdcOut" style="color:var(--text-dim)">Trames émises poêle (OUT)</td><td id="cdcOut">--</td></tr>
          <tr><td id="lblUptime" style="color:var(--text-dim)">Temps d'activité (Uptime)</td><td id="dongleUptime">--</td></tr>
        </tbody>
      </table>
      <div style="display:flex;justify-content:flex-end;margin-top:14px">
        <button class="btn-lock" id="btnRestart" style="color:var(--amber);border-color:var(--amber)" onclick="restartDongle()">🔄 Redémarrer la clef</button>
      </div>
    </div>
  </div>

  <!-- Tab 4: CDC Logs -->
  <div class="tab-content" id="tab-logs">
    <div class="card" style="padding:14px;gap:10px">
      <div style="display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px">
        <div style="font-size:0.85rem">
          <span style="color:var(--blue)">■</span> <span id="lblLogRx">Poêle → Clef (RX)</span>
          &nbsp;&nbsp;
          <span style="color:var(--green)">■</span> <span id="lblLogTx">Clef → Poêle (TX)</span>
        </div>
        <div style="display:flex;gap:12px;align-items:center">
          <label style="font-size:0.8rem;color:var(--text-dim);cursor:pointer"><input type="checkbox" id="logAuto" checked> <span id="lblLogAuto">Auto</span></label>
          <button class="btn-lang" id="btnLogDownload" style="padding:4px 10px;font-size:0.8rem" onclick="downloadLog()">⬇ Télécharger</button>
          <button class="btn-lang" id="btnLogClear" style="padding:4px 10px;font-size:0.8rem" onclick="clearLog()">Effacer</button>
        </div>
      </div>
      <pre id="logConsole" class="log-console">…</pre>
    </div>
  </div>
</main>
<div class="toast" id="toast"></div>

<script>
let lastState = null;
let userInteracting = false;
let userInteractTimer = null;
let curLang = localStorage.getItem('lang') || 'fr';

const I18N = {
  fr: {
    pageTitle: "Open-Firenet — Tableau de Bord",
    langNext: "EN",
    online: "En ligne",
    waiting: "Attente poêle",
    connected: "Poêle connecté",
    offline: "Hors ligne",
    commError: "Erreur de communication",
    powerOn: "Allumer",
    powerOff: "Éteindre",
    confirmOn: "Voulez-vous vraiment ALLUMER le poêle ?",
    confirmOff: "Voulez-vous vraiment ÉTEINDRE le poêle ?",
    roomHeader: "Température Ambiante",
    roomTargetPrefix: "Consigne : ",
    flameHeader: "Température Chambre Combustion",
    flameSub: "Sonde thermocouple",
    flameLive: "Combustion active",
    flameCombustion: "Combustion",
    flameCold: "Froid / Inactif",
    powerHeader: "Puissance",
    modePrefix: "Mode : ",
    stageTgtPrefix: "Cible : ",
    pelletsHeader: "Compteurs & Entretien",
    serviceCountPrefix: "Service restant : ",
    pelletHoursSuffix: " h granulés",
    deckTitle: "Commandes & Consignes",
    regulationMode: "MODE DE RÉGULATION",
    modeTitle2: "Confort",
    modeDesc2: "Sonde de température",
    modeTitle1: "Auto",
    modeDesc1: "Horaires programmés",
    modeTitle0: "Manuel",
    modeDesc0: "Puissance fixe (%)",
    sliderTemp: "Consigne Ambiance (Confort)",
    btnApplyTemp: "Appliquer la température",
    sliderStage: "Puissance (Auto & Manuel)",
    btnApplyStage: "Appliquer la puissance",
    tabTelemetry: "📊 Télémétrie complète",
    tabNetwork: "📶 Réseau & WiFi",
    tabLink: "⚙️ Liaison CDC",
    tabLogs: "📜 Logs CDC",
    logRx: "Poêle → Clef (RX)",
    logTx: "Clef → Poêle (TX)",
    logAuto: "Auto",
    logDownload: "⬇ Télécharger",
    logClear: "Effacer",
    btnRestart: "🔄 Redémarrer la clef",
    confirmRestart: "Redémarrer la clef ? La liaison avec le poêle sera coupée quelques secondes.",
    restartToast: "Redémarrage en cours…",
    sensorSearchPlaceholder: "🔍 Filtrer les 53 capteurs...",
    thSensorName: "Capteur",
    thSensorId: "Identifiant",
    thSensorVal: "Valeur",
    netTitle: "Configuration Réseau",
    netModePrefix: "Mode : ",
    netRssiPrefix: "Signal RSSI : ",
    joinWifi: "Rejoindre un réseau WiFi (2.4 GHz) :",
    btnScan: "🔄 Scanner",
    selectNetwork: "-- Choisir un réseau détecté --",
    scanning: "Recherche des réseaux 2.4 GHz...",
    scanError: "Erreur lors du scan WiFi",
    apBannerTitle: "Mode Point d'Accès actif",
    apBannerDesc: "Sélectionnez votre réseau WiFi ci-dessous pour connecter Open-Firenet à votre box.",
    ssidPlaceholder: "Nom du réseau (SSID)",
    passPlaceholder: "Mot de passe",
    btnForgetWifi: "Oublier le WiFi",
    btnSaveWifi: "Enregistrer & Redémarrer",
    cdcPort: "Liaison série USB",
    cdcAck: "Version poêle acquittée",
    cdcGen: "Génération protocole",
    cdcRev: "Révision poêle courante",
    cdcIn: "Trames reçues poêle (IN)",
    cdcOut: "Trames émises poêle (OUT)",
    cdcSpeed: "Opérationnelle (Full Speed)",
    lblUptime: "Temps d'activité (Uptime)",
    lblNetUptimePrefix: "Uptime : ",
    yes: "Oui",
    no: "Non",
    confirmSaveWifi: "La carte va redémarrer et tenter de rejoindre ",
    saveWifiSuccess: "Paramètres enregistrés ! La carte redémarre.\n\nReconnectez votre appareil à votre WiFi habituel et rendez-vous sur :\nhttp://open-firenet.local",
    rebootTitle: "Connexion au réseau en cours...",
    rebootDesc: "La carte redémarre pour rejoindre votre box WiFi. Reconnectez votre smartphone ou PC à votre réseau habituel, puis accédez au tableau de bord :",
    confirmForgetWifi: "Effacer les paramètres WiFi et repasser en mode Point d'Accès ?",
    forgetWifiSuccess: "WiFi effacé. La carte redémarre en AP.",
    orderSent: "Ordre envoyé : ",
    errorPrefix: "Erreur : ",
    netError: "Erreur réseau",
    stateMap: {
      0: { title: "Arrêt", desc: "Poêle complètement éteint", icon: "🛑", active: false },
      1: { title: "Veille (Standby)", desc: "En attente de consigne", icon: "⚪", active: false },
      2: { title: "Allumage (Ignition)", desc: "Préchauffage bougie & granulés", icon: "🔥", active: true },
      3: { title: "Démarrage flamme", desc: "Stabilisation de la combustion", icon: "🔥", active: true },
      4: { title: "En régulation (Chauffe)", desc: "Combustion active normale", icon: "🔥", active: true },
      5: { title: "Nettoyage grille", desc: "Cycle de décrassage automatique", icon: "🧹", active: true },
      6: { title: "Extinction (Burn off)", desc: "Fin de combustion & ventilation", icon: "💨", active: true }
    },
    modeMap: { 0: "Manuel (%)", 1: "Automatique", 2: "Confort" },
    sensorDesc: {
      roomTemp: "Température ambiante (°C)",
      flame: "Température chambre de combustion (°C)",
      boardSensor: "Sonde carte mère (°C)",
      pelletsTotal: "Consommation totale (kg)",
      pelletHours: "Heures granulés (h)",
      serviceCountdown: "Compte à rebours entretien (kg)",
      mainState: "État principal poêle",
      subState: "Sous-état détaillé",
      stageCur: "Puissance actuelle (%)",
      stageCur1: "Puissance demandée (%)",
      stageTgt2: "Puissance cible (%)",
      idFanMeas: "Vitesse ventilateur fumées (RPM)",
      idFanSet: "Consigne ventilateur fumées (RPM)",
      augerSet: "Consigne vis d'alimentation (RPM)",
      model: "Modèle poêle",
      appVerBoard: "Version firmware carte mère",
      firmwareBuild: "Sous-version build",
      language: "Langue configurée (3 = FR)",
      rssi: "Signal radio WiFi (dBm)",
      errMask32: "Masque erreurs 32 bits",
      errSub: "Code sous-erreur active",
      serviceOffset: "Décalage compteur révision",
      serviceMinutes: "Minutes totales écoulées révision"
    }
  },
  en: {
    pageTitle: "Open-Firenet — Dashboard",
    langNext: "FR",
    online: "Online",
    waiting: "Waiting for stove",
    connected: "Stove connected",
    offline: "Offline",
    commError: "Communication error",
    powerOn: "Turn On",
    powerOff: "Turn Off",
    confirmOn: "Are you sure you want to TURN ON the stove?",
    confirmOff: "Are you sure you want to TURN OFF the stove?",
    roomHeader: "Room Temperature",
    roomTargetPrefix: "Setpoint: ",
    flameHeader: "Combustion Chamber Temp",
    flameSub: "Thermocouple sensor",
    flameLive: "Active combustion",
    flameCombustion: "Combustion",
    flameCold: "Cold / Inactive",
    powerHeader: "Power",
    modePrefix: "Mode: ",
    stageTgtPrefix: "Target: ",
    pelletsHeader: "Counters & Service",
    serviceCountPrefix: "Service countdown: ",
    pelletHoursSuffix: " h pellets",
    deckTitle: "Controls & Setpoints",
    regulationMode: "REGULATION MODE",
    modeTitle2: "Comfort",
    modeDesc2: "Room thermostat sensor",
    modeTitle1: "Auto",
    modeDesc1: "Scheduled heating",
    modeTitle0: "Manual",
    modeDesc0: "Fixed power level (%)",
    sliderTemp: "Room Setpoint (Comfort)",
    btnApplyTemp: "Apply Temperature",
    sliderStage: "Power (Auto & Manual)",
    btnApplyStage: "Apply Power",
    tabTelemetry: "📊 Full Telemetry",
    tabNetwork: "📶 Network & WiFi",
    tabLink: "⚙️ USB CDC Link",
    tabLogs: "📜 CDC Logs",
    logRx: "Stove → Dongle (RX)",
    logTx: "Dongle → Stove (TX)",
    logAuto: "Auto",
    logDownload: "⬇ Download",
    logClear: "Clear",
    btnRestart: "🔄 Restart dongle",
    confirmRestart: "Restart the dongle? The link with the stove will drop for a few seconds.",
    restartToast: "Restarting…",
    sensorSearchPlaceholder: "🔍 Filter 53 sensors...",
    thSensorName: "Sensor",
    thSensorId: "Internal ID",
    thSensorVal: "Value",
    netTitle: "Network Configuration",
    netModePrefix: "Mode: ",
    netRssiPrefix: "RSSI Signal: ",
    joinWifi: "Join a WiFi network (2.4 GHz):",
    btnScan: "🔄 Scan",
    selectNetwork: "-- Select detected network --",
    scanning: "Scanning 2.4 GHz networks...",
    scanError: "Error scanning WiFi",
    apBannerTitle: "Access Point mode active",
    apBannerDesc: "Select your WiFi network below to connect Open-Firenet to your router.",
    ssidPlaceholder: "Network Name (SSID)",
    passPlaceholder: "Password",
    btnForgetWifi: "Forget WiFi",
    btnSaveWifi: "Save & Reboot",
    cdcPort: "USB CDC Serial Link",
    cdcAck: "Stove version acknowledged",
    cdcGen: "Protocol generation",
    cdcRev: "Current stove revision",
    cdcIn: "Stove incoming frames (IN)",
    cdcOut: "Stove outgoing frames (OUT)",
    cdcSpeed: "Operational (Full Speed)",
    lblUptime: "Dongle Uptime",
    lblNetUptimePrefix: "Uptime: ",
    yes: "Yes",
    no: "No",
    confirmSaveWifi: "Board will reboot and attempt to join ",
    saveWifiSuccess: "Settings saved! The board is rebooting.\n\nReconnect your device to your home WiFi and go to:\nhttp://open-firenet.local",
    rebootTitle: "Connecting to home network...",
    rebootDesc: "The board is rebooting to join your WiFi router. Reconnect your smartphone or PC to your home network, then access the dashboard:",
    confirmForgetWifi: "Erase WiFi settings and switch back to Access Point mode?",
    forgetWifiSuccess: "WiFi erased. Board is rebooting in AP mode.",
    orderSent: "Order sent: ",
    errorPrefix: "Error: ",
    netError: "Network error",
    stateMap: {
      0: { title: "Off", desc: "Stove completely turned off", icon: "🛑", active: false },
      1: { title: "Standby", desc: "Waiting for heat demand", icon: "⚪", active: false },
      2: { title: "Ignition", desc: "Preheating plug & pellets", icon: "🔥", active: true },
      3: { title: "Flame Start", desc: "Stabilizing flame combustion", icon: "🔥", active: true },
      4: { title: "Heating", desc: "Regulated active combustion", icon: "🔥", active: true },
      5: { title: "Grate Cleaning", desc: "Automatic de-ashing cycle", icon: "🧹", active: true },
      6: { title: "Burn off", desc: "Cooling down & post-ventilation", icon: "💨", active: true }
    },
    modeMap: { 0: "Manual (%)", 1: "Automatic", 2: "Comfort" },
    sensorDesc: {
      roomTemp: "Measured room temperature (°C)",
      flame: "Combustion chamber temperature (°C)",
      boardSensor: "Mainboard temperature (°C)",
      pelletsTotal: "Total pellet consumption (kg)",
      pelletHours: "Total pellet runtime (h)",
      serviceCountdown: "Service countdown (kg)",
      mainState: "Main operating state",
      subState: "Detailed sub-state",
      stageCur: "Current power (%)",
      stageCur1: "Demanded power (%)",
      stageTgt2: "Target power (%)",
      idFanMeas: "Flue draft fan speed (RPM)",
      idFanSet: "Flue draft fan setpoint (RPM)",
      augerSet: "Pellet feed auger setpoint (RPM)",
      model: "Stove model",
      appVerBoard: "Mainboard firmware version",
      firmwareBuild: "Build sub-version",
      language: "Configured language (3 = FR)",
      rssi: "WiFi signal strength (dBm)",
      errMask32: "Active error bitmask (32 bits)",
      errSub: "Active error subcode",
      serviceOffset: "Service counter offset",
      serviceMinutes: "Total elapsed service minutes"
    }
  }
};

function applyLang() {
  const t = I18N[curLang] || I18N.fr;
  document.getElementById('pageTitle').textContent = t.pageTitle;
  const ls = document.getElementById('langSelect');
  if (ls) ls.value = curLang;
  document.getElementById('lblRoomHeader').textContent = t.roomHeader;
  document.getElementById('lblRoomTargetPrefix').textContent = t.roomTargetPrefix;
  document.getElementById('lblFlameHeader').textContent = t.flameHeader;
  document.getElementById('lblFlameSub').textContent = t.flameSub;
  document.getElementById('lblPowerHeader').textContent = t.powerHeader;
  document.getElementById('lblModePrefix').textContent = t.modePrefix;
  document.getElementById('lblStageTgtPrefix').textContent = t.stageTgtPrefix;
  document.getElementById('lblPelletsHeader').textContent = t.pelletsHeader;
  document.getElementById('lblServiceCountPrefix').textContent = t.serviceCountPrefix;
  document.getElementById('lblPelletHoursSuffix').textContent = t.pelletHoursSuffix;
  document.getElementById('lblDeckTitle').textContent = t.deckTitle;
  document.getElementById('lblRegulationMode').textContent = t.regulationMode;
  document.getElementById('lblModeTitle2').textContent = t.modeTitle2;
  document.getElementById('lblModeDesc2').textContent = t.modeDesc2;
  document.getElementById('lblModeTitle1').textContent = t.modeTitle1;
  document.getElementById('lblModeDesc1').textContent = t.modeDesc1;
  document.getElementById('lblModeTitle0').textContent = t.modeTitle0;
  document.getElementById('lblModeDesc0').textContent = t.modeDesc0;
  document.getElementById('lblSliderTemp').textContent = t.sliderTemp;
  document.getElementById('btnApplyTemp').textContent = t.btnApplyTemp;
  document.getElementById('lblSliderStage').textContent = t.sliderStage;
  document.getElementById('btnApplyStage').textContent = t.btnApplyStage;
  document.getElementById('tabBtnTelemetry').textContent = t.tabTelemetry;
  document.getElementById('tabBtnNetwork').textContent = t.tabNetwork;
  document.getElementById('tabBtnLink').textContent = t.tabLink;
  document.getElementById('tabBtnLogs').textContent = t.tabLogs;
  document.getElementById('lblLogRx').textContent = t.logRx;
  document.getElementById('lblLogTx').textContent = t.logTx;
  document.getElementById('lblLogAuto').textContent = t.logAuto;
  document.getElementById('btnLogDownload').textContent = t.logDownload;
  document.getElementById('btnLogClear').textContent = t.logClear;
  document.getElementById('btnRestart').textContent = t.btnRestart;
  document.getElementById('sensorSearch').placeholder = t.sensorSearchPlaceholder;
  document.getElementById('thSensorName').textContent = t.thSensorName;
  document.getElementById('thSensorId').textContent = t.thSensorId;
  document.getElementById('thSensorVal').textContent = t.thSensorVal;
  document.getElementById('lblNetTitle').textContent = t.netTitle;
  document.getElementById('lblNetModePrefix').textContent = t.netModePrefix;
  document.getElementById('lblNetRssiPrefix').textContent = t.netRssiPrefix;
  document.getElementById('lblJoinWifi').textContent = t.joinWifi;
  if (document.getElementById('btnScan')) document.getElementById('btnScan').textContent = t.btnScan;
  if (document.getElementById('apBannerTitle')) document.getElementById('apBannerTitle').textContent = t.apBannerTitle;
  if (document.getElementById('apBannerDesc')) document.getElementById('apBannerDesc').textContent = t.apBannerDesc;
  document.getElementById('newSsid').placeholder = t.ssidPlaceholder;
  document.getElementById('newPass').placeholder = t.passPlaceholder;
  document.getElementById('btnForgetWifi').textContent = t.btnForgetWifi;
  document.getElementById('btnSaveWifi').textContent = t.btnSaveWifi;
  document.getElementById('lblCdcPort').textContent = t.cdcPort;
  document.getElementById('lblCdcAck').textContent = t.cdcAck;
  document.getElementById('lblCdcGen').textContent = t.cdcGen;
  document.getElementById('lblCdcRev').textContent = t.cdcRev;
  document.getElementById('lblCdcIn').textContent = t.cdcIn;
  document.getElementById('lblCdcOut').textContent = t.cdcOut;
  const elU = document.getElementById('lblUptime');
  if (elU) elU.textContent = t.lblUptime;
  const elNetP = document.getElementById('lblNetUptimePrefix');
  if (elNetP) elNetP.textContent = t.lblNetUptimePrefix;
}

function onSelectLang(l) {
  curLang = (l === 'en') ? 'en' : 'fr';
  localStorage.setItem('lang', curLang);
  applyLang();
  if (lastState) renderSensors(lastState.raw_sensors || lastState.sensors || {}, document.getElementById('sensorSearch') ? document.getElementById('sensorSearch').value : '');
  tick();
}

function toast(msg) {
  const t = document.getElementById('toast');
  t.textContent = msg;
  t.style.display = 'block';
  clearTimeout(t.timer);
  t.timer = setTimeout(() => { t.style.display = 'none'; }, 3000);
}

function showTab(id) {
  document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
  document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
  const btnMap = {'tab-network':'tabBtnNetwork','tab-link':'tabBtnLink','tab-logs':'tabBtnLogs','tab-telemetry':'tabBtnTelemetry'};
  const btn = document.getElementById(btnMap[id] || 'tabBtnTelemetry');
  if (btn) btn.classList.add('active');
  const tab = document.getElementById(id);
  if (tab) tab.classList.add('active');
  if (id === 'tab-network' && document.getElementById('ssidSelect').options.length <= 1) {
    scanWifi();
  }
  if (id === 'tab-logs') fetchLogs();
}

// --- Logs CDC : /log renvoie des lignes "[millis][rx|tx] contenu" ------------
async function fetchLogs() {
  const tab = document.getElementById('tab-logs');
  if (!tab || !tab.classList.contains('active')) return;
  if (!document.getElementById('logAuto').checked) return;
  try {
    const r = await fetch('/log');
    const txt = await r.text();
    const esc = s => s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
    // Regroupe les trames multi-lignes en UNE entrée : le contenu d'une trame
    // (ex. POST_CDCDEVICE_STATUS) contient des \n ; on réassemble ses champs
    // avec " · " au lieu d'exploser en lignes de chiffres.
    const entries = [];
    for (const line of txt.split('\n')) {
      const m = line.match(/^\[(\d+)\]\[(rx|tx)\]\s?(.*)$/);
      if (m) entries.push({ts: m[1], dir: m[2], parts: [m[3]]});
      else if (entries.length && line !== '') entries[entries.length - 1].parts.push(line);
    }
    const html = entries.map(e => {
      const tag = e.dir === 'rx' ? 'RX' : 'TX';
      if (e.parts.length >= 18 && e.parts[0].includes('STATUS=')) {
        if (e.parts[17] && e.parts[17] !== '0') e.parts[17] = '********';
      }
      const payload = e.parts.filter(p => p.trim() !== '').join(' · ');
      return '<span class="ts">[' + e.ts + ']</span> ' +
             '<span class="' + e.dir + '"><b>' + tag + '</b>  ' + esc(payload) + '</span>';
    }).join('\n');
    const el = document.getElementById('logConsole');
    const atBottom = el.scrollHeight - el.scrollTop - el.clientHeight < 40;
    el.innerHTML = html || '(vide)';
    if (atBottom) el.scrollTop = el.scrollHeight;
  } catch (e) { /* silencieux */ }
}
function clearLog() { document.getElementById('logConsole').innerHTML = '(effacé, en attente…)'; }
async function restartDongle() {
  if (!confirm(I18N[curLang].confirmRestart)) return;
  try { await fetch('/api/restart', {method: 'POST'}); } catch (e) { /* la carte redémarre */ }
  toast(I18N[curLang].restartToast);
}
async function downloadLog() {
  try {
    const txt = await (await fetch('/log')).text();
    const pad = n => String(n).padStart(2, '0');
    const d = new Date();
    const name = 'open-firenet-log-' + d.getFullYear() + pad(d.getMonth()+1) + pad(d.getDate())
               + '-' + pad(d.getHours()) + pad(d.getMinutes()) + pad(d.getSeconds()) + '.txt';
    const url = URL.createObjectURL(new Blob([txt], {type: 'text/plain'}));
    const a = document.createElement('a');
    a.href = url; a.download = name;
    document.body.appendChild(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  } catch (e) { /* silencieux */ }
}

function setInteracting() {
  userInteracting = true;
  clearTimeout(userInteractTimer);
  userInteractTimer = setTimeout(() => { userInteracting = false; }, 4000);
}

function onTempSlider(v) {
  document.getElementById('sliderTempVal').textContent = parseInt(v, 10);
  setInteracting();
}

function onStageSlider(v) {
  document.getElementById('sliderStageVal').textContent = v;
  setInteracting();
}

function stepTarget(delta) {
  const r = document.getElementById('tempRange');
  let v = parseInt(r.value, 10) + delta;
  if (v < 14) v = 14; if (v > 28) v = 28;
  r.value = v;
  onTempSlider(v);
  applyRoomTarget();
}

async function sendControl(name, value) {
  const t = I18N[curLang] || I18N.fr;
  try {
    const body = new URLSearchParams({ name: name, value: value });
    const res = await fetch('/api/control', { method: 'POST', body: body });
    if (res.ok) {
      toast(t.orderSent + name + " = " + value);
      setTimeout(tick, 300);
    } else {
      const err = await res.json();
      toast(t.errorPrefix + (err.error || "failed"));
    }
  } catch(e) { toast(t.netError); }
}

function togglePower() {
  if (!lastState) return;
  const t = I18N[curLang] || I18N.fr;
  const curOn = (lastState.controls && (lastState.controls.on === true || lastState.controls.onOff == 1)) ||
                (lastState.controls_pos && lastState.controls_pos[1] == 1);
  const promptText = curOn ? t.confirmOff : t.confirmOn;
  if (confirm(promptText)) {
    sendControl("on", curOn ? 0 : 1);
  }
}

function setMode(m) {
  sendControl("mode", m);
}

function applyRoomTarget() {
  const v = parseInt(document.getElementById('tempRange').value, 10);
  sendControl("roomTarget", v * 10);
}

function applyStageTarget() {
  const v = parseInt(document.getElementById('stageRange').value, 10);
  sendControl("targetStage", v);
}

async function saveWifi() {
  const t = I18N[curLang] || I18N.fr;
  const s = document.getElementById('newSsid').value.trim();
  const p = document.getElementById('newPass').value;
  if (!s) { alert("SSID required"); return; }
  if (confirm(t.confirmSaveWifi + s + " ?")) {
    const body = new URLSearchParams({ ssid: s, pass: p });
    try {
      await fetch('/api/wifi', { method: 'POST', body: body });
    } catch (e) {}
    document.body.innerHTML = `
      <div style="display:flex;align-items:center;justify-content:center;min-height:100vh;background:#0c0f17;color:#f1f5f9;font-family:sans-serif;padding:24px;box-sizing:border-box;">
        <div style="background:#161b26;border:1px solid #232a3b;border-radius:16px;padding:32px;max-width:440px;width:100%;text-align:center;box-shadow:0 10px 30px rgba(0,0,0,0.6);">
          <div style="font-size:48px;margin-bottom:16px;">🔄</div>
          <h2 style="font-size:20px;font-weight:700;margin:0 0 12px 0;">${t.rebootTitle}</h2>
          <p style="font-size:14px;color:#94a3b8;line-height:1.6;margin:0 0 24px 0;">${t.rebootDesc}</p>
          <a href="http://open-firenet.local" style="display:inline-block;width:100%;box-sizing:border-box;background:#38bdf8;color:#0c0f17;font-weight:700;padding:14px;border-radius:10px;text-decoration:none;font-size:16px;box-shadow:0 4px 12px rgba(56,189,248,0.3);">http://open-firenet.local</a>
        </div>
      </div>
    `;
    alert(t.saveWifiSuccess);
  }
}

async function forgetWifi() {
  const t = I18N[curLang] || I18N.fr;
  if (confirm(t.confirmForgetWifi)) {
    await fetch('/api/forget', { method: 'POST' });
    alert(t.forgetWifiSuccess);
  }
}

function togglePassView() {
  const p = document.getElementById('newPass');
  p.type = (p.type === 'password') ? 'text' : 'password';
}

function onSelectSsid(val) {
  if (val) {
    document.getElementById('newSsid').value = decodeURIComponent(val);
    document.getElementById('newPass').focus();
  }
}

async function scanWifi() {
  const sel = document.getElementById('ssidSelect');
  const btn = document.getElementById('btnScan');
  const t = I18N[curLang] || I18N.fr;
  if (btn) btn.disabled = true;
  sel.innerHTML = '<option value="">' + t.scanning + '</option>';
  try {
    let res = await fetch('/api/scan');
    let data = await res.json();
    if (data.status === 'scanning') {
      await new Promise(r => setTimeout(r, 1500));
      res = await fetch('/api/scan');
      data = await res.json();
    }
    if (Array.isArray(data)) {
      let h = '<option value="">' + t.selectNetwork + ' (' + data.length + ')</option>';
      data.sort((a,b) => b.rssi - a.rssi);
      data.forEach(net => {
        let stars = net.rssi > -60 ? '🟢' : (net.rssi > -75 ? '🟡' : '🔴');
        h += '<option value="' + encodeURIComponent(net.ssid) + '">' + stars + ' ' + net.ssid + ' (' + net.rssi + ' dBm)</option>';
      });
      sel.innerHTML = h;
    }
  } catch(e) {
    sel.innerHTML = '<option value="">' + t.scanError + '</option>';
  } finally {
    if (btn) btn.disabled = false;
  }
}

function formatUptime(sec) {
  if (sec === undefined || sec === null || isNaN(sec) || sec < 0) return '--';
  const d = Math.floor(sec / 86400);
  const h = Math.floor((sec % 86400) / 3600);
  const m = Math.floor((sec % 3600) / 60);
  const s = Math.floor(sec % 60);
  const dayUnit = (curLang === 'fr') ? 'j' : 'd';
  if (d > 0) return d + dayUnit + ' ' + String(h).padStart(2,'0') + 'h ' + String(m).padStart(2,'0') + 'm';
  if (h > 0) return h + 'h ' + String(m).padStart(2,'0') + 'm ' + String(s).padStart(2,'0') + 's';
  if (m > 0) return m + 'm ' + String(s).padStart(2,'0') + 's';
  return s + 's';
}

function renderSensors(sObj, filterText) {
  const t = I18N[curLang] || I18N.fr;
  const tb = document.getElementById('sensorsBody');
  let h = '';
  const f = (filterText || '').toLowerCase();
  for (const k in sObj) {
    const label = (t.sensorDesc && t.sensorDesc[k]) ? t.sensorDesc[k] : k;
    if (f && !k.toLowerCase().includes(f) && !label.toLowerCase().includes(f)) continue;
    let v = sObj[k];
    if (k === 'roomTemp') v = (v / 10).toFixed(1) + ' °C';
    else if (k === 'flame' || k === 'boardSensor') v = v + ' °C';
    else if (k === 'pelletsTotal' || k === 'serviceCountdown') v = v + ' kg';
    else if (k === 'pelletHours') v = v + ' h';
    else if (k === 'stageCur' || k === 'stageCur1') v = v + ' %';
    else if (k === 'mainState') v = (t.stateMap && t.stateMap[v] ? t.stateMap[v].title : v);
    else if (k === 'model') {
      const mNames = { 1: 'INDUO (1)', 2: 'INDUO II (2)', 10: 'INTERNO (10)', 13: 'DOMO (13)', 23: 'DOMO BACK (23)' };
      const mFallback = (curLang === 'fr' ? 'Modèle ' : 'Model ') + v;
      v = mNames[v] || mFallback;
    }
    h += '<tr><td>' + label + '</td><td style="color:var(--text-dim)">' + k + '</td><td>' + v + '</td></tr>';
  }
  tb.innerHTML = h;
}

function filterSensors(t) {
  if (lastState) renderSensors(lastState.raw_sensors || lastState.sensors || {}, t);
}

async function tick() {
  const t = I18N[curLang] || I18N.fr;
  try {
    const res = await fetch('/api/state');
    const s = await res.json();
    lastState = s;

    if (s.wifi_mode === 'AP' && !window.apHandled) {
      window.apHandled = true;
      document.getElementById('apBanner').style.display = 'flex';
      showTab('tab-network');
      scanWifi();
    }

    document.getElementById('connState').textContent = s.version_ack ? t.connected : t.waiting;

    // Hero State
    const stObj = s.stove || {};
    const sens = s.sensors || {};
    const ctrl = s.controls || {};
    const rawS = s.raw_sensors || {};

    const mainSt = stObj.state_code !== undefined ? stObj.state_code : (rawS.mainState !== undefined ? rawS.mainState : (s.sensors_pos ? s.sensors_pos[31] : 1));
    const stInfo = (t.stateMap && t.stateMap[mainSt]) || { title: stObj.state_label || ("State " + mainSt), desc: stObj.state || "Unknown", icon: "❓", active: stObj.is_burning || false };
    document.getElementById('mainStateTitle').textContent = stInfo.title;
    document.getElementById('subStateDesc').textContent = stInfo.desc;
    document.getElementById('stateIconBox').textContent = stInfo.icon;
    document.getElementById('stateIconBox').className = 'state-icon-box ' + (stInfo.active ? 'active' : '');

    const onOffVal = (ctrl.on !== undefined) ? (ctrl.on ? 1 : 0) : ((ctrl.onOff !== undefined) ? ctrl.onOff : (s.controls_pos && s.controls_pos.length > 1 ? s.controls_pos[1] : 0));
    const pBtn = document.getElementById('powerBtn');
    const pTxt = document.getElementById('powerBtnText');
    if (onOffVal == 1) {
      pBtn.className = 'power-btn';
      pTxt.textContent = t.powerOff;
    } else {
      pBtn.className = 'power-btn btn-off';
      pTxt.textContent = t.powerOn;
    }

    // Room Temp
    if (sens.room_temperature !== undefined) {
      document.getElementById('roomTemp').textContent = sens.room_temperature.toFixed(1);
    } else if (rawS.roomTemp !== undefined) {
      document.getElementById('roomTemp').textContent = (rawS.roomTemp / 10).toFixed(1);
    }
    const rTgtVal = ctrl.target_temperature !== undefined ? Math.round(ctrl.target_temperature) : (ctrl.roomTarget !== undefined ? Math.round(ctrl.roomTarget / 10) : (s.controls_pos && s.controls_pos.length > 4 ? Math.round(s.controls_pos[4] / 10) : 20));
    document.getElementById('roomTarget').textContent = rTgtVal;

    if (!userInteracting) {
      document.getElementById('tempRange').value = rTgtVal;
      document.getElementById('sliderTempVal').textContent = rTgtVal;
    }

    // Flame / Combustion Chamber
    const flVal = sens.combustion_temperature !== undefined ? Math.round(sens.combustion_temperature) : (rawS.flame !== undefined ? rawS.flame : 0);
    document.getElementById('flameTemp').textContent = flVal;
    const fBadge = document.getElementById('flameBadge');
    if (flVal > 80) { fBadge.textContent = t.flameLive; fBadge.style.color = "var(--primary)"; }
    else if (flVal > 40) { fBadge.textContent = t.flameCombustion; fBadge.style.color = "var(--amber)"; }
    else { fBadge.textContent = t.flameCold; fBadge.style.color = "var(--text-muted)"; }

    // Stage / Power
    const stCur = sens.power_percent !== undefined ? sens.power_percent : (rawS.stageCur !== undefined ? rawS.stageCur : 0);
    document.getElementById('stageCur').textContent = stCur;
    const stTgt = ctrl.power_percent !== undefined ? ctrl.power_percent : (ctrl.targetStage !== undefined ? ctrl.targetStage : (s.controls_pos && s.controls_pos.length > 3 ? s.controls_pos[3] : 70));
    document.getElementById('stageTgt').textContent = stTgt;
    if (!userInteracting) {
      document.getElementById('stageRange').value = stTgt;
      document.getElementById('sliderStageVal').textContent = stTgt;
    }

    // Mode
    const curMode = ctrl.mode_code !== undefined ? ctrl.mode_code : (ctrl.mode !== undefined ? ctrl.mode : (s.controls_pos && s.controls_pos.length > 2 ? s.controls_pos[2] : 2));
    document.getElementById('modeText').textContent = (t.modeMap && t.modeMap[curMode]) || curMode;
    [0, 1, 2].forEach(m => {
      const b = document.getElementById('modeBtn' + m);
      if (b) { if (m === curMode) b.classList.add('active'); else b.classList.remove('active'); }
    });

    // Pellets & Service
    const pTot = sens.pellets_total_kg !== undefined ? sens.pellets_total_kg : (rawS.pelletsTotal !== undefined ? rawS.pelletsTotal : (s.sensors_pos ? s.sensors_pos[49] : 0));
    document.getElementById('pelletsTotal').textContent = pTot.toLocaleString(curLang === 'fr' ? 'fr-FR' : 'en-US');
    const pHours = sens.pellet_hours !== undefined ? sens.pellet_hours : (rawS.pelletHours !== undefined ? rawS.pelletHours : (s.sensors_pos ? s.sensors_pos[47] : 0));
    document.getElementById('pelletHours').textContent = pHours;
    const sCount = sens.service_countdown_kg !== undefined ? sens.service_countdown_kg : (rawS.serviceCountdown !== undefined ? rawS.serviceCountdown : (s.sensors_pos ? s.sensors_pos[50] : 700));
    document.getElementById('serviceCount').textContent = sCount;
    const sPct = Math.min(100, Math.max(0, Math.round((sCount / 700) * 100)));
    document.getElementById('serviceBar').style.width = sPct + '%';

    // Model & Net
    const modelNames = { 1: 'INDUO', 2: 'INDUO II', 10: 'INTERNO', 13: 'DOMO', 23: 'DOMO BACK' };
    const mId = stObj.model !== undefined ? stObj.model : (rawS.model !== undefined ? rawS.model : 13);
    const mFallback = (curLang === 'fr' ? 'Modèle ' : 'Model ') + mId;
    const mName = stObj.model_name || modelNames[mId] || mFallback;
    const vStr = stObj.mainboard_version ? (' V' + stObj.mainboard_version) : '';
    document.getElementById('modelBadge').textContent = mName + vStr;
    document.getElementById('netMode').textContent = s.wifi_mode;
    document.getElementById('netIp').textContent = s.ip;
    const rssiVal = (s.device && s.device.wifi_rssi !== undefined) ? s.device.wifi_rssi : (rawS.rssi || '--');
    document.getElementById('netRssi').textContent = rssiVal;

    // CDC Link
    document.getElementById('cdcRev').textContent = s.revision;
    document.getElementById('cdcIn').textContent = s.frames_in;
    document.getElementById('cdcOut').textContent = s.frames_out;
    document.getElementById('cdcState').textContent = t.cdcSpeed;
    document.getElementById('cdcAck').textContent = s.version_ack ? t.yes : t.no;
    document.getElementById('cdcGen').textContent = s.generation ? s.generation : '--';
    const upSec = (s.uptime_seconds !== undefined) ? s.uptime_seconds : ((s.device && s.device.uptime_seconds !== undefined) ? s.device.uptime_seconds : rawS.uptime);
    const upStr = formatUptime(upSec);
    const elUptime = document.getElementById('dongleUptime');
    if (elUptime) elUptime.textContent = upStr;
    const elNetUp = document.getElementById('netUptime');
    if (elNetUp) elNetUp.textContent = upStr;

    // Table render if search empty
    const sInput = document.getElementById('sensorSearch');
    if (sInput && !sInput.value) renderSensors(rawS, '');
  } catch(e) {
    document.getElementById('connState').textContent = t.offline;
  }
}

applyLang();
setInterval(tick, 2000);
setInterval(fetchLogs, 1500);
tick();
</script>
</body>
</html>)HTML";
