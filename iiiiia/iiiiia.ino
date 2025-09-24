#include "esp_camera.h"
#include "WiFi.h"
#include <WebServer.h>
#include <ArduinoJson.h>
#include "FS.h"
#include "SD_MMC.h"   // Librería para la microSD (modo 4-bit)

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
camera_fb_t *currentFb = NULL;  

// === SD ===
String fotosGuardadas[3];  // Mantener solo 3 fotos
int fotoIndex = 0;

// --- Inicializar cámara ---
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
  config.frame_size   = FRAMESIZE_VGA;
  config.jpeg_quality = 15;
  config.fb_count     = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[ERROR] Fallo inicialización cámara: 0x%x\n", err);
    return false;
  }
  return true;
}

// --- Guardar en SD ---
void guardarEnSD(camera_fb_t *fb) {
  if (!fb) return;

  String filename = "/foto_" + String(millis()) + ".jpg";

  File file = SD_MMC.open(filename, FILE_WRITE);
  if (!file) {
    Serial.println("[ERROR] No se pudo abrir archivo en SD");
    return;
  }
  file.write(fb->buf, fb->len);
  file.close();
  Serial.printf("[LOG] Foto guardada en SD: %s (%u bytes)\n", filename.c_str(), fb->len);

  fotosGuardadas[fotoIndex] = filename;
  fotoIndex = (fotoIndex + 1) % 3;
}

// --- Capturar foto ---
bool tomarFoto() {
  if (currentFb) {
    esp_camera_fb_return(currentFb);
  }
  currentFb = esp_camera_fb_get();
  if (!currentFb) {
    Serial.println("[ERROR] Fallo al capturar foto");
    return false;
  }
  fotoCounter++;
  guardarEnSD(currentFb);
  return true;
}

// === Handlers API ===
void handleImage() {
  if (!currentFb) {
    server.send(404, "text/plain", "No hay foto");
    return;
  }
  server.sendHeader("Content-Type", "image/jpeg");
  server.send_P(200, "image/jpeg", (const char*)currentFb->buf, currentFb->len);
}

void handleTest() {
  StaticJsonDocument<200> doc;
  doc["status"] = "success";
  doc["uptime"] = millis();
  doc["fotos"] = fotoCounter;
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleStatus() {
  StaticJsonDocument<300> doc;
  doc["status"] = "success";
  doc["uptime"] = millis();
  doc["fotos"] = fotoCounter;
  doc["memoria"] = ESP.getFreeHeap();
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleCapture() {
  StaticJsonDocument<200> doc;
  if (tomarFoto()) {
    doc["success"] = true;
    doc["message"] = "Foto tomada";
  } else {
    doc["success"] = false;
    doc["message"] = "Fallo";
  }
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// === NUEVO DISEÑO INTERFAZ ===
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><style>";
  html += "body { background-color: #f0f4f8; color: #333; font-family: Arial, sans-serif; margin:0; padding:0; text-align:center; }";
  html += "header { background:#2c3e50; color:white; padding:15px; font-size:20px; font-weight:bold; }";
  html += ".container { padding:20px; }";
  html += ".gallery { display:flex; justify-content:center; gap:15px; margin-bottom:20px; }";
  html += ".gallery img { width:120px; height:90px; object-fit:cover; border-radius:10px; box-shadow:0 2px 6px rgba(0,0,0,0.3); border:2px solid #3498db; transition: transform 0.2s; }";
  html += ".gallery img:hover { transform:scale(1.05); }";
  html += ".main-photo { margin-top:20px; }";
  html += ".main-photo img { width:90%; max-width:600px; border-radius:12px; box-shadow:0 4px 12px rgba(0,0,0,0.4); border:3px solid #2980b9; }";
  html += "button { margin:10px; padding:10px 20px; border:none; border-radius:6px; background:#2980b9; color:white; font-size:16px; cursor:pointer; transition:background 0.3s; }";
  html += "button:hover { background:#1f5c85; }";
  html += "footer { margin-top:20px; font-size:12px; color:#666; }";
  html += "</style></head><body>";

  html += "<header>📸 ESP32-CAM Timelapse</header>";
  html += "<div class='container'>";
  html += "<h3>Últimas Fotos</h3><div class='gallery'>";

  for (int i = 0; i < 3; i++) {
    if (fotosGuardadas[i] != "") {
      html += "<img src='/sd" + String(i) + "'>";
    }
  }

  html += "</div>";

  if (fotosGuardadas[(fotoIndex + 2) % 3] != "") {
    html += "<div class='main-photo'><img src='/sd" + String((fotoIndex + 2) % 3) + "'></div>";
  }

  html += "<div><button onclick=\"fetch('/capture',{method:'POST'}).then(r=>r.json()).then(()=>location.reload());\">📷 Tomar Foto</button>";
  html += "<button onclick='location.reload()'>🔄 Actualizar</button></div>";

  html += "<footer>Fotos tomadas: " + String(fotoCounter) + "</footer>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

// Endpoint dinámico para fotos de SD
void handleSDPhoto(int index) {
  if (fotosGuardadas[index] == "") {
    server.send(404, "text/plain", "No existe");
    return;
  }
  File file = SD_MMC.open(fotosGuardadas[index]);
  if (!file) {
    server.send(500, "text/plain", "Error SD");
    return;
  }
  server.streamFile(file, "image/jpeg");
  file.close();
}

// === SETUP ===
void setup() {
  Serial.begin(115200);
  delay(1000);

  if (!initCamera()) {
    delay(5000);
    esp_restart();
  }

  if (!SD_MMC.begin("/sdcard", true)) { 
    Serial.println("[ERROR] No se pudo montar SD");
  } else {
    Serial.println("[LOG] SD montada OK");
  }

  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.printf("[LOG] AP OK: %s | IP: %s\n", ssid, IP.toString().c_str());

  server.on("/", handleRoot);
  server.on("/photo", handleImage);
  server.on("/test", handleTest);
  server.on("/status", handleStatus);
  server.on("/capture", HTTP_POST, handleCapture);
  server.on("/sd0", []() { handleSDPhoto(0); });
  server.on("/sd1", []() { handleSDPhoto(1); });
  server.on("/sd2", []() { handleSDPhoto(2); });

  server.begin();

  tomarFoto(); // Primera foto
}

void loop() {
  server.handleClient();

  static unsigned long lastPhoto = 0;
  if (millis() - lastPhoto > fotoInterval) {
    tomarFoto();
    lastPhoto = millis();
  }
}
