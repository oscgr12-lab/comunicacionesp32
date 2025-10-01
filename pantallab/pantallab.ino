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
#define LCD_WIDTH 240
#define LCD_HEIGHT 280
Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI);
Arduino_GFX *gfx = new Arduino_ST7789(bus, LCD_RST /* RST */,
                                      0 /* rotation */, true /* IPS */,
                                      LCD_WIDTH, LCD_HEIGHT, 0, 20, 0, 0);

// --- Paleta de colores premium ---
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
const char *camTriggerURL = "http://192.168.4.1/capture";
const char *camImageURL = "http://192.168.4.1/photo";

// -------------------- Funciones de visualización premium --------------------
void dibujarPanelModerno(int x, int y, int ancho, int alto, uint16_t colorFondo, uint16_t colorAcento, bool sombra = true) {
  if (sombra) {
    gfx->fillRoundRect(x + 2, y + 2, ancho, alto, 12, gfx->color565(10, 15, 25));
  }
  gfx->fillRoundRect(x, y, ancho, alto, 12, colorFondo);
  gfx->drawRoundRect(x, y, ancho, alto, 12, colorAcento);
  // Gradiente simulado
  for (int i = 0; i < 5; i++) {
    gfx->drawFastHLine(x + 5, y + i + 5, ancho - 10, gfx->color565(
      ((colorAcento >> 11) & 0x1F) * 8,
      ((colorAcento >> 5) & 0x3F) * 4,
      (colorAcento & 0x1F) * 8
    ));
  }
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
  gfx->fillRect(x - 2, y - radio + 15, 4, radio - 5, BACKGROUND);
  gfx->fillRect(x - 2, y + radio - 10, 4, 4, BACKGROUND);
  gfx->drawTriangle(x, y - radio + 7, x - radio + 5, y + radio - 7, x + radio - 5, y + radio - 7, TEXTO_PRINCIPAL);
}

void dibujarIconoConexion(int x, int y, int tamaño, uint16_t color) {
  int radio = tamaño / 2;
  gfx->fillCircle(x, y, radio - 8, color);
  for (int i = 0; i < 4; i++) {
    int ondaRadio = radio - 15 + (i * 8);
    gfx->drawCircle(x, y, ondaRadio, color);
  }
  for (int i = 0; i < 4; i++) {
    int puntos = 4 + i * 2;
    for (int j = 0; j < puntos; j++) {
      float angulo = 2 * PI * j / puntos;
      int px = x + (radio - 15 + i * 8) * cos(angulo - PI/2);
      int py = y + (radio - 15 + i * 8) * sin(angulo - PI/2);
      gfx->fillCircle(px, py, 2, color);
    }
  }
}

void mostrarEstadoConexion(bool conectado, int rssi = 0) {
  gfx->fillScreen(BACKGROUND);
  dibujarPanelModerno(15, 50, 210, 120, FONDO_CONECTADO, ACENTO_CONEXION);
  if (conectado) {
    dibujarIconoConexion(120, 110, 60, TEXTO_PRINCIPAL);
    mostrarTextoCentrado("CONEXIÓN ESTABLECIDA", 40, 2, TEXTO_SECUNDARIO);
    char infoStr[30];
    snprintf(infoStr, sizeof(infoStr), "Señal: %d dBm", rssi);
    mostrarTextoCentrado(infoStr, 170, 1, TEXTO_SECUNDARIO);
    mostrarTextoCentrado("Sistema listo", 185, 1, TEXTO_SECUNDARIO);
  } else {
    gfx->drawCircle(120, 110, 25, TEXTO_PRINCIPAL);
    gfx->drawLine(100, 90, 140, 130, TEXTO_PRINCIPAL);
    gfx->drawLine(100, 130, 140, 90, TEXTO_PRINCIPAL);
    mostrarTextoCentrado("SIN CONEXIÓN", 40, 2, TEXTO_SECUNDARIO);
    mostrarTextoCentrado("Verifique la red WiFi", 170, 1, TEXTO_SECUNDARIO);
  }
}

void mostrarPantallaAlertaPremium() {
  gfx->fillScreen(BACKGROUND);
  for (int i = 0; i < 5; i++) {
    gfx->drawRoundRect(10 + i*2, 10 + i*2, LCD_WIDTH - 20 - i*4, LCD_HEIGHT - 20 - i*4, 8, FONDO_ALERTA);
  }
  dibujarPanelModerno(20, 60, 200, 100, FONDO_ALERTA, ACENTO_ALERTA);
  dibujarIconoAlerta(120, 110, 80, ACENTO_ALERTA);
  mostrarTextoCentrado("DETECCIÓN", 35, 3, ACENTO_ALERTA);
  mostrarTextoCentrado("MOVIMIENTO", 170, 2, TEXTO_PRINCIPAL);
  mostrarTextoCentrado("ACTIVIDAD SUSPECTA DETECTADA", 190, 1, TEXTO_SECUNDARIO);
}

