#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

const int hallSensorPin = D3; // Chân D3 trên ESP, kết nối với dây cảm biến quạt (dây màu vàng)
const int fanPWMPin = D1; // Chân D1 trên ESP, dây PWM điều khiển quạt (dây màu xanh)
volatile unsigned long pulseCount = 0;
unsigned long previousMillis = 0;
const long interval = 1000; // Thời gian để tính tốc độ quạt (1 giây)

const char* ssid = "Archer C7"; // Tên WIFI
const char* password = "0987718868"; // Mật khẩu WIFI
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

ESP8266WebServer server(80);

void IRAM_ATTR countPulse() {
  pulseCount++;
}

void handleRoot() {
  server.send(200, "text/html", indexPage);
}

void setFanSpeed(int percentage) {
  // Đảm bảo giá trị nằm trong khoảng 0-100
  percentage = constrain(percentage, 0, 100);

  // Chuyển đổi phần trăm thành giá trị PWM (0-255)
  int pwmValue = map(percentage, 0, 100, 0, 255);

  analogWrite(fanPWMPin, pwmValue);
}

void handleGetSpeed() {
  server.send(200, "text/plain", String(currentRPM));
}

void handleSetSpeed() {
  if (server.hasArg("speed")) {
    int speed = server.arg("speed").toInt();
    setFanSpeed(speed);
    server.send(200, "text/plain", "SUCCESS");
  } else {
    server.send(400, "text/plain", "FAIL");
  }
}

void setup() {
  Serial.begin(9600);

  pinMode(hallSensorPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(hallSensorPin), countPulse, FALLING);

  pinMode(fanPWMPin, OUTPUT);
  analogWrite(fanPWMPin, 0);

  Serial.println("");
  Serial.println("");
  Serial.print("Connect to WIFI");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("Connected. ");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/getspeed", handleGetSpeed);
  server.on("/setspeed", handleSetSpeed);
  server.begin();
  Serial.println("WebServer started");
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    // Tính tốc độ quạt
    float rpm = (pulseCount / 2) * (60000 / interval);  // Chia cho 2 vì mỗi vòng quạt có 2 xung (từ trường đi qua cảm biến)
    currentRPM = rpm;
    pulseCount = 0;

    Serial.print("Fan Speed: ");
    Serial.print(rpm);
    Serial.println(" RPM");
  }

  server.handleClient();
}