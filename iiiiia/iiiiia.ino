#include "esp_camera.h"         // Incluye la librería para controlar la cámara ESP32-CAM
#include "WiFi.h"               // Incluye la librería para funciones WiFi
#include <WebServer.h>          // Incluye la librería para crear un servidor web
#include <ArduinoJson.h>        // Incluye la librería para manejar JSON
#include "FS.h"                 // Incluye la librería para sistema de archivos (no usada aquí)
//#include "SD_MMC.h"           // Librería para SD (comentada, no se usa)

// ==== Pines para módulo AI Thinker ====
// Define los pines de conexión entre el ESP32 y el módulo de cámara AI Thinker.
#define PWDN_GPIO_NUM     32    // Pin de apagado de la cámara
#define RESET_GPIO_NUM    -1    // Pin de reset (no usado)
#define XCLK_GPIO_NUM      0    // Pin de reloj externo
#define SIOD_GPIO_NUM     26    // Pin de datos I2C SDA
#define SIOC_GPIO_NUM     27    // Pin de datos I2C SCL
#define Y9_GPIO_NUM       35    // Pines de datos de la cámara
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25    // Pin de sincronización vertical
#define HREF_GPIO_NUM     23    // Pin de referencia de línea
#define PCLK_GPIO_NUM     22

// ==== Datos WiFi (AP local) ====
// Define el nombre y la contraseña del punto de acceso WiFi que creará el ESP32.
const char* ssid     = "ESP32-CAM-Test";
const char* password = "12345678";

// ==== Variables de control ====
// Inicializa el contador de fotos, el servidor web y el puntero al frame buffer de la última foto.
int fotoCounter = 0;            
WebServer server(80);            // Crea el servidor web en el puerto 80
camera_fb_t *currentFb = NULL;  // Puntero al frame buffer de la última foto

// ==== Sensor PIR en otro pin seguro ====
// Define el pin donde está conectado el sensor PIR y variables para el estado de movimiento.
#define PIR_PIN 13
bool estadoMovimiento = false;   // Indica si hay movimiento detectado
bool movimientoPrevio = false;   // Guarda el estado anterior del PIR

// === Variables para control de intervalo de fotos ===
unsigned long lastPhotoMs = 0;
const unsigned long minIntervalMs = 5000; // 5 segundos entre fotos

// --- Inicializar cámara ---
// Inicializa la cámara con la configuración adecuada para el módulo AI Thinker.
bool initCamera() {
  Serial.println("[LOG] === INICIANDO CÁMARA ==="); // Mensaje de inicio
  camera_config_t config;                           // Estructura de configuración de la cámara
  config.ledc_channel = LEDC_CHANNEL_0;             // Canal PWM para el reloj
  config.ledc_timer   = LEDC_TIMER_0;               // Timer PWM
  config.pin_d0       = Y2_GPIO_NUM;                // Asigna pines de datos
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;              // Pin de reloj externo
  config.pin_pclk     = PCLK_GPIO_NUM;              // Pin de reloj de píxel
  config.pin_vsync    = VSYNC_GPIO_NUM;             // Pin de sincronización vertical
  config.pin_href     = HREF_GPIO_NUM;              // Pin de referencia de línea
  config.pin_sscb_sda = SIOD_GPIO_NUM;              // Pin I2C SDA
  config.pin_sscb_scl = SIOC_GPIO_NUM;              // Pin I2C SCL
  config.pin_pwdn     = PWDN_GPIO_NUM;              // Pin de apagado
  config.pin_reset    = RESET_GPIO_NUM;             // Pin de reset
  config.xclk_freq_hz = 20000000;                   // Frecuencia del reloj externo
  config.pixel_format = PIXFORMAT_JPEG;             // Formato de imagen JPEG
  config.frame_size   = FRAMESIZE_QVGA;             // Resolución 320x240
  config.jpeg_quality = 12;                         // Calidad JPEG (0=mejor, 63=peor)
  config.fb_count     = 1;                          // Solo un frame buffer

  esp_err_t err = esp_camera_init(&config);         // Inicializa la cámara
  if (err != ESP_OK) {
    Serial.printf("[ERROR] Fallo inicialización cámara: 0x%x\n", err); // Error si falla
    return false;
  }
  return true;                                      // Éxito
}

// --- Capturar foto ---
// Captura una nueva foto y la almacena en currentFb. Libera el frame buffer anterior si existe.
bool tomarFoto(const char* motivo = "desconocido") {
  if (currentFb) {
    esp_camera_fb_return(currentFb);  // Libera el frame buffer anterior si existe
    currentFb = NULL;
  }
  currentFb = esp_camera_fb_get();    // Captura una nueva foto
  if (!currentFb) {
    Serial.println("[ERROR] Fallo al capturar foto");
    return false;
  }
  fotoCounter++;                      // Incrementa el contador de fotos
  Serial.printf("[LOG] Foto tomada por: %s (fb len: %u)\n", motivo, currentFb->len);
  return true;
}

