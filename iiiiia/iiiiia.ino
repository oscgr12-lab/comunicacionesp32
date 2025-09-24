#include "esp_camera.h"
#include "WiFi.h"
#include <WebServer.h>
#include <ArduinoJson.h>  // Instala "ArduinoJson" en Library Manager

// ==== Pines para módulo AI Thinker ====
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ==== Datos WiFi (AP local para prueba) ====
const char* ssid     = "ESP32-CAM-Test";
const char* password = "12345678";

// Configuración
const int fotoInterval = 30000;  // Intervalo entre fotos (ms)
int fotoCounter = 0;

WebServer server(80);
camera_fb_t *currentFb = NULL;  // Buffer global para la foto actual (sin SD)

bool initCamera() {
  Serial.println("[LOG] === INICIANDO CÁMARA ===");
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = FRAMESIZE_QCIF;
  config.jpeg_quality = 20;
  config.fb_count     = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[ERROR] Fallo inicialización cámara: 0x%x (revisa cables/alimentación)\n", err);
    return false;
  }
  Serial.println("[LOG] Cámara OK - Listo para capturar");
  return true;
}

bool tomarFoto() {
  Serial.println("[LOG] === CAPTURANDO FOTO ===");
  if (currentFb) {
    esp_camera_fb_return(currentFb);  // Libera anterior
    Serial.println("[LOG] Buffer anterior liberado");
  }
  currentFb = esp_camera_fb_get();
  if (!currentFb) {
    Serial.println("[ERROR] Fallo al capturar foto (revisa luz/cámara)");
    return false;
  }
  Serial.printf("[LOG] Foto capturada: %u bytes, formato JPEG\n", currentFb->len);
  fotoCounter++;
  Serial.printf("[LOG] Contador de fotos: %d\n", fotoCounter);
  return true;
}

void handleImage() {
  Serial.println("[LOG] === SOLICITUD DE IMAGEN ===");
  Serial.printf("[LOG] Cliente IP: %s\n", server.client().remoteIP().toString().c_str());
  if (!currentFb) {
    Serial.println("[ERROR] No hay foto disponible (toma una primero)");
    server.send(404, "text/plain", "No hay foto - Reinicia para capturar");
    return;
  }
  Serial.printf("[LOG] Sirviendo foto de %u bytes\n", currentFb->len);
  server.setContentLength(currentFb->len);
  server.send(200, "image/jpeg", "");
  server.sendContent((const char*)currentFb->buf, currentFb->len);
  Serial.println("[LOG] Imagen enviada exitosamente");
}

void handleTest() {
  Serial.println("[LOG] === TEST DE CONEXIÓN ===");
  StaticJsonDocument<200> doc;
  doc["status"] = "success";
  doc["uptime"] = millis();
  doc["fotos"] = fotoCounter;
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
  Serial.println("[LOG] Test respondido OK");
}

void handleStatus() {
  Serial.println("[LOG] === SOLICITUD DE ESTADO ===");
  StaticJsonDocument<300> doc;
  doc["status"] = "success";
  doc["uptime"] = millis();
  doc["fotos"] = fotoCounter;
  doc["memoria"] = ESP.getFreeHeap();
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
  Serial.println("[LOG] Estado enviado OK");
}

void handleCapture() {
  Serial.println("[LOG] === SOLICITUD DE CAPTURA MANUAL ===");
  StaticJsonDocument<200> doc;
  if (tomarFoto()) {
    doc["success"] = true;
    doc["message"] = "Foto tomada";
    doc["fotos"] = fotoCounter;
  } else {
    doc["success"] = false;
    doc["message"] = "Fallo al tomar foto";
  }
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
  Serial.println("[LOG] Captura manejada OK");
}

