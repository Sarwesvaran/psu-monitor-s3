#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <ModbusMaster.h>

// ── Debug Flag ─────────────────────────────────────────────
bool DEBUG_MODE = true;

// ── Hardware Pins ──────────────────────────────────────────
#define TFT_MOSI 7
#define TFT_SCLK 6
#define TFT_CS   5
#define TFT_DC   4
#define TFT_RST  3
#define TFT_BL   2

// RS485 PINS
#define MAX485_RO    12 // RX
#define MAX485_DI    8  // TX
#define MAX485_RE_DE 9  // Transmit/Receive Control

#define POT_PIN 10
#define PWM_PIN 11

// ── Calibration & Smoothing ────────────────────────────────
#define EMA_ALPHA 0.35f          
#define VOLTAGE_MULTIPLIER 1.000f 
#define VOLTAGE_OFFSET 0.000f     
#define CURRENT_MULTIPLIER 1.000f 
#define CURRENT_OFFSET 0.000f     

// ── Objects ────────────────────────────────────────────────
Adafruit_ST7789 tft = Adafruit_ST7789(&SPI, TFT_CS, TFT_DC, TFT_RST);
ModbusMaster node;
WebServer server(80);
Preferences preferences;

// ── Theme Engine ───────────────────────────────────────────
struct Theme {
    uint16_t bg;
    uint16_t text;
    uint16_t accent;
    uint16_t setting;
    uint16_t muted;
    const char *name;
};

uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

Theme themes[5] = {
    {0x0823, 0xFFFF, 0x07E0, 0x07FF, 0x8410, "Hyper Dark"},
    {0x0000, 0xFFFF, 0xF81F, 0xFFE0, 0x5AEB, "Cyberpunk"},
    {0x10A2, 0xFFFF, 0xF800, 0x07FF, 0x4208, "Analog Gauge"},
    {0xFFFF, 0x0000, 0x03E0, 0x001F, 0xBDD7, "Minimal Light"},
    {0x0000, 0xFDA0, 0xFDA0, 0xFDA0, 0x8200, "Retro Terminal"}
};

uint8_t currentTheme = 0;
float lastDrawnNeedleAngle = -999;

// ── Dynamic Color Thresholds ───────────────────────────────
bool dynCol = false;
float v_y = 10.0, v_r = 12.0;
float i_y = 5.0,  i_r = 8.0;
float p_y = 50.0, p_r = 80.0;

// ── State Variables ────────────────────────────────────────
bool softwareControlMode = false;

float measuredVoltage = 0.0;
float measuredCurrent = 0.0;
float measuredPower   = 0.0; 
float measuredEnergy  = 0.0; // Wh
float setVoltage = 0.0;
int pwmValue = 0;

int modbusFailCount = 0; 
int lastPotValue = -1;
unsigned long lastUpdate = 0;
unsigned long lastDisplayUpdate = 0;

// ── Forward Declarations ───────────────────────────────────
void handleRoot();
void handleData();
void handleSet();
void handleMode();
void handleTheme();
void handleConfig();
void handleResetEnergy();
void resetPZEMEnergy();
void updateDisplay();
void drawStaticUI();
void drawGaugeUI();
void updateGaugeNeedle(float voltage);
uint16_t getDynColor(float val, float y, float r, uint16_t defaultColor);

// ── Modbus Callbacks ───────────────────────────────────────
void preTransmission() { digitalWrite(MAX485_RE_DE, 1); }
void postTransmission() { digitalWrite(MAX485_RE_DE, 0); }

uint16_t pzem_crc(uint16_t crc, uint8_t a) {
    crc ^= a;
    for (int i = 0; i < 8; ++i) {
        if (crc & 1) crc = (crc >> 1) ^ 0xA001;
        else crc = (crc >> 1);
    }
    return crc;
}

