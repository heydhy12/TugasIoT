#include <WiFi.h>
#include <PubSubClient.h>
#include <HX711.h>
#include <OneWire.h>
#include <DallasTemperature.h>

const char* ssid = "Wokwi-GUEST";
const char* password = "";

#define tb_server "demo.thingsboard.io"
const int tb_port = 1883;
#define tb_token "heydhy12"

WiFiClient espClient;
PubSubClient client(espClient);

// Pins for relays and sensors
#define LOAD_CELL_DT_PIN 25
#define LOAD_CELL_SCK_PIN 33
#define DS18B20_PIN 12
#define HC_SR04_TRIG_PIN 14
#define HC_SR04_ECHO_PIN 27
#define PH_PIN 34
#define TURBIDITY_PIN 35
#define RELAY_PUMP_PIN 2
#define RELAY_MOTOR_PIN 4
#define RELAY_HEATER_PIN 5
#define RELAY_OUTLET_VALVE_PIN 18
#define RELAY_CHEESE_PRESS_PIN 19

// Setup HX711 load cell
HX711 scale;
OneWire oneWire(DS18B20_PIN);
DallasTemperature sensors(&oneWire);

long duration;
int distance;
float temperature = 0.0;
float pH = 0.0;
const int adcMaxValue = 4095;

// Rentang pH (biasanya pH air 0-14)
const float pHMin = 0.0;
const float pHMax = 14.0;

// Rentang Turbidity 0-400
const float turMin = 0.0;
const float turMax = 400.0;

float turbidity = 0.0;
float weight = 0.0;
unsigned long pressStartTime = 0;
bool pressActive = false;

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Koneksi WiFi...");
  }
  Serial.println("Terhubung ke WiFi");

  scale.begin(LOAD_CELL_DT_PIN, LOAD_CELL_SCK_PIN);
  sensors.begin();

  pinMode(RELAY_PUMP_PIN, OUTPUT);
  pinMode(RELAY_MOTOR_PIN, OUTPUT);
  pinMode(RELAY_HEATER_PIN, OUTPUT);
  pinMode(RELAY_OUTLET_VALVE_PIN, OUTPUT);
  pinMode(RELAY_CHEESE_PRESS_PIN, OUTPUT);

  client.setServer(tb_server, tb_port);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Baca sensor ultrasonik
  distance = readUltrasonic();

  // Pembacaan data pH
  int pHValue = analogRead(PH_PIN);
  pH = (pHValue / (float)adcMaxValue) * (pHMax - pHMin) + pHMin;


  // Pembacaan data kekeruhan (turbidity)
  int turbValue = analogRead(TURBIDITY_PIN);
  turbidity = (turbValue / (float)adcMaxValue) * (turMax - turMin) + turMin;


  // Pembacaan suhu
  sensors.requestTemperatures();
  temperature = sensors.getTempCByIndex(0);

  // Berat dari load cell
  weight = scale.get_units(10);

  // Kirim data ke ThingsBoard
  sendDataToThingsBoard();

  // Logika kontrol tangki
  tankControl();

  delay(1000);
}



void tankControl() {
  // Stage 1: Filling the tank
  if (distance > 400) {
    digitalWrite(RELAY_PUMP_PIN, HIGH);  // Turn on pump
    digitalWrite(RELAY_OUTLET_VALVE_PIN, HIGH); // Open inlet valve
  } else if (distance < 0) {
    digitalWrite(RELAY_PUMP_PIN, LOW);  // Turn off pump
    digitalWrite(RELAY_OUTLET_VALVE_PIN, LOW); // Close inlet valve
  }

  // Stage 2: Tank full and start heating/mixing
  if (distance < 5) {
    digitalWrite(RELAY_MOTOR_PIN, HIGH);  // Turn on motor
    digitalWrite(RELAY_HEATER_PIN, HIGH); // Turn on heater

    if (pH <= 5 && temperature >= 53 && weight >= 5000) {
      digitalWrite(RELAY_MOTOR_PIN, LOW);  // Turn off motor
      digitalWrite(RELAY_HEATER_PIN, LOW); // Turn off heater
    }
  }

  // Stage 4: Cheese press operation
  if (weight <= 5 && !pressActive) {
    digitalWrite(RELAY_CHEESE_PRESS_PIN, HIGH); // Turn on cheese press
    pressStartTime = millis();
    pressActive = true;
  }

  if (pressActive && millis() - pressStartTime >= 300000) { // 5 minutes
    digitalWrite(RELAY_CHEESE_PRESS_PIN, LOW); // Turn off cheese press
    pressActive = false;
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  // Callback for incoming MQTT messages (not used in this code)
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Menghubungkan ke ThingsBoard...");
    if (client.connect("ESP32_Client", tb_token, "")) {
      Serial.println("Berhasil");
    } else {
      Serial.print("Gagal, status=");
      Serial.print(client.state());
      Serial.println(" Coba lagi dalam 5 detik");
      delay(5000);
    }
  }
}

int readUltrasonic() {
  pinMode(HC_SR04_TRIG_PIN, OUTPUT);
  digitalWrite(HC_SR04_TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(HC_SR04_TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(HC_SR04_TRIG_PIN, LOW);
  pinMode(HC_SR04_ECHO_PIN, INPUT);
  duration = pulseIn(HC_SR04_ECHO_PIN, HIGH);
  distance = duration * 0.034 / 2;
  return distance;
}

void sendDataToThingsBoard() {
  String telemetryData = "{\"temperature\":" + String(temperature) +
                         ",\"pH\":" + String(pH, 2) +
                         ",\"turbidity\":" + String(turbidity, 2) + 
                         ",\"weight\":" + String(weight) +
                         ",\"distance\":" + String(distance) + "}";

  char attributes[telemetryData.length() + 1];
  telemetryData.toCharArray(attributes, telemetryData.length() + 1);

  client.publish("v1/devices/me/telemetry", attributes);
  Serial.println("Data terkirim: " + telemetryData);
}
