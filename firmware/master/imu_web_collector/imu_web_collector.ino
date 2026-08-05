/*
 * SideEye — IMU web collector (checkpoint 3 re-collection, web version).
 *
 * Same architecture as the vision collector (XIAO_WEBCAM_EDGE_IMPULSE_GUIDE.md):
 * ESP32 hosts a WiFi AP + browser page. No API key on the board or in the
 * browser -- a local bridge process on the PC (imu_harness/imu_upload_bridge.py)
 * holds the Edge Impulse key and forwards uploads.
 *
 *   ESP32 (this board, AP)         Browser              Local bridge (PC)
 *   /record?label=X  ------------> fetch()
 *     blocks ~2s sampling BNO055,
 *     returns JSON {label,values}  stores in memory
 *                                  "업로드" button ----> POST 127.0.0.1:8787/upload
 *                                                            forwards to EI with API key
 *
 * Mounting reminder (PDR_SideEye.md 3.1): BNO055 must be in its final helmet
 * position (back of head) during collection.
 *
 * Wiring: same as checkpoint1_hw_test (PDR_SideEye.md section 4).
 */
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <WiFi.h>
#include <WebServer.h>

#define BNO_ADDR 0x29
constexpr uint32_t SAMPLE_INTERVAL_MS = 20;   // 50Hz
constexpr int WINDOW_SAMPLES = 25;            // 500ms window (user times the motion to the GO cue)

const char *AP_SSID = "sideeye-imu";
const char *AP_PASSWORD = "sideeye123";

Adafruit_BNO055 bno(55, BNO_ADDR, &Wire);
WebServer server(80);