void handleRoot() {
  Serial.println("[LOG] === PÁGINA PRINCIPAL ===");
  unsigned long lastCapture = (millis() - (fotoInterval * (fotoCounter - 1))) / 1000; // Tiempo desde última captura
  String html = "<html><head><style>";
  html += "body { background-color: #1a1a1a; color: #ffffff; font-family: Arial, sans-serif; text-align: center; margin: 0; padding: 20px; }";
  html += "h1 { color: #00ff00; }";
  html += "img { max-width: 100%; border: 2px solid #00ff00; border-radius: 10px; }";
  html += "p { font-size: 16px; }";
  html += "button { background-color: #00ff00; color: #1a1a1a; border: none; padding: 10px 20px; font-size: 16px; cursor: pointer; border-radius: 5px; }";
  html += "button:hover { background-color: #00cc00; }";
  html += "</style></head><body>";
  html += "<h1>ESP32-CAM Test - Foto #" + String(fotoCounter) + "</h1>";
  if (currentFb) {
    html += "<img src='/photo' alt='Foto Cámara'>";
    html += "<p>Última captura: hace " + String(lastCapture) + " segundos</p>";
  } else {
    html += "<p>No hay foto aún. Espera...</p>";
  }
  html += "<p><a href='/test' style='color: #00ff00; text-decoration: none;'>Test</a> | <a href='/status' style='color: #00ff00; text-decoration: none;'>Estado</a> | <a href='/photo' style='color: #00ff00; text-decoration: none;'>Foto</a></p>";
  html += "<button onclick=\"fetch('/capture', { method: 'POST' }).then(response => response.json()).then(data => alert(JSON.stringify(data)));\">Tomar Foto Ahora</button>";
  html += "</body></html>";
  server.send(200, "text/html", html);
  Serial.println("[LOG] Página principal servida");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- Timelapse ESP32-CAM (v2.1.1 - API Consumible) ---");
  Serial.println("[LOG] Iniciando setup...");

  // Inicializar cámara
  Serial.println("[LOG] Configurando cámara...");
  if (!initCamera()) {
    Serial.println("[ERROR] Fallo cámara - Reiniciando en 5s");
    delay(5000);
    esp_restart();
  }

  // Configurar AP WiFi
  Serial.println("[LOG] Configurando AP WiFi...");
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.printf("[LOG] AP OK: %s (pass: %s) | IP: %s\n", ssid, password, IP.toString().c_str());
  Serial.println("[INSTRUCCIÓN] Conéctate al WiFi desde tu celular/PC y usa http://192.168.4.1/ o Postman");

  // Configurar servidor
  server.on("/", handleRoot);
  server.on("/photo", handleImage);  // Renombrado a /photo para API
  server.on("/test", handleTest);
  server.on("/status", handleStatus);
  server.on("/capture", HTTP_POST, handleCapture);
  server.begin();
  Serial.println("[LOG] Servidor web iniciado - Manejo requests en loop()");

  // Tomar primera foto
  Serial.println("[LOG] Capturando primera foto...");
  if (!tomarFoto()) {
    Serial.println("[ERROR] Fallo primera foto");
  } else {
    Serial.println("[LOG] Primera foto lista - Accede al link para verla");
  }
  Serial.println("[LOG] Setup completo - Loop iniciado");
}

void loop() {
  server.handleClient();  // Prioridad: maneja requests inmediatamente

  // Captura periódica
  static unsigned long lastPhoto = 0;
  if (millis() - lastPhoto > fotoInterval) {
    Serial.println("[LOG] Capturando foto periódica...");
    if (tomarFoto()) {
      Serial.println("[LOG] Foto periódica OK");
    } else {
      Serial.println("[ERROR] Fallo foto periódica");
    }
    lastPhoto = millis();
  }

  // Log de estado cada 10s
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 10000) {
    Serial.printf("[STATUS] Uptime: %lu ms | Fotos: %d | Clientes conectados: %d\n", millis(), fotoCounter, WiFi.softAPgetStationNum());
    lastStatus = millis();
  }

  delay(50);  // Delay mínimo para estabilidad
}