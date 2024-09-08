#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>

// Thiết lập access point
const char *ssidWifiAP = "ESP";           // Tên WIFI của ESP
const char *passwordWifiAP = "12345678";  // Mật khẩu của ESP
IPAddress ip(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

// thiết lập update OTA
const char *updateHost = "esp";
const char *updateEndpoint = "/firmware";
const char *updateUsername = "admin";  // Username khi up fw
const char *updatePassword = "admin";  // Mật khẩu khi up fw

// Thiết lập chức năng ESP
const int hallSensorPin = D2;  // Chân D2 trên ESP, kết nối với dây cảm biến quạt (thường là dây màu vàng)
const int fanPWMPin = D1;      // Chân D1 trên ESP, dây PWM điều khiển quạt (thường là dây màu xanh)
volatile unsigned long pulseCount = 0;
unsigned long previousMillis = 0;
const long interval = 1000;  // Thời gian để tính tốc độ quạt (1 giây)
int speed = 66;
float currentRPM = 0;

const char *indexPage = R"rawliteral(
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

      .wifi-connect {
        width: fit-content;
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

    <div class="wifi-connect">
      <label for="fname">SSID</label><br />
      <input type="text" id="ssid" /><br />
      <label for="lname">Password</label><br />
      <input type="text" id="password" /><br />
      <button id="btnConnectWifi" onclick="setWifiConnect()">Connect</button>

      <p id="newAddress" style="display: none">
        IP address in network is <span id="newIpAddress"></span>
      </p>
    </div>

    <script>
      async function setWifiConnect() {
        document.querySelector("#btnConnectWifi").disabled = true;
        const ssid = document.querySelector("#ssid").value;
        const password = document.querySelector("#password").value;

        let data = await fetch(
          "/setwifi?ssid=" + ssid + "&password=" + password
        );
        data = await data.text();
        // console.log(data);
        document.querySelector("#btnConnectWifi").disabled = false;

        if (data === "FAIL")
          return alert("Connect WIFI fail, refresh site then connect again");

        document.querySelector("#newIpAddress").innerText = data;
        document.querySelector("#newAddress").style.display = "block";
      }

      async function getSpeedInternal() {
        getSpeed();
      }

      async function getSpeed() {
        try {
          let data = await fetch("/getspeed");

          data = await data.text();
          data = data.split(",");

          const currentSpeed = data[0];
          const range = data[1];

          document.querySelector("#currentSpeed").innerText = currentSpeed;
          document.querySelector(".value").innerText = range;
          document.querySelector("#range").value = range;
          document.querySelector(
            "#range"
          ).style.background = `linear-gradient(to right, #f50 ${range}%, #ccc ${range}%)`;

          setTimeout(getSpeedInternal, 1e3);
        } catch (error) {
          console.log(error);
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
        // getSpeed();
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
  httpServer.send(200, "text/plain", String(currentRPM) + ',' + String(speed));
}

void handleSetSpeed() {
  if (httpServer.hasArg("speed")) {
    speed = httpServer.arg("speed").toInt();
    setFanSpeed(speed);
    httpServer.send(200, "text/plain", "SUCCESS");
  } else {
    httpServer.send(400, "text/plain", "FAIL");
  }
}

void handleSetWifi() {
  int count = 0;
  if (httpServer.hasArg("ssid")) {
    Serial.println("Connect to " + String(httpServer.arg("ssid")) + ":" + String(httpServer.arg("password")));

    WiFi.begin(String(httpServer.arg("ssid")), String(httpServer.arg("password")));

    while (WiFi.waitForConnectResult() != WL_CONNECTED) {
      WiFi.begin(String(httpServer.arg("ssid")), String(httpServer.arg("password")));
      Serial.println("Retry...");

      count++;

      if (count == 3) {
        Serial.println("Connect fail");
        return httpServer.send(400, "text/plain", "FAIL");
      }
    }

    Serial.print("Connected with address " + WiFi.localIP().toString());

    httpServer.send(200, "text/plain", WiFi.localIP().toString());

    Serial.setTimeout(3000);

    // WiFi.softAPdisconnect();
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
  analogWrite(fanPWMPin, speed);

  Serial.println();
  Serial.println();
  Serial.println("ESP booting...");

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ssidWifiAP, passwordWifiAP);
  WiFi.softAPConfig(ip, gateway, subnet);

  MDNS.begin(updateHost);

  // Tạo server
  httpServer.on("/", handleRoot);
  httpServer.on("/getspeed", handleGetSpeed);
  httpServer.on("/setspeed", handleSetSpeed);
  httpServer.on("/setwifi", handleSetWifi);

  httpUpdater.setup(&httpServer, updateEndpoint, updateUsername, updatePassword);
  httpServer.begin();

  MDNS.addService("http", "tcp", 80);

  Serial.println("Boot success");
}

void loop(void) {
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    // Tính tốc độ quạt
    float rpm = (pulseCount / 2) * (60000 / interval);  // Chia cho 2 vì mỗi vòng quạt có 2 xung (từ trường đi qua cảm biến)
    currentRPM = rpm;
    pulseCount = 0;

    // Serial.print("Fan Speed: ");
    // Serial.print(rpm);
    // Serial.println(" RPM");
  }

  httpServer.handleClient();
}