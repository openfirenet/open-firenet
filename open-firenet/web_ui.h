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
.progress-fill { height: 100%; background: linear-gradient(90deg, var(--amber), var(--green)); transition: width 0.5s; }

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

/* MultiAir Fans */
.multiair-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
  gap: 16px;
}
.fan-card {
  background: #1a202c;
  border: 1px solid var(--border);
  border-radius: 12px;
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 14px;
}
.fan-toggle {
  background: #202738;
  border: 1px solid var(--border);
  color: var(--text-dim);
  padding: 6px 14px;
  border-radius: 20px;
  font-size: 0.8rem;
  font-weight: 700;
  cursor: pointer;
  transition: all 0.2s;
  display: flex;
  align-items: center;
  gap: 6px;
}
.fan-toggle:hover { border-color: #3b455c; color: var(--text); }
.fan-toggle.active {
  background: rgba(16, 185, 129, 0.15);
  border-color: var(--green);
  color: var(--green);
  box-shadow: 0 0 10px var(--green-glow);
}
.fan-levels {
  display: grid;
  grid-template-columns: repeat(6, 1fr);
  gap: 6px;
  margin-top: 4px;
}
.btn-lvl {
  background: #202738;
  border: 1px solid var(--border);
  color: var(--text-dim);
  padding: 8px 0;
  border-radius: 8px;
  font-weight: 700;
  font-size: 0.85rem;
  cursor: pointer;
  transition: all 0.2s;
  text-align: center;
}
.btn-lvl:hover {
  background: #2c364c;
  color: var(--text);
  border-color: #4b5878;
}
.btn-lvl.active {
  background: rgba(249, 115, 22, 0.2);
  border-color: var(--primary);
  color: var(--primary);
  box-shadow: 0 0 10px var(--primary-glow);
}

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

.subtab-btn {
  background: transparent;
  color: var(--text-dim);
  border: none;
  padding: 6px 14px;
  font-weight: 600;
  font-size: 0.85rem;
  cursor: pointer;
  border-radius: 8px;
  transition: all 0.2s;
  display: flex;
  align-items: center;
  gap: 6px;
}
.subtab-btn:hover { color: var(--text); background: rgba(255,255,255,0.04); }
.subtab-btn.active { color: var(--text); background: #202738; box-shadow: 0 2px 6px rgba(0,0,0,0.3); }

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

/* Heating Schedule */
.switch { position: relative; display: inline-block; width: 44px; height: 24px; vertical-align: middle; }
.switch input { opacity: 0; width: 0; height: 0; }
.slider-switch { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #202738; border: 1px solid var(--border); transition: .25s; border-radius: 24px; }
.slider-switch:before { position: absolute; content: ""; height: 16px; width: 16px; left: 3px; bottom: 3px; background-color: var(--text-dim); transition: .25s; border-radius: 50%; }
input:checked + .slider-switch { background-color: rgba(16,185,129,0.2); border-color: var(--green); }
input:checked + .slider-switch:before { transform: translateX(20px); background-color: var(--green); }

.sched-day-row {
  background: #1a202c;
  border: 1px solid var(--border);
  border-radius: 12px;
  padding: 12px 16px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}
@media (min-width: 720px) {
  .sched-day-row {
    flex-direction: row;
    align-items: center;
    justify-content: space-between;
  }
}
.sched-day-name {
  font-weight: 700;
  font-size: 0.95rem;
  min-width: 100px;
}
.sched-day-slots {
  display: flex;
  flex-direction: column;
  gap: 8px;
  flex: 1;
}
@media (min-width: 580px) {
  .sched-day-slots {
    flex-direction: row;
    justify-content: flex-end;
    gap: 12px;
  }
}
.sched-slot-box {
  display: flex;
  align-items: center;
  gap: 8px;
  background: #141822;
  border: 1px solid rgba(255,255,255,0.05);
  padding: 6px 10px;
  border-radius: 8px;
  transition: opacity 0.2s;
}
.sched-slot-box.disabled { opacity: 0.45; }
.slot-chk-label { display: flex; align-items: center; gap: 6px; cursor: pointer; user-select: none; }
.slot-chk-label input[type=checkbox] { width: 16px; height: 16px; accent-color: var(--primary); cursor: pointer; }
.slot-pill { font-size: 0.75rem; font-weight: 700; background: #252d3f; color: var(--text-dim); padding: 2px 6px; border-radius: 4px; }
.slot-arrow { color: var(--text-muted); font-size: 0.85rem; }
.input-time {
  background: #0f131c;
  color: var(--text);
  border: 1px solid var(--border);
  border-radius: 6px;
  padding: 4px 6px;
  font-family: inherit;
  font-size: 0.85rem;
  outline: none;
}
.input-time:focus { border-color: var(--primary); }
.input-time:disabled { color: var(--text-muted); background: #0a0d14; cursor: not-allowed; }
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
      <option value="de">🇩🇪 Deutsch</option>
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

  <div id="hopperLidBanner" style="display:none;background:rgba(245,158,11,0.15);border:1px solid var(--amber);border-radius:10px;padding:12px 16px;font-size:0.9rem;align-items:center;gap:12px">
    <span style="font-size:1.5rem">⚠️</span>
    <div>
      <b id="hopperLidBannerTitle">Trappe du réservoir à pellets ouverte</b>
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
    <div style="display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:12px;border-bottom:1px solid rgba(255,255,255,0.06);padding-bottom:12px">
      <div class="deck-title" style="margin:0">
        <span>🎛️</span> <span id="lblDeckTitle">Pilotage du Poêle</span>
      </div>
      <div style="display:flex;gap:6px;background:#10141e;padding:4px;border-radius:10px;border:1px solid var(--border)">
        <button type="button" class="subtab-btn active" id="subtabBtnDirect" onclick="showCtrlTab('ctrl-direct')">🔥 <span id="lblSubtabDirect">Commandes directes</span></button>
        <button type="button" class="subtab-btn" id="subtabBtnSched" onclick="showCtrlTab('ctrl-sched')">📅 <span id="lblSubtabSched">Programmation</span></button>
        <button type="button" class="subtab-btn" id="subtabBtnParams" onclick="showCtrlTab('ctrl-params')">⚙️ <span id="lblSubtabParams">Paramètres</span></button>
      </div>
    </div>

    <!-- Sub-tab 1: Commandes directes -->
    <div id="ctrl-direct" class="ctrl-content" style="display:flex;flex-direction:column;gap:20px">
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

      <!-- MultiAir Fans -->
      <div id="multiairDeck" style="display:none;border-top:1px solid rgba(255,255,255,0.06);padding-top:16px;flex-direction:column;gap:14px">
        <div style="display:flex;justify-content:space-between;align-items:center">
          <label id="lblMultiAirTitle" style="font-size:0.85rem;color:var(--text-dim);font-weight:600;letter-spacing:0.5px">VENTILATION MULTIAIR</label>
        </div>
        <div class="multiair-grid">
          <!-- Fan 1 -->
          <div class="fan-card" id="fanCard1">
            <div style="display:flex;justify-content:space-between;align-items:center">
              <div style="display:flex;align-items:center;gap:8px;font-weight:700">
                <span>🌀</span> <span id="lblFan1Title">MultiAir 1</span>
              </div>
              <button class="fan-toggle" id="fan1ToggleBtn" onclick="toggleFan(1)">
                <span class="dot" style="display:inline-block"></span>
                <span id="fan1ToggleText">Arrêt</span>
              </button>
            </div>

            <div>
              <div style="display:flex;justify-content:space-between;margin-bottom:6px">
                <span id="lblFan1Speed" style="font-size:0.8rem;color:var(--text-dim);font-weight:600">Vitesse</span>
                <span id="fan1SpeedText" style="font-size:0.85rem;font-weight:700;color:var(--text)">Auto</span>
              </div>
              <div class="fan-levels">
                <button class="btn-lvl active" id="f1Lvl0" onclick="setFanLevel(1, 0)">Auto</button>
                <button class="btn-lvl" id="f1Lvl1" onclick="setFanLevel(1, 1)">1</button>
                <button class="btn-lvl" id="f1Lvl2" onclick="setFanLevel(1, 2)">2</button>
                <button class="btn-lvl" id="f1Lvl3" onclick="setFanLevel(1, 3)">3</button>
                <button class="btn-lvl" id="f1Lvl4" onclick="setFanLevel(1, 4)">4</button>
                <button class="btn-lvl" id="f1Lvl5" onclick="setFanLevel(1, 5)">5</button>
              </div>
            </div>

            <div style="display:flex;flex-direction:column;gap:8px">
              <div style="display:flex;justify-content:space-between;align-items:baseline">
                <span id="lblFan1Area" style="font-size:0.8rem;color:var(--text-dim);font-weight:600">Correction convection</span>
                <span class="val" style="font-size:1.1rem;font-weight:700"><span id="fan1AreaVal">0</span> <small style="font-size:0.8rem;color:var(--text-muted)">%</small></span>
              </div>
              <input type="range" id="fan1AreaRange" min="-30" max="30" step="5" value="0" oninput="onFanAreaInput(1, this.value)">
              <button class="btn-apply" id="btnApplyFan1Area" onclick="applyFanArea(1)">Appliquer correction</button>
            </div>
          </div>

          <!-- Fan 2 -->
          <div class="fan-card" id="fanCard2">
            <div style="display:flex;justify-content:space-between;align-items:center">
              <div style="display:flex;align-items:center;gap:8px;font-weight:700">
                <span>🌀</span> <span id="lblFan2Title">MultiAir 2</span>
              </div>
              <button class="fan-toggle" id="fan2ToggleBtn" onclick="toggleFan(2)">
                <span class="dot" style="display:inline-block"></span>
                <span id="fan2ToggleText">Arrêt</span>
              </button>
            </div>

            <div>
              <div style="display:flex;justify-content:space-between;margin-bottom:6px">
                <span id="lblFan2Speed" style="font-size:0.8rem;color:var(--text-dim);font-weight:600">Vitesse</span>
                <span id="fan2SpeedText" style="font-size:0.85rem;font-weight:700;color:var(--text)">Auto</span>
              </div>
              <div class="fan-levels">
                <button class="btn-lvl active" id="f2Lvl0" onclick="setFanLevel(2, 0)">Auto</button>
                <button class="btn-lvl" id="f2Lvl1" onclick="setFanLevel(2, 1)">1</button>
                <button class="btn-lvl" id="f2Lvl2" onclick="setFanLevel(2, 2)">2</button>
                <button class="btn-lvl" id="f2Lvl3" onclick="setFanLevel(2, 3)">3</button>
                <button class="btn-lvl" id="f2Lvl4" onclick="setFanLevel(2, 4)">4</button>
                <button class="btn-lvl" id="f2Lvl5" onclick="setFanLevel(2, 5)">5</button>
              </div>
            </div>

            <div style="display:flex;flex-direction:column;gap:8px">
              <div style="display:flex;justify-content:space-between;align-items:baseline">
                <span id="lblFan2Area" style="font-size:0.8rem;color:var(--text-dim);font-weight:600">Correction convection</span>
                <span class="val" style="font-size:1.1rem;font-weight:700"><span id="fan2AreaVal">0</span> <small style="font-size:0.8rem;color:var(--text-muted)">%</small></span>
              </div>
              <input type="range" id="fan2AreaRange" min="-30" max="30" step="5" value="0" oninput="onFanAreaInput(2, this.value)">
              <button class="btn-apply" id="btnApplyFan2Area" onclick="applyFanArea(2)">Appliquer correction</button>
            </div>
          </div>
        </div>
      </div>

      <!-- Baking Oven (DOMO BACK) -->
      <div id="bakeDeck" style="border-top:1px solid rgba(255,255,255,0.06);padding-top:16px;display:none;flex-direction:column;gap:14px">
        <div style="display:flex;justify-content:space-between;align-items:center">
          <label id="lblBakeTitle" style="font-size:0.85rem;color:var(--text-dim);font-weight:600;letter-spacing:0.5px">FOUR DE CUISSON (DOMO BACK)</label>
        </div>
        <div class="fan-card" style="padding:16px">
          <div style="display:flex;justify-content:space-between;align-items:center">
            <div style="display:flex;align-items:center;gap:8px;font-weight:700">
              <span style="font-size:1.2rem">🍲</span> <span id="lblBakeSubTitle">Consigne température de cuisson</span>
            </div>
          </div>
          <div style="margin-top:14px;display:flex;flex-direction:column;gap:8px">
            <div style="display:flex;justify-content:space-between;align-items:baseline">
              <span id="lblBakeTemp" style="font-size:0.85rem;color:var(--text-dim);font-weight:600">Température du four</span>
              <span class="val" style="font-size:1.2rem;font-weight:700"><span id="bakeTempVal">180</span> <small style="font-size:0.85rem;color:var(--text-muted)">°C</small></span>
            </div>
            <input type="range" id="bakeTempRange" min="130" max="340" step="5" value="180" oninput="onBakeTempInput(this.value)">
            <button class="btn-apply" id="btnApplyBakeTemp" onclick="applyBakeTemp()">Appliquer consigne four</button>
          </div>
        </div>
      </div>
    </div>

    <!-- Sub-tab 2: Programmation -->
    <div id="ctrl-sched" class="ctrl-content" style="display:none;flex-direction:column;gap:16px">
      <div style="display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:10px">
        <label id="lblSchedSectionTitle" style="font-size:0.85rem;color:var(--text-dim);font-weight:600;letter-spacing:0.5px">PLANNING HEBDOMADAIRE</label>
        <label style="display:flex;align-items:center;gap:10px;cursor:pointer">
          <span id="lblSchedActiveTitle" style="font-weight:600;font-size:0.9rem">Programmation active</span>
          <label class="switch">
            <input type="checkbox" id="schedActiveToggle">
            <span class="slider-switch"></span>
          </label>
        </label>
      </div>
      <p id="lblSchedActiveDesc" style="font-size:0.82rem;color:var(--text-dim);margin-top:-8px">Active ou désactive le planning des plages horaires de chauffe.</p>

      <!-- Setback Temperature Slider -->
      <div class="slider-box">
        <div class="slider-head">
          <label id="lblSetbackTemp">Température de maintien (Éco)</label>
          <span class="val"><span id="setbackTempVal">16.0</span> <small style="font-size:0.9rem;color:var(--text-muted)">°C</small></span>
        </div>
        <input type="range" id="setbackTempRange" min="12" max="22" step="0.5" value="16" oninput="onSetbackInput(this.value)">
      </div>

      <!-- Quick Actions -->
      <div style="display:flex;gap:10px;flex-wrap:wrap;align-items:center">
        <span style="font-size:0.85rem;color:var(--text-dim)" id="lblQuickCopy">Actions rapides :</span>
        <button type="button" class="btn-lock" id="btnCopyWeekdays" onclick="copyMonday(false)">📋 Lun ➔ Lun-Ven</button>
        <button type="button" class="btn-lock" id="btnCopyAll" onclick="copyMonday(true)">📋 Lun ➔ Semaine</button>
      </div>

      <!-- Days Rows -->
      <div style="display:flex;flex-direction:column;gap:10px" id="schedDaysContainer"></div>

      <!-- Save Button -->
      <div style="display:flex;justify-content:flex-end;margin-top:8px">
        <button class="power-btn" id="btnSaveSchedule" onclick="saveSchedule()" style="width:100%;justify-content:center">
          💾 Enregistrer la programmation
        </button>
      </div>
    </div>

    <!-- Sub-tab 3: Paramètres -->
    <div id="ctrl-params" class="ctrl-content" style="display:none;flex-direction:column;gap:16px">
      <div style="display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:10px">
        <label id="lblParamsSectionTitle" style="font-size:0.85rem;color:var(--text-dim);font-weight:600;letter-spacing:0.5px">PARAMÈTRES AVANCÉS DU POÊLE</label>
      </div>

      <div class="multiair-grid">
        <!-- Frost Protection (Hors-Gel) -->
        <div class="fan-card" id="frostCard" style="padding:16px">
          <div style="display:flex;justify-content:space-between;align-items:center">
            <div style="display:flex;align-items:center;gap:8px;font-weight:700">
              <span style="font-size:1.2rem">❄️</span> <span id="lblFrostToggle">Protection hors-gel</span>
            </div>
            <button class="fan-toggle" id="frostToggleBtn" onclick="toggleFrost()">
              <span class="dot" style="display:inline-block"></span>
              <span id="frostToggleText">Arrêt</span>
            </button>
          </div>
          <div style="margin-top:14px;display:flex;flex-direction:column;gap:8px">
            <div style="display:flex;justify-content:space-between;align-items:baseline">
              <span id="lblFrostTemp" style="font-size:0.85rem;color:var(--text-dim);font-weight:600">Température de consigne</span>
              <span class="val" style="font-size:1.2rem;font-weight:700"><span id="frostTempVal">5</span> <small style="font-size:0.85rem;color:var(--text-muted)">°C</small></span>
            </div>
            <input type="range" id="frostTempRange" min="4" max="10" step="1" value="5" oninput="onFrostTempInput(this.value)">
            <button class="btn-apply" id="btnApplyFrostTemp" onclick="applyFrostTemp()">Appliquer hors-gel</button>
          </div>
        </div>

        <!-- Room Temperature Offset Calibration -->
        <div class="fan-card" id="offsetCard" style="padding:16px">
          <div style="display:flex;justify-content:space-between;align-items:center">
            <div style="display:flex;align-items:center;gap:8px;font-weight:700">
              <span style="font-size:1.2rem">🌡️</span> <span id="lblOffsetSubTitle">Calibrage sonde ambiance</span>
            </div>
          </div>
          <div style="margin-top:14px;display:flex;flex-direction:column;gap:8px">
            <div style="display:flex;justify-content:space-between;align-items:baseline">
              <span id="lblOffsetDesc" style="font-size:0.85rem;color:var(--text-dim);font-weight:600">Décalage appliqué</span>
              <span class="val" style="font-size:1.2rem;font-weight:700"><span id="roomOffsetVal">0.0</span> <small style="font-size:0.85rem;color:var(--text-muted)">°C</small></span>
            </div>
            <input type="range" id="roomOffsetRange" min="-4.0" max="4.0" step="0.1" value="0.0" oninput="onRoomOffsetInput(this.value)">
            <button class="btn-apply" id="btnApplyRoomOffset" onclick="applyRoomOffset()">Appliquer calibrage</button>
          </div>
        </div>
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
      <!-- Native HTML form POST (no fetch/confirm/alert): works inside captive
           portal browsers (macOS/iOS) that block XHR to local IPs. -->
      <form id="wifiForm" method="POST" action="/api/wifi" style="display:flex;gap:10px;flex-direction:column">
        <select class="input-text" id="ssidSelect" onchange="onSelectSsid(this.value)">
          <option value="">-- Choisir un réseau détecté --</option>
        </select>
        <div style="display:flex;gap:10px;flex-wrap:wrap">
          <input class="input-text" id="newSsid" name="ssid" placeholder="Nom du réseau (SSID)" style="flex:1;min-width:180px" required>
          <div style="flex:1;min-width:180px;position:relative;display:flex;align-items:center">
            <input class="input-text" id="newPass" name="pass" type="password" placeholder="Mot de passe" style="width:100%;padding-right:36px">
            <span style="position:absolute;right:10px;cursor:pointer;user-select:none;font-size:1.1rem" onclick="togglePassView()" title="Afficher/Masquer">👁️</span>
          </div>
        </div>
        <div style="display:flex;gap:10px;justify-content:flex-end">
          <button type="button" class="btn-lock" id="btnForgetWifi" style="color:#ef4444;border-color:#ef4444" onclick="forgetWifi()">Oublier le WiFi</button>
          <button type="submit" class="power-btn" id="btnSaveWifi" style="padding:8px 16px;font-size:0.9rem">Enregistrer & Redémarrer</button>
        </div>
        <p id="captiveHint" style="font-size:0.8rem;color:var(--text-dim);margin:2px 0 0 0;text-align:center">Si le bouton ne réagit pas, ouvrez http://192.168.4.1 dans Safari ou Chrome. · If the button does nothing, open http://192.168.4.1 in Safari or Chrome.</p>
      </form>
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
    bcpLanguageTag: 'fr-FR',
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
    deckTitle: "Pilotage du Poêle",
    subtabDirect: "Commandes directes",
    subtabSched: "Programmation",
    subtabParams: "Paramètres",
    paramsSectionTitle: "PARAMÈTRES AVANCÉS DU POÊLE",
    schedSectionTitle: "PLANNING HEBDOMADAIRE",
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
    multiAirTitle: "VENTILATION MULTIAIR",
    fan1Title: "MultiAir 1",
    fan2Title: "MultiAir 2",
    fanSpeed: "Vitesse",
    fanArea: "Correction convection",
    btnApplyFanArea: "Appliquer correction",
    fanOn: "Actif",
    fanOff: "Arrêt",
    fanAuto: "Auto",
    frostTitle: "PROTECTION HORS-GEL",
    frostToggle: "Hors-gel",
    frostTemp: "Température de consigne",
    btnApplyFrostTemp: "Appliquer hors-gel",
    frostOn: "Actif",
    frostOff: "Arrêt",
    bakeTitle: "FOUR DE CUISSON (DOMO BACK)",
    bakeSubTitle: "Consigne de cuisson",
    bakeTemp: "Température du four",
    btnApplyBakeTemp: "Appliquer consigne four",
    offsetTitle: "CALIBRAGE SONDE D'AMBIANCE",
    offsetSubTitle: "Correction température ambiante",
    offsetDesc: "Décalage appliqué",
    btnApplyRoomOffset: "Appliquer calibrage",
    tabTelemetry: "📊 Télémétrie complète",
    tabNetwork: "📶 Réseau & WiFi",
    tabLink: "⚙️ Liaison CDC",
    tabLogs: "📜 Logs CDC",
    schedActive: "Programmation active",
    schedActiveDesc: "Active ou désactive le planning des plages horaires de chauffe.",
    setbackTemp: "Température de maintien (Éco)",
    quickCopy: "Actions rapides :",
    btnCopyWeekdays: "📋 Lun ➔ Lun-Ven",
    btnCopyAll: "📋 Lun ➔ Semaine",
    copiedWeekdays: "Horaires copiés du Lundi au Vendredi",
    copiedAll: "Horaires copiés sur toute la semaine",
    btnSaveSchedule: "💾 Enregistrer la programmation",
    schedSaved: "Programmation enregistrée avec succès !",
    days: { Mon: "Lundi", Tue: "Mardi", Wed: "Mercredi", Thu: "Jeudi", Fri: "Vendredi", Sat: "Samedi", Sun: "Dimanche" },
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
    hopperLidBannerTitle: "Trappe du réservoir à pellets ouverte",
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
      appVersion: "Version application dongle",
      blVersion: "Version bootloader dongle",
      appRevision: "Révision application dongle",
      firmwareBuild: "Sous-version build",
      language: "Langue configurée (3 = FR)",
      rssi: "Signal radio WiFi (dBm)",
      errMask32: "Masque erreurs 32 bits",
      errSub: "Code sous-erreur active",
      serviceOffset: "Décalage compteur révision",
      serviceMinutes: "Minutes totales écoulées révision",
      ignitionCount: "Nombre d'allumages",
      onOffCycles: "Cycles marche/arrêt",
      hopperLidClosed: "Trappe réservoir pellets fermée",
      convectionFan1Active: "MultiAir 1 actif",
      convectionFan1Level: "MultiAir 1 vitesse (0=Auto, 1-5)",
      convectionFan1Area: "MultiAir 1 correction (%)",
      convectionFan2Active: "MultiAir 2 actif",
      convectionFan2Level: "MultiAir 2 vitesse (0=Auto, 1-5)",
      convectionFan2Area: "MultiAir 2 correction (%)",
      frostProtectionActive: "Protection hors-gel active",
      frostProtectionTemp: "Température hors-gel (°C ×10)",
      bakeTarget: "Consigne température four (°C)",
      roomTempOffset: "Calibrage sonde ambiance (°C ×10)"
    }
  },
  en: {
    bcpLanguageTag: 'en-US',
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
    deckTitle: "Stove Controls",
    subtabDirect: "Direct Controls",
    subtabSched: "Schedule",
    subtabParams: "Settings",
    paramsSectionTitle: "ADVANCED STOVE SETTINGS",
    schedSectionTitle: "WEEKLY SCHEDULE",
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
    multiAirTitle: "MULTIAIR VENTILATION",
    fan1Title: "MultiAir 1",
    fan2Title: "MultiAir 2",
    fanSpeed: "Speed",
    fanArea: "Convection correction",
    btnApplyFanArea: "Apply correction",
    fanOn: "Active",
    fanOff: "Off",
    fanAuto: "Auto",
    frostTitle: "FROST PROTECTION",
    frostToggle: "Frost protection",
    frostTemp: "Target temperature",
    btnApplyFrostTemp: "Apply frost temp",
    frostOn: "Active",
    frostOff: "Off",
    bakeTitle: "BAKING OVEN (DOMO BACK)",
    bakeSubTitle: "Baking setpoint",
    bakeTemp: "Oven temperature",
    btnApplyBakeTemp: "Apply oven target",
    offsetTitle: "ROOM SENSOR CALIBRATION",
    offsetSubTitle: "Room temperature offset",
    offsetDesc: "Applied calibration offset",
    btnApplyRoomOffset: "Apply calibration",
    tabTelemetry: "📊 Full Telemetry",
    tabNetwork: "📶 Network & WiFi",
    tabLink: "⚙️ USB CDC Link",
    tabLogs: "📜 CDC Logs",
    schedActive: "Heating schedule active",
    schedActiveDesc: "Enables or disables the weekly heating schedule.",
    setbackTemp: "Setback temperature (Eco)",
    quickCopy: "Quick actions:",
    btnCopyWeekdays: "📋 Mon ➔ Mon-Fri",
    btnCopyAll: "📋 Mon ➔ All week",
    copiedWeekdays: "Schedule copied to Monday-Friday",
    copiedAll: "Schedule copied to entire week",
    btnSaveSchedule: "💾 Save Heating Schedule",
    schedSaved: "Schedule saved successfully!",
    days: { Mon: "Monday", Tue: "Tuesday", Wed: "Wednesday", Thu: "Thursday", Fri: "Friday", Sat: "Saturday", Sun: "Sunday" },
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
    hopperLidBannerTitle: "Pellet hopper lid open",
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
      appVersion: "Dongle app version",
      blVersion: "Dongle bootloader version",
      appRevision: "Dongle app revision",
      firmwareBuild: "Build sub-version",
      language: "Configured language (3 = FR)",
      rssi: "WiFi signal strength (dBm)",
      errMask32: "Active error bitmask (32 bits)",
      errSub: "Active error subcode",
      serviceOffset: "Service counter offset",
      serviceMinutes: "Total elapsed service minutes",
      ignitionCount: "Total ignition count",
      onOffCycles: "Total on/off cycles",
      hopperLidClosed: "Pellet hopper lid closed",
      convectionFan1Active: "MultiAir 1 active",
      convectionFan1Level: "MultiAir 1 speed (0=Auto, 1-5)",
      convectionFan1Area: "MultiAir 1 correction (%)",
      convectionFan2Active: "MultiAir 2 active",
      convectionFan2Level: "MultiAir 2 speed (0=Auto, 1-5)",
      convectionFan2Area: "MultiAir 2 correction (%)",
      frostProtectionActive: "Frost protection active",
      frostProtectionTemp: "Frost protection temperature (°C ×10)",
      bakeTarget: "Bake target temperature (°C)",
      roomTempOffset: "Room sensor offset (°C ×10)"
    }
  },
  de: {
    bcpLanguageTag: 'de-DE',
    pageTitle: "Open-Firenet — Dashboard",
    langNext: "EN",
    online: "Online",
    waiting: "Warte auf Ofen",
    connected: "Ofen verbunden",
    offline: "Offline",
    commError: "Kommunikationsfehler",
    powerOn: "Einschalten",
    powerOff: "Ausschalten",
    confirmOn: "Möchten Sie den Ofen wirklich EINSCHALTEN?",
    confirmOff: "Möchten Sie den Ofen wirklich AUSSCHALTEN?",
    roomHeader: "Raumtemperatur",
    roomTargetPrefix: "Sollwert: ",
    flameHeader: "Brennkammer Temp.",
    flameSub: "Thermoelement-Sensor",
    flameLive: "Aktive Verbrennung",
    flameCombustion: "Verbrennung",
    flameCold: "Kalt / Inaktiv",
    powerHeader: "Leistung",
    modePrefix: "Modus: ",
    stageTgtPrefix: "Ziel: ",
    pelletsHeader: "Zähler & Wartung",
    serviceCountPrefix: "Wartung in: ",
    pelletHoursSuffix: " h Pellets",
    deckTitle: "Ofensteuerung",
    subtabDirect: "Direktsteuerung",
    subtabSched: "Heizzeiten",
    subtabParams: "Einstellungen",
    paramsSectionTitle: "ERWEITERTE OFEN-EINSTELLUNGEN",
    schedSectionTitle: "WÖCHENTLICHER ZEITPLAN",
    regulationMode: "REGELUNGSMODUS",
    modeTitle2: "Komfort",
    modeDesc2: "Raumtemperatursensor",
    modeTitle1: "Auto",
    modeDesc1: "Zeitgesteuerte Heizung",
    modeTitle0: "Manuell",
    modeDesc0: "Feste Leistung (%)",
    sliderTemp: "Raum-Sollwert (Komfort)",
    btnApplyTemp: "Temperatur übernehmen",
    sliderStage: "Leistung (Auto & Manuell)",
    btnApplyStage: "Leistung übernehmen",
    multiAirTitle: "MULTIAIR GEBLÄSE",
    fan1Title: "MultiAir 1",
    fan2Title: "MultiAir 2",
    fanSpeed: "Stufe",
    fanArea: "Konvektions-Korrektur",
    btnApplyFanArea: "Korrektur übernehmen",
    fanOn: "Aktiv",
    fanOff: "Aus",
    fanAuto: "Auto",
    frostTitle: "FROSTSCHUTZ",
    frostToggle: "Frostschutz",
    frostTemp: "Soll-Temperatur",
    btnApplyFrostTemp: "Frostschutz übernehmen",
    frostOn: "Aktiv",
    frostOff: "Aus",
    bakeTitle: "BACKOFEN (DOMO BACK)",
    bakeSubTitle: "Backtemperatur-Sollwert",
    bakeTemp: "Backofentemperatur",
    btnApplyBakeTemp: "Backtemperatur übernehmen",
    offsetTitle: "RAUMFÜHLER-KALIBRIERUNG",
    offsetSubTitle: "Raumtemperatur-Korrektur",
    offsetDesc: "Angewendeter Offset",
    btnApplyRoomOffset: "Kalibrierung übernehmen",
    tabTelemetry: "📊 Vollständige Telemetrie",
    tabNetwork: "📶 Netzwerk & WLAN",
    tabLink: "⚙️ USB CDC Verbindung",
    tabLogs: "📜 CDC Protokolle",
    schedActive: "Heizzeiten aktiv",
    schedActiveDesc: "Aktiviert oder deaktiviert den wöchentlichen Heizzeitplan.",
    setbackTemp: "Absenktemperatur (Eco)",
    quickCopy: "Schnellaktionen:",
    btnCopyWeekdays: "📋 Mo ➔ Mo-Fr",
    btnCopyAll: "📋 Mo ➔ Ganze Woche",
    copiedWeekdays: "Heizzeiten auf Montag-Freitag kopiert",
    copiedAll: "Heizzeiten auf ganze Woche kopiert",
    btnSaveSchedule: "💾 Heizzeiten speichern",
    schedSaved: "Heizzeiten erfolgreich gespeichert!",
    days: { Mon: "Montag", Tue: "Dienstag", Wed: "Mittwoch", Thu: "Donnerstag", Fri: "Freitag", Sat: "Samstag", Sun: "Sonntag" },
    logRx: "Ofen → Dongle (RX)",
    logTx: "Dongle → Ofen (TX)",
    logAuto: "Auto",
    logDownload: "⬇ Herunterladen",
    logClear: "Löschen",
    btnRestart: "🔄 Dongle neustarten",
    confirmRestart: "Dongle neustarten? Die Verbindung zum Ofen wird für einige Sekunden unterbrochen.",
    restartToast: "Neustart läuft…",
    sensorSearchPlaceholder: "🔍 53 Sensoren filtern...",
    thSensorName: "Sensor",
    thSensorId: "Kennung",
    thSensorVal: "Wert",
    netTitle: "Netzwerkkonfiguration",
    netModePrefix: "Modus: ",
    netRssiPrefix: "RSSI Signal: ",
    joinWifi: "WLAN-Netzwerk beitreten (2,4 GHz):",
    btnScan: "🔄 Scannen",
    selectNetwork: "-- Erkanntes Netzwerk auswählen --",
    scanning: "2,4 GHz Netzwerke werden gescannt...",
    scanError: "Fehler beim WLAN-Scan",
    apBannerTitle: "Access Point Modus aktiv",
    apBannerDesc: "Wählen Sie unten Ihr WLAN aus, um Open-Firenet mit Ihrem Router zu verbinden.",
    hopperLidBannerTitle: "Pelletbehälter-Deckel offen",
    ssidPlaceholder: "Netzwerkname (SSID)",
    passPlaceholder: "Passwort",
    btnForgetWifi: "WLAN vergessen",
    btnSaveWifi: "Speichern & Neustarten",
    cdcPort: "USB CDC Serielle Verbindung",
    cdcAck: "Ofenversion bestätigt",
    cdcGen: "Protokollgeneration",
    cdcRev: "Aktuelle Ofenrevision",
    cdcIn: "Eingehende Rahmen (IN)",
    cdcOut: "Ausgehende Rahmen (OUT)",
    cdcSpeed: "Betriebsbereit (Full Speed)",
    lblUptime: "Dongle Betriebszeit",
    lblNetUptimePrefix: "Betriebszeit: ",
    yes: "Ja",
    no: "Nein",
    confirmSaveWifi: "Der Dongle wird neu gestartet und versucht, sich zu verbinden mit ",
    saveWifiSuccess: "Einstellungen gespeichert! Der Dongle startet neu.\n\nVerbinden Sie Ihr Gerät wieder mit Ihrem heimischen WLAN und gehen Sie zu:\nhttp://open-firenet.local",
    rebootTitle: "Verbindung zum Heimnetzwerk wird hergestellt...",
    rebootDesc: "Der Dongle startet neu, um eine Verbindung zu Ihrem WLAN-Router herzustellen. Verbinden Sie Ihr Smartphone oder Ihren PC wieder mit Ihrem Heimnetzwerk und rufen Sie dann das Dashboard auf:",
    confirmForgetWifi: "WLAN-Einstellungen löschen und zurück in den Access Point Modus wechseln?",
    forgetWifiSuccess: "WLAN gelöscht. Dongle startet im AP-Modus neu.",
    orderSent: "Befehl gesendet: ",
    errorPrefix: "Fehler: ",
    netError: "Netzwerkfehler",
    stateMap: {
      0: { title: "Aus", desc: "Ofen komplett ausgeschaltet", icon: "🛑", active: false },
      1: { title: "Bereitschaft (Standby)", desc: "Wartet auf Wärmeanforderung", icon: "⚪", active: false },
      2: { title: "Zündung", desc: "Glühkerze & Pellets werden vorgeheizt", icon: "🔥", active: true },
      3: { title: "Flammenstart", desc: "Verbrennung wird stabilisiert", icon: "🔥", active: true },
      4: { title: "Heizen", desc: "Aktive geregelte Verbrennung", icon: "🔥", active: true },
      5: { title: "Rostreinigung", desc: "Automatischer Entaschungszyklus", icon: "🧹", active: true },
      6: { title: "Abbrand (Burn off)", desc: "Abkühlung & Nachbelüftung", icon: "💨", active: true }
    },
    modeMap: { 0: "Manuell (%)", 1: "Automatisch", 2: "Komfort" },
    sensorDesc: {
      roomTemp: "Gemessene Raumtemperatur (°C)",
      flame: "Brennkammer Temp. (°C)",
      boardSensor: "Hauptplatine Temperatur (°C)",
      pelletsTotal: "Gesamter Pelletverbrauch (kg)",
      pelletHours: "Pellet-Betriebsstunden gesamt (h)",
      serviceCountdown: "Wartungs-Countdown (kg)",
      mainState: "Hauptbetriebszustand",
      subState: "Detailierter Unterzustand",
      stageCur: "Aktuelle Leistung (%)",
      stageCur1: "Angeforderte Leistung (%)",
      stageTgt2: "Zielleistung (%)",
      idFanMeas: "Rauchsauger-Drehzahl (RPM)",
      idFanSet: "Rauchsauger-Sollwert (RPM)",
      augerSet: "Pelletförderschnecke Sollwert (RPM)",
      model: "Ofenmodell",
      appVerBoard: "Firmware-Version Hauptplatine",
      appVersion: "Dongle App-Version",
      blVersion: "Dongle Bootloader-Version",
      appRevision: "Dongle App-Revision",
      firmwareBuild: "Build-Unterversion",
      language: "Konfigurierte Sprache (3 = FR)",
      rssi: "WLAN-Signalstärke (dBm)",
      errMask32: "Aktive Fehlerbitmaske (32 Bit)",
      errSub: "Aktiver Fehler-Untercode",
      serviceOffset: "Wartungszähler-Offset",
      serviceMinutes: "Gesamte vergangene Wartungsminuten",
      ignitionCount: "Anzahl Zündungen gesamt",
      onOffCycles: "Anzahl Ein/Aus-Zyklen gesamt",
      hopperLidClosed: "Pelletbehälter-Deckel geschlossen",
      convectionFan1Active: "MultiAir 1 aktiv",
      convectionFan1Level: "MultiAir 1 Stufe (0=Auto, 1-5)",
      convectionFan1Area: "MultiAir 1 Korrektur (%)",
      convectionFan2Active: "MultiAir 2 aktiv",
      convectionFan2Level: "MultiAir 2 Stufe (0=Auto, 1-5)",
      convectionFan2Area: "MultiAir 2 Korrektur (%)",
      frostProtectionActive: "Frostschutz aktiv",
      frostProtectionTemp: "Frostschutz-Temperatur (°C ×10)",
      bakeTarget: "Backofen-Solltemperatur (°C)",
      roomTempOffset: "Raumfühler-Offset (°C ×10)"
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
  if (document.getElementById('lblDeckTitle')) document.getElementById('lblDeckTitle').textContent = t.deckTitle;
  if (document.getElementById('lblSubtabDirect')) document.getElementById('lblSubtabDirect').textContent = t.subtabDirect;
  if (document.getElementById('lblSubtabSched')) document.getElementById('lblSubtabSched').textContent = t.subtabSched;
  if (document.getElementById('lblSubtabParams')) document.getElementById('lblSubtabParams').textContent = t.subtabParams;
  if (document.getElementById('lblSchedSectionTitle')) document.getElementById('lblSchedSectionTitle').textContent = t.schedSectionTitle;
  if (document.getElementById('lblParamsSectionTitle')) document.getElementById('lblParamsSectionTitle').textContent = t.paramsSectionTitle;
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
  if (document.getElementById('lblMultiAirTitle')) document.getElementById('lblMultiAirTitle').textContent = t.multiAirTitle;
  if (document.getElementById('lblFan1Title')) document.getElementById('lblFan1Title').textContent = t.fan1Title;
  if (document.getElementById('lblFan2Title')) document.getElementById('lblFan2Title').textContent = t.fan2Title;
  if (document.getElementById('lblFan1Speed')) document.getElementById('lblFan1Speed').textContent = t.fanSpeed;
  if (document.getElementById('lblFan2Speed')) document.getElementById('lblFan2Speed').textContent = t.fanSpeed;
  if (document.getElementById('lblFan1Area')) document.getElementById('lblFan1Area').textContent = t.fanArea;
  if (document.getElementById('lblFan2Area')) document.getElementById('lblFan2Area').textContent = t.fanArea;
  if (document.getElementById('btnApplyFan1Area')) document.getElementById('btnApplyFan1Area').textContent = t.btnApplyFanArea;
  if (document.getElementById('btnApplyFan2Area')) document.getElementById('btnApplyFan2Area').textContent = t.btnApplyFanArea;
  if (document.getElementById('f1Lvl0')) document.getElementById('f1Lvl0').textContent = t.fanAuto;
  if (document.getElementById('f2Lvl0')) document.getElementById('f2Lvl0').textContent = t.fanAuto;
  if (document.getElementById('lblFrostTitle')) document.getElementById('lblFrostTitle').textContent = t.frostTitle;
  if (document.getElementById('lblFrostToggle')) document.getElementById('lblFrostToggle').textContent = t.frostToggle;
  if (document.getElementById('lblFrostTemp')) document.getElementById('lblFrostTemp').textContent = t.frostTemp;
  if (document.getElementById('btnApplyFrostTemp')) document.getElementById('btnApplyFrostTemp').textContent = t.btnApplyFrostTemp;
  if (document.getElementById('lblBakeTitle')) document.getElementById('lblBakeTitle').textContent = t.bakeTitle;
  if (document.getElementById('lblBakeSubTitle')) document.getElementById('lblBakeSubTitle').textContent = t.bakeSubTitle;
  if (document.getElementById('lblBakeTemp')) document.getElementById('lblBakeTemp').textContent = t.bakeTemp;
  if (document.getElementById('btnApplyBakeTemp')) document.getElementById('btnApplyBakeTemp').textContent = t.btnApplyBakeTemp;
  if (document.getElementById('lblOffsetTitle')) document.getElementById('lblOffsetTitle').textContent = t.offsetTitle;
  if (document.getElementById('lblOffsetSubTitle')) document.getElementById('lblOffsetSubTitle').textContent = t.offsetSubTitle;
  if (document.getElementById('lblOffsetDesc')) document.getElementById('lblOffsetDesc').textContent = t.offsetDesc;
  if (document.getElementById('btnApplyRoomOffset')) document.getElementById('btnApplyRoomOffset').textContent = t.btnApplyRoomOffset;
  document.getElementById('tabBtnTelemetry').textContent = t.tabTelemetry;
  document.getElementById('tabBtnNetwork').textContent = t.tabNetwork;
  document.getElementById('tabBtnLink').textContent = t.tabLink;
  document.getElementById('tabBtnLogs').textContent = t.tabLogs;
  if (document.getElementById('lblSchedActiveTitle')) document.getElementById('lblSchedActiveTitle').textContent = t.schedActive;
  if (document.getElementById('lblSchedActiveDesc')) document.getElementById('lblSchedActiveDesc').textContent = t.schedActiveDesc;
  if (document.getElementById('lblSetbackTemp')) document.getElementById('lblSetbackTemp').textContent = t.setbackTemp;
  if (document.getElementById('lblQuickCopy')) document.getElementById('lblQuickCopy').textContent = t.quickCopy;
  if (document.getElementById('btnCopyWeekdays')) document.getElementById('btnCopyWeekdays').textContent = t.btnCopyWeekdays;
  if (document.getElementById('btnCopyAll')) document.getElementById('btnCopyAll').textContent = t.btnCopyAll;
  if (document.getElementById('btnSaveSchedule')) document.getElementById('btnSaveSchedule').textContent = t.btnSaveSchedule;
  if (t.days) {
    for (const k in t.days) {
      const el = document.getElementById('dayName_' + k);
      if (el) el.textContent = t.days[k];
    }
  }
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
  if (document.getElementById('hopperLidBannerTitle')) document.getElementById('hopperLidBannerTitle').textContent = t.hopperLidBannerTitle;
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
  curLang = (l === 'fr' || l === 'en' || l === 'de') ? l : 'fr';
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

function showCtrlTab(id) {
  document.querySelectorAll('.subtab-btn').forEach(b => b.classList.remove('active'));
  document.querySelectorAll('.ctrl-content').forEach(c => c.style.display = 'none');
  if (id === 'ctrl-sched') {
    const btn = document.getElementById('subtabBtnSched');
    if (btn) btn.classList.add('active');
    const panel = document.getElementById('ctrl-sched');
    if (panel) panel.style.display = 'flex';
    initScheduleUI();
    loadSchedule();
  } else if (id === 'ctrl-params') {
    const btn = document.getElementById('subtabBtnParams');
    if (btn) btn.classList.add('active');
    const panel = document.getElementById('ctrl-params');
    if (panel) panel.style.display = 'flex';
  } else {
    const btn = document.getElementById('subtabBtnDirect');
    if (btn) btn.classList.add('active');
    const panel = document.getElementById('ctrl-direct');
    if (panel) panel.style.display = 'flex';
  }
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

function toggleFan(n) {
  if (!lastState) return;
  const ctrl = lastState.controls || {};
  const cPos = lastState.controls_pos || [];
  let curOn = 0;
  if (n === 1) {
    curOn = (ctrl.convection_fan1_active !== undefined) ? (ctrl.convection_fan1_active ? 1 : 0) : (cPos.length > 23 ? cPos[23] : 0);
    sendControl("convectionFan1Active", curOn ? 0 : 1);
  } else {
    curOn = (ctrl.convection_fan2_active !== undefined) ? (ctrl.convection_fan2_active ? 1 : 0) : (cPos.length > 26 ? cPos[26] : 0);
    sendControl("convectionFan2Active", curOn ? 0 : 1);
  }
}

function setFanLevel(n, lvl) {
  if (n === 1) {
    sendControl("convectionFan1Level", lvl);
  } else {
    sendControl("convectionFan2Level", lvl);
  }
}

function onFanAreaInput(n, val) {
  const el = document.getElementById('fan' + n + 'AreaVal');
  if (el) el.textContent = (val > 0 ? '+' : '') + val;
  setInteracting();
}

function applyFanArea(n) {
  const r = document.getElementById('fan' + n + 'AreaRange');
  if (!r) return;
  const val = parseInt(r.value, 10);
  if (n === 1) {
    sendControl("convectionFan1Area", val);
  } else {
    sendControl("convectionFan2Area", val);
  }
}

function toggleFrost() {
  if (!lastState) return;
  const ctrl = lastState.controls || {};
  const cPos = lastState.controls_pos || [];
  const curOn = (ctrl.frost_protection_active !== undefined) ? (ctrl.frost_protection_active ? 1 : 0) :
                ((ctrl.frostProtectionActive !== undefined) ? (ctrl.frostProtectionActive ? 1 : 0) :
                (cPos.length > 29 ? cPos[29] : 0));
  sendControl("frostProtectionActive", curOn ? 0 : 1);
}

function onFrostTempInput(val) {
  const el = document.getElementById('frostTempVal');
  if (el) el.textContent = parseInt(val, 10);
  setInteracting();
}

function applyFrostTemp() {
  const r = document.getElementById('frostTempRange');
  if (!r) return;
  const val = parseInt(r.value, 10);
  sendControl("frostProtectionTemp", val * 10);
}

function onBakeTempInput(val) {
  const el = document.getElementById('bakeTempVal');
  if (el) el.textContent = parseInt(val, 10);
  setInteracting();
}

function applyBakeTemp() {
  const r = document.getElementById('bakeTempRange');
  if (!r) return;
  const val = parseInt(r.value, 10);
  sendControl("bakeTarget", val);
}

function onRoomOffsetInput(val) {
  const el = document.getElementById('roomOffsetVal');
  const num = parseFloat(val);
  if (el) el.textContent = (num > 0 ? '+' : '') + num.toFixed(1);
  setInteracting();
}

function applyRoomOffset() {
  const r = document.getElementById('roomOffsetRange');
  if (!r) return;
  const val = parseFloat(r.value);
  sendControl("roomTempOffset", Math.round(val * 10));
}

// --- Programmation hebdomadaire (Heating Schedule) -------------------------
const DAYS = [
  { key: 'Mon', fr: 'Lundi', en: 'Monday', de: 'Montag', idx1: 7, idx2: 8 },
  { key: 'Tue', fr: 'Mardi', en: 'Tuesday', de: 'Dienstag', idx1: 9, idx2: 10 },
  { key: 'Wed', fr: 'Mercredi', en: 'Wednesday', de: 'Mittwoch', idx1: 11, idx2: 12 },
  { key: 'Thu', fr: 'Jeudi', en: 'Thursday', de: 'Donnerstag', idx1: 13, idx2: 14 },
  { key: 'Fri', fr: 'Vendredi', en: 'Friday', de: 'Freitag', idx1: 15, idx2: 16 },
  { key: 'Sat', fr: 'Samedi', en: 'Saturday', de: 'Samstag', idx1: 17, idx2: 18 },
  { key: 'Sun', fr: 'Dimanche', en: 'Sunday', de: 'Sonntag', idx1: 19, idx2: 20 }
];
let scheduleLoaded = false;

function decodeSlot(val) {
  if (!val || val <= 0) return { enabled: false, start: '06:00', end: '22:00' };
  const v = parseInt(val, 10);
  const sVal = Math.floor(v / 10000);
  const eVal = v % 10000;
  const sH = String(Math.floor(sVal / 100)).padStart(2, '0');
  const sM = String(sVal % 100).padStart(2, '0');
  const eH = String(Math.floor(eVal / 100)).padStart(2, '0');
  const eM = String(eVal % 100).padStart(2, '0');
  return { enabled: true, start: sH + ':' + sM, end: eH + ':' + eM };
}

function encodeSlot(enabled, startStr, endStr) {
  if (!enabled) return 0;
  const sParts = (startStr || '00:00').split(':');
  const eParts = (endStr || '00:00').split(':');
  const sH = parseInt(sParts[0], 10) || 0;
  const sM = parseInt(sParts[1], 10) || 0;
  const eH = parseInt(eParts[0], 10) || 0;
  const eM = parseInt(eParts[1], 10) || 0;
  return (sH * 100 + sM) * 10000 + (eH * 100 + eM);
}

function initScheduleUI() {
  const cont = document.getElementById('schedDaysContainer');
  if (!cont || cont.children.length > 0) return;
  const t = I18N[curLang] || I18N.fr;
  let html = '';
  DAYS.forEach(d => {
    const dName = (t.days && t.days[d.key]) ? t.days[d.key] : d.fr;
    html += '<div class="sched-day-row">';
    html += '  <div class="sched-day-name" id="dayName_' + d.key + '">' + dName + '</div>';
    html += '  <div class="sched-day-slots">';
    html += '    <div class="sched-slot-box disabled" id="box_' + d.key + '1">';
    html += '      <label class="slot-chk-label">';
    html += '        <input type="checkbox" id="chk_' + d.key + '1" onchange="onSlotToggle(\'' + d.key + '\', 1)">';
    html += '        <span class="slot-pill">P1</span>';
    html += '      </label>';
    html += '      <input type="time" class="input-time" id="start_' + d.key + '1" value="06:00" step="60" disabled onchange="setInteracting()">';
    html += '      <span class="slot-arrow">➔</span>';
    html += '      <input type="time" class="input-time" id="end_' + d.key + '1" value="09:00" step="60" disabled onchange="setInteracting()">';
    html += '    </div>';
    html += '    <div class="sched-slot-box disabled" id="box_' + d.key + '2">';
    html += '      <label class="slot-chk-label">';
    html += '        <input type="checkbox" id="chk_' + d.key + '2" onchange="onSlotToggle(\'' + d.key + '\', 2)">';
    html += '        <span class="slot-pill">P2</span>';
    html += '      </label>';
    html += '      <input type="time" class="input-time" id="start_' + d.key + '2" value="17:00" step="60" disabled onchange="setInteracting()">';
    html += '      <span class="slot-arrow">➔</span>';
    html += '      <input type="time" class="input-time" id="end_' + d.key + '2" value="22:00" step="60" disabled onchange="setInteracting()">';
    html += '    </div>';
    html += '  </div>';
    html += '</div>';
  });
  cont.innerHTML = html;
}

function onSlotToggle(dayKey, slotNum) {
  const chk = document.getElementById('chk_' + dayKey + slotNum);
  const sIn = document.getElementById('start_' + dayKey + slotNum);
  const eIn = document.getElementById('end_' + dayKey + slotNum);
  const box = document.getElementById('box_' + dayKey + slotNum);
  if (!chk) return;
  if (sIn) sIn.disabled = !chk.checked;
  if (eIn) eIn.disabled = !chk.checked;
  if (box) box.classList.toggle('disabled', !chk.checked);
  setInteracting();
}

function onSetbackInput(v) {
  const el = document.getElementById('setbackTempVal');
  if (el) el.textContent = parseFloat(v).toFixed(1);
  setInteracting();
}

function copyMonday(all) {
  const t = I18N[curLang] || I18N.fr;
  const chk1 = document.getElementById('chk_Mon1').checked;
  const s1 = document.getElementById('start_Mon1').value;
  const e1 = document.getElementById('end_Mon1').value;

  const chk2 = document.getElementById('chk_Mon2').checked;
  const s2 = document.getElementById('start_Mon2').value;
  const e2 = document.getElementById('end_Mon2').value;

  const targets = all ? ['Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'] : ['Tue', 'Wed', 'Thu', 'Fri'];
  targets.forEach(k => {
    const c1 = document.getElementById('chk_' + k + '1');
    const st1 = document.getElementById('start_' + k + '1');
    const en1 = document.getElementById('end_' + k + '1');
    if (c1 && st1 && en1) {
      c1.checked = chk1;
      st1.value = s1;
      en1.value = e1;
      onSlotToggle(k, 1);
    }
    const c2 = document.getElementById('chk_' + k + '2');
    const st2 = document.getElementById('start_' + k + '2');
    const en2 = document.getElementById('end_' + k + '2');
    if (c2 && st2 && en2) {
      c2.checked = chk2;
      st2.value = s2;
      en2.value = e2;
      onSlotToggle(k, 2);
    }
  });
  setInteracting();
  toast(all ? t.copiedAll : t.copiedWeekdays);
}

function populateScheduleUI(data) {
  initScheduleUI();
  const isAct = data.active !== undefined ? data.active : (data.heatingTimesActive == 1);
  const actToggle = document.getElementById('schedActiveToggle');
  if (actToggle) actToggle.checked = isAct;

  let sb = 16.0;
  if (data.setback_temperature !== undefined) sb = data.setback_temperature;
  else if (data.setBackTemp !== undefined) sb = data.setBackTemp / 10.0;
  const sbRange = document.getElementById('setbackTempRange');
  const sbVal = document.getElementById('setbackTempVal');
  if (sbRange) sbRange.value = sb;
  if (sbVal) sbVal.textContent = parseFloat(sb).toFixed(1);

  const slots = data.slots || {};
  DAYS.forEach(d => {
    for (let s = 1; s <= 2; s++) {
      const key = 'heatTime' + d.key + s;
      const val = slots[key] || 0;
      const dec = decodeSlot(val);
      const chk = document.getElementById('chk_' + d.key + s);
      const sIn = document.getElementById('start_' + d.key + s);
      const eIn = document.getElementById('end_' + d.key + s);
      if (chk && sIn && eIn) {
        chk.checked = dec.enabled;
        sIn.value = dec.start;
        eIn.value = dec.end;
        onSlotToggle(d.key, s);
      }
    }
  });
}

async function loadSchedule(force = false) {
  initScheduleUI();
  if (scheduleLoaded && !force) return;
  try {
    const res = await fetch('/api/schedule');
    if (res.ok) {
      const data = await res.json();
      populateScheduleUI(data);
      scheduleLoaded = true;
      return;
    }
  } catch(e) {}

  if (lastState && lastState.controls_pos && lastState.controls_pos.length >= 23) {
    const cp = lastState.controls_pos;
    const slots = {};
    DAYS.forEach(d => {
      slots['heatTime' + d.key + '1'] = cp[d.idx1] || 0;
      slots['heatTime' + d.key + '2'] = cp[d.idx2] || 0;
    });
    populateScheduleUI({
      active: cp[21] == 1,
      heatingTimesActive: cp[21],
      setBackTemp: cp[22] || 160,
      setback_temperature: (cp[22] || 160) / 10.0,
      slots: slots
    });
    scheduleLoaded = true;
  }
}

async function saveSchedule() {
  const t = I18N[curLang] || I18N.fr;
  const isHtActive = document.getElementById('schedActiveToggle').checked ? 1 : 0;
  const sbVal = parseFloat(document.getElementById('setbackTempRange').value);
  const payload = {
    heatingTimesActive: isHtActive,
    setBackTemp: Math.round(sbVal * 10)
  };
  DAYS.forEach(d => {
    const c1 = document.getElementById('chk_' + d.key + '1');
    const s1 = document.getElementById('start_' + d.key + '1');
    const e1 = document.getElementById('end_' + d.key + '1');
    payload['heatTime' + d.key + '1'] = encodeSlot(c1 ? c1.checked : false, s1 ? s1.value : '06:00', e1 ? e1.value : '09:00');

    const c2 = document.getElementById('chk_' + d.key + '2');
    const s2 = document.getElementById('start_' + d.key + '2');
    const e2 = document.getElementById('end_' + d.key + '2');
    payload['heatTime' + d.key + '2'] = encodeSlot(c2 ? c2.checked : false, s2 ? s2.value : '17:00', e2 ? e2.value : '22:00');
  });

  const btn = document.getElementById('btnSaveSchedule');
  if (btn) btn.disabled = true;
  try {
    const res = await fetch('/api/schedule', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
    if (res.ok) {
      toast(t.schedSaved);
      scheduleLoaded = false;
      setTimeout(() => loadSchedule(true), 800);
      setTimeout(tick, 300);
    } else {
      const err = await res.json();
      toast(t.errorPrefix + (err.error || 'Erreur'));
    }
  } catch(e) {
    toast(t.netError);
  } finally {
    if (btn) btn.disabled = false;
  }
}

// WiFi credentials are submitted by the native <form> POST to /api/wifi (see the
// Network tab). The board saves them and returns a reboot confirmation page. A form
// submission is used instead of fetch()/confirm()/alert() because captive portal
// browsers (macOS/iOS) block XHR to local IPs and suppress those dialogs (issue #8).

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
      const mNames = {
        1: 'INDUO (1)', 2: 'TOPO (2)', 3: 'ROCO (3)', 4: 'ROCO MULTIAIR (4)', 5: 'ROCO RAO (5)',
        6: 'KAPO (6)', 7: 'MIRO (7)', 8: 'COMO (8)', 9: 'REVO (9)', 10: 'INTERNO (10)', 11: 'FILO (11)',
        12: 'SUMO (12)', 13: 'DOMO (13)', 14: 'CORSO (14)', 15: 'INDUO II (15)', 16: 'REVIVO (16)', 17: 'PARO (17)',
        18: 'LIVO (18)', 19: 'COMO II (19)', 20: 'REVO II (20)', 21: 'COSMO (21)', 22: 'SONO (22)',
        23: 'DOMO BACK (23)', 24: 'PK E (24)', 25: 'SUMO MULTIAIR (25)', 26: 'CONNECT (26)'
      };
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
  const bcpLanguageTag = t.bcpLanguageTag || 'en-US';
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

    if (rawS.hopperLidClosed !== undefined) {
      document.getElementById('hopperLidBanner').style.display = (rawS.hopperLidClosed === 0) ? 'flex' : 'none';
    }

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
      document.getElementById('roomTemp').textContent = sens.room_temperature.toLocaleString(bcpLanguageTag, { minimumFractionDigits: 1, maximumFractionDigits: 1 });
    } else if (rawS.roomTemp !== undefined) {
      document.getElementById('roomTemp').textContent = (rawS.roomTemp / 10).toLocaleString(bcpLanguageTag, { minimumFractionDigits: 1, maximumFractionDigits: 1 });
    }
    const rTgtVal = ctrl.target_temperature !== undefined ? Math.round(ctrl.target_temperature) : (ctrl.roomTarget !== undefined ? Math.round(ctrl.roomTarget / 10) : (s.controls_pos && s.controls_pos.length > 4 ? Math.round(s.controls_pos[4] / 10) : 20));
    document.getElementById('roomTarget').textContent = rTgtVal;

    if (!userInteracting) {
      document.getElementById('tempRange').value = rTgtVal;
      document.getElementById('sliderTempVal').textContent = rTgtVal;
    }

    // Flame / Combustion Chamber
    const flVal = sens.combustion_temperature !== undefined ? Math.round(sens.combustion_temperature) : (rawS.flame !== undefined ? rawS.flame : 0);
    document.getElementById('flameTemp').textContent = flVal.toLocaleString(bcpLanguageTag);
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
    document.getElementById('pelletsTotal').textContent = pTot.toLocaleString(bcpLanguageTag);
    const pHours = sens.pellet_hours !== undefined ? sens.pellet_hours : (rawS.pelletHours !== undefined ? rawS.pelletHours : (s.sensors_pos ? s.sensors_pos[47] : 0));
    document.getElementById('pelletHours').textContent = pHours.toLocaleString(bcpLanguageTag);
    const sCount = sens.service_countdown_kg !== undefined ? sens.service_countdown_kg : (rawS.serviceCountdown !== undefined ? rawS.serviceCountdown : (s.sensors_pos ? s.sensors_pos[50] : 700));
    document.getElementById('serviceCount').textContent = sCount.toLocaleString(bcpLanguageTag);
    const sPct = Math.min(100, Math.max(0, Math.round((sCount / 700) * 100)));
    document.getElementById('serviceBar').style.width = sPct + '%';

    // Model & Net
    const modelNames = {
      1: 'INDUO', 2: 'TOPO', 3: 'ROCO', 4: 'ROCO MULTIAIR', 5: 'ROCO RAO',
      6: 'KAPO', 7: 'MIRO', 8: 'COMO', 9: 'REVO', 10: 'INTERNO', 11: 'FILO',
      12: 'SUMO', 13: 'DOMO', 14: 'CORSO', 15: 'INDUO II', 16: 'REVIVO', 17: 'PARO',
      18: 'LIVO', 19: 'COMO II', 20: 'REVO II', 21: 'COSMO', 22: 'SONO',
      23: 'DOMO BACK', 24: 'PK E', 25: 'SUMO MULTIAIR', 26: 'CONNECT'
    };
    const mId = stObj.model !== undefined ? stObj.model : (rawS.model !== undefined ? rawS.model : 13);
    const mFallback = (curLang === 'fr' ? 'Modèle ' : 'Model ') + mId;
    const mName = stObj.model_name || modelNames[mId] || mFallback;
    const vStr = stObj.mainboard_version ? (' V' + stObj.mainboard_version) : '';
    document.getElementById('modelBadge').textContent = mName + vStr;

    // MultiAir (modèles RIKA équipés : 4=ROCO MA, 13=DOMO, 17=PARO, 23=DOMO BACK, 25=SUMO MA)
    const deck = document.getElementById('multiairDeck');
    const MULTIAIR_MODELS = [4, 13, 17, 23, 25];
    const hasMultiAir = MULTIAIR_MODELS.includes(mId);
    if (deck) {
      deck.style.display = hasMultiAir ? 'flex' : 'none';
      if (hasMultiAir) {
        // Fan 1
        const f1Active = (ctrl.convection_fan1_active !== undefined) ? (ctrl.convection_fan1_active ? 1 : 0) :
                         (s.controls_pos && s.controls_pos.length > 23 ? s.controls_pos[23] : 0);
        const f1Lvl = (ctrl.convection_fan1_level !== undefined) ? ctrl.convection_fan1_level :
                      (s.controls_pos && s.controls_pos.length > 24 ? s.controls_pos[24] : 0);
        const f1Area = (ctrl.convection_fan1_area !== undefined) ? ctrl.convection_fan1_area :
                       (s.controls_pos && s.controls_pos.length > 25 ? s.controls_pos[25] : 0);

        const btnF1 = document.getElementById('fan1ToggleBtn');
        const txtF1 = document.getElementById('fan1ToggleText');
        if (btnF1 && txtF1) {
          if (f1Active == 1) {
            btnF1.className = 'fan-toggle active';
            txtF1.textContent = t.fanOn;
          } else {
            btnF1.className = 'fan-toggle';
            txtF1.textContent = t.fanOff;
          }
        }
        for (let l = 0; l <= 5; l++) {
          const bl = document.getElementById('f1Lvl' + l);
          if (bl) {
            if (l === f1Lvl) bl.classList.add('active');
            else bl.classList.remove('active');
          }
        }
        const sp1Text = document.getElementById('fan1SpeedText');
        if (sp1Text) sp1Text.textContent = (f1Lvl === 0) ? t.fanAuto : (t.fanSpeed + ' ' + f1Lvl);

        if (!userInteracting) {
          const r1 = document.getElementById('fan1AreaRange');
          const v1 = document.getElementById('fan1AreaVal');
          if (r1) r1.value = f1Area;
          if (v1) v1.textContent = (f1Area > 0 ? '+' : '') + f1Area;
        }

        // Fan 2
        const f2Active = (ctrl.convection_fan2_active !== undefined) ? (ctrl.convection_fan2_active ? 1 : 0) :
                         (s.controls_pos && s.controls_pos.length > 26 ? s.controls_pos[26] : 0);
        const f2Lvl = (ctrl.convection_fan2_level !== undefined) ? ctrl.convection_fan2_level :
                      (s.controls_pos && s.controls_pos.length > 27 ? s.controls_pos[27] : 0);
        const f2Area = (ctrl.convection_fan2_area !== undefined) ? ctrl.convection_fan2_area :
                       (s.controls_pos && s.controls_pos.length > 28 ? s.controls_pos[28] : 0);

        const btnF2 = document.getElementById('fan2ToggleBtn');
        const txtF2 = document.getElementById('fan2ToggleText');
        if (btnF2 && txtF2) {
          if (f2Active == 1) {
            btnF2.className = 'fan-toggle active';
            txtF2.textContent = t.fanOn;
          } else {
            btnF2.className = 'fan-toggle';
            txtF2.textContent = t.fanOff;
          }
        }
        for (let l = 0; l <= 5; l++) {
          const bl = document.getElementById('f2Lvl' + l);
          if (bl) {
            if (l === f2Lvl) bl.classList.add('active');
            else bl.classList.remove('active');
          }
        }
        const sp2Text = document.getElementById('fan2SpeedText');
        if (sp2Text) sp2Text.textContent = (f2Lvl === 0) ? t.fanAuto : (t.fanSpeed + ' ' + f2Lvl);

        if (!userInteracting) {
          const r2 = document.getElementById('fan2AreaRange');
          const v2 = document.getElementById('fan2AreaVal');
          if (r2) r2.value = f2Area;
          if (v2) v2.textContent = (f2Area > 0 ? '+' : '') + f2Area;
        }
      }
    }

    // Frost Protection
    const frostActive = (ctrl.frost_protection_active !== undefined) ? (ctrl.frost_protection_active ? 1 : 0) :
                        ((ctrl.frostProtectionActive !== undefined) ? (ctrl.frostProtectionActive ? 1 : 0) :
                        (s.controls_pos && s.controls_pos.length > 29 ? s.controls_pos[29] : 0));
    const frostTemp = (ctrl.frost_protection_temperature !== undefined) ? Math.round(ctrl.frost_protection_temperature) :
                      ((ctrl.frostProtectionTemp !== undefined) ? Math.round(ctrl.frostProtectionTemp / 10.0) :
                      (s.controls_pos && s.controls_pos.length > 30 && s.controls_pos[30] > 0 ? Math.round(s.controls_pos[30] / 10.0) : 5));

    const btnFrost = document.getElementById('frostToggleBtn');
    const txtFrost = document.getElementById('frostToggleText');
    if (btnFrost && txtFrost) {
      if (frostActive == 1) {
        btnFrost.className = 'fan-toggle active';
        txtFrost.textContent = t.frostOn;
      } else {
        btnFrost.className = 'fan-toggle';
        txtFrost.textContent = t.frostOff;
      }
    }
    if (!userInteracting) {
      const rf = document.getElementById('frostTempRange');
      const vf = document.getElementById('frostTempVal');
      if (rf) rf.value = frostTemp;
      if (vf) vf.textContent = frostTemp;
    }

    // Baking Oven (DOMO BACK model 23)
    const hasBake = (mId === 23);
    const bakeDeck = document.getElementById('bakeDeck');
    if (bakeDeck) {
      bakeDeck.style.display = hasBake ? 'flex' : 'none';
      if (hasBake) {
        const bakeTarget = (ctrl.bake_target_temperature !== undefined) ? ctrl.bake_target_temperature :
                           ((ctrl.bakeTarget !== undefined) ? ctrl.bakeTarget :
                           (s.controls_pos && s.controls_pos.length > 5 && s.controls_pos[5] > 0 ? s.controls_pos[5] : 180));
        if (!userInteracting) {
          const rb = document.getElementById('bakeTempRange');
          const vb = document.getElementById('bakeTempVal');
          if (rb) rb.value = bakeTarget;
          if (vb) vb.textContent = bakeTarget;
        }
      }
    }

    // Room Temp Offset Calibration (Slot 31)
    const tempOffset = (ctrl.room_temperature_offset !== undefined) ? ctrl.room_temperature_offset :
                       ((ctrl.roomTempOffset !== undefined) ? (ctrl.roomTempOffset / 10.0) :
                       (s.controls_pos && s.controls_pos.length > 31 ? (s.controls_pos[31] / 10.0) : 0.0));
    if (!userInteracting) {
      const ro = document.getElementById('roomOffsetRange');
      const vo = document.getElementById('roomOffsetVal');
      if (ro) ro.value = tempOffset.toFixed(1);
      if (vo) vo.textContent = (tempOffset > 0 ? '+' : '') + tempOffset.toFixed(1);
    }

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