const char PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SideEye IMU 수집기</title>
<style>
body{font-family:sans-serif;max-width:480px;margin:0 auto;padding:16px;background:#111;color:#eee}
select,button{font-size:18px;padding:10px;margin:6px 0;width:100%;border-radius:8px;border:none}
button{background:#3a7;color:#fff;font-weight:bold}
button:disabled{background:#555}
#status{font-size:24px;text-align:center;margin:16px 0;min-height:32px}
#counts{display:flex;justify-content:space-around;margin:12px 0}
.count{text-align:center}
.count b{font-size:22px;display:block}
#upload{background:#37a}
canvas{width:100%;background:#000;border-radius:8px;margin-top:8px}
#legend{display:flex;flex-wrap:wrap;gap:8px;font-size:12px;margin-top:6px}
#legend span{display:flex;align-items:center;gap:4px}
#legend i{width:10px;height:10px;display:inline-block;border-radius:2px}
</style></head><body>
<h2>SideEye IMU 수집기</h2>
<select id="label">
  <option value="idle">idle (정지)</option>
  <option value="left">left (왼쪽 기울임)</option>
  <option value="right">right (오른쪽 기울임)</option>
</select>
<button id="go">녹화 시작 (0.5초)</button>
<div id="status"></div>
<canvas id="chart" width="400" height="200"></canvas>
<div id="legend">
  <span><i style="background:#e33"></i>accX</span>
  <span><i style="background:#3e3"></i>accY</span>
  <span><i style="background:#33e"></i>accZ</span>
  <span><i style="background:#ee3"></i>gyrX</span>
  <span><i style="background:#3ee"></i>gyrY</span>
  <span><i style="background:#e3e"></i>gyrZ</span>
</div>
<div id="counts">
  <div class="count">idle<b id="c-idle">0</b></div>
  <div class="count">left<b id="c-left">0</b></div>
  <div class="count">right<b id="c-right">0</b></div>
</div>
<button id="upload">Edge Impulse로 업로드</button>
<div id="uploadStatus"></div>
<script>
const samples = [];
const counts = {idle:0, left:0, right:0};
const statusEl = document.getElementById('status');
const goBtn = document.getElementById('go');
const uploadBtn = document.getElementById('upload');
const uploadStatusEl = document.getElementById('uploadStatus');

function sleep(ms){ return new Promise(r => setTimeout(r, ms)); }

const chart = document.getElementById('chart');
const ctx = chart.getContext('2d');
const CHART_COLORS = ['#e33','#3e3','#33e','#ee3','#3ee','#e3e'];

function drawChart(values) {
  const w = chart.width, h = chart.height;
  ctx.clearRect(0, 0, w, h);
  if (!values.length) return;

  let min = Infinity, max = -Infinity;
  for (const row of values) for (const v of row) { if (v < min) min = v; if (v > max) max = v; }
  if (min === max) { min -= 1; max += 1; }
  const pad = (max - min) * 0.1;
  min -= pad; max += pad;

  // zero line
  ctx.strokeStyle = '#444';
  ctx.beginPath();
  const zeroY = h - ((0 - min) / (max - min)) * h;
  ctx.moveTo(0, zeroY); ctx.lineTo(w, zeroY);
  ctx.stroke();

  for (let ch = 0; ch < 6; ch++) {
    ctx.strokeStyle = CHART_COLORS[ch];
    ctx.lineWidth = 2;
    ctx.beginPath();
    values.forEach((row, i) => {
      const x = (i / (values.length - 1)) * w;
      const y = h - ((row[ch] - min) / (max - min)) * h;
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    });
    ctx.stroke();
  }
}

goBtn.onclick = async () => {
  const label = document.getElementById('label').value;
  goBtn.disabled = true;
  for (const n of [3,2,1]) { statusEl.textContent = n; await sleep(700); }
  statusEl.textContent = 'GO!';
  try {
    const res = await fetch(`/record?label=${label}`);
    const data = await res.json();
    samples.push(data);
    counts[label]++;
    document.getElementById('c-'+label).textContent = counts[label];
    statusEl.textContent = `저장됨 (${data.values.length} rows)`;
    drawChart(data.values);
  } catch (e) {
    statusEl.textContent = '오류: ' + e;
  }
  goBtn.disabled = false;
};

uploadBtn.onclick = async () => {
  uploadBtn.disabled = true;
  let ok = 0, fail = 0;
  for (const s of samples) {
    try {
      const res = await fetch('http://127.0.0.1:8787/upload', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(s)
      });
      if (res.ok) ok++; else fail++;
    } catch (e) { fail++; }
    uploadStatusEl.textContent = `업로드 중... 성공 ${ok}, 실패 ${fail} / 전체 ${samples.length}`;
  }
  uploadStatusEl.textContent = `완료: 성공 ${ok}, 실패 ${fail} / 전체 ${samples.length}`;
  uploadBtn.disabled = false;
};
</script>
</body></html>
)HTML";

void handleRoot() {
  server.send_P(200, "text/html", PAGE);
}

void handleRecord() {
  String label = server.hasArg("label") ? server.arg("label") : "idle";

  String json = "{\"label\":\"" + label + "\",\"values\":[";
  uint32_t nextSampleAt = millis();
  for (int i = 0; i < WINDOW_SAMPLES; i++) {
    while (millis() < nextSampleAt) delay(1);
    nextSampleAt += SAMPLE_INTERVAL_MS;

    imu::Vector<3> accel = bno.getVector(Adafruit_BNO055::VECTOR_LINEARACCEL);
    imu::Vector<3> gyro = bno.getVector(Adafruit_BNO055::VECTOR_GYROSCOPE);

    if (i > 0) json += ",";
    json += "[";
    json += String(accel.x(), 4) + "," + String(accel.y(), 4) + "," + String(accel.z(), 4) + ",";
    json += String(gyro.x(), 4) + "," + String(gyro.y(), 4) + "," + String(gyro.z(), 4);
    json += "]";
  }
  json += "]}";

  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  delay(3000);

  Wire.begin(D4, D5);
  Wire.setClock(100000);
  if (!bno.begin()) {
    Serial.println("ERROR: BNO055 begin() failed -- check wiring");
    while (true) delay(1000);
  }
  delay(1000);
  // do NOT call setExtCrystalUse(true) -- see xiao-i2c-sensors skill pitfall 2

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("AP started. Connect to WiFi \"");
  Serial.print(AP_SSID);
  Serial.println("\" then open http://192.168.4.1");

  server.on("/", handleRoot);
  server.on("/record", handleRecord);
  server.begin();
}

void loop() {
  server.handleClient();
}
