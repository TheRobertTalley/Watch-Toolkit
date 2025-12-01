#pragma once
#include <cstddef>

namespace graphics {
constexpr const char msdCalculatorPage[] = R"msdcalc(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <title>Talley's Explosive Calculator</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <style>
    :root {
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI",
        sans-serif;
      background-color: #0b1016;
      color: #e6edf7;
    }

    body {
      margin: 0;
      padding: 0;
      display: flex;
      justify-content: center;
      align-items: flex-start;
      min-height: 100vh;
    }

    .page {
      width: 100%;
      max-width: 900px;
      padding: 16px;
      box-sizing: border-box;
    }

    h1 {
      text-align: center;
      margin-bottom: 4px;
      letter-spacing: 0.06em;
      text-transform: uppercase;
      font-size: 1.4rem;
    }

    .subtitle {
      text-align: center;
      font-size: 0.85rem;
      color: #9aa4b5;
      margin-bottom: 16px;
    }

    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
      gap: 16px;
    }

    .card {
      background: radial-gradient(circle at top left, #1d2735, #111722);
      border-radius: 14px;
      padding: 14px 16px 16px;
      box-shadow: 0 10px 25px rgba(0, 0, 0, 0.45);
      border: 1px solid rgba(255, 255, 255, 0.04);
    }

    .card h2 {
      font-size: 1.05rem;
      margin: 0 0 4px;
    }

    .card small {
      display: block;
      color: #9aa4b5;
      font-size: 0.75rem;
      margin-bottom: 8px;
    }

    .field-row {
      display: flex;
      gap: 8px;
      margin-bottom: 8px;
      align-items: center;
    }

    .field-row label {
      flex: 0 0 125px;
      font-size: 0.8rem;
    }

    .field-row input {
      flex: 1;
      padding: 5px 7px;
      border-radius: 8px;
      border: 1px solid #253143;
      background: #05070c;
      color: #e6edf7;
      font-size: 0.85rem;
    }

    .field-row input:focus {
      outline: none;
      border-color: #3ea6ff;
      box-shadow: 0 0 0 1px rgba(62, 166, 255, 0.4);
    }

    .btn-row {
      margin-top: 6px;
      display: flex;
      gap: 8px;
      flex-wrap: wrap;
    }

    button {
      border-radius: 999px;
      border: 1px solid #3ea6ff;
      background: linear-gradient(135deg, #3ea6ff, #0073ff);
      color: #ffffff;
      padding: 6px 12px;
      font-size: 0.8rem;
      cursor: pointer;
      font-weight: 600;
    }

    button.secondary {
      background: transparent;
      border-color: #37455a;
      color: #cfd6e3;
    }

    button:active {
      transform: translateY(1px);
    }

    .result-line {
      font-size: 0.8rem;
      margin-top: 4px;
    }

    .result-label {
      color: #9aa4b5;
    }

    .result-value {
      font-weight: 600;
    }

    .tag-row {
      margin-top: 6px;
      font-size: 0.7rem;
      color: #9aa4b5;
    }

    .tag {
      display: inline-block;
      border-radius: 999px;
      border: 1px solid #37455a;
      padding: 2px 8px;
      margin-right: 4px;
      margin-top: 2px;
    }

    .note {
      font-size: 0.7rem;
      color: #ffb36b;
      margin-top: 6px;
    }

    .footer-note {
      margin-top: 16px;
      font-size: 0.75rem;
      color: #7d8798;
      text-align: center;
    }

    .badge-row {
      display: flex;
      justify-content: center;
      gap: 10px;
      margin-top: 10px;
      font-size: 0.75rem;
    }

    .badge {
      border-radius: 999px;
      border: 1px solid #37455a;
      padding: 3px 10px;
      color: #cfd6e3;
    }

    .radio-wrap label {
      font-size: 0.8rem;
      margin-right: 10px;
    }

    .radio-wrap input {
      margin-right: 4px;
    }
  </style>
</head>
<body>
  <div class="page">
    <h1>Talley's Explosive Calculator</h1>
    <div class="subtitle">Grains to TNT to MSD with priming</div>

    <!-- Priming selector -->
    <div class="card" style="margin-bottom: 16px;">
      <h2>Charge Priming</h2>
      <small>Single or double primed detonator added to total grains</small>

      <div class="field-row">
        <label>Priming mode</label>
        <div class="radio-wrap">
          <label>
            <input type="radio" name="primeMode" value="single" checked onclick="setPriming('single')" />
            Single primed
          </label>
          <label>
            <input type="radio" name="primeMode" value="double" onclick="setPriming('double')" />
            Double primed
          </label>
        </div>
      </div>

      <div class="result-line">
        <span class="result-label">Priming grains (detonators):</span>
        <span id="primingOut" class="result-value">15.4</span>
      </div>
      <div class="result-line">
        <span class="result-label">Total grains incl. priming:</span>
        <span id="totalWithPrimeOut" class="result-value">0.0</span>
      </div>
    </div>

    <div class="grid">
      <!-- Column 1: compute base grains (line or sheet) -->
      <div class="card">
        <h2>Column 1: Build the Charge</h2>
        <small>Pick line or sheet, both feed base grains</small>

        <h3 style="font-size:0.9rem; margin-top:6px;">Line or ribbon charge</h3>
        <div class="field-row">
          <label for="feet">Feet</label>
          <input id="feet" type="number" step="0.1" placeholder="Run in feet" />
        </div>
        <div class="field-row">
          <label for="grainsPerFt">Grains/ft</label>
          <input
            id="grainsPerFt"
            type="number"
            step="0.1"
            placeholder="Grains per foot"
          />
        </div>
        <div class="btn-row">
          <button onclick="grainsPerFoot()">Calc total grains (line)</button>
        </div>
        <div class="result-line">
          <span class="result-label">Total grains:</span>
          <span id="grainsPerFtOut" class="result-value">0.0</span>
        </div>

        <hr style="margin: 10px 0; border: 0; border-top: 1px solid #202a38" />

        <div class="tag-row">
        </div>
      </div>

      <!-- Column 2: basic conversions from base grains -->
      <div class="card">
        <h2>Column 2: Basic Conversions</h2>
        <small>Grains to pounds to TNT with priming</small>

        <div class="field-row">
          <label for="grainsIn">Base grains</label>
          <input
            id="grainsIn"
            type="number"
            step="0.01"
            placeholder="Total grains (auto from col 1 or manual)"
          />
        </div>
        <div class="btn-row">
          <button onclick="fromGrains()">Grains to lbs and TNT</button>
          <button class="secondary" onclick="clearConversions()">Clear</button>
        </div>

        <!-- NEW: show base grains coming into Column 2 -->
        <div class="result-line">
          <span class="result-label">Base grains (from Column 1):</span>
          <span id="baseGrainsOut" class="result-value">0.0</span>
        </div>

        <div class="result-line">
          <span class="result-label">Total grains incl. priming:</span>
          <span id="totalGrainsDisplay" class="result-value">0.0</span>
        </div>
        <div class="result-line">
          <span class="result-label">Actual lbs:</span>
          <span id="lbsOut" class="result-value">0.000</span>
        </div>
        <div class="result-line">
          <span class="result-label">TNT lbs (×1.66):</span>
          <span id="tntFromGrains" class="result-value">0.000</span>
        </div>

        <hr style="margin: 10px 0; border: 0; border-top: 1px solid #202a38" />

        <small>Alternate path if you only know pounds</small>
        <div class="field-row">
          <label for="lbsIn">Actual lbs</label>
          <input id="lbsIn" type="number" step="0.001" placeholder="Actual pounds" />
        </div>
        <div class="btn-row">
          <button onclick="fromLbs()">Lbs to TNT (no grains)</button>
        </div>
        <div class="result-line">
          <span class="result-label">TNT lbs (×1.66):</span>
          <span id="tntFromLbs" class="result-value">0.000</span>
        </div>

        <div class="tag-row">
          <span class="tag">Grains ÷ 7000 = actual lbs</span>
          <span class="tag">Actual lbs × 1.66 = TNT lbs</span>
        </div>
      </div>

      <!-- Column 3: MSD -->
      <div class="card">
        <h2>Column 3: Minimum Safe Distance</h2>
        <small>Based on TNT equivalent in pounds</small>

        <div class="field-row">
          <label for="tntIn">Lbs TNT equivalent</label>
          <input
            id="tntIn"
            type="number"
            step="0.001"
            placeholder="TNT lbs (auto from conversions)"
          />
        </div>
        <div class="btn-row">
          <button onclick="msdFromTNT()">Recalc MSD from TNT</button>
          <button class="secondary" onclick="msdFromCurrentTNT()">Use TNT from above</button>
        </div>

        <div class="result-line">
          <span class="result-label">TNT lbs:</span>
          <span id="cuberootOut" class="result-value">0.000</span>
        </div>

        <div class="result-line">
          <span class="result-label">MSD 4 psi (K18:):</span>
          <span id="msd18Out" class="result-value">0.0 ft</span>
        </div>
        <div class="result-line">
          <span class="result-label">MSD 2.3 psi (K24:):</span>
          <span id="msd24Out" class="result-value">0.0 ft</span>
        </div>

        <hr style="margin: 10px 0; border: 0; border-top: 1px solid #202a38" />

        <div class="note">
          This does not account for reflective overpressure, colliding shockwaves, or full duration of exposure. 
        </div>
      </div>
    </div>

    <div class="badge-row">
      <div class="badge">Distance = K factor × ³√(lbs TNT)</div>
      <div class="badge">K18 for 4 psi, K24 for 2.3 psi</div>
    </div>

    <div class="footer-note">
      For training use only. You are still responsible for your charge, your team, and your target.
    </div>
  </div>

  <script>
    let primeGrains = 15.4;

    function safeNumber(value) {
      const n = parseFloat(value);
      if (isNaN(n) || !isFinite(n)) return 0;
      return n;
    }

    function fmt(n, digits) {
      return n.toFixed(digits);
    }

    function postResults(msd18, msd24, tnt) {
      if (typeof fetch !== 'function' || tnt <= 0) {
        return;
      }
      const params = new URLSearchParams();
      params.set('msd18', msd18.toFixed(1));
      params.set('msd24', msd24.toFixed(1));
      params.set('tnt', tnt.toFixed(3));
      fetch('/msd?' + params.toString(), {
        method: 'GET',
        cache: 'no-store'
      }).catch(() => {});
    }

    function setPriming(mode) {
      primeGrains = mode === "double" ? 30.8 : 15.4;
      document.getElementById("primingOut").textContent = fmt(primeGrains, 1);

      const base = safeNumber(document.getElementById("grainsIn").value);
      if (base > 0) {
        fromGrains();
      } else {
        document.getElementById("totalWithPrimeOut").textContent = "0.0";
        document.getElementById("totalGrainsDisplay").textContent = "0.0";
      }
    }

    function fromGrains() {
      const baseGrains = safeNumber(document.getElementById("grainsIn").value);

      document.getElementById("baseGrainsOut").textContent = fmt(baseGrains, 1);

      if (baseGrains <= 0) {
        document.getElementById("totalWithPrimeOut").textContent = "0.0";
        document.getElementById("totalGrainsDisplay").textContent = "0.0";
        document.getElementById("lbsOut").textContent = "0.000";
        document.getElementById("tntFromGrains").textContent = "0.000";
        msdCore(0);
        return;
      }

      const totalGrains = baseGrains + primeGrains;
      document.getElementById("totalWithPrimeOut").textContent = fmt(
        totalGrains,
        1
      );
      document.getElementById("totalGrainsDisplay").textContent = fmt(
        totalGrains,
        1
      );

      const lbs = totalGrains / 7000.0;
      const tnt = lbs * 1.66;
      document.getElementById("lbsOut").textContent = fmt(lbs, 3);
      document.getElementById("tntFromGrains").textContent = fmt(tnt, 3);

      if (tnt > 0) {
        document.getElementById("tntIn").value = fmt(tnt, 3);
        msdCore(tnt);
      } else {
        msdCore(0);
      }
    }

    function fromLbs() {
      const lbs = safeNumber(document.getElementById("lbsIn").value);
      if (lbs <= 0) {
        document.getElementById("tntFromLbs").textContent = "0.000";
        return;
      }
      const tnt = lbs * 1.66;
      document.getElementById("tntFromLbs").textContent = fmt(tnt, 3);
      document.getElementById("tntIn").value = fmt(tnt, 3);
      msdCore(tnt);
    }

    function clearConversions() {
      document.getElementById("grainsIn").value = "";
      document.getElementById("baseGrainsOut").textContent = "0.0";
      document.getElementById("totalWithPrimeOut").textContent = "0.0";
      document.getElementById("totalGrainsDisplay").textContent = "0.0";
      document.getElementById("lbsOut").textContent = "0.000";
      document.getElementById("tntFromGrains").textContent = "0.000";
      document.getElementById("lbsIn").value = "";
      document.getElementById("tntFromLbs").textContent = "0.000";
      document.getElementById("tntIn").value = "";
      msdCore(0);
    }

    function grainsPerFoot() {
      const feet = safeNumber(document.getElementById("feet").value);
      const grainsPerFt = safeNumber(
        document.getElementById("grainsPerFt").value
      );
      const totalGrains = feet * grainsPerFt;
      document.getElementById("grainsPerFtOut").textContent = fmt(
        totalGrains,
        1
      );

      if (totalGrains > 0) {
        document.getElementById("grainsIn").value = fmt(totalGrains, 1);
        fromGrains();
      }
    }

    function sheetExplosive() {
      const L = safeNumber(document.getElementById("len").value);
      const W = safeNumber(document.getElementById("wid").value);
      const C = safeNumber(document.getElementById("cNum").value);
      const grains = L * W * C * 15.4;
      document.getElementById("sheetGrainsOut").textContent = fmt(grains, 1);

      if (grains > 0) {
        document.getElementById("grainsIn").value = fmt(grains, 1);
        fromGrains();
      }
    }

    function msdCore(tntLbs) {
      const W = tntLbs;
      if (W <= 0) {
        document.getElementById("cuberootOut").textContent = "0.000";
        document.getElementById("msd18Out").textContent = "0.0 ft";
        document.getElementById("msd24Out").textContent = "0.0 ft";
        return;
      }
      const cbrt = Math.cbrt(W);
      const d18 = 18.0 * cbrt;
      const d24 = 24.0 * cbrt;
      document.getElementById("cuberootOut").textContent = fmt(cbrt, 3);
      document.getElementById("msd18Out").textContent = fmt(d18, 1) + " ft";
      document.getElementById("msd24Out").textContent = fmt(d24, 1) + " ft";

      postResults(d18, d24, tntLbs);
    }

    function msdFromTNT() {
      const tnt = safeNumber(document.getElementById("tntIn").value);
      msdCore(tnt);
    }

    function msdFromCurrentTNT() {
      const tntFromG = safeNumber(
        document.getElementById("tntFromGrains").textContent
      );
      const tntFromL = safeNumber(
        document.getElementById("tntFromLbs").textContent
      );
      const tntField = safeNumber(
        document.getElementById("tntIn").value
      );

      const tnt = tntFromL > 0 ? tntFromL : tntFromG > 0 ? tntFromG : tntField;
      msdCore(tnt);
    }

    (function selfTest() {
      const w = 1.0;
      const cbrt = Math.cbrt(w);
      const d18 = 18 * cbrt;
      const d24 = 24 * cbrt;
      console.log(
        "[BreacherCalc self test] 1 lb TNT ->",
        "18 factor:",
        d18.toFixed(2),
        "24 factor:",
        d24.toFixed(2)
      );
    })();
  </script>
</body>
</html>

)msdcalc";
constexpr const std::size_t msdCalculatorPageSize = sizeof(msdCalculatorPage) - 1;
constexpr const char msdCalculatorSsid[] = "MSD Calculator";
}
