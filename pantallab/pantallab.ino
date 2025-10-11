// ======================= INCLUDES Y LIBRERÍAS =======================
// Librerías estándar y de hardware necesarias para el funcionamiento del ESP32 y la pantalla
#include <Arduino.h>                // Funciones básicas de Arduino
#include "Arduino_GFX_Library.h"    // Librería para manejo de pantallas gráficas
#include "pin_config.h"             // Definición de pines personalizados
#include "HWCDC.h"                  // Comunicación USB Serial
#include <WiFi.h>                   // Conexión WiFi
#include <HTTPClient.h>             // Cliente HTTP para peticiones a la cámara
#include <JPEGDEC.h>                // Decodificador JPEG para imágenes
#include <ArduinoJson.h>            // Parseo de JSON para respuestas de la cámara

// ======================= OBJETO SERIAL USB ==========================
HWCDC USBSerial; // Comunicación serial por USB para debug

// ======================= CONFIGURACIÓN DE DISPLAY ===================
// Dimensiones de la pantalla LCD
#define LCD_WIDTH 240
#define LCD_HEIGHT 280

// Agrega aquí la definición del pin vibrador
#define PIN_VIBRADOR 18

// Configuración del bus SPI para la pantalla
Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI);

// Inicialización del objeto de pantalla (ST7789)
Arduino_GFX *gfx = new Arduino_ST7789(
    bus, LCD_RST, 0, true, LCD_WIDTH, LCD_HEIGHT, 0, 20, 0, 0
);

// ======================= PALETA DE COLORES =========================
// Definición de colores personalizados para la interfaz gráfica
#define BACKGROUND gfx->color565(20, 25, 35)
#define FONDO_CONECTADO gfx->color565(30, 60, 90)
#define FONDO_ERROR gfx->color565(120, 50, 60)
#define FONDO_ALERTA gfx->color565(180, 70, 40)
#define TEXTO_PRINCIPAL gfx->color565(245, 245, 245)
#define TEXTO_SECUNDARIO gfx->color565(180, 200, 220)
#define ACENTO_ALERTA gfx->color565(255, 100, 50)
#define ACENTO_CONEXION gfx->color565(50, 200, 255)
#define BORDE_SUAVE gfx->color565(60, 70, 90)

// ======================= CONFIGURACIÓN WIFI Y CÁMARA ===============
// Credenciales WiFi y URLs de la cámara ESP32-CAM
const char *ssid = "ESP32-CAM-Test";              // SSID del AP de la cámara
const char *password = "12345678";                // Contraseña del AP
const char *camImageURL = "http://192.168.4.1/photo";   // URL para obtener la foto
const char *camStatusURL = "http://192.168.4.1/status"; // URL para obtener el estado

bool ultimoMovimiento = false; // Flag para evitar alertas repetidas

// ======================= CONTROL DE FOTOS ==========================
// Lleva el control del último contador de fotos recibido
int ultimoFotoCounter = -1;

// Estructura para almacenar el estado de movimiento y el contador de fotos
struct MovimientoStatus {
  bool movimiento; // Indica si hay movimiento detectado
  int fotos;       // Contador de fotos tomadas por la cámara
};

// ======================= FUNCIONES DE INTERFAZ =====================
// ...Aquí documenta cada función con un comentario breve arriba...
/**
 * Dibuja un panel moderno con bordes redondeados y sombra.
 * @param x, y: posición
 * @param ancho, alto: dimensiones
 * @param colorFondo: color de fondo
 * @param colorAcento: color del borde/acento
 * @param sombra: si se dibuja sombra o no
 */
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
  mostrarTextoCentrado("DETECCION", 40, 3, ACENTO_ALERTA); // y=40, sin tilde
  mostrarTextoCentrado("DE MOVIMIENTO", 80, 2, TEXTO_PRINCIPAL); // y=80
  mostrarTextoCentrado("ACTIVIDAD SOSPECHOSA DETECTADA", 130, 1, TEXTO_SECUNDARIO); // y=130
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
  mostrarTextoCentrado("ERROR", 45, 2, TEXTO_PRINCIPAL); // y=45
  mostrarTextoCentrado(mensaje, 160, 1, TEXTO_SECUNDARIO); // y=160
}

