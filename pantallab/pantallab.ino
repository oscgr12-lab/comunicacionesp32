#include <Arduino.h>
#include "Arduino_GFX_Library.h"
#include "pin_config.h"
#include <Wire.h>
#include "HWCDC.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <TJpg_Decoder.h>
#include <JPEGDEC.h>
#include <ArduinoJson.h>

HWCDC USBSerial;

// --- DISPLAY / GFX ---
#define LCD_WIDTH 240
#define LCD_HEIGHT 280
Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI);
Arduino_GFX *gfx = new Arduino_ST7789(bus, LCD_RST, 0, true, LCD_WIDTH, LCD_HEIGHT, 0, 20, 0, 0);

// --- Colores ---
#define BACKGROUND gfx->color565(20, 25, 35)
#define FONDO_CONECTADO gfx->color565(30, 60, 90)
#define FONDO_ERROR gfx->color565(120, 50, 60)
#define FONDO_ALERTA gfx->color565(180, 70, 40)
#define TEXTO_PRINCIPAL gfx->color565(245, 245, 245)
#define TEXTO_SECUNDARIO gfx->color565(180, 200, 220)
#define ACENTO_ALERTA gfx->color565(255, 100, 50)
#define ACENTO_CONEXION gfx->color565(50, 200, 255)
#define BORDE_SUAVE gfx->color565(60, 70, 90)

// --- WiFi / Cámara ---
const char *ssid = "ESP32-CAM-Test";
const char *password = "12345678";
const char *camImageURL = "http://192.168.4.1/photo";
const char *camStatusURL = "http://192.168.4.1/status";

bool ultimoMovimiento = false;
bool enProceso = false;

// -------------------- Funciones de visualización --------------------
void dibujarPanelModerno(int x, int y, int ancho, int alto, uint16_t colorFondo, uint16_t colorAcento, bool sombra = true) {
  if (sombra) gfx->fillRoundRect(x + 2, y + 2, ancho, alto, 12, gfx->color565(10, 15, 25));
  gfx->fillRoundRect(x, y, ancho, alto, 12, colorFondo);
  gfx->drawRoundRect(x, y, ancho, alto, 12, colorAcento);
}

void mostrarTextoCentrado(const char *texto, int y, int tamaño, uint16_t color, bool sombra = true) {
  gfx->setTextSize(tamaño);
  int textoAncho = strlen(texto) * 6 * tamaño;
  int x = (LCD_WIDTH - textoAncho) / 2;
  if (sombra) {
    gfx->setTextColor(gfx->color565(10, 15, 25));
    gfx->setCursor(x + 1, y + 1);
    gfx->println(texto);
  }
  gfx->setTextColor(color);
  gfx->setCursor(x, y);
  gfx->println(texto);
}

void dibujarIconoAlerta(int x, int y, int tamaño, uint16_t color) {
  int radio = tamaño / 2;
  gfx->fillTriangle(x, y - radio + 5, x - radio + 3, y + radio - 5, x + radio - 3, y + radio - 5, color);
}

void dibujarIconoConexion(int x, int y, int tamaño, uint16_t color) {
  int radio = tamaño / 2;
  gfx->fillCircle(x, y, radio - 8, color);
}

void mostrarPantallaEspera() {
  gfx->fillScreen(BACKGROUND);
  dibujarPanelModerno(30, 80, 180, 80, FONDO_CONECTADO, ACENTO_CONEXION);
  mostrarTextoCentrado("ESPERANDO CONEXIÓN", 110, 2, TEXTO_SECUNDARIO);
  dibujarIconoConexion(120, 170, 60, ACENTO_CONEXION);
}

void mostrarPantallaEsperaSinMovimiento() {
  gfx->fillScreen(BACKGROUND);
  dibujarPanelModerno(30, 80, 180, 80, FONDO_CONECTADO, ACENTO_CONEXION);
  mostrarTextoCentrado("SIN MOVIMIENTO", 100, 2, TEXTO_SECUNDARIO);
  mostrarTextoCentrado("Sensor PIR inactivo", 130, 1, TEXTO_SECUNDARIO);
  dibujarIconoConexion(120, 170, 40, gfx->color565(100, 100, 100));
  mostrarTextoCentrado("Esperando detección...", 220, 1, TEXTO_SECUNDARIO);
}

