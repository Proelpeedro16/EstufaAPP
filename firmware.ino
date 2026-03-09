#include <ESP8266WiFi.h>
#include <Firebase_ESP_Client.h>
#include <DHT.h>
#include <time.h>

/* ================= CONFIGURAÇÕES ================= */

// Wi-Fi
#define WIFI_SSID     "Estufa-Alberico"
#define WIFI_PASSWORD "58505878"

// Firebase
#define API_KEY       "AIzaSyC6-erp_Gta2YwOMT9z9tamzspGv1BZR-E"
#define DATABASE_URL  "https://estufa-97b55-default-rtdb.firebaseio.com/"
#define USER_EMAIL    "pedrolucasalvesgonalves@gmail.com"
#define USER_PASSWORD "Pedrou@E"

// DHT
#define DHT_PIN  2     // D4
#define DHT_TYPE DHT11

// Relé
#define RELAY_PIN 5    // D1

#define MAX_HORARIOS 6
/* ================= VARIÁVEIS ================= */

DHT dht(DHT_PIN, DHT_TYPE);

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Controle
float temperaturaLigar = 30.0;
float offset = 3.0;
int duracaoMinutos = 90;
int horaInicio = 0;
int minutoInicio = 0;
int modoControle = 0;   // 0=OFF | 1=Temperatura | 2=Horário

bool releLigado = false;

bool sensorTemperaturaOK = true;

// Tempo
const unsigned long intervaloConfig = 30000;

FirebaseData stream;
/* ================= FUNÇÕES ================= */

void conectaWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Conectando WiFi");

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
    Serial.print(".");
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi conectado");
    configTime(-3 * 3600, 0, "pool.ntp.org");
  } else {
    Serial.println("\nFalha WiFi");
  }
}


int horas[MAX_HORARIOS];
int minutos[MAX_HORARIOS];
int totalHorarios = 0;

void lerHorariosFirebase() {

  if (Firebase.RTDB.getArray(&fbdo, "/config/inicioHorarios")) {

    FirebaseJsonArray arr = fbdo.jsonArray();
    FirebaseJsonData data;

    totalHorarios = arr.size();

    if (totalHorarios > MAX_HORARIOS)
      totalHorarios = MAX_HORARIOS;

    for (int i = 0; i < totalHorarios; i++) {

      arr.get(data, i);

      FirebaseJson json;
      json.setJsonData(data.stringValue);

      json.get(data, "hora");
      horas[i] = data.intValue;

      json.get(data, "min");
      minutos[i] = data.intValue;

      Serial.print("Horario carregado: ");
      Serial.print(horas[i]);
      Serial.print(":");
      Serial.println(minutos[i]);
    }
  }
  else {
    Serial.println("Erro ao ler inicioHorarios");
  }
}

bool dentroHorario() {
  if (agora < 100000) return false;
  time_t agora = time(nullptr);
  struct tm* t = localtime(&agora);

  int atual = t->tm_hour * 60 + t->tm_min;

  for (int i = 0; i < totalHorarios; i++) {

    int inicio = horas[i] * 60 + minutos[i];

    if (atual >= inicio && atual < inicio + duracaoMinutos) {
      return true;
    }
  }

  return false;
}

void enviarSensores(float temp, float hum) {

  if (!Firebase.ready()) return;

  if (sensorTemperaturaOK) {
    Firebase.RTDB.setFloat(&fbdo, "/sensores/temperatura", temp);
    Firebase.RTDB.setFloat(&fbdo, "/sensores/umidade", hum);
  }

  Firebase.RTDB.setBool(&fbdo, "/sensores/sensorTemperaturaOK", sensorTemperaturaOK);
  Firebase.RTDB.setBool(&fbdo, "/sensores/rele", releLigado);
  Firebase.RTDB.setInt(&fbdo, "/sensores/live", time(nullptr));
}

void streamCallback(FirebaseStream data) {

  Serial.println("Mudança recebida do Firebase");

  String path = data.dataPath();
  Serial.println(path);

  if (path == "/modoControle") {
    modoControle = data.intData();
  }

  if (path == "/temperaturaLigar") {
    temperaturaLigar = data.floatData();
  }

  if (path == "/offset") {
    offset = data.floatData();
  }

  if (path == "/duracaoMinutos") {
    duracaoMinutos = data.intData();
  }

  if (path.indexOf("/inicioHorarios") >= 0) {
    Serial.println("Atualizando horários");
    lerHorariosFirebase();
  }
}

void streamTimeoutCallback(bool timeout) {

  if (timeout) {
    Serial.println("Stream timeout, reconectando...");
  }

  if (!stream.httpConnected()) {
    Serial.println("Stream desconectado");
  }
}

void verificarRebootDiario() {

  time_t agora = time(nullptr);
  struct tm* t = localtime(&agora);

  if (t->tm_hour == 0 && t->tm_min == 0 && t->tm_sec <= 5) {
    Serial.println("Reinício diário");
    delay(2000);
    ESP.restart();
  }
}

void setup() {
  Serial.begin(9600);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  dht.begin();
  conectaWiFi();

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("Sistema iniciado");

  Firebase.RTDB.beginStream(&stream, "/config");
  Firebase.RTDB.setStreamCallback(&stream, streamCallback, streamTimeoutCallback);

  lerHorariosFirebase();
  

}

/* ================= LOOP ================= */

void loop() {

  if (WiFi.status() != WL_CONNECTED) {
  conectaWiFi();
}

  Firebase.RTDB.readStream(&stream);
  float temp = dht.readTemperature();
  float hum  = dht.readHumidity();
  

if (isnan(temp) || isnan(hum)) {
  Serial.println("Erro DHT");
  sensorTemperaturaOK = false;

} else {
  sensorTemperaturaOK = true;
}

if (!sensorTemperaturaOK && modoControle == 1) {
  modoControle = 0;  // sai do modo temperatura
  releLigado = false;
}

  unsigned long agora = millis();

  // -------- LÓGICA DO RELÉ --------
  if (modoControle == 1 && !releLigado && temp >= temperaturaLigar) {
    releLigado = true;
  }

  if (modoControle == 2 && !releLigado && dentroHorario()) {
    releLigado = true;
  }

  if (
      (modoControle == 1 && releLigado && temp <= temperaturaLigar - offset) ||
      (modoControle == 2 && releLigado && !dentroHorario()) ||
      (modoControle == 0 && releLigado)
     ) {
    releLigado = false;
  }

  digitalWrite(RELAY_PIN, releLigado ? HIGH : LOW);

  unsigned long ultimoEnvio = 0;

  if (millis() - ultimoEnvio > 5000) {
  enviarSensores(temp, hum);
  ultimoEnvio = millis();
}

  verificarRebootDiario();
  delay(1000);
}
