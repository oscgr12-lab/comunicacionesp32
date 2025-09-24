#include <Arduino.h>
#include "Arduino_GFX_Library.h"
#include "pin_config.h"
#include <Wire.h>
#include "HWCDC.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <TJpg_Decoder.h>

HWCDC USBSerial;

// --- DISPLAY / GFX ---
Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI);
Arduino_GFX *gfx = new Arduino_ST7789(bus, LCD_RST /* RST */,
                                      0 /* rotation */, true /* IPS */,
                                      LCD_WIDTH, LCD_HEIGHT, 0, 20, 0, 0);

// --- Colores ---
#define BACKGROUND BLACK

// --- WiFi / Cámara ---
const char *ssid = "ESP32-CAM-Test";
const char *password = "12345678";
const char *camTriggerURL = "http://192.168.4.1/capture"; // Para disparar captura (POST)
const char *camImageURL = "http://192.168.4.1/photo";     // Para obtener la imagen (GET)

// -------------------- Callback JPEG --------------------
bool jpgDrawToGfx(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  USBSerial.printf("JPEG bloque (%d,%d) %dx%d\n", x, y, w, h);
  gfx->draw16bitRGBBitmap(x, y, bitmap, w, h);
  return true;
}

// -------------------- Disparar captura en la CAM --------------------
bool triggerCapture() {
  HTTPClient http;
  if (!http.begin(camTriggerURL)) {
    USBSerial.println("http.begin() falló para trigger");
    return false;
  }
  USBSerial.printf("Disparando captura en %s ...\n", camTriggerURL);
  int httpCode = http.POST(""); // POST vacío para disparar
  USBSerial.printf("HTTP code (trigger): %d\n", httpCode);
  http.end();
  return (httpCode == HTTP_CODE_OK);
}

// -------------------- Descargar y mostrar imagen --------------------
void fetchAndShowImage() {
  USBSerial.println("=== Inicio fetchAndShowImage() ===");

  // Primero, disparar una nueva captura
  if (!triggerCapture()) {
    USBSerial.println("Fallo al disparar captura");
  } else {
    delay(500); // Espera para que la CAM procese
  }

  HTTPClient http;
  if (!http.begin(camImageURL)) {
    USBSerial.println("http.begin() falló para imagen");
    return;
  }
  USBSerial.printf("Solicitando imagen en %s ...\n", camImageURL);
  int httpCode = http.GET();
  USBSerial.printf("HTTP code: %d\n", httpCode);

  if (httpCode == HTTP_CODE_OK) {
    WiFiClient *stream = http.getStreamPtr();
    size_t contentLen = http.getSize();
    USBSerial.printf("Content-Length: %u bytes\n", (unsigned)contentLen);

    size_t maxBuf = 100 * 1024; // Reducido a 100KB para ahorrar memoria
    size_t bufSize = (contentLen > 0 && contentLen < maxBuf) ? contentLen : maxBuf;

    uint8_t *buf = (uint8_t *)malloc(bufSize); // Usamos malloc (memoria interna)
    if (!buf) {
      USBSerial.println("❌ malloc falló - Verifica memoria disponible");
      http.end();
      return;
    }

    size_t idx = 0;
    while (stream->connected() && (idx < bufSize)) {
      while (stream->available() && (idx < bufSize)) {
        int r = stream->read();
        if (r < 0) break;
        buf[idx++] = (uint8_t)r;
      }
      if (!stream->available()) delay(1);
    }
    USBSerial.printf("Bytes recibidos: %u\n", (unsigned)idx);

    if (idx > 0) {
      USBSerial.println("Decodificando...");
      if (TJpgDec.drawJpg(0, 0, buf, idx)) {
        USBSerial.println("✅ Imagen mostrada");
      } else {
        USBSerial.println("❌ Fallo en TJpgDec.drawJpg - Verifica si es JPEG válido");
      }
    }
    free(buf);
  } else {
    USBSerial.printf("❌ HTTP GET error, code=%d\n", httpCode);
  }
  http.end();
  USBSerial.println("=== Fin fetchAndShowImage() ===");
}

// -------------------- setup / loop --------------------
void setup() {
  USBSerial.begin(115200);
  USBSerial.println("ESP32-S3 + Arduino_GFX + Cámara");

  // Init Display
  if (!gfx->begin()) {
    USBSerial.println("gfx->begin() falló!");
  }
  gfx->fillScreen(BACKGROUND);

  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  // Config TJpg_Decoder
  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(jpgDrawToGfx);
  TJpgDec.setSwapBytes(true);

  // Conexión WiFi
  USBSerial.printf("Conectando a AP: %s\n", ssid);
  WiFi.begin(ssid, password);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 12000) {
    delay(250);
    USBSerial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    USBSerial.printf("\n✅ Conectado, IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    USBSerial.println("\n❌ No conectado al AP");
  }
}

unsigned long lastFetchMs = 0;
const unsigned long fetchIntervalMs = 10000; // cada 10s

void loop() {
  if ((millis() - lastFetchMs) >= fetchIntervalMs) {
    USBSerial.println("\n>>> Capturando imagen de la CAM...");
    lastFetchMs = millis();
    fetchAndShowImage();
  }
}