// -------------------- JSON / HTTP --------------------
bool checkMovimiento() {
  HTTPClient http;
  if (!http.begin(camStatusURL)) {
    USBSerial.println("http.begin() falló para status");
    return false;
  }

  int httpCode = http.GET();
  USBSerial.printf("HTTP code (status): %d\n", httpCode);

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    StaticJsonDocument<300> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      bool movimiento = doc["movimiento"];
      http.end();
      return movimiento;
    }
  }
  http.end();
  return false;
}

JPEGDEC jpeg;
int jpegDrawCallback(JPEGDRAW *pDraw) {
  gfx->draw16bitRGBBitmap(pDraw->x, pDraw->y, (uint16_t*)pDraw->pPixels, pDraw->iWidth, pDraw->iHeight);
  return 1;
}

void fetchAndShowImage() {
  USBSerial.println("=== Descargando imagen por movimiento ===");
  HTTPClient http;

  if (!http.begin(camImageURL)) {
    USBSerial.println("http.begin() falló para imagen");
    mostrarPantallaEspera();
    delay(500);
    return;
  }

  int httpCode = http.GET();
  USBSerial.printf("HTTP code: %d\n", httpCode);

  if (httpCode == HTTP_CODE_OK) {
    WiFiClient *stream = http.getStreamPtr();
    size_t contentLen = http.getSize();
    size_t maxBuf = 200 * 1024;
    size_t bufSize = (contentLen > 0 && contentLen < maxBuf) ? contentLen : maxBuf;

    uint8_t *buf = (uint8_t *)malloc(bufSize);
    if (!buf) {
      mostrarTextoCentrado("MEMORIA INSUFICIENTE", 130, 1, TEXTO_PRINCIPAL);
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
      delay(1);
    }

    if (idx > 0) {
      jpeg.openRAM(buf, idx, jpegDrawCallback);
      int imgWidth = jpeg.getWidth();
      int imgHeight = jpeg.getHeight();

      int scale = 0;
      while ((imgWidth >> scale) > LCD_WIDTH || (imgHeight >> scale) > LCD_HEIGHT) scale++;
      int scaledWidth = imgWidth >> scale;
      int scaledHeight = imgHeight >> scale;

      int x = (LCD_WIDTH - scaledWidth) / 2;
      int y = (LCD_HEIGHT - scaledHeight) / 2;

      gfx->fillScreen(BACKGROUND);
      dibujarPanelModerno(x-8, y-8, scaledWidth+16, scaledHeight+16, BORDE_SUAVE, ACENTO_CONEXION);
      jpeg.decode(x, y, scale);
      jpeg.close();
      delay(100);
    }

    free(buf);
  }

  http.end();
  USBSerial.println("=== Fin fetchAndShowImage() ===");
}

// -------------------- setup / loop --------------------
void setup() {
  USBSerial.begin(115200);
  gfx->begin();
  gfx->fillScreen(BACKGROUND);
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  WiFi.begin(ssid, password);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 12000) {
    delay(250);
    USBSerial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    USBSerial.printf("\n✅ Conectado, IP: %s\n", WiFi.localIP().toString().c_str());
    mostrarTextoCentrado("WiFi Conectado", 110, 2, TEXTO_PRINCIPAL);
  } else {
    mostrarTextoCentrado("Error WiFi", 110, 2, TEXTO_PRINCIPAL);
  }

  delay(2000);
}

unsigned long lastCheckMs = 0;
const unsigned long checkIntervalMs = 3000;

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    mostrarPantallaEspera();
    WiFi.reconnect();
    delay(1000);
    return;
  }

  if (!enProceso && millis() - lastCheckMs >= checkIntervalMs) {
    enProceso = true;
    lastCheckMs = millis();

    USBSerial.println("\n>>> Chequeando movimiento...");
    bool hayMovimiento = checkMovimiento();

    if (hayMovimiento && !ultimoMovimiento) {
      ultimoMovimiento = true;
      gfx->fillScreen(FONDO_ALERTA);
      mostrarTextoCentrado("¡MOVIMIENTO!", 120, 2, TEXTO_PRINCIPAL);
      fetchAndShowImage();
    } else if (!hayMovimiento) {
      ultimoMovimiento = false;
      mostrarPantallaEsperaSinMovimiento();
    }

    enProceso = false;
  }

  delay(50);
}
