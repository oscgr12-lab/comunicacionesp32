#include <Arduino.h>
#include "Arduino_GFX_Library.h"
#include "pin_config.h"
#include <Wire.h>
#include "HWCDC.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <TJpg_Decoder.h>
#include <JPEGDEC.h>

HWCDC USBSerial;

// --- DISPLAY / GFX ---
Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI);
Arduino_GFX *gfx = new Arduino_ST7789(bus, LCD_RST /* RST */,
                                      0 /* rotation */, true /* IPS */,
                                      LCD_WIDTH, LCD_HEIGHT, 0, 20, 0, 0);

// --- Colores sobrios ---
#define BACKGROUND gfx->color565(30, 30, 30)         // Gris oscuro
#define FONDO_CONECTADO gfx->color565(40, 60, 120)   // Azul sobrio
#define FONDO_ERROR gfx->color565(120, 40, 40)       // Rojo oscuro
#define FONDO_ALERTA gfx->color565(40, 120, 40)      // Verde sobrio

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
JPEGDEC jpeg; // Declaración global del objeto JPEGDEC

int jpegDrawCallback(JPEGDRAW *pDraw) {
  // Dibuja cada bloque en la pantalla
  gfx->draw16bitRGBBitmap(pDraw->x, pDraw->y, (uint16_t*)pDraw->pPixels, pDraw->iWidth, pDraw->iHeight);
  return 1;
}

void fetchAndShowImage() {
  USBSerial.println("=== Inicio fetchAndShowImage() ===");

  // Fondo sobrio para alerta
  gfx->fillScreen(FONDO_ALERTA);
  gfx->drawRect(10, 10, 220, 260, YELLOW); // Marco dorado fino

  // Texto profesional y centrado
  gfx->setTextSize(5);
  gfx->setTextColor(RED);
  gfx->setCursor(40, 40);
  gfx->println("ALERTA!");

  gfx->setTextSize(3);
  gfx->setTextColor(WHITE);
  gfx->setCursor(30, 110);
  gfx->println("MOVIMIENTO");

  gfx->setTextSize(2);
  gfx->setTextColor(YELLOW);
  gfx->setCursor(60, 170);
  gfx->println("DETECTADO");

  // Parpadeo elegante de "ALERTA!"
  for (int i = 0; i < 2; i++) {
    gfx->fillRect(40, 40, 160, 50, FONDO_ALERTA);
    gfx->setTextSize(5);
    gfx->setTextColor(RED);
    gfx->setCursor(40, 40);
    gfx->println("ALERTA!");
    delay(200);
    gfx->fillRect(40, 40, 160, 50, FONDO_ALERTA);
    gfx->setTextSize(5);
    gfx->setTextColor(YELLOW);
    gfx->setCursor(40, 40);
    gfx->println("ALERTA!");
    delay(200);
  }
  delay(700);

  // Disparar captura
  if (!triggerCapture()) {
    USBSerial.println("Fallo al disparar captura");
    gfx->fillScreen(FONDO_ERROR);
    gfx->drawRect(10, 60, 220, 80, WHITE);
    gfx->setTextSize(4);
    gfx->setTextColor(WHITE);
    gfx->setCursor(60, 80);
    gfx->println("ERROR");
    gfx->setTextSize(2);
    gfx->setTextColor(YELLOW);
    gfx->setCursor(40, 120);
    gfx->println("No se pudo capturar");
    delay(1500);
    return;
  } else {
    delay(3000);
  }

  HTTPClient http;
  if (!http.begin(camImageURL)) {
    USBSerial.println("http.begin() falló para imagen");
    gfx->fillScreen(FONDO_ERROR);
    gfx->drawRect(10, 60, 220, 80, WHITE);
    gfx->setTextSize(4);
    gfx->setTextColor(WHITE);
    gfx->setCursor(60, 80);
    gfx->println("ERROR");
    gfx->setTextSize(2);
    gfx->setTextColor(YELLOW);
    gfx->setCursor(40, 120);
    gfx->println("No se pudo descargar");
    delay(1500);
    return;
  }
  USBSerial.printf("Solicitando imagen en %s ...\n", camImageURL);
  int httpCode = http.GET();
  USBSerial.printf("HTTP code: %d\n", httpCode);

  if (httpCode == HTTP_CODE_OK) {
    WiFiClient *stream = http.getStreamPtr();
    size_t contentLen = http.getSize();
    USBSerial.printf("Content-Length: %u bytes\n", (unsigned)contentLen);

    size_t maxBuf = 200 * 1024;
    size_t bufSize = (contentLen > 0 && contentLen < maxBuf) ? contentLen : maxBuf;

    uint8_t *buf = (uint8_t *)malloc(bufSize);
    if (!buf) {
      USBSerial.println("❌ malloc falló - Verifica memoria disponible");
      gfx->fillScreen(FONDO_ERROR);
      gfx->drawRect(10, 60, 220, 80, WHITE);
      gfx->setTextSize(4);
      gfx->setTextColor(WHITE);
      gfx->setCursor(60, 80);
      gfx->println("ERROR");
      gfx->setTextSize(2);
      gfx->setTextColor(YELLOW);
      gfx->setCursor(40, 120);
      gfx->println("Memoria insuficiente");
      delay(1500);
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
      jpeg.openRAM(buf, idx, jpegDrawCallback);

      int imgWidth = jpeg.getWidth();
      int imgHeight = jpeg.getHeight();

      int scale = 0;
      while ((imgWidth >> scale) > LCD_WIDTH || (imgHeight >> scale) > LCD_HEIGHT) {
        scale++;
      }

      int scaledWidth = imgWidth >> scale;
      int scaledHeight = imgHeight >> scale;
      int x = (LCD_WIDTH - scaledWidth) / 2;
      int y = (LCD_HEIGHT - scaledHeight) / 2;

      gfx->fillScreen(BACKGROUND); // Fondo gris oscuro para la imagen
      jpeg.decode(x, y, scale);
      jpeg.close();
    }
    free(buf);
  } else {
    gfx->fillScreen(FONDO_ERROR);
    gfx->drawRect(10, 60, 220, 80, WHITE);
    gfx->setTextSize(4);
    gfx->setTextColor(WHITE);
    gfx->setCursor(60, 80);
    gfx->println("ERROR");
    gfx->setTextSize(2);
    gfx->setTextColor(YELLOW);
    gfx->setCursor(40, 120);
    gfx->println("No se pudo descargar");
    delay(1500);
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
    USBSerial.printf("RSSI (calidad señal): %d dBm\n", WiFi.RSSI());
    gfx->fillScreen(FONDO_CONECTADO);
    gfx->drawRect(10, 60, 220, 80, WHITE);
    gfx->setTextSize(3);
    gfx->setTextColor(WHITE);
    gfx->setCursor(40, 90);
    gfx->println("Conectado a");
    gfx->setTextSize(2);
    gfx->setTextColor(YELLOW);
    gfx->setCursor(60, 130);
    gfx->println("ESP32-CAM");
    delay(1500);
  } else {
    gfx->fillScreen(FONDO_ERROR);
    gfx->drawRect(10, 60, 220, 80, WHITE);
    gfx->setTextSize(3);
    gfx->setTextColor(WHITE);
    gfx->setCursor(60, 90);
    gfx->println("SIN CONEXION");
    gfx->setTextSize(2);
    gfx->setTextColor(YELLOW);
    gfx->setCursor(40, 130);
    gfx->println("Verifique red WiFi");
    delay(1500);
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