void efectoAlertaModerno() {
  for (int pulso = 0; pulso < 3; pulso++) {
    for (int intensidad = 0; intensidad <= 20; intensidad += 5) {
      uint16_t colorPulso = gfx->color565(180 + intensidad*3, 70 + intensidad, 40 + intensidad*2);
      gfx->fillScreen(BACKGROUND);
      dibujarPanelModerno(20, 60, 200, 100, FONDO_ALERTA, colorPulso);
      dibujarIconoAlerta(120, 110, 80, colorPulso);
      mostrarTextoCentrado("DETECCION", 40, 3, colorPulso); // sin tilde
      mostrarTextoCentrado("MOVIMIENTO", 170, 2, TEXTO_PRINCIPAL);
      delay(50);
    }
    for (int intensidad = 20; intensidad >= 0; intensidad -= 5) {
      uint16_t colorPulso = gfx->color565(180 + intensidad*3, 70 + intensidad, 40 + intensidad*2);
      gfx->fillScreen(BACKGROUND);
      dibujarPanelModerno(20, 60, 200, 100, FONDO_ALERTA, colorPulso);
      dibujarIconoAlerta(120, 110, 80, colorPulso);
      mostrarTextoCentrado("DETECCION", 40, 3, colorPulso); // sin tilde
      mostrarTextoCentrado("MOVIMIENTO", 170, 2, TEXTO_PRINCIPAL);
      delay(50);
    }
  }
  mostrarPantallaAlertaPremium();
}

void mostrarPantallaProcesando() {
  // Solo dibuja el fondo y panel si realmente cambió de pantalla
  static bool yaDibujado = false;
  if (!yaDibujado) {
    gfx->fillScreen(BACKGROUND);
    dibujarPanelModerno(30, 80, 180, 80, FONDO_CONECTADO, ACENTO_CONEXION);
    mostrarTextoCentrado("PROCESANDO", 100, 2, TEXTO_PRINCIPAL);
    yaDibujado = true;
  }
  // Solo actualiza los puntos animados
  static int frame = 0;
  frame = (frame + 1) % 4;
  gfx->fillRect(140, 130, 40, 20, BACKGROUND); // Limpia solo la zona de los puntos
  gfx->setTextSize(2);
  gfx->setTextColor(TEXTO_SECUNDARIO);
  gfx->setCursor(140, 130);
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
int jpegOffsetX = 0;
int jpegOffsetY = 0;

int jpegDrawCallback(JPEGDRAW *pDraw) {
  // Dibuja la imagen tal cual, sin rotar
  for (int y = 0; y < pDraw->iHeight; y++) {
    for (int x = 0; x < pDraw->iWidth; x++) {
      int drawX = jpegOffsetX + pDraw->x + x;
      int drawY = jpegOffsetY + pDraw->y + y;
      uint16_t color = ((uint16_t*)pDraw->pPixels)[y * pDraw->iWidth + x];
      gfx->drawPixel(drawX, drawY, color);
    }
  }
  return 1;
}

// -------------------- Chequear estado PIR via /status (nuevo, método GET) ---
MovimientoStatus checkMovimiento() {
  mostrarPantallaProcesando();
  gfx->fillRect(0, 140, LCD_WIDTH, 20, BACKGROUND); // Limpia solo la zona del texto secundario
  mostrarTextoCentrado("CONSULTANDO SENSOR", 140, 2, TEXTO_SECUNDARIO); // y=140, debajo de "PROCESANDO"
  delay(5000);

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
    USBSerial.printf("Payload status: %s\n", payload.c_str());

    StaticJsonDocument<300> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      status.movimiento = doc["movimiento"];
      status.fotos = doc["fotos"];
      USBSerial.printf("Movimiento detectado: %s | Fotos: %d\n", status.movimiento ? "SI" : "NO", status.fotos);
    } else {
      USBSerial.printf("JSON parse error: %s\n", error.c_str());
    }
  } else {
    USBSerial.printf("Error GET status: %d\n", httpCode);
  }
  http.end();
  return status;
}

