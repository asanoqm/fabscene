// original program "soracom-uptime.ino" / Copyright (C) Seeed K.K. / MIT License

#include <Adafruit_TinyUSB.h>
#include <WioCellular.h>
#include <ArduinoJson.h>
#include <csignal>
// センサー用のライブラリを追加
#include "SCD30.h"

#define SEARCH_ACCESS_TECHNOLOGY (WioCellularNetwork::SearchAccessTechnology::LTEM)  // https://seeedjp.github.io/Wiki/Wio_BG770A/kb/kb4.html
#define LTEM_BAND (WioCellularNetwork::NTTDOCOMO_LTEM_BAND)                          // https://seeedjp.github.io/Wiki/Wio_BG770A/kb/kb4.html
static const char APN[] = "soracom.io";

static const char HOST[] = "uni.soracom.io";
static constexpr int PORT = 23080;

template<typename MODULE> using CellularClient = WioCellularTcpClient2<MODULE>;  // TCP
// template<typename MODULE> using CellularClient = WioCellularUdpClient2<MODULE>; // UDP

// ここで送信のインターバルを指定
static constexpr int INTERVAL = 1000 * 60 * 5;         // [ms]
static constexpr int POWER_ON_TIMEOUT = 1000 * 20;     // [ms]
static constexpr int NETWORK_TIMEOUT = 1000 * 60 * 3;  // [ms]
static constexpr int CONNECT_TIMEOUT = 1000 * 10;      // [ms]
static constexpr int RECEIVE_TIMEOUT = 1000 * 10;      // [ms]

static void abortHandler(int sig) {
  Serial.printf("ABORT: Signal %d received\n", sig);
  yield();
  vTaskSuspendAll();  // FreeRTOS
  while (true) {
    ledOn(LED_BUILTIN);
    nrfx_coredep_delay_us(100000);  // Spin
    ledOff(LED_BUILTIN);
    nrfx_coredep_delay_us(100000);  // Spin
  }
}

static JsonDocument JsonDoc;

void setup() {
  signal(SIGABRT, abortHandler);
  Serial.begin(115200);
  {
    const auto start = millis();
    while (!Serial && millis() - start < 5000) {
      delay(2);
    }
  }
  Serial.println();
  Serial.println("Startup");
  digitalWrite(LED_BUILTIN, HIGH);

  // Network configuration
  WioNetwork.config.searchAccessTechnology = SEARCH_ACCESS_TECHNOLOGY;
  WioNetwork.config.ltemBand = LTEM_BAND;
  WioNetwork.config.apn = APN;

  // Start WioCellular
  WioCellular.begin();

  // Power on the cellular module
  if (WioCellular.powerOn(POWER_ON_TIMEOUT) != WioCellularResult::Ok) abort();
  WioNetwork.begin();

  // Wait for communication available
  if (!WioNetwork.waitUntilCommunicationAvailable(NETWORK_TIMEOUT)) abort();

  // Groveの電源を投入, 通信開始
  WioCellular.enableGrovePower();
  Wire.begin();

  digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  JsonDoc.clear();
  if (measure(JsonDoc)) {
    send(JsonDoc);
  }
  digitalWrite(LED_BUILTIN, LOW);
  WioCellular.doWorkUntil(INTERVAL);
}


// static bool measure(JsonDocument &doc) {
//   Serial.println("### Measuring");
//   doc["uptime"] = millis() / 1000;
//   Serial.println("### Completed");
//   return true;
// }

static bool measure(JsonDocument &doc) {
  Serial.println("### Measuring");

  float result[3] = {0};

  if (!scd30.isAvailable()) {
    Serial.println("No data available from SCD30");
    return false;
  }

  scd30.getCarbonDioxideConcentration(result);

  doc["uptime"] = millis() / 1000;
  doc["co2"] = result[0];
  doc["temperature"] = result[1];
  doc["humidity"] = result[2];

  Serial.println("### Completed");
  return true;
}


static bool send(const JsonDocument &doc) {
  Serial.println("### Sending");
  Serial.print("Connecting ");
  Serial.print(HOST);
  Serial.print(":");
  Serial.println(PORT);
  {
    CellularClient<WioCellularModule> client{ WioCellular };
    if (!client.open(WioNetwork.config.pdpContextId, HOST, PORT)) {
      Serial.printf("ERROR: Failed to open %s\n", WioCellularResultToString(client.getLastResult()));
      return false;
    }
    if (!client.waitForConnect(CONNECT_TIMEOUT)) {
      Serial.printf("ERROR: Failed to connect %s\n", WioCellularResultToString(client.getLastResult()));
      return false;
    }
    Serial.print("Sending ");
    std::string str;
    serializeJson(doc, str);
    printData(Serial, str.data(), str.size());
    Serial.println();
    if (!client.send(str.data(), str.size())) {
      Serial.printf("ERROR: Failed to send socket %s\n", WioCellularResultToString(client.getLastResult()));
      return false;
    }

    Serial.println("Receiving");
    static uint8_t recvData[WioCellular.RECEIVE_SOCKET_SIZE_MAX];
    size_t recvSize;
    if (!client.receive(recvData, sizeof(recvData), &recvSize, RECEIVE_TIMEOUT)) {
      Serial.printf("ERROR: Failed to receive socket %s\n", WioCellularResultToString(client.getLastResult()));
      return false;
    }
    printData(Serial, recvData, recvSize);
    Serial.println();
  }
  Serial.println("### Completed");
  return true;
}

template<typename T>
void printData(T &stream, const void *data, size_t size) {
  auto p = static_cast<const char *>(data);
  for (; size > 0; --size, ++p)
    stream.write(0x20 <= *p && *p <= 0x7f ? *p : '.');
}