void mostrarPantallaError(const char *mensaje) {
  gfx->fillScreen(BACKGROUND);
  dibujarPanelModerno(20, 70, 200, 80, FONDO_ERROR, TEXTO_PRINCIPAL);
  gfx->fillCircle(120, 110, 25, TEXTO_PRINCIPAL);
  gfx->fillCircle(120, 110, 18, FONDO_ERROR);
  gfx->setTextSize(3);
  gfx->setTextColor(TEXTO_PRINCIPAL);
  gfx->setCursor(113, 103);
  gfx->print("!");
  mostrarTextoCentrado("ERROR", 45, 2, TEXTO_PRINCIPAL);
  mostrarTextoCentrado(mensaje, 160, 1, TEXTO_SECUNDARIO);
}

void efectoAlertaModerno() {
  for (int pulso = 0; pulso < 3; pulso++) {
    for (int intensidad = 0; intensidad <= 20; intensidad += 5) {
      uint16_t colorPulso = gfx->color565(180 + intensidad*3, 70 + intensidad, 40 + intensidad*2);
      gfx->fillScreen(BACKGROUND);
      dibujarPanelModerno(20, 60, 200, 100, FONDO_ALERTA, colorPulso);
      dibujarIconoAlerta(120, 110, 80, colorPulso);
      mostrarTextoCentrado("DETECCIÓN", 35, 3, colorPulso);
      mostrarTextoCentrado("MOVIMIENTO", 170, 2, TEXTO_PRINCIPAL);
      delay(50);
    }
    for (int intensidad = 20; intensidad >= 0; intensidad -= 5) {
      uint16_t colorPulso = gfx->color565(180 + intensidad*3, 70 + intensidad, 40 + intensidad*2);
      gfx->fillScreen(BACKGROUND);
      dibujarPanelModerno(20, 60, 200, 100, FONDO_ALERTA, colorPulso);
      dibujarIconoAlerta(120, 110, 80, colorPulso);
      mostrarTextoCentrado("DETECCIÓN", 35, 3, colorPulso);
      mostrarTextoCentrado("MOVIMIENTO", 170, 2, TEXTO_PRINCIPAL);
      delay(50);
    }
  }
  mostrarPantallaAlertaPremium();
}

void mostrarPantallaProcesando() {
  gfx->fillScreen(BACKGROUND);
  dibujarPanelModerno(30, 80, 180, 80, FONDO_CONECTADO, ACENTO_CONEXION);
  static int frame = 0;
  frame = (frame + 1) % 4;
  mostrarTextoCentrado("PROCESANDO", 105, 2, TEXTO_PRINCIPAL);
  gfx->setTextSize(2);
  gfx->setTextColor(TEXTO_SECUNDARIO);
  gfx->setCursor(140, 105);
  for (int i = 0; i < frame; i++) {
    gfx->print(".");
  }
  for (int i = frame; i < 3; i++) {
    gfx->print(" ");
  }
}

void mostrarBarraProgreso(int porcentaje, const char* texto = "CARGANDO") {
  int barWidth = 160;
  int barHeight = 12;
  int x = (LCD_WIDTH - barWidth) / 2;
  int y = 150;
  gfx->fillRoundRect(x, y, barWidth, barHeight, 6, BORDE_SUAVE);
  int progressWidth = (barWidth - 4) * porcentaje / 100;
  if (progressWidth > 0) {
    uint16_t colorProgreso = gfx->color565(50 + porcentaje * 2, 150 + porcentaje, 50 + porcentaje);
    gfx->fillRoundRect(x + 2, y + 2, progressWidth, barHeight - 4, 4, colorProgreso);
  }
  char progresoStr[20];
  snprintf(progresoStr, sizeof(progresoStr), "%s %d%%", texto, porcentaje);
  mostrarTextoCentrado(progresoStr, 170, 1, TEXTO_SECUNDARIO);
}

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
  int httpCode = http.POST("");
  USBSerial.printf("HTTP code (trigger): %d\n", httpCode);
  http.end();
  return (httpCode == HTTP_CODE_OK);
}

// -------------------- Descargar y mostrar imagen --------------------
JPEGDEC jpeg;

int jpegDrawCallback(JPEGDRAW *pDraw) {
  gfx->draw16bitRGBBitmap(pDraw->x, pDraw->y, (uint16_t*)pDraw->pPixels, pDraw->iWidth, pDraw->iHeight);
  return 1;
}