// === Handlers API ===
// Handler para la ruta /photo: envía la última foto capturada en formato JPEG o un error si no hay foto disponible.
void handleImage() {
  Serial.printf("[LOG] /photo solicitado - currentFb: %s, len: %u\n", currentFb ? "OK" : "NULL", currentFb ? currentFb->len : 0);
  if (!currentFb) {
    Serial.println("[LOG] /photo denegado: sin fb disponible");
    server.send(404, "text/plain", "No se ha detectado un movimiento");
    return;
  }
  server.sendHeader("Content-Type", "image/jpeg");
  server.send_P(200, "image/jpeg", (const char*)currentFb->buf, currentFb->len);
  Serial.println("[LOG] /photo enviado OK");
  estadoMovimiento = false; // Resetea el estado de movimiento solo aquí
}

// Handler para la ruta /test: responde con un JSON de prueba con el estado, uptime y cantidad de fotos.
void handleTest() {
  StaticJsonDocument<200> doc;
  doc["status"] = "success";
  doc["uptime"] = millis();
  doc["fotos"] = fotoCounter;
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// Handler para la ruta /status: responde con un JSON que incluye estado, uptime, fotos, memoria libre y estado del PIR.
void handleStatus() {
  StaticJsonDocument<300> doc;
  doc["status"] = "success";
  doc["uptime"] = millis();
  doc["fotos"] = fotoCounter;
  doc["memoria"] = ESP.getFreeHeap();
  doc["movimiento"] = estadoMovimiento; // Estado del PIR
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
  Serial.printf("[LOG] /status enviado\n");
}

// === Interfaz web principal ===
// Handler para la página principal: genera una interfaz web con la última foto, estado del PIR y contador de fotos.
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><style>";
  html += "body { background-color: #f0f4f8; color: #333; font-family: Arial, sans-serif; margin:0; padding:0; text-align:center; }";
  html += "header { background:#2c3e50; color:white; padding:15px; font-size:20px; font-weight:bold; }";
  html += ".container { padding:20px; }";
  html += ".main-photo { margin-top:20px; }";
  html += ".main-photo img { width:100vw; max-width:100vw; height:auto; border-radius:12px; box-shadow:0 4px 12px rgba(0,0,0,0.4); border:3px solid #2980b9; }";
  html += "button { margin:10px; padding:10px 20px; border:none; border-radius:6px; background:#2980b9; color:white; font-size:16px; cursor:pointer; transition:background 0.3s; }";
  html += "button:hover { background:#1f5c85; }";
  html += ".status { margin:15px; padding:10px; font-size:16px; color:#fff; border-radius:6px; }";
  html += ".motion { background:#e74c3c; }";
  html += ".no-motion { background:#2ecc71; }";
  html += "footer { margin-top:20px; font-size:12px; color:#666; }";
  html += "</style></head><body>";

  html += "<header>📸 ESP32-CAM Timelapse</header>";
  html += "<div class='container'>";
  html += "<h3>Última Foto</h3>";

  if (currentFb) {
    html += "<div class='main-photo'><img src='/photo'></div>";
  } else {
    html += "<p style='color:#888;font-size:18px;'>No hay foto disponible.</p>";
  }

  // Muestra el estado del PIR en la web
  html += "<div class='status " + String(estadoMovimiento ? "motion" : "no-motion") + "'>";
  html += "Estado PIR: " + String(estadoMovimiento ? "💥 Movimiento Detectado" : "✅ Sin Movimiento");
  html += "</div>";

  html += "<div><button onclick='location.reload()'>🔄 Actualizar</button></div>";

  html += "<footer>Fotos tomadas: " + String(fotoCounter) + "</footer>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

// === SETUP ===
// Función de configuración inicial: inicializa la cámara, el WiFi, el servidor web y el sensor PIR.
void setup() {
  Serial.begin(115200);           // Inicializa el puerto serie para depuración
  delay(1000);                    // Espera breve para estabilidad

  if (!initCamera()) {            // Inicializa la cámara
    delay(5000);
    esp_restart();                // Reinicia si falla la cámara
  }

  WiFi.softAP(ssid, password);    // Inicia el punto de acceso WiFi
  IPAddress IP = WiFi.softAPIP(); // Obtiene la IP del AP
  Serial.printf("[LOG] AP OK: %s | IP: %s\n", ssid, IP.toString().c_str());

  // Asocia las rutas del servidor web con sus handlers
  server.on("/", handleRoot);
  server.on("/photo", handleImage);
  server.on("/test", handleTest);
  server.on("/status", handleStatus);

  server.begin();                 // Inicia el servidor web

  pinMode(PIR_PIN, INPUT);        // Configura el pin del PIR como entrada
  Serial.println("[LOG] PIR listo en pin 13");
}

// === LOOP ===
// Bucle principal: atiende peticiones web y gestiona la detección de movimiento con el PIR.
void loop() {
  server.handleClient();          // Atiende peticiones web

  bool pirActual = digitalRead(PIR_PIN); // Lee el estado actual del PIR

  // Si hay un flanco de subida (nuevo movimiento) y pasó el intervalo mínimo, toma una foto
  if (pirActual && !movimientoPrevio && millis() - lastPhotoMs > minIntervalMs) {
    Serial.println("[LOG] Movimiento PIR detectado");
    tomarFoto("PIR");
    estadoMovimiento = true;
    lastPhotoMs = millis();
  }

  // Actualiza el estado previo del PIR para detectar flancos
  movimientoPrevio = pirActual;
}