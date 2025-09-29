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
Arduino_GFX *gfx = new Arduino_ST7789(bus, LCD_RST, 0, true, LCD_WIDTH, LCD_HEIGHT, 0, 20, 0, 0);

// --- Colores ---
#define BACKGROUND BLACK
#define PLACEHOLDER 0x5294  // Gris suave
#define TEXT_COLOR WHITE
#define BUTTON_COLOR 0x07E0
#define BUTTON_HIGHLIGHT 0x001F

// --- WiFi / Cámara ---
const char *ssid = "ESP32-CAM-Test";
const char *password = "12345678";
const char *camTriggerURL = "http://192.168.4.1/capture";
const char *camImageURL = "http://192.168.4.1/photo";

// --- Indicadores ---
bool showPlaceholder = true;
bool fetchError = false;

// -------------------- Callback JPEG --------------------
bool jpgDrawToGfx(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  // Centrar imagen
  int16_t x0 = (LCD_WIDTH - w) / 2;
  int16_t y0 = (LCD_HEIGHT - h) / 2;
  gfx->draw16bitRGBBitmap(x0, y0, bitmap, w, h);
  showPlaceholder = false;  // Imagen cargada
  return true;
}

// -------------------- Funciones visuales UX --------------------
void drawPlaceholder() {
  gfx->fillScreen(PLACEHOLDER);
  gfx->setCursor(20, LCD_HEIGHT / 2 - 10);
  gfx->setTextColor(TEXT_COLOR);
  gfx->setTextSize(2);
  gfx->print("📸 Cargando imagen...");
}

void drawError() {
  gfx->fillScreen(BACKGROUND);
  gfx->setCursor(20, LCD_HEIGHT / 2 - 10);
  gfx->setTextColor(0xF800); // Rojo
  gfx->setTextSize(2);
  gfx->print("❌ Error al mostrar imagen");
}

void drawCaptureButton() {
  int x = LCD_WIDTH/2 - 50, y = LCD_HEIGHT - 40, w = 100, h = 30;
  gfx->fillRect(x, y, w, h, BUTTON_COLOR);
  gfx->drawRect(x, y, w, h, BUTTON_HIGHLIGHT);
  gfx->setCursor(x+10, y+8);
  gfx->setTextColor(TEXT_COLOR);
  gfx->setTextSize(2);
  gfx->print("📷 Capturar");
}

// -------------------- Disparar captura --------------------
bool triggerCapture() {
  HTTPClient http;
  if (!http.begin(camTriggerURL)) return false;
  int httpCode = http.POST("");
  http.end();
  return (httpCode == HTTP_CODE_OK);
}

// -------------------- Descargar y mostrar imagen --------------------
void fetchAndShowImage() {
  showPlaceholder = true;
  fetchError = false;
  drawPlaceholder();

  if (!triggerCapture()) {
    fetchError = true;
    drawError();
    return;
  }
  delay(500);

  HTTPClient http;
  if (!http.begin(camImageURL)) {
    fetchError = true;
    drawError();
    return;
  }

  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    WiFiClient *stream = http.getStreamPtr();
    size_t contentLen = http.getSize();
    size_t bufSize = (contentLen > 0 && contentLen < 100*1024) ? contentLen : 100*1024;

    uint8_t *buf = (uint8_t *)malloc(bufSize);
    if (!buf) {
      fetchError = true;
      drawError();
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

    if (idx > 0) {
      if (!TJpgDec.drawJpg(0, 0, buf, idx)) {
        fetchError = true;
        drawError();
      }
    }
    free(buf);
  } else {
    fetchError = true;
    drawError();
  }
  http.end();
}

// -------------------- setup / loop --------------------
void setup() {
  USBSerial.begin(115200);
  USBSerial.println("ESP32-S3 + Arduino_GFX + Cámara");

  // Init Display
  gfx->begin();
  gfx->fillScreen(BACKGROUND);

  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(jpgDrawToGfx);
  TJpgDec.setSwapBytes(true);

  // Conexión WiFi
  WiFi.begin(ssid, password);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis()-start)<12000) { delay(250); USBSerial.print("."); }
  if (WiFi.status() == WL_CONNECTED) {
    USBSerial.printf("\n✅ Conectado, IP: %s\n", WiFi.localIP().toString().c_str());
  }
}

unsigned long lastFetchMs = 0;
const unsigned long fetchIntervalMs = 10000; // 10s

void loop() {
  // Botón táctil: simplificación (simulado)
  // Aquí podrías leer un pin táctil o capacitivo
  bool capturePressed = false; // placeholder

  if (capturePressed) fetchAndShowImage();

  if ((millis()-lastFetchMs) >= fetchIntervalMs) {
    lastFetchMs = millis();
    fetchAndShowImage();
  }

  // Mostrar placeholder si aún no hay imagen
  if (showPlaceholder && !fetchError) drawPlaceholder();
}
