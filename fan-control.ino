#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>

const char* updateHost = "esp8266-webupdate";
const char* updateEndpoint = "/firmware";
const char* updateUsername = "admin";
const char* updatePassword = "admin";
const char* ssidWifi = "Archer C7"; // Thay tên WIFI vào (chỉ dùng được WIFI 2.4G)
const char* passwordWifi = "0987718868"; // Thay mật khẩu WIFI vào

const int hallSensorPin = D3; // Chân D3 trên ESP, kết nối với dây cảm biến quạt (dây màu vàng)
const int fanPWMPin = D1; // Chân D1 trên ESP, dây PWM điều khiển quạt (dây màu xanh)
volatile unsigned long pulseCount = 0;
unsigned long previousMillis = 0;
const long interval = 1000; // Thời gian để tính tốc độ quạt (1 giây)
float currentRPM = 0;

const char* indexPage = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
  <head>
    <title>FAN Control</title>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />

    <style>
      input[type="range"] {
        -webkit-appearance: none;
        appearance: none;
        width: 100%;
        cursor: pointer;
        outline: none;
        border-radius: 15px;
        height: 6px;
        background: #ccc;
      }

      input[type="range"]::-webkit-slider-thumb {
        -webkit-appearance: none;
        appearance: none;
        height: 15px;
        width: 15px;
        background-color: #f50;
        border-radius: 50%;
        border: none;
        transition: 0.2s ease-in-out;
      }

      input[type="range"]::-moz-range-thumb {
        height: 15px;
        width: 15px;
        background-color: #f50;
        border-radius: 50%;
        border: none;
        transition: 0.2s ease-in-out;
      }

      input[type="range"]::-webkit-slider-thumb:hover {
        box-shadow: 0 0 0 10px rgba(255, 85, 0, 0.1);
      }
      input[type="range"]:active::-webkit-slider-thumb {
        box-shadow: 0 0 0 13px rgba(255, 85, 0, 0.2);
      }
      input[type="range"]:focus::-webkit-slider-thumb {
        box-shadow: 0 0 0 13px rgba(255, 85, 0, 0.2);
      }

      input[type="range"]::-moz-range-thumb:hover {
        box-shadow: 0 0 0 10px rgba(255, 85, 0, 0.1);
      }
      input[type="range"]:active::-moz-range-thumb {
        box-shadow: 0 0 0 13px rgba(255, 85, 0, 0.2);
      }
      input[type="range"]:focus::-moz-range-thumb {
        box-shadow: 0 0 0 13px rgba(255, 85, 0, 0.2);
      }

      body {
        font-family: system-ui;
      }

      h1 {
        color: #4b4949;
        text-align: center;
      }

      .range {
        display: flex;
        align-items: center;
        gap: 1rem;
        max-width: 500px;
        margin: 0 auto;
        height: 4rem;
        width: 80%;
        background: #fff;
        padding: 0px 10px;
      }

      .wrapper-value {
        font-size: 26px;
      }

      .value {
        width: 50px;
        text-align: center;
      }
    </style>
  </head>
  <body>
    <h1>Fan Speed: <span id="currentSpeed">0</span> RPM</h1>

    <div class="wrapper">
      <div class="range">
        <input
          type="range"
          min="0"
          max="100"
          value="0"
          id="range"
          onchange="setSpeed(this.value)"
        />
        <div class="wrapper-value"><span class="value">0</span>%</div>
      </div>
    </div>

    <script>
      async function getSpeedInternal(params) {
        getSpeed();
      }

      async function getSpeed() {
        try {
          const data = await fetch("/getspeed");
          const currentSpeed = await data.json();

          document.getElementById("currentSpeed").innerText = currentSpeed;

          setTimeout(getSpeedInternal, 1e3);
        } catch (error) {
          return alert("ESP disconnected");
        }
      }

      async function setSpeed(speed) {
        await fetch("/setspeed?speed=" + speed);
      }

      const sliderEl = document.querySelector("#range");
      const sliderValue = document.querySelector(".value");

      sliderEl.addEventListener("input", (event) => {
        const tempSliderValue = event.target.value;

        sliderValue.textContent = tempSliderValue;

        const progress = (tempSliderValue / sliderEl.max) * 100;

        sliderEl.style.background = `linear-gradient(to right, #f50 ${progress}%, #ccc ${progress}%)`;
      });

      window.onload = () => {
        getSpeed();
      };
    </script>
  </body>
</html>

)rawliteral";

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

void IRAM_ATTR countPulse() {
  pulseCount++;
}

void setFanSpeed(int percentage) {
  // Đảm bảo giá trị nằm trong khoảng 0-100
  percentage = constrain(percentage, 0, 100);

  // Chuyển đổi phần trăm thành giá trị PWM (0-255)
  int pwmValue = map(percentage, 0, 100, 0, 255);

  analogWrite(fanPWMPin, pwmValue);
}

void handleRoot() {
  httpServer.send(200, "text/html", indexPage);
}

void handleGetSpeed() {
  httpServer.send(200, "text/plain", String(currentRPM));
}

void handleSetSpeed() {
  if (httpServer.hasArg("speed")) {
    int speed = httpServer.arg("speed").toInt();
    setFanSpeed(speed);
    httpServer.send(200, "text/plain", "SUCCESS");
  } else {
    httpServer.send(400, "text/plain", "FAIL");
  }
}

void setup(void) {
  // Khởi tạo serial với baud 115200
  Serial.begin(115200);

  pinMode(hallSensorPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(hallSensorPin), countPulse, FALLING);

  pinMode(fanPWMPin, OUTPUT);
  analogWrite(fanPWMPin, 0);

  Serial.println();
  Serial.println();
  Serial.println("ESP booting...");
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssidWifi, passwordWifi);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    WiFi.begin(ssidWifi, passwordWifi);
    Serial.println("WIFI connecting...");
  }
  // In địa chỉ IP
  Serial.print("ESP address: ");
  Serial.println(WiFi.localIP());
  MDNS.begin(updateHost);
  // Tạo server

  httpServer.on("/", handleRoot);
  httpServer.on("/getspeed", handleGetSpeed);
  httpServer.on("/setspeed", handleSetSpeed);

  httpUpdater.setup(&httpServer, updateEndpoint, updateUsername, updatePassword);
  httpServer.begin();

  MDNS.addService("http", "tcp", 80);
  Serial.printf("Firmware update at ESP address/firmware");
}

void loop(void) {

  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    // Tính tốc độ quạt
    float rpm = (pulseCount / 2) * (60000 / interval); // Chia cho 2 vì mỗi vòng quạt có 2 xung (từ trường đi qua cảm biến)
    currentRPM = rpm;
    pulseCount = 0;

    Serial.print("Fan Speed: ");
    Serial.print(rpm);
    Serial.println(" RPM");
  }

  httpServer.handleClient();
}