/*
 * OrbitalGuard — Estacao Terrestre de Monitoramento Climatico (ESP32)
 * Global Solution FIAP 2026/1 — Disruptive Architectures: IoT, IOB & Generative IA
 * Turma 2TDSPW
 *
 * A estacao terrestre coleta dados ambientais de uma regiao monitorada
 * (temperatura, umidade e pressao atmosferica), classifica o RISCO CLIMATICO
 * localmente (edge computing) e publica a telemetria via MQTT para a plataforma
 * OrbitalGuard (camada de satelite + IA). Sinaliza o estado por LEDs, dispara um
 * buzzer em caso critico e exibe as leituras em um display LCD I2C.
 *
 * Simulado no Wokwi: https://wokwi.com
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <DHT.h>
#include <Adafruit_BMP280.h>
#include <LiquidCrystal_I2C.h>

// ----- Identificacao da estacao / regiao monitorada -----
const char* ESTACAO_ID  = "EST-SP-001";
const int   REGIAO_ID    = 1;
const char* REGIAO_NOME  = "Litoral Norte-SP";

// ----- Rede Wi-Fi (Wokwi) -----
const char* SSID     = "Wokwi-GUEST";
const char* PASSWORD = "";

// ----- Broker MQTT publico -----
const char* MQTT_SERVER = "broker.hivemq.com";
const int   MQTT_PORT   = 1883;

// ----- Topicos MQTT (3 topicos documentados) -----
const char* TOPIC_TELEMETRIA = "orbitalguard/estacao/telemetria"; // leituras periodicas
const char* TOPIC_ALERTA     = "orbitalguard/estacao/alerta";     // eventos de risco
const char* TOPIC_STATUS     = "orbitalguard/estacao/status";     // heartbeat online/offline

// ----- Pinos -----
#define DHT_PIN       4
#define DHT_TYPE      DHT22
#define LED_VERDE     25   // risco BAIXO / NORMAL
#define LED_AMARELO   26   // risco MEDIO (ALERTA)
#define LED_VERMELHO  27   // risco CRITICO
#define BUZZER_PIN    14   // alarme sonoro (saida 2)

// ----- Limiares de classificacao de risco climatico -----
const float TEMP_ALTA      = 38.0;    // calor extremo (graus C)
const float UMID_BAIXA     = 30.0;    // ar muito seco -> risco de queimada (%)
const float PRESSAO_BAIXA  = 1000.0;  // queda de pressao -> tempestade/ciclone (hPa)

DHT dht(DHT_PIN, DHT_TYPE);
Adafruit_BMP280 bmp;                  // I2C (0x76)
LiquidCrystal_I2C lcd(0x27, 16, 2);   // I2C (0x27)

WiFiClient espClient;
PubSubClient mqtt(espClient);

unsigned long ultimoEnvio = 0;
const unsigned long INTERVALO = 5000; // 5 s
bool bmpOk = false;

// ----------------------------------------------------------------------------
void conectarWiFi() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Conectando WiFi");
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado! IP: " + WiFi.localIP().toString());
}

void conectarMQTT() {
  while (!mqtt.connected()) {
    String clientId = String("orbitalguard-") + ESTACAO_ID;
    if (mqtt.connect(clientId.c_str())) {
      Serial.println("MQTT conectado ao broker " + String(MQTT_SERVER));
      // Publica status "ONLINE" como mensagem retida
      StaticJsonDocument<160> st;
      st["estacao"] = ESTACAO_ID;
      st["regiao"]  = REGIAO_NOME;
      st["estado"]  = "ONLINE";
      char buf[160];
      serializeJson(st, buf);
      mqtt.publish(TOPIC_STATUS, buf, true);
    } else {
      Serial.println("Falha MQTT, rc=" + String(mqtt.state()) + " tentando novamente...");
      delay(1500);
    }
  }
}

// Classifica o risco combinando os 3 fatores ambientais.
// Retorna o nivel textual e devolve, por referencia, a quantidade de fatores ativos.
String classificarRisco(float temp, float umid, float pressao, int &fatores) {
  fatores = 0;
  if (temp >= TEMP_ALTA)        fatores++;
  if (umid <= UMID_BAIXA)       fatores++;
  if (pressao <= PRESSAO_BAIXA) fatores++;

  if (fatores >= 2) return "CRITICO";
  if (fatores == 1) return "ALERTA";
  return "NORMAL";
}

void atualizarSaidas(const String& risco) {
  digitalWrite(LED_VERDE,    risco == "NORMAL"  ? HIGH : LOW);
  digitalWrite(LED_AMARELO,  risco == "ALERTA"  ? HIGH : LOW);
  digitalWrite(LED_VERMELHO, risco == "CRITICO" ? HIGH : LOW);

  if (risco == "CRITICO") {
    tone(BUZZER_PIN, 1000);  // alarme continuo enquanto o risco for critico
  } else {
    noTone(BUZZER_PIN);
  }
}

// ----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_AMARELO, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  Wire.begin(21, 22);   // SDA=21, SCL=22
  lcd.init();
  lcd.backlight();
  dht.begin();

  bmpOk = bmp.begin(0x76);
  if (!bmpOk) {
    Serial.println("AVISO: BMP280 nao encontrado, usando pressao padrao 1013 hPa.");
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("OrbitalGuard");
  lcd.setCursor(0, 1);
  lcd.print(REGIAO_NOME);
  delay(2000);

  conectarWiFi();
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
}

void loop() {
  if (!mqtt.connected()) conectarMQTT();
  mqtt.loop();

  if (millis() - ultimoEnvio < INTERVALO) return;
  ultimoEnvio = millis();

  float temp    = dht.readTemperature();
  float umid    = dht.readHumidity();
  float pressao = bmpOk ? (bmp.readPressure() / 100.0F) : 1013.0; // Pa -> hPa

  if (isnan(temp) || isnan(umid)) {
    Serial.println("Falha na leitura do DHT22.");
    return;
  }

  int fatores;
  String risco = classificarRisco(temp, umid, pressao, fatores);
  atualizarSaidas(risco);

  // ----- Display LCD -----
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.printf("T:%.1fC U:%.0f%%", temp, umid);
  lcd.setCursor(0, 1);
  lcd.printf("P:%.0f %s", pressao, risco.c_str());

  // ----- Telemetria (JSON) -----
  StaticJsonDocument<256> doc;
  doc["estacao"]     = ESTACAO_ID;
  doc["regiao_id"]   = REGIAO_ID;
  doc["regiao"]      = REGIAO_NOME;
  doc["temperatura"] = round(temp * 10) / 10.0;
  doc["umidade"]     = round(umid * 10) / 10.0;
  doc["pressao_hpa"] = round(pressao * 10) / 10.0;
  doc["risco"]       = risco;
  doc["timestamp"]   = millis();

  char payload[256];
  serializeJson(doc, payload);
  mqtt.publish(TOPIC_TELEMETRIA, payload);
  Serial.println(String("[TELEMETRIA] ") + payload);

  // ----- Alerta dedicado quando ha risco -----
  if (risco != "NORMAL") {
    StaticJsonDocument<256> al;
    al["estacao"]   = ESTACAO_ID;
    al["regiao_id"] = REGIAO_ID;
    al["regiao"]    = REGIAO_NOME;
    al["nivel"]     = risco;
    al["fatores"]   = fatores;

    String causa = "";
    if (temp >= TEMP_ALTA)        causa += "calor_extremo;";
    if (umid <= UMID_BAIXA)       causa += "ar_seco;";
    if (pressao <= PRESSAO_BAIXA) causa += "queda_pressao;";
    al["causa"]     = causa;
    al["timestamp"] = millis();

    char abuf[256];
    serializeJson(al, abuf);
    mqtt.publish(TOPIC_ALERTA, abuf);
    Serial.println(String("[ALERTA] ") + abuf);
  }
}
