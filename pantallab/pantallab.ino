#include <Arduino.h>
#include "Arduino_GFX_Library.h"
#include "pin_config.h"
#include <Wire.h>
#include "HWCDC.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <TJpg_Decoder.h>
#include <JPEGDEC.h>
#include <ArduinoJson.h>  // Agregado para parse JSON en checkMovimiento

HWCDC USBSerial;

// --- DISPLAY / GFX ---
#define LCD_WIDTH 240
#define LCD_HEIGHT 280

// Configuración del bus SPI para la pantalla
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
const char *camImageURL = "http://192.168.4.1/photo";
const char *camStatusURL = "http://192.168.4.1/status";  // Nueva URL para chequear movimiento
bool ultimoMovimiento = false;  // Para evitar spam de alertas

// -------------------- Funciones de visualización premium --------------------
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

// -------------------- Callback JPEG --------------------
bool jpgDrawToGfx(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  USBSerial.printf("JPEG bloque (%d,%d) %dx%d\n", x, y, w, h);
  gfx->draw16bitRGBBitmap(x, y, bitmap, w, h);
  return true;
}

// -------------------- Chequear estado PIR via /status (nuevo, método GET) ---
bool checkMovimiento() {
  mostrarPantallaProcesando();
  mostrarTextoCentrado("CONSULTANDO SENSOR", 110, 2, TEXTO_SECUNDARIO);
  delay(500);

  HTTPClient http;
  MovimientoStatus status = {false, ultimoFotoCounter};
  if (!http.begin(camStatusURL)) {
    USBSerial.println("http.begin() falló para status");
    return status;
  }

  int httpCode = http.GET();
  USBSerial.printf("HTTP code (status): %d\n", httpCode);

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    StaticJsonDocument<300> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      bool movimiento = doc["movimiento"];
      USBSerial.printf("Movimiento detectado: %s\n", movimiento ? "SI" : "NO");
      http.end();
      return movimiento;
    } else {
      USBSerial.printf("JSON parse error: %s\n", error.c_str());
    }
  }
  http.end();
  return false;  // Asume no movimiento si error
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
    mostrarPantallaError("Error de conexión");
    delay(2000);
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
      USBSerial.println("❌ malloc falló - Verifica memoria disponible");
      mostrarPantallaError("Memoria insuficiente");
      delay(2000);
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
      mostrarBarraProgreso(100, "COMPLETADO");
      delay(500);

      jpeg.openRAM(buf, idx, jpegDrawCallback);
      int imgWidth = jpeg.getWidth();
      int imgHeight = jpeg.getHeight();
      jpeg.close();

      // --- Escalado automático para que la imagen quepa en la pantalla ---
      int scale = 0;
      while ((imgWidth >> scale) > LCD_WIDTH || (imgHeight >> scale) > LCD_HEIGHT) {
        scale++;
      }

      int scaledWidth = imgWidth >> scale;
      int scaledHeight = imgHeight >> scale;
      int x = (LCD_WIDTH - scaledWidth) / 2;
      int y = (LCD_HEIGHT - scaledHeight) / 2;
      if (x < 0) x = 0;
      if (y < 0) y = 0;

      // --- Dibuja la imagen centrada y ajustada ---
      gfx->fillScreen(BACKGROUND);
      dibujarPanelModerno(x-8, y-8, scaledWidth+16, scaledHeight+16, BORDE_SUAVE, ACENTO_CONEXION);
      jpeg.openRAM(buf, idx, jpegDrawCallback);
      jpeg.decode(x, y, scale); // Aplica el escalado calculado
      jpeg.close();
      delay(100);
    }

    free(buf);
  } else {
    mostrarPantallaError("Descarga fallida");
    delay(2000);
  }

  http.end();
  USBSerial.println("=== Fin fetchAndShowImage() ===");

  // --- Espera para que la foto se vea al menos 10 segundos ---
  delay(10000);
}

// ======================= SETUP =======================
/**
 * Inicializa la pantalla, WiFi y muestra la pantalla de inicio.
 */
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

/**
 * Variable global para almacenar el tiempo del último chequeo de movimiento.
 */
unsigned long lastCheckMs = 0;
const unsigned long checkIntervalMs = 3000;  // Chequea cada 3s

// ======================= LOOP PRINCIPAL =======================
/**
 * Bucle principal del programa.
 * - Si hay WiFi, consulta el estado de la cámara cada 3 segundos.
 * - Si detecta movimiento y una nueva foto, muestra alerta y la imagen.
 * - Si no hay movimiento, muestra pantalla de espera.
 * - Si no hay WiFi, intenta reconectar y muestra pantalla de espera.
 */
void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if ((millis() - lastCheckMs) >= checkIntervalMs) {
      USBSerial.println("\n>>> Chequeando movimiento en CAM...");
      lastCheckMs = millis();
      bool hayMovimiento = checkMovimiento();

      if (hayMovimiento && !ultimoMovimiento) {  // Solo alerta si es nuevo
        USBSerial.println(">>> ¡MOVIMIENTO! Mostrando foto...");
        ultimoMovimiento = true;
        efectoAlertaModerno();  // Efecto solo aquí
        delay(1000);
        fetchAndShowImage();  // Descarga y muestra foto
      } else if (!hayMovimiento) {
        ultimoMovimiento = false;
        mostrarPantallaEsperaSinMovimiento();  // Muestra sin movimiento
      }
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