void fetchAndShowImage() {
  USBSerial.println("=== Inicio fetchAndShowImage() ===");

  // Pantalla de alerta premium
  efectoAlertaModerno();
  delay(1000);

  // Disparar captura
  mostrarPantallaProcesando();
  mostrarBarraProgreso(10, "INICIANDO CAPTURA");

  if (!triggerCapture()) {
    USBSerial.println("Fallo al disparar captura");
    mostrarPantallaError("Fallo en captura");
    delay(2000);
    return;
  }

  delay(1000);
  mostrarBarraProgreso(40, "PROCESANDO IMAGEN");

  HTTPClient http;
  if (!http.begin(camImageURL)) {
    USBSerial.println("http.begin() falló para imagen");
    mostrarPantallaError("Error de conexión");
    delay(2000);
    return;
  }

  USBSerial.printf("Solicitando imagen en %s ...\n", camImageURL);
  mostrarBarraProgreso(60, "DESCARGANDO");

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
      mostrarPantallaError("Memoria insuficiente");
      delay(2000);
      http.end();
      return;
    }

    mostrarBarraProgreso(80, "DECODIFICANDO");

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
      mostrarBarraProgreso(100, "COMPLETADO");
      delay(500);

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

      gfx->fillScreen(BACKGROUND);
      dibujarPanelModerno(x-8, y-8, scaledWidth+16, scaledHeight+16, BORDE_SUAVE, ACENTO_CONEXION);
      jpeg.decode(x, y, scale);
      jpeg.close();

      mostrarTextoCentrado("CAPTURA COMPLETADA", 270, 1, TEXTO_SECUNDARIO);

      char timestamp[20];
      snprintf(timestamp, sizeof(timestamp), "%lu", millis() / 1000);
      mostrarTextoCentrado(timestamp, 285, 1, TEXTO_SECUNDARIO);
    }
    free(buf);
  } else {
    mostrarPantallaError("Descarga fallida");
    delay(2000);
  }
  http.end();
  USBSerial.println("=== Fin fetchAndShowImage() ===");
}

// -------------------- setup / loop --------------------
void setup() {
  USBSerial.begin(115200);
  USBSerial.println("ESP32-S3 + Arduino_GFX + Cámara - Sistema Premium");

  // Init Display
  if (!gfx->begin()) {
    USBSerial.println("gfx->begin() falló!");
  }
  gfx->fillScreen(BACKGROUND);
  gfx->setTextWrap(false);

  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  // Config TJpg_Decoder
  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(jpgDrawToGfx);
  TJpgDec.setSwapBytes(true);

  // Pantalla de inicio premium
  gfx->fillScreen(BACKGROUND);
  mostrarTextoCentrado("SISTEMA DE SEGURIDAD", 80, 2, ACENTO_CONEXION);
  mostrarTextoCentrado("ESP32-CAM PRO", 110, 2, TEXTO_PRINCIPAL);
  dibujarIconoConexion(120, 170, 80, ACENTO_CONEXION);
  mostrarTextoCentrado("INICIANDO...", 220, 1, TEXTO_SECUNDARIO);
  delay(1500);

  // Conexión WiFi
  USBSerial.printf("Conectando a AP: %s\n", ssid);
  WiFi.begin(ssid, password);
  unsigned long start = millis();

  int frame = 0;
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 12000) {
    delay(250);
    USBSerial.print(".");
    gfx->fillRect(40, 200, 160, 20, BACKGROUND);
    gfx->setTextSize(1);
    gfx->setTextColor(TEXTO_SECUNDARIO);
    gfx->setCursor(40, 200);
    gfx->print("Conectando a la red");
    for (int i = 0; i < (frame % 4); i++) {
      gfx->print(".");
    }
    frame++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    USBSerial.printf("\n✅ Conectado, IP: %s\n", WiFi.localIP().toString().c_str());
    USBSerial.printf("RSSI (calidad señal): %d dBm\n", WiFi.RSSI());
    mostrarEstadoConexion(true, WiFi.RSSI());
  } else {
    USBSerial.println("\n❌ Fallo de conexión");
    mostrarEstadoConexion(false);
  }
  delay(2000);
}

unsigned long lastFetchMs = 0;
const unsigned long fetchIntervalMs = 10000;

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if ((millis() - lastFetchMs) >= fetchIntervalMs) {
      USBSerial.println("\n>>> Capturando imagen de la CAM...");
      lastFetchMs = millis();
      fetchAndShowImage();
    }
  } else {
    mostrarPantallaEspera();
    delay(1000); // Espera antes de volver a checar conexión
    WiFi.reconnect(); // Intenta reconectar
  }
}

void mostrarPantallaEspera() {
  gfx->fillScreen(BACKGROUND);
  dibujarPanelModerno(30, 80, 180, 80, FONDO_CONECTADO, ACENTO_CONEXION);
  mostrarTextoCentrado("ESPERANDO CONEXIÓN", 110, 2, TEXTO_SECUNDARIO);
  dibujarIconoConexion(120, 170, 60, ACENTO_CONEXION);
}