// -------------------- Descargar y mostrar imagen (modificado: sin trigger) --------------------
JPEGDEC jpeg;

void fetchAndShowImage() {
  USBSerial.println("=== Descargando imagen por movimiento ===");

  mostrarBarraProgreso(40, "PROCESANDO IMAGEN");

  HTTPClient http;
  if (!http.begin(camImageURL)) {
    USBSerial.println("http.begin() falló para imagen");
    mostrarPantallaError("Error de conexión");
    delay(5000);
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
      delay(5000);
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

      // --- Fondo limpio ---
      gfx->fillScreen(BACKGROUND);

      // --- Obtener dimensiones de la imagen JPEG ---
      int imgWidth = 0, imgHeight = 0;
      if (jpeg.openRAM(buf, idx, jpegDrawCallback)) {
        imgWidth = jpeg.getWidth();
        imgHeight = jpeg.getHeight();
        jpeg.close();
      } else {
        USBSerial.println("No se pudo abrir el JPEG para obtener dimensiones");
        mostrarPantallaError("JPEG inválido");
        free(buf);
        http.end();
        return;
      }

      // --- Escalado proporcional para que la imagen quepa dentro de la pantalla ---
      int scale = 0;
      while ((imgWidth / (1 << (scale + 1))) >= LCD_WIDTH && (imgHeight / (1 << (scale + 1))) >= LCD_HEIGHT) {
        scale++;
      }
      int scaledWidth = imgWidth >> scale;
      int scaledHeight = imgHeight >> scale;

      // --- Coordenadas para centrar la imagen ---
      jpegOffsetX = (LCD_WIDTH - scaledWidth) / 2;
      jpegOffsetY = (LCD_HEIGHT - scaledHeight) / 2;

      // --- Dibuja la imagen centrada ---
      jpeg.openRAM(buf, idx, jpegDrawCallback);
      jpeg.decode(0, 0, scale);
      jpeg.close();

      mostrarTextoCentrado("CAPTURA COMPLETADA", 270, 1, TEXTO_SECUNDARIO);

      char timestamp[20];
      snprintf(timestamp, sizeof(timestamp), "%lu", millis() / 1000);
      mostrarTextoCentrado(timestamp, 285, 1, TEXTO_SECUNDARIO);
    }
    free(buf);
  } else {
    mostrarPantallaError("Descarga fallida");
    delay(5000);
  }
  http.end();
  USBSerial.println("=== Fin fetchAndShowImage() ===");

  // --- Espera para que la foto se vea al menos 10 segundos ---
  delay(10000);
}

// Nueva función para sin movimiento
void mostrarPantallaEsperaSinMovimiento() {
  gfx->fillScreen(BACKGROUND);
  dibujarPanelModerno(30, 80, 180, 80, FONDO_CONECTADO, ACENTO_CONEXION);
  mostrarTextoCentrado("SIN MOVIMIENTO", 100, 2, TEXTO_SECUNDARIO); // y=100
  mostrarTextoCentrado("SENSOR PIR INACTIVO", 130, 1, TEXTO_SECUNDARIO); // y=130
  dibujarIconoConexion(120, 170, 40, gfx->color565(100, 100, 100));  // Icono gris para inactivo
  mostrarTextoCentrado("ESPERANDO DETECCION...", 210, 1, TEXTO_SECUNDARIO); // y=210, sin tilde
}

// ======================= SETUP =======================
/**
 * Inicializa la pantalla, WiFi y muestra la pantalla de inicio.
 */