// ── Web UI HTML ────────────────────────────────────────────
const char *htmlPage PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Hyper PSU</title>
    <style>
        body { background-color: #0d1117; color: #c9d1d9; font-family: 'Segoe UI', sans-serif; text-align: center; margin: 0; padding: 20px; }
        .card { background: #161b22; border: 1px solid #30363d; border-radius: 12px; padding: 20px; max-width: 500px; margin: 0 auto; box-shadow: 0 8px 24px rgba(0,0,0,0.5); }
        h1 { color: #58a6ff; font-size: 24px; margin-bottom: 5px; }
        h2 { color: #8b949e; font-size: 14px; font-weight: normal; margin-top: 0; letter-spacing: 2px; }
        
        .readout-container { display: flex; flex-wrap: wrap; justify-content: space-around; margin: 20px 0; background: #0d1117; padding: 15px; border-radius: 8px; border: 1px solid #30363d; gap: 10px; }
        .readout-box { flex: 1 1 40%; display: flex; flex-direction: column; justify-content: center; }
        .readout { font-size: 26px; font-weight: bold; text-shadow: 0 0 10px rgba(0,0,0,0.3); margin-top: 5px; transition: color 0.3s; }
        .label { color: #8b949e; font-size: 11px; text-transform: uppercase; display: block; }
        
        .btn-group { display: flex; justify-content: center; gap: 10px; margin: 20px 0; }
        .btn { background: #21262d; color: #c9d1d9; border: 1px solid #30363d; padding: 10px; border-radius: 6px; cursor: pointer; font-size: 14px; flex: 1; transition: 0.2s;}
        .btn.active { background: #2ea043; color: #ffffff; border-color: #238636; font-weight: bold; }
        .btn-danger { background: #da3633; color: white; border-color: #b62324; }
        
        .slider-container { margin-top: 20px; background: #0d1117; padding: 15px; border-radius: 8px; border: 1px solid #30363d;}
        input[type=range] { -webkit-appearance: none; width: 100%; background: transparent; }
        input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; height: 24px; width: 24px; border-radius: 50%; background: #58a6ff; cursor: pointer; margin-top: -8px; box-shadow: 0 0 10px rgba(88,166,255,0.5); }
        input[type=range]::-webkit-slider-runnable-track { width: 100%; height: 8px; cursor: pointer; background: #30363d; border-radius: 4px; }
        input[type=range]:disabled::-webkit-slider-thumb { background: #6e7681; box-shadow: none; cursor: not-allowed; }
        
        .target-val { color: #d2a8ff; font-size: 20px; margin-top: 15px; }
        select, input[type=number] { background: #0d1117; color: #c9d1d9; border: 1px solid #30363d; padding: 8px; border-radius: 6px; outline: none; }
        select { width: 100%; margin-top: 15px; }
        
        .settings-panel { display: none; background: #21262d; border-radius: 8px; padding: 15px; margin-top: 20px; text-align: left; border: 1px solid #30363d; }
        .settings-panel label { font-size: 12px; color: #8b949e; display: inline-block; width: 100px; }
        .settings-row { margin-bottom: 10px; }
        .settings-row input[type=number] { width: 60px; margin-right: 10px; }
        
        .c-green { color: #3fb950 !important; }
        .c-yellow { color: #d29922 !important; }
        .c-red { color: #f85149 !important; }
        .c-static { color: #58a6ff !important; } /* Fallback default */
    </style>
</head>
<body>
    <div class="card">
        <h1>Power Supply Control</h1>
        <h2>HYPER ENTITY</h2>
        
        <div class="readout-container">
            <div class="readout-box">
                <span class="label">Voltage</span>
                <div class="readout" id="v_val">--.-- V</div>
            </div>
            <div class="readout-box">
                <span class="label">Current</span>
                <div class="readout" id="i_val">--.-- A</div>
            </div>
            <div class="readout-box">
                <span class="label">Power</span>
                <div class="readout" id="p_val">--.-- W</div>
            </div>
            <div class="readout-box">
                <span class="label">Energy</span>
                <div class="readout" id="e_val" style="color: #a371f7;">-- Wh</div>
            </div>
        </div>
        
        <div class="btn-group">
            <button id="btnHW" class="btn active" onclick="setMode(0)">Pot Mode</button>
            <button id="btnSW" class="btn" onclick="setMode(1)">Web Mode</button>
        </div>

        <div class="slider-container">
            <span class="label" id="sliderLabel">Target Voltage (0 - 5V)</span>
            <input type="range" id="vSlider" min="0" max="5" step="0.05" value="0" disabled oninput="updateSliderUI(this.value)" onchange="sendVoltage(this.value)">
            <div class="target-val">Target: <span id="targetV">0.00</span> V</div>
        </div>

        <select id="themeSelect" onchange="changeTheme(this.value)">
            <option value="0">Theme: Hyper Dark</option>
            <option value="1">Theme: Cyberpunk</option>
            <option value="2">Theme: Analog Gauge</option>
            <option value="3">Theme: Minimal Light</option>
            <option value="4">Theme: Retro Terminal</option>
        </select>
        
        <button class="btn" style="margin-top:20px; width:100%;" onclick="document.getElementById('settings').style.display='block'">⚙️ Advanced Settings</button>
        
        <div class="settings-panel" id="settings">
            <h3 style="margin-top:0; color:#58a6ff;">Color & Limits</h3>
            <div class="settings-row">
                <input type="checkbox" id="dynColCheck"> <label style="width:auto; color:white;">Enable Dynamic Range Colors</label>
            </div>
            <div class="settings-row">
                <label>Volts (Yel/Red)</label>
                <input type="number" id="vY" step="0.1"> <input type="number" id="vR" step="0.1">
            </div>
            <div class="settings-row">
                <label>Amps (Yel/Red)</label>
                <input type="number" id="iY" step="0.1"> <input type="number" id="iR" step="0.1">
            </div>
            <div class="settings-row">
                <label>Power (Yel/Red)</label>
                <input type="number" id="pY" step="1"> <input type="number" id="pR" step="1">
            </div>
            <div class="btn-group">
                <button class="btn active" onclick="saveConfig()">Save Settings</button>
                <button class="btn btn-danger" onclick="resetEnergy()">Reset Energy (Wh)</button>
            </div>
            <button class="btn" style="width:100%;" onclick="document.getElementById('settings').style.display='none'">Close</button>
        </div>
    </div>

    <script>
        let isDragging = false;
        let dynData = {en: false, vy:10, vr:12, iy:5, ir:8, py:50, pr:80};
        const slider = document.getElementById('vSlider');
        
        slider.addEventListener('mousedown', () => isDragging = true);
        slider.addEventListener('touchstart', () => isDragging = true);
        slider.addEventListener('mouseup', () => { isDragging = false; sendVoltage(slider.value); });
        slider.addEventListener('touchend', () => { isDragging = false; sendVoltage(slider.value); });

        function updateSliderUI(val) { document.getElementById('targetV').innerText = parseFloat(val).toFixed(2); }
        function sendVoltage(val) { fetch('/set?v=' + val); }
        function changeTheme(val) { fetch('/setTheme?t=' + val); }
        function setMode(mode) { fetch('/setMode?m=' + mode); }
        function resetEnergy() { if(confirm("Clear Energy (Wh) data?")) fetch('/resetEnergy'); }
        
        function saveConfig() {
            let dy = document.getElementById('dynColCheck').checked ? 1 : 0;
            let vy = document.getElementById('vY').value; let vr = document.getElementById('vR').value;
            let iy = document.getElementById('iY').value; let ir = document.getElementById('iR').value;
            let py = document.getElementById('pY').value; let pr = document.getElementById('pR').value;
            fetch(`/setConfig?dy=${dy}&vy=${vy}&vr=${vr}&iy=${iy}&ir=${ir}&py=${py}&pr=${pr}`);
            alert("Settings Saved");
        }

        function getColorClass(val, y, r) {
            if(!dynData.en) return 'c-static';
            if(val >= r) return 'c-red';
            if(val >= y) return 'c-yellow';
            return 'c-green';
        }

        setInterval(() => {
            fetch('/data').then(r => r.json()).then(data => {
                // Readouts
                let vEl = document.getElementById('v_val');
                let iEl = document.getElementById('i_val');
                let pEl = document.getElementById('p_val');
                
                vEl.innerText = data.mv.toFixed(2) + ' V';
                iEl.innerText = data.ma.toFixed(2) + ' A';
                pEl.innerText = data.mp.toFixed(2) + ' W';
                document.getElementById('e_val').innerText = data.me + ' Wh';
                
                // Color Config Update
                dynData = data.cfg;
                document.getElementById('dynColCheck').checked = dynData.en;
                if(document.activeElement.tagName !== "INPUT") {
                    document.getElementById('vY').value = dynData.vy; document.getElementById('vR').value = dynData.vr;
                    document.getElementById('iY').value = dynData.iy; document.getElementById('iR').value = dynData.ir;
                    document.getElementById('pY').value = dynData.py; document.getElementById('pR').value = dynData.pr;
                }
                
                vEl.className = "readout " + getColorClass(data.mv, dynData.vy, dynData.vr);
                iEl.className = "readout " + getColorClass(data.ma, dynData.iy, dynData.ir);
                pEl.className = "readout " + getColorClass(data.mp, dynData.py, dynData.pr);

                // Control Logic
                if (data.mode === 0) {
                    document.getElementById('btnHW').classList.add('active');
                    document.getElementById('btnSW').classList.remove('active');
                    slider.disabled = true;
                    document.getElementById('sliderLabel').innerText = "Target (Synced to Pot)";
                    slider.value = data.set;
                    document.getElementById('targetV').innerText = data.set.toFixed(2);
                } else {
                    document.getElementById('btnHW').classList.remove('active');
                    document.getElementById('btnSW').classList.add('active');
                    slider.disabled = false;
                    document.getElementById('sliderLabel').innerText = "Target (Web Override)";
                    if(!isDragging) {
                        slider.value = data.set;
                        document.getElementById('targetV').innerText = data.set.toFixed(2);
                    }
                }
                document.getElementById('themeSelect').value = data.theme;
            });
        }, 1000); // 1000ms helps WiFi stability
    </script>
</body>
</html>
)rawliteral";

// ── Setup ─────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    // Initialize Preferences
    preferences.begin("psu", false);
    currentTheme = preferences.getUInt("theme", 0);
    if (currentTheme > 4) currentTheme = 0;
    
    dynCol = preferences.getBool("dynCol", false);
    v_y = preferences.getFloat("v_y", 10.0); v_r = preferences.getFloat("v_r", 12.0);
    i_y = preferences.getFloat("i_y", 5.0);  i_r = preferences.getFloat("i_r", 8.0);
    p_y = preferences.getFloat("p_y", 50.0); p_r = preferences.getFloat("p_r", 80.0);

    // Setup Pins
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    
    // PWM
    ledcAttach(PWM_PIN, 5000, 8);
    ledcWrite(PWM_PIN, 0);

    // Display SPI
    SPI.begin(TFT_SCLK, -1, TFT_MOSI, -1);
    tft.init(240, 320);
    tft.setRotation(0);

    // Start AP Immediately
    WiFi.softAP("Power Supply", "12345678");
    server.begin();
    drawStaticUI(); 

    // ── PZEM-017 Setup ──
    pinMode(MAX485_RE_DE, OUTPUT);
    digitalWrite(MAX485_RE_DE, 0); // Default to receive mode
    
    Serial1.begin(9600, SERIAL_8N1, MAX485_RO, MAX485_DI);
    node.begin(1, Serial1);
    node.preTransmission(preTransmission);
    node.postTransmission(postTransmission);

    // Set PZEM Shunt to 200A 
    node.writeSingleRegister(0x0003, 0x0002);

    // Web Routes
    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.on("/set", handleSet);
    server.on("/setMode", handleMode);
    server.on("/setTheme", handleTheme);
    server.on("/setConfig", handleConfig);
    server.on("/resetEnergy", handleResetEnergy);
}

// ── Loop ──────────────────────────────────────────────────────
void loop() {
    unsigned long now = millis();
    server.handleClient();

    if (now - lastUpdate > 250) {
        lastUpdate = now;

        // READ PZEM-017: Read 6 registers (Volts, Amps, PowerL, PowerH, EnergyL, EnergyH)
        uint8_t result = node.readInputRegisters(0x0000, 6);
        
        if (result == node.ku8MBSuccess) {
            modbusFailCount = 0; 
            
            float rawVolts = node.getResponseBuffer(0) / 100.0f;
            float rawAmps = node.getResponseBuffer(1) / 100.0f;
            
            float calV = max(0.0f, (rawVolts * VOLTAGE_MULTIPLIER) + VOLTAGE_OFFSET);
            float calA = max(0.0f, (rawAmps * CURRENT_MULTIPLIER) + CURRENT_OFFSET);

            if (abs(calV - measuredVoltage) > 1.5f) {
                measuredVoltage = calV; 
            } else {
                measuredVoltage = (EMA_ALPHA * calV) + ((1.0f - EMA_ALPHA) * measuredVoltage);
            }
            
            if (abs(calA - measuredCurrent) > 1.0f) {
                measuredCurrent = calA;
            } else {
                measuredCurrent = (EMA_ALPHA * calA) + ((1.0f - EMA_ALPHA) * measuredCurrent);
            }

            // Power (0.1W steps)
            uint32_t pwrRaw = (node.getResponseBuffer(3) << 16) | node.getResponseBuffer(2);
            measuredPower = pwrRaw / 10.0f;

            // Energy (1Wh steps)
            uint32_t enRaw = (node.getResponseBuffer(5) << 16) | node.getResponseBuffer(4);
            measuredEnergy = enRaw;
            
        } else {
            modbusFailCount++;
            if (modbusFailCount > 3) {
                measuredVoltage = 0.0f; measuredCurrent = 0.0f;
                measuredPower = 0.0f; 
            }
        }

        // POTENTIOMETER
        if (!softwareControlMode) {
            int potVal = analogRead(POT_PIN);
            if (abs(potVal - lastPotValue) > 10) {
                lastPotValue = potVal;
                setVoltage = ((4095 - potVal) / 4095.0) * 5.0;
            }
        }

        pwmValue = map(setVoltage * 100, 0, 497, 0, 255);
        pwmValue = constrain(pwmValue, 0, 255);
        ledcWrite(PWM_PIN, pwmValue);
    }

    // Display Render (Every 200ms)
    if (now - lastDisplayUpdate > 200) {
        lastDisplayUpdate = now;
        updateDisplay();
    }
}

// ── Display Engine ────────────────────────────────────────────
void drawStaticUI() {
    tft.fillScreen(themes[currentTheme].bg);
    lastDrawnNeedleAngle = -999;

    tft.fillRect(0, 0, 240, 40, themes[currentTheme].muted);
    tft.setTextColor(themes[currentTheme].text);
    tft.setTextSize(2);
    tft.setCursor(10, 12);
    tft.print("HYPER - PSMC");

    if (currentTheme == 2) {
        drawGaugeUI();
    } else {
        tft.setTextColor(themes[currentTheme].muted);
        tft.setTextSize(1);
        tft.setCursor(10, 50); tft.print("VOLTAGE OUTPUT");
        tft.setCursor(10, 105); tft.print("CURRENT OUTPUT");
        tft.drawFastHLine(10, 155, 220, themes[currentTheme].muted);
        tft.setCursor(10, 165); tft.print("TARGET SETTING");
    }

    // Always-ON WiFi Text
    tft.fillRect(0, 260, 240, 60, themes[currentTheme].muted);
    tft.setTextColor(themes[currentTheme].text);
    tft.setTextSize(1);
    tft.setCursor(10, 270); tft.print("AP: Power Supply");
    tft.setCursor(10, 285); tft.print("PWD: 12345678");
    tft.setTextColor(themes[currentTheme].accent);
    tft.setCursor(10, 300); tft.print("http://192.168.4.1");
}

void drawGaugeUI() {
    int cx = 120, cy = 180, r = 100;
    for (int a = 180; a <= 360; a += 18) {
        float rad = a * DEG_TO_RAD;
        tft.drawLine(cx + (r * cos(rad)), cy + (r * sin(rad)), cx + ((r - 10) * cos(rad)), cy + ((r - 10) * sin(rad)), themes[currentTheme].muted);
    }
    tft.setTextColor(themes[currentTheme].muted);
    tft.setTextSize(1);
    tft.setCursor(10, cy + 5); tft.print("0V");
    tft.setCursor(215, cy + 5); tft.print("5V");
    tft.setCursor(85, 220); tft.print("TARGET SETTING");
}

uint16_t getDynColor(float val, float y, float r, uint16_t defaultColor) {
    if (!dynCol) return defaultColor;
    if (val >= r) return 0xF800; // Red
    if (val >= y) return 0xFFE0; // Yellow
    return 0x07E0; // Green
}

void updateDisplay() {
    char bufV[10], bufA[10], bufSet[10];
    Theme t = themes[currentTheme];

    sprintf(bufV, "%05.2fV", measuredVoltage);
    sprintf(bufA, "%05.2fA", measuredCurrent);
    sprintf(bufSet, "%05.2fV", setVoltage);

    uint16_t colorV = getDynColor(measuredVoltage, v_y, v_r, t.accent);
    uint16_t colorI = getDynColor(measuredCurrent, i_y, i_r, 0xF800);

    if (currentTheme == 2) {
        updateGaugeNeedle(measuredVoltage);
        
        tft.setTextColor(colorV, t.bg);
        tft.setTextSize(2);
        tft.setCursor(75, 120); tft.print(bufV);
        
        tft.setTextColor(colorI, t.bg); 
        tft.setCursor(75, 145); tft.print(bufA);

        tft.setTextColor(t.setting, t.bg);
        tft.setTextSize(2);
        tft.setCursor(85, 235); tft.print(bufSet);
    } else {
        tft.setTextColor(colorV, t.bg);
        tft.setTextSize(4);
        tft.setCursor(10, 65); tft.print(bufV);

        tft.setTextColor(colorI, t.bg); 
        tft.setCursor(10, 120); tft.print(bufA);

        tft.setTextColor(t.setting, t.bg);
        tft.setCursor(10, 180); tft.print(bufSet);

        int barWidth = map(setVoltage * 10, 0, 50, 0, 220);
        tft.fillRect(10, 215, barWidth, 6, t.setting);
        tft.fillRect(10 + barWidth, 215, 220 - barWidth, 6, t.muted);
    }
}

void updateGaugeNeedle(float voltage) {
    int cx = 120, cy = 180, r = 85;
    Theme t = themes[currentTheme];
    float constrainedV = constrain(voltage, 0.0, 5.0);
    float targetAngle = map(constrainedV * 100, 0, 500, 180, 360);
    
    if (abs(targetAngle - lastDrawnNeedleAngle) > 1.0) {
        if (lastDrawnNeedleAngle != -999) {
            float radOld = lastDrawnNeedleAngle * DEG_TO_RAD;
            tft.drawLine(cx, cy, cx + (r * cos(radOld)), cy + (r * sin(radOld)), t.bg);
        }
        float radNew = targetAngle * DEG_TO_RAD;
        tft.drawLine(cx, cy, cx + (r * cos(radNew)), cy + (r * sin(radNew)), t.accent);
        tft.fillCircle(cx, cy, 6, t.text);
        lastDrawnNeedleAngle = targetAngle;
    }
}

// ── PZEM Direct Reset ─────────────────────────────────────────
void resetPZEMEnergy() {
    uint16_t u16CRC = 0xFFFF;
    uint8_t slaveAddr = 0x01;
    uint8_t cmd = 0x42; // Reset Command
    
    u16CRC = pzem_crc(u16CRC, slaveAddr);
    u16CRC = pzem_crc(u16CRC, cmd);
    
    preTransmission();
    Serial1.write(slaveAddr);
    Serial1.write(cmd);
    Serial1.write(lowByte(u16CRC));
    Serial1.write(highByte(u16CRC));
    delay(10);
    postTransmission();
    
    measuredEnergy = 0; // Reset local display instantly
}

// ── Web Handlers ──────────────────────────────────────────────
void handleRoot() { server.send(200, "text/html", htmlPage); }

void handleData() {
    char json[300];
    sprintf(json, "{\"mv\":%.2f, \"ma\":%.2f, \"mp\":%.2f, \"me\":%.0f, \"set\":%.2f, \"theme\":%d, \"mode\":%d, \"cfg\":{\"en\":%s, \"vy\":%.1f, \"vr\":%.1f, \"iy\":%.1f, \"ir\":%.1f, \"py\":%.0f, \"pr\":%.0f}}",
            measuredVoltage, measuredCurrent, measuredPower, measuredEnergy, setVoltage, currentTheme, softwareControlMode ? 1 : 0,
            dynCol ? "true" : "false", v_y, v_r, i_y, i_r, p_y, p_r);
    server.send(200, "application/json", json);
}

void handleSet() {
    if (server.hasArg("v")) { setVoltage = server.arg("v").toFloat(); }
    server.send(200, "text/plain", "OK");
}

void handleMode() {
    if (server.hasArg("m")) {
        softwareControlMode = (server.arg("m") == "1");
        if (!softwareControlMode) lastPotValue = -1;
    }
    server.send(200, "text/plain", "OK");
}

void handleTheme() {
    if (server.hasArg("t")) {
        int newTheme = server.arg("t").toInt();
        if (newTheme >= 0 && newTheme <= 4) {
            currentTheme = newTheme;
            preferences.putUInt("theme", currentTheme);
            drawStaticUI();
        }
    }
    server.send(200, "text/plain", "OK");
}

void handleConfig() {
    dynCol = (server.arg("dy") == "1");
    v_y = server.arg("vy").toFloat(); v_r = server.arg("vr").toFloat();
    i_y = server.arg("iy").toFloat(); i_r = server.arg("ir").toFloat();
    p_y = server.arg("py").toFloat(); p_r = server.arg("pr").toFloat();
    
    preferences.putBool("dynCol", dynCol);
    preferences.putFloat("v_y", v_y); preferences.putFloat("v_r", v_r);
    preferences.putFloat("i_y", i_y); preferences.putFloat("i_r", i_r);
    preferences.putFloat("p_y", p_y); preferences.putFloat("p_r", p_r);
    
    server.send(200, "text/plain", "OK");
    drawStaticUI(); // Force redraw colors
}

void handleResetEnergy() {
    resetPZEMEnergy();
    server.send(200, "text/plain", "OK");
}