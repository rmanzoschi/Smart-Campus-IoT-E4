#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <BH1750.h>
#include <driver/i2s.h>
#include <math.h>

// ==========================================
// 1. PINES DEL NODO G4 (ESP32-C3)
// ==========================================
#define DHTPIN         10
#define DHTTYPE        DHT22
#define I2C_SDA        8
#define I2C_SCL        9
#define PIN_PIR        3
#define PIN_MQ135_AO   0
#define PIN_MQ135_DO   7

#define I2S_SCK        4
#define I2S_WS         5
#define I2S_SD         6
#define I2S_PORT       I2S_NUM_0

// ==========================================
// 2. CREDENCIALES WI-FI Y BROKER MQTT
// ==========================================
const char* ssid        = "TU_WIFI";
const char* password    = "TU_CLAVE_WIFI";

const char* mqtt_server = "IP_DEL_BROKER";
const int   mqtt_port   = 1883;
const char* mqtt_user   = "USUARIO_MQTT";
const char* mqtt_pass   = "CLAVE_MQTT";

const char* topic_temp    = "6to_info/A/G4/temperatura";
const char* topic_hum     = "6to_info/A/G4/humedad";
const char* topic_pres    = "6to_info/A/G4/presencia";
const char* topic_luz     = "6to_info/A/G4/luz";
const char* topic_ruido   = "6to_info/A/G4/ruido";
const char* topic_gases   = "6to_info/A/G4/gases";
const char* topic_confort = "6to_info/A/G4/confort";

// ==========================================
// 3. CALIBRACIÓN EXPERIMENTAL INMP441
// ==========================================
const float calibrationOffset = 103.97;
const float I2S_FULL_SCALE    = 8388607.0;

// ==========================================
// 4. OBJETOS Y VARIABLES GLOBALES
// ==========================================
WiFiClient espClient;
PubSubClient client(espClient);
DHT dht(DHTPIN, DHTTYPE);
BH1750 bh1750;
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Banderas de estado de los sensores (Para detectar desconexiones)
bool bhOk       = false;
bool i2sOk      = false;
bool dhtError   = true;
bool ruidoError = true;
bool gasError   = true;
bool pirError   = true;

int  fallosDHT  = 0;

float  tempVal           = 0.0;
float  humVal            = 0.0;
float  luxVal            = 0.0;
int    gasPpm            = 350;
bool   pirDetectado      = false;

double ultimoRMS         = 0.0;
float  ultimoDBFS        = -47.0;
float  ruidoDB           = 56.9;
int    ruidoDbLCD        = 57;

String estadoConfortMQTT = "Confortable";
String estadoConfortLCD  = "Optimo";

unsigned long lastSensorRead  = 0;
unsigned long lastDHTRead     = 0;
unsigned long lastLcdUpdate   = 0;
unsigned long lastMqttPublish = 0;
unsigned long lastMqttRetry   = 0;
unsigned long lastWiFiCheck   = 0;
unsigned long lastPirTime     = 0;

int pantallaActual = 0; 

// --- VARIABLES PARA ALERTA A PANTALLA COMPLETA Y COOLDOWN ---
unsigned long tiempoDisparoAlerta = 0;
String alertaEnPantalla = "";
const unsigned long TIEMPO_MOSTRAR_ALERTA = 5000;   // 5 segundos en pantalla
const unsigned long TIEMPO_COOLDOWN = 300000;       // 5 minutos (300.000 ms) sin mostrar otra alerta

// ==========================================
// 5. IMPRESIÓN LCD 20x4 SEGURA
// ==========================================
void imprimirLineaLCD(uint8_t fila, const char* texto) {
  char buffer[21];
  snprintf(buffer, sizeof(buffer), "%-20.20s", texto);
  lcd.setCursor(0, fila);
  lcd.print(buffer);
}

// ==========================================
// 6. MICRÓFONO I2S CALIBRADO
// ==========================================
void setupI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 128,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };
  if (i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL) == ESP_OK) {
    if (i2s_set_pin(I2S_PORT, &pin_config) == ESP_OK) {
      i2s_zero_dma_buffer(I2S_PORT);
      i2sOk = true;
    }
  }
}