void setup() {
  USBSerial.begin(115200); // Inicia comunicación serial para debug
  USBSerial.println("ESP32-S3 + Arduino_GFX + Cámara - Sistema Premium");

  // Inicializa la pantalla
  if (!gfx->begin()) {
    USBSerial.println("gfx->begin() falló!");
  }
  gfx->fillScreen(BACKGROUND);
  gfx->setTextWrap(false);

  pinMode(LCD_BL, OUTPUT);      // Pin de retroiluminación
  digitalWrite(LCD_BL, HIGH);   // Enciende la retroiluminación

  // Elimina o comenta estas líneas:
  // TJpgDec.setJpgScale(1);
  // TJpgDec.setCallback(jpgDrawToGfx);
  // TJpgDec.setSwapBytes(true);

  // Pantalla de inicio
  gfx->fillScreen(BACKGROUND);
  mostrarTextoCentrado("SISTEMA DE SEGURIDAD", 60, 2, ACENTO_CONEXION); // y=60
  mostrarTextoCentrado("ESP32-CAM PRO", 90, 2, TEXTO_PRINCIPAL); // y=90
  dibujarIconoConexion(120, 140, 80, ACENTO_CONEXION); // y=140
  mostrarTextoCentrado("INICIANDO...", 230, 1, TEXTO_SECUNDARIO); // y=230
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
    gfx->print("CONECTANDO A LA RED..."); // Mayúsculas y puntos suspensivos
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

/**
 * Variable global para almacenar el tiempo del último chequeo de movimiento.
 */
unsigned long lastCheckMs = 0;

/**
 * Intervalo de tiempo (en milisegundos) entre chequeos de movimiento.
 */
const unsigned long checkIntervalMs = 2000;  // Chequea cada 2s

// ======================= LOOP PRINCIPAL =======================
/**
 * Bucle principal del programa.
 * - Si hay WiFi, consulta el estado de la cámara cada 3 segundos.
 * - Si detecta movimiento y una nueva foto, muestra alerta y la imagen.
 * - Si no hay movimiento, muestra pantalla de espera.
 * - Si no hay WiFi, intenta reconectar y muestra pantalla de espera.
 */
void loop() {
  if (WiFi.status() == WL_CONNECTED) { // Si está conectado al WiFi
    if ((millis() - lastCheckMs) >= checkIntervalMs) { // Si pasó el intervalo
      USBSerial.println("\n>>> Chequeando movimiento en CAM...");
      lastCheckMs = millis(); // Actualiza el tiempo del último chequeo
      MovimientoStatus status = checkMovimiento(); // Consulta el estado de la cámara

      // Si hay movimiento y una nueva foto (contador de fotos cambió)
      if (status.movimiento && status.fotos != ultimoFotoCounter) {
        USBSerial.println(">>> ¡MOVIMIENTO NUEVO! Mostrando foto...");
        ultimoMovimiento = true;
        ultimoFotoCounter = status.fotos; // Actualiza el contador de fotos
        efectoAlertaModerno();            // Muestra animación de alerta
        delay(1000);                      // Espera breve
        fetchAndShowImage();              // Descarga y muestra la imagen
      } else if (!status.movimiento) {
        // Si no hay movimiento, muestra pantalla de espera
        ultimoMovimiento = false;
        mostrarPantallaEsperaSinMovimiento();
      }
    }
  } else {
    // Si no hay WiFi, muestra pantalla de espera y reintenta conexión
    mostrarPantallaEspera();
    delay(1000);
    WiFi.reconnect();
  }
}

/**
 * Muestra una pantalla de espera cuando no hay conexión WiFi.
 * Dibuja un panel y un icono de conexión.
 */
void mostrarPantallaEspera() {
  gfx->fillScreen(BACKGROUND);
  dibujarPanelModerno(30, 80, 180, 80, FONDO_CONECTADO, ACENTO_CONEXION);
  mostrarTextoCentrado("ESPERANDO CONEXIÓN", 110, 2, TEXTO_SECUNDARIO); // y=110
  dibujarIconoConexion(120, 170, 60, ACENTO_CONEXION);
}