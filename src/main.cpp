#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <Update.h>

const char* ssid = "DMA-IR-Bluster";
const char* password = "dmabd987";
const char* mqtt_server = "broker2.dma-bd.com";
const int mqtt_port = 1883;
const char* mqtt_user = "broker2";
const char* mqtt_pass = "Secret!@#$1234";
const char* mqtt_topic = "DMA/OTA/PUB";
const char* ota_command_topic = "DMA/OTA/SUB";
const char* ota_url = "https://raw.githubusercontent.com/rezaul-rimon/OTA-Practice/main/ota/firmware.bin";
const char* device_id = "OTA-1357";

void performOTA();
WiFiClient espClient;
PubSubClient client(espClient);

void connectWiFi() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
  Serial.println("\nWiFi Connected.");
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.printf("Message arrived [%s]: ", topic);
  String message;
  for (int i = 0; i < length; i++) message += (char)payload[i];
  Serial.println(message);
  if (String(topic) == ota_command_topic && message == "update_firmware") {
    Serial.println("OTA update command received.");
    performOTA();
  }
}

void mqttReconnect() {
  while (!client.connected()) {
    Serial.println("Connecting to MQTT...");
    String clientId = "";
    for (int i = 0; i < 10; i++) clientId += String("0123456789ABCDEF"[esp_random() % 16]);
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println("MQTT Connected.");
      client.subscribe(mqtt_topic);
      client.subscribe(ota_command_topic);
    } else {
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.println(" trying again in 2 seconds...");
      vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
  }
}

/*
void performOTA() {
  Serial.println("Starting OTA update...");
  HTTPClient http;
  http.begin(ota_url);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();
    Serial.printf("Content-Length: %d bytes\n", contentLength);
    if (Update.begin(contentLength)) {
      Update.writeStream(http.getStream());
      if (Update.end() && Update.isFinished()) {
        Serial.println("OTA update completed. Restarting...");
        ESP.restart();
      } else {
        Serial.println("OTA update failed!");
      }
    }
  } else {
    Serial.printf("HTTP request failed, error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}
*/

void performOTA() {
  Serial.println("Starting OTA update...");

  HTTPClient http;
  http.begin(ota_url);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();
    Serial.printf("Content-Length: %d bytes\n", contentLength);
    if (Update.begin(contentLength)) {
      Update.writeStream(http.getStream());
      if (Update.end() && Update.isFinished()) {
        Serial.println("OTA update completed. Restarting...");
        client.loop(); // Ensure MQTT client processes the publish
        client.publish(mqtt_topic, "OTA update successful");
        vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay to allow message to send
        ESP.restart();
      } else {
        Serial.println("OTA update failed!");
        client.publish(mqtt_topic, "OTA update failed, restarting with last firmware");
      }
    } else {
      Serial.println("OTA begin failed!");
      client.publish(mqtt_topic, "OTA begin failed, restarting with last firmware");
    }
  } else {
    Serial.printf("HTTP request failed, error: %s\n", http.errorToString(httpCode).c_str());
    client.publish(mqtt_topic, "OTA HTTP request failed, restarting with last firmware");
  }
  http.end();

  vTaskDelay(1000 / portTICK_PERIOD_MS); // Give time for MQTT message to send
  ESP.restart();
}


void networkTask(void* pvParameters) {
  connectWiFi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
  mqttReconnect();
  while (true) {
    if (!client.connected()) mqttReconnect();
    client.loop();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void sensorTask(void* pvParameters) {
  while (true) {
    float temp = random(2000, 3500) / 100.0;
    float hum = random(3000, 7000) / 100.0;
    char payload[100];
    sprintf(payload, "%s,%.2f,%.2f", device_id, temp, hum);
    Serial.printf("Publishing: %s\n", payload);
    client.publish(mqtt_topic, payload);
    vTaskDelay(15000 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 FreeRTOS MQTT OTA Starting...");
  xTaskCreatePinnedToCore(networkTask, "Network Task", 4096, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(sensorTask, "Sensor Task", 4096, NULL, 1, NULL, 1);
}

void loop() { vTaskDelete(NULL); }