void medirRuidoCalibrado() {
  if (!i2sOk) { ruidoError = true; return; }

  int32_t samples[64];
  size_t bytesRead = 0;

  if (i2s_read(I2S_PORT, &samples, sizeof(samples), &bytesRead, 10) != ESP_OK || bytesRead == 0) {
    ruidoError = true;
    return;
  }

  int n = bytesRead / sizeof(int32_t);
  if (n <= 0) { ruidoError = true; return; }

  // Detector de cable SD desconectado: Si todas las muestras son puro cero
  int muestrasEnCero = 0;
  double media = 0;
  for (int i = 0; i < n; i++) {
    if (samples[i] == 0) muestrasEnCero++;
    media += (double)(samples[i] >> 14);
  }
  
  if (muestrasEnCero == n) { 
    ruidoError = true; 
    return; 
  }
  
  ruidoError = false; // El micrófono está conectado y entregando audio
  media /= n;

  double sumaCuadrados = 0;
  for (int i = 0; i < n; i++) {
    double s = (double)(samples[i] >> 14) - media;
    sumaCuadrados += s * s;
  }
  double rms = sqrt(sumaCuadrados / n);
  if (rms < 1.0) rms = 1.0;

  float dbfs = 20.0 * log10(rms / I2S_FULL_SCALE);
  float calculado = dbfs + calibrationOffset;

  if (isnan(calculado) || isinf(calculado)) return;
  calculado = constrain(calculado, 30.0, 105.0);

  ultimoRMS  = rms;
  ultimoDBFS = dbfs;
  ruidoDB    = (ruidoDB * 0.4) + (calculado * 0.6);
  ruidoDbLCD = (int)round(ruidoDB);
}

