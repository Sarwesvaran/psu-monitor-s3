# Hyper Entity: Power Supply Monitor & Control (PSMC) ⚡

A high-performance, dual-core smart power supply controller and monitor built on the ESP32-S3. Designed to interface with a PZEM-017 DC Energy Meter and an SMPS, this project features a 2-inch IPS display, an ultra-fast Web UI, and a non-volatile custom theme engine.

## ✨ Key Features
* **Dual-Core Architecture (FreeRTOS):** Core 0 handles hardware polling (Modbus, PWM, LCD) while Core 1 is strictly dedicated to hosting an instant-response (200ms) Web UI.
* **PZEM-017 Modbus Integration:** Industrial-grade reading of Voltage, Current, Power (W), and Energy (Wh) using RS485.
* **Instant Hardware SPI Display:** Blazing fast screen refreshes on the ST7789 2-inch IPS display.
* **Dual-Mode Control:** Switch seamlessly between physical Potentiometer control and Web UI slider overrides.
* **NVS Theme Engine:** 5 built-in UI themes (including an algorithmic Analog Gauge) that save to flash memory.
* **Dynamic Range Colors:** User-configurable thresholds turn readouts green, yellow, or red based on live data (configurable via Web UI).

## 🛠️ Hardware Requirements
* **Microcontroller:** Waveshare ESP32-S3-Zero (or similar S3 board)
* **Display:** Waveshare 2-inch LCD Module (ST7789, 240x320, SPI)
* **Energy Meter:** PZEM-017 + 200A Shunt
* **RS485 Transceiver:** MAX485 TTL to RS485 Module
* **Control:** 10K Linear Potentiometer
* **Output Filter:** RC Low-Pass filter + Op-Amp (for scaling 3.3V PWM to 5V SMPS control)

## 🔌 Wiring & Pinout

| Component | Pin Name | ESP32-S3 Pin | Note |
| :--- | :--- | :--- | :--- |
| **ST7789 LCD** | MOSI (DIN) | GPIO 7 | Hardware SPI |
| | SCLK (CLK) | GPIO 6 | Hardware SPI |
| | CS | GPIO 5 | |
| | DC | GPIO 4 | |
| | RST | GPIO 3 | |
| | BL | GPIO 2 | Backlight (Active High) |
| **MAX485** | RO (RX) | GPIO 12 | *Requires 3.3V logic level shift if MAX485 is powered by 5V* |
| | DI (TX) | GPIO 8 | |
| | RE & DE | GPIO 9 | Shorted together for TX/RX toggle |
| **Potentiometer** | Wiper | GPIO 10 | Connect ends to 3.3V and GND |
| **SMPS Control** | PWM Out | GPIO 11 | Route to RC filter and Op-Amp |

## 💻 Software Dependencies
This project is built using **PlatformIO**. Add the following to your `platformio.ini` `lib_deps`:

```ini
lib_deps =
    adafruit/Adafruit GFX Library@^1.11.9
    adafruit/Adafruit ST7735 and ST7789 Library@^1.10.4
    4-20ma/ModbusMaster@^2.0.1

🚀 **Installation & Usage**

Clone this repository and open it in VS Code with PlatformIO.
Build and upload the code to your ESP32-S3-Zero.
Power on the device. The ESP32 will immediately broadcast a WiFi Access Point.
SSID: Power Supply
Password: 12345678
Connect to the network using your phone or PC and navigate to http://192.168.4.1.
Web UI Features:
View live V, A, W, and Wh.
Toggle between "Pot Mode" (hardware) and "Web Mode" (slider).
Change UI themes (saves automatically).
Access the ⚙️ Advanced Settings to tune dynamic color thresholds and reset the PZEM energy counter.

⚠️ **Important Safety Notes**

MAX485 Voltage: The MAX485 module requires 5V to communicate properly with the PZEM-017 over RS485. Because the ESP32-S3 is a 3.3V device, you must use a voltage divider (e.g., 1kΩ / 2kΩ) or use atleast 1K resistor in series or a logic level shifter on the RO line before it connects to GPIO 12 to prevent frying the ESP32.
SMPS Control: Ensure your Op-Amp gain is calibrated so that a 100% duty cycle on the 3.3V PWM pin equates perfectly to the 5.0V feedback required by your power supply.

Developed by **Sarvs Electric**
