#include <WebServer.h>
#include <WiFi.h>
#include <esp32cam.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#define API_KEY "AIzaSyD404wKaLURJrhck0XMSLtvpC2KR4MfMZk"
#define DATABASE_URL "https://capstone-afdd7-default-rtdb.asia-southeast1.firebasedatabase.app/"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long sendDataPrevMillis = 0;
bool signupOK = false;

const char* WIFI_SSID = "FREE";
const char* WIFI_PASS = "12345679";

WebServer server(80);

unsigned int altitude;
unsigned int batteryRemaining;
int32_t latitude;    // vi do
int32_t longtitude;  // kinh do
int32_t pressure;
uint32_t temperature;
int32_t yaw;

const unsigned int MAX_MESSAGE_LENGTH = 64;
bool startMessage = false;
bool endMessage = false;
char message[MAX_MESSAGE_LENGTH];
unsigned int message_pos = 0;

static auto loRes = esp32cam::Resolution::find(480, 320);
static auto midRes = esp32cam::Resolution::find(350, 530);
static auto hiRes = esp32cam::Resolution::find(800, 600);

void serveJpg() {
  auto frame = esp32cam::capture();
  if (frame == nullptr) {
    // Serial.println("CAPTURE FAIL");
    server.send(503, "", "");
    return;
  }
  // Serial.printf("CAPTURE OK %dx%d %db\n", frame->getWidth(), frame->getHeight(),
  //               static_cast<int>(frame->size()));

  server.setContentLength(frame->size());
  server.send(200, "image/jpeg");
  WiFiClient client = server.client();
  frame->writeTo(client);
}

void handleJpgLo() {
  if (!esp32cam::Camera.changeResolution(loRes)) {
    // Serial.println("SET-LO-RES FAIL");
  }
  serveJpg();
}

void handleJpgHi() {
  if (!esp32cam::Camera.changeResolution(hiRes)) {
    // Serial.println("SET-HI-RES FAIL");
  }
  serveJpg();
}

void handleJpgMid() {
  if (!esp32cam::Camera.changeResolution(midRes)) {
    // Serial.println("SET-MID-RES FAIL");
  }
  serveJpg();
}

bool writeIntToFirebase(const char* path, int value) {
  for (int i = 0; i < 5; i++) {  // Retry up to 5 times
    if (Firebase.RTDB.setInt(&fbdo, path, value)) {
      return true;
    } else {
      //Serialprintf("Failed to write %s: %s\n", path, fbdo.errorReason().c_str());
      vTaskDelay(1000 / portTICK_PERIOD_MS);  // Wait 1 second before retrying
    }
  }
  return false;
}

void updateFirebaseTask(void* pvParameters) {
  for (;;) {
    if (Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > 1000 || sendDataPrevMillis == 0)) {
      sendDataPrevMillis = millis();

      writeIntToFirebase("more_data/battery", batteryRemaining);
      writeIntToFirebase("more_data/lng", longtitude);
      writeIntToFirebase("more_data/lat", latitude);
      writeIntToFirebase("more_data/direction", yaw);
      writeIntToFirebase("more_data/height", altitude);
      writeIntToFirebase("more_data/pressure", pressure);
      writeIntToFirebase("more_data/temperature", temperature);
    }
    vTaskDelay(2000 / portTICK_PERIOD_MS);  // Ensure the task runs every 1 second
  }
}

void setup() {
  Serial.begin(115200);
  //Serialprintln();
  {
    using namespace esp32cam;
    Config cfg;
    cfg.setPins(pins::AiThinker);
    cfg.setResolution(loRes);
    cfg.setBufferCount(1);
    cfg.setJpeg(90);
    cfg.setXCLK(15000000);

    bool ok = Camera.begin(cfg);
    //Serialprintln(ok ? "CAMERA OK" : "CAMERA FAIL");
  }
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  // Serial.print("http://");
  // Serial.println(WiFi.localIP());
  // Serial.println("  /cam-lo.jpg");
  // Serial.println("  /cam-hi.jpg");
  // Serial.println("  /cam-mid.jpg");

  server.on("/cam-lo.jpg", handleJpgLo);
  // server.on("/cam-hi.jpg", handleJpgHi);
  // server.on("/cam-mid.jpg", handleJpgMid);

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  if (Firebase.signUp(&config, &auth, "", "")) {
    // Serial.println("ok");
    signupOK = true;
  } else {
    // Serial.printf("%s\n", config.signer.signupError.message.c_str());
  }

  config.token_status_callback = tokenStatusCallback;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  server.begin();

  altitude = 150;
  batteryRemaining = 50;
  latitude = 10879653;
  longtitude = 106808559;
  pressure = 100234;
  temperature = 25;
  yaw = 90;
  xTaskCreatePinnedToCore(handleSerialInputTask, "SerialInputTask", 8192, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(updateFirebaseTask, "FirebaseTask", 10000, NULL, 1, NULL, 1);
  // xTaskCreate(updateFirebaseTask, "UpdateFirebaseTask", 10000, NULL, 1, NULL);
}

void loop() {
  server.handleClient();
}

int str2Int(char* str) {
  char temp[20];
  int i = 3, j = 0;
  while (str[i] != '#') {
    temp[j] = str[i];
    j++;
    i++;
  }
  temp[j] = '\0';
  return atoi(temp);
}
void handleSerialInputTask(void* pvParameters) {
  while (true) {
    while (Serial.available() > 0) {
      char inByte = Serial.read();
      if (inByte == '!' && message_pos < MAX_MESSAGE_LENGTH - 1) {
        startMessage = true;
        message_pos = 0;
      }
      if (startMessage) {
        message[message_pos++] = inByte;
        if (inByte == '#') {
          message[message_pos] = '\0';
          processMessage();
          startMessage = false;
        }
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void processMessage() {
  if (strstr(message, "AL"))
    altitude = str2Int(message);
  else if (strstr(message, "BA"))
    batteryRemaining = str2Int(message);
  else if (strstr(message, "La"))
    latitude = str2Int(message);
  else if (strstr(message, "Lo"))
    longtitude = str2Int(message);
  else if (strstr(message, "PR"))
    pressure = str2Int(message);
  else if (strstr(message, "TE"))
    temperature = str2Int(message);
  else if (strstr(message, "YA"))
    yaw = str2Int(message);
}