// ==========================================
// 7. LECTURA DE SENSORES Y CONFORT AVANZADA
// ==========================================
void leerDHT() {
  pinMode(DHTPIN, INPUT_PULLUP);
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (isnan(t) || isnan(h)) {
    delay(30);
    t = dht.readTemperature(false, true);
    h = dht.readHumidity(true);
  }

  if (!isnan(t) && !isnan(h) && t > -10.0 && t < 60.0 && h >= 0.0 && h <= 100.0) {
    tempVal   = t;
    humVal    = h;
    dhtError  = false;
    fallosDHT = 0;
  } else {
    fallosDHT++;
    if (fallosDHT >= 3) dhtError = true;
  }
}
void leerSensores(unsigned long now) {
  // 1. PIR CON DETECCIÓN DE CABLE FLOTANTE
  pinMode(PIN_PIR, INPUT_PULLUP);
  delay(2);
  bool pinArriba = digitalRead(PIN_PIR);
  
  pinMode(PIN_PIR, INPUT_PULLDOWN);
  delay(2);
  bool pinAbajo = digitalRead(PIN_PIR);

  if (pinArriba == HIGH && pinAbajo == LOW) {
    pirError = true;
    pirDetectado = false;
  } else {
    pirError = false;
    bool estadoReal = digitalRead(PIN_PIR);
    if (estadoReal == HIGH) {
      pirDetectado = true;
      lastPirTime = now;
    } else if (now - lastPirTime > 4000) {
      pirDetectado = false;
    }
  }
  pinMode(PIN_PIR, INPUT); 

  // 2. DHT22
  if (now - lastDHTRead >= 3000) {
    lastDHTRead = now;
    leerDHT();
  }

  // 3. BH1750
  if (!bhOk) bhOk = bh1750.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &Wire);
  if (bhOk) {
    float l = bh1750.readLightLevel();
    if (isfinite(l) && l >= 0.0 && l < 65000.0) luxVal = l;
    else bhOk = false;
  }

  // 4. MQ-135 CON DETECCIÓN AVANZADA DE CABLE FLOTANTE (Efecto Antena)
  pinMode(PIN_MQ135_AO, INPUT_PULLDOWN);
  delay(2);
  int pruebaAbajo = analogRead(PIN_MQ135_AO);
  
  pinMode(PIN_MQ135_AO, INPUT_PULLUP);
  delay(2);
  int pruebaArriba = analogRead(PIN_MQ135_AO);
  
  pinMode(PIN_MQ135_AO, INPUT); 
  delay(2);

  if (pruebaAbajo < 100 && pruebaArriba > 4000) {
    gasError = true;
  } else {
    gasError = false;
    long sumaAdc = 0;
    for (int i = 0; i < 8; i++) sumaAdc += analogRead(PIN_MQ135_AO);
    
    int ppmCalc = constrain(map(sumaAdc / 8, 0, 4095, 280, 1200), 280, 1500);
    gasPpm = (int)((gasPpm * 0.5) + (ppmCalc * 0.5));
  }

  // 5. INMP441
  medirRuidoCalibrado();

  // 6. CONFORT AMBIENTAL (Solo puntúa sensores válidos)
  int puntos = 100;

  if (!dhtError) {
    if (tempVal < 18.0 || tempVal > 28.0) puntos -= 24; 
    else if (tempVal < 20.0 || tempVal > 26.0) puntos -= 8; 

    if (humVal < 30.0 || humVal > 70.0) puntos -= 18; 
    else if (humVal < 40.0 || humVal > 60.0) puntos -= 7; 
  }

  if (bhOk) {
    if (luxVal < 200.0 || luxVal > 1500.0) puntos -= 15; 
    else if (luxVal < 300.0 || luxVal > 1000.0) puntos -= 6; 
  }

  if (!ruidoError) {
    if (ruidoDB > 60.0) puntos -= 20; 
    else if (ruidoDB > 50.0) puntos -= 7; 
  }

  if (!gasError) {
    if (gasPpm > 700) puntos -= 23; 
    else if (gasPpm > 500) puntos -= 8; 
  }

  if (puntos >= 85)      { estadoConfortMQTT = "Confortable"; estadoConfortLCD = "Optimo";  }
  else if (puntos >= 50) { estadoConfortMQTT = "Regular";     estadoConfortLCD = "Regular"; }
  else                   { estadoConfortMQTT = "Malo";        estadoConfortLCD = "Malo";    }

  // --- 7. LÓGICA DE ALERTAS EN TIEMPO REAL ---
  String nuevaAlerta = "";
  
  if (dhtError || !bhOk || gasError || ruidoError || pirError) nuevaAlerta = "FALLA";
  else if (!gasError && gasPpm > 700)                          nuevaAlerta = "GAS";
  else if (!dhtError && tempVal > 28.0)                        nuevaAlerta = "TEMP_ALTA";
  else if (!dhtError && tempVal < 18.0)                        nuevaAlerta = "TEMP_BAJA";
  else if (!dhtError && humVal > 70.0)                         nuevaAlerta = "HUM_ALTA";
  else if (!ruidoError && ruidoDB > 60.0)                      nuevaAlerta = "RUIDO";
  else if (bhOk && luxVal < 200.0 && luxVal > 0)               nuevaAlerta = "LUZ_BAJA";

  // Dispara al instante si es una alerta DIFERENTE a la anterior, o si ya pasó el cooldown
  if (nuevaAlerta != "" && (nuevaAlerta != alertaEnPantalla || now - tiempoDisparoAlerta >= TIEMPO_COOLDOWN)) {
    alertaEnPantalla = nuevaAlerta;
    tiempoDisparoAlerta = now;
    actualizarLCD();     // Fuerza el dibujo de la alerta en este mismo milisegundo
    lastLcdUpdate = now; // Resetea el timer del loop para que dure los 5 segundos exactos
  }
}

// ==========================================
// 8. ACTUALIZAR LCD 20x4
// ==========================================
void actualizarLCD() {
  char linea0[21], linea1[21], linea2[21], linea3[21];
  unsigned long now = millis();

  // --- 1. DIBUJAR ALERTA CRITICA FULL-SCREEN ---
  if (alertaEnPantalla != "" && (now - tiempoDisparoAlerta < TIEMPO_MOSTRAR_ALERTA)) {
    
    snprintf(linea0, sizeof(linea0), "                    ");
    
    if (alertaEnPantalla == "FALLA") {
      snprintf(linea1, sizeof(linea1), "  FALLA DE SENSOR!  ");
      snprintf(linea2, sizeof(linea2), "  Revisar hardware  ");
    } else if (alertaEnPantalla == "GAS") {
      snprintf(linea1, sizeof(linea1), "  NIVEL DE GAS ALTO ");
      snprintf(linea2, sizeof(linea2), "   Abrir ventanas!  ");
    } else if (alertaEnPantalla == "TEMP_ALTA") {
      snprintf(linea1, sizeof(linea1), "  TEMPERATURA ALTA  ");
      snprintf(linea2, sizeof(linea2), "   Abrir ventanas!  ");
    } else if (alertaEnPantalla == "TEMP_BAJA") {
      snprintf(linea1, sizeof(linea1), "  TEMPERATURA BAJA  ");
      snprintf(linea2, sizeof(linea2), "  Cerrar ventanas!  ");
    } else if (alertaEnPantalla == "HUM_ALTA") {
      snprintf(linea1, sizeof(linea1), "    HUMEDAD ALTA    ");
      snprintf(linea2, sizeof(linea2), "   Abrir ventanas!  ");
    } else if (alertaEnPantalla == "RUIDO") {
      snprintf(linea1, sizeof(linea1), "     RUIDO ALTO     ");
      snprintf(linea2, sizeof(linea2), "  Bajar el volumen! ");
    } else if (alertaEnPantalla == "LUZ_BAJA") {
      snprintf(linea1, sizeof(linea1), "      LUZ BAJA      ");
      snprintf(linea2, sizeof(linea2), "  Encender luces!   ");
    }
    
    snprintf(linea3, sizeof(linea3), "                    ");
    
  } 
  // --- 2. DIBUJAR PANTALLA ROTATIVA NORMAL ---
  else {
    snprintf(linea0, sizeof(linea0), "                    ");
    snprintf(linea3, sizeof(linea3), "                    ");

    if (pantallaActual == 0) {
      snprintf(linea1, sizeof(linea1), "  Variables censadas");
      snprintf(linea2, sizeof(linea2), "  por el nodo 4     ");
    } 
    else if (pantallaActual == 1) {
      if (dhtError) {
        snprintf(linea1, sizeof(linea1), "  Temp: ERR         ");
        snprintf(linea2, sizeof(linea2), "  Humedad: ERR      ");
      } else {
        snprintf(linea1, sizeof(linea1), "  Temp: %0.1f C       ", tempVal);
        snprintf(linea2, sizeof(linea2), "  Humedad: %0.0f %%     ", humVal);
      }
    } 
    else if (pantallaActual == 2) {
      if (bhOk) snprintf(linea1, sizeof(linea1), "  Luz: %d lx        ", (int)luxVal);
      else      snprintf(linea1, sizeof(linea1), "  Luz: ERR          ");
      
      if (pirError) snprintf(linea2, sizeof(linea2), "  Movimiento: ERR   ");
      else          snprintf(linea2, sizeof(linea2), "  Movimiento: %s    ", pirDetectado ? "SI" : "NO");
    } 
    else if (pantallaActual == 3) {
      if (gasError) snprintf(linea1, sizeof(linea1), "  Gas: ERR          ");
      else          snprintf(linea1, sizeof(linea1), "  Gas: %d ppm       ", gasPpm);

      if (ruidoError) snprintf(linea2, sizeof(linea2), "  Ruido: ERR        ");
      else            snprintf(linea2, sizeof(linea2), "  Ruido: %d dB      ", ruidoDbLCD);
    } 
    else if (pantallaActual == 4) {
      snprintf(linea1, sizeof(linea1), " ESTADO DE CONFORT: ");
      if (estadoConfortLCD == "Optimo")       snprintf(linea2, sizeof(linea2), "       OPTIMO       ");
      else if (estadoConfortLCD == "Regular") snprintf(linea2, sizeof(linea2), "      REGULAR       ");
      else                                    snprintf(linea2, sizeof(linea2), "        MALO        ");
    }

    pantallaActual = (pantallaActual + 1) % 5; 
  }

  imprimirLineaLCD(0, linea0);
  imprimirLineaLCD(1, linea1);
  imprimirLineaLCD(2, linea2);
  imprimirLineaLCD(3, linea3);
}

// ==========================================
// 9. GESTIÓN WI-FI Y MQTT
// ==========================================
void gestionarConectividad(unsigned long now) {
  if (WiFi.status() != WL_CONNECTED) {
    if (now - lastWiFiCheck >= 8000) {
      lastWiFiCheck = now;
      WiFi.disconnect();
      WiFi.begin(ssid, password);
    }
    return;
  }

  if (!client.connected() && (now - lastMqttRetry >= 4000)) {
    lastMqttRetry = now;
    espClient.stop();
    String clientId = "NodoG4_" + String((uint32_t)ESP.getEfuseMac(), HEX);

    if (!client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      espClient.stop();
      client.connect(clientId.c_str());
    }
  }
}

// ==========================================
// 10. SETUP PRINCIPAL
// ==========================================
void setup() {
  Serial.begin(115200);

  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_MQ135_DO, INPUT);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);

  lcd.init();
  lcd.backlight();
  imprimirLineaLCD(1, "  SISTEMA LISTO  ");
  imprimirLineaLCD(2, "  Cargando WiFi...  ");

  dht.begin(55);
  pinMode(DHTPIN, INPUT_PULLUP);
  bhOk = bh1750.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &Wire);
  setupI2S();

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_15dBm);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);

  delay(2000);
  leerDHT();
  lastDHTRead = millis();

  client.setServer(mqtt_server, mqtt_port);
  client.setSocketTimeout(2);
  client.setKeepAlive(20);
}

// ==========================================
// 11. LOOP PRINCIPAL
// ==========================================
void loop() {
  unsigned long now = millis();

  if (client.connected()) client.loop();
  gestionarConectividad(now);

  if (now - lastSensorRead >= 1000) {
    lastSensorRead = now;
    leerSensores(now);
  }

  unsigned long tiempoEsperaLCD = (pantallaActual == 1) ? 3000 : 5000; 
  
  if (now - lastLcdUpdate >= tiempoEsperaLCD) {
    lastLcdUpdate = now;
    actualizarLCD();
  }

  if (now - lastMqttPublish >= 10000) {
    lastMqttPublish = now;

    if (client.connected()) {
      char buf[16];
      
      if (dhtError) {
        client.publish(topic_temp, "ERROR", true);
        client.publish(topic_hum, "ERROR", true);
      } else {
        snprintf(buf, sizeof(buf), "%.1f", tempVal); client.publish(topic_temp, buf, true);
        snprintf(buf, sizeof(buf), "%.1f", humVal);  client.publish(topic_hum, buf, true);
      }

      if (bhOk) { snprintf(buf, sizeof(buf), "%d", (int)luxVal); client.publish(topic_luz, buf, true); }
      else      { client.publish(topic_luz, "ERROR", true); }

      if (ruidoError) {
        client.publish(topic_ruido, "ERROR", true);
      } else {
        snprintf(buf, sizeof(buf), "%.1f", ruidoDB); client.publish(topic_ruido, buf, true);
      }
      
      if (gasError) {
        client.publish(topic_gases, "ERROR", true);
      } else {
        snprintf(buf, sizeof(buf), "%d", gasPpm); client.publish(topic_gases, buf, true);
      }
      
      if (pirError) {
        client.publish(topic_pres, "ERROR", true);
      } else {
        client.publish(topic_pres, pirDetectado ? "SI" : "NO", true);
      }
      
      client.publish(topic_confort, estadoConfortMQTT.c_str(), true);
    }
  }
}