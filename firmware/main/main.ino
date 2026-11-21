
#include <Arduino.h>
#include <Wire.h>
#include <BluetoothA2DPSink.h>
#include <esp_avrc_api.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// =====================================
// CONFIGURACAO OLED
// =====================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_RESET -1
#define OLED_ADDRESS 0x3C

#define OLED_SDA 33
#define OLED_SCL 32

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);

// =====================================
// BLUETOOTH
// =====================================

class SilentA2DPSink : public BluetoothA2DPSink {
public:
  SilentA2DPSink() { is_output = false; }
};
SilentA2DPSink a2dp_sink;

// =====================================
// INFORMACOES DA MUSICA
// =====================================

char musicTitle[100] = "Aguardando musica";
char musicArtist[70] = "Desconhecido";
char musicAlbum[70] = "";

bool metadataReceived = false;
unsigned long lastMetadataRequest = 0;

uint32_t durationMs = 0;
uint32_t positionMs = 0;

bool bluetoothConnected = false;
bool isPlaying = false;
bool serialMetadataActive = false;

unsigned long lastDisplayUpdate = 0;
unsigned long lastPositionTick = 0;

// =====================================
// FORMATAR TEMPO
// =====================================

void formatTime(uint32_t milliseconds, char *buffer, size_t size) {

  uint32_t seconds = milliseconds / 1000;

  snprintf(
    buffer,
    size,
    "%02lu:%02lu",
    (unsigned long)(seconds / 60),
    (unsigned long)(seconds % 60)
  );
}

// =====================================
// METADADOS AVRCP
// =====================================

void metadataCallback(uint8_t id, const uint8_t *text) {

  if (text == nullptr) return;

  metadataReceived = true;

  Serial.printf("AVRC meta id=0x%x len=%u text=%s\n", id, (unsigned)strlen((const char*)text), (const char*)text);

  const char *value = (const char *)text;

  switch (id) {

    case ESP_AVRC_MD_ATTR_TITLE:

      if (value[0] != '\0') {
        strncpy(musicTitle, value, sizeof(musicTitle) - 1);
        musicTitle[sizeof(musicTitle) - 1] = '\0';
      }

      Serial.printf("Titulo: %s\n", musicTitle);

      break;

    case ESP_AVRC_MD_ATTR_ARTIST:

      if (value[0] != '\0') {
        strncpy(musicArtist, value, sizeof(musicArtist) - 1);
        musicArtist[sizeof(musicArtist) - 1] = '\0';
      }

      Serial.printf("Artista: %s\n", musicArtist);

      break;

    case ESP_AVRC_MD_ATTR_ALBUM:

      if (value[0] != '\0') {
        strncpy(musicAlbum, value, sizeof(musicAlbum) - 1);
        musicAlbum[sizeof(musicAlbum) - 1] = '\0';
      }

      Serial.printf("Album: %s\n", musicAlbum);

      break;

    case ESP_AVRC_MD_ATTR_PLAYING_TIME:

      if (value[0] != '\0') {
        durationMs = strtoul(value, nullptr, 10);
      }

      Serial.printf("Duracao: %lu ms\n", durationMs);

      break;
  }
}

// =====================================
// ESTADO DA REPRODUCAO
// =====================================

void playbackCallback(esp_avrc_playback_stat_t status) {

  switch (status) {

    case ESP_AVRC_PLAYBACK_PLAYING:

      isPlaying = true;
      lastPositionTick = millis();
      Serial.println("PLAYING");

      break;

    case ESP_AVRC_PLAYBACK_PAUSED:

      isPlaying = false;
      lastPositionTick = 0;
      Serial.println("PAUSED");

      break;

    case ESP_AVRC_PLAYBACK_STOPPED:

      isPlaying = false;
      positionMs = 0;
      lastPositionTick = 0;

      Serial.println("STOPPED");

      break;

    default:
      break;
  }
}

// =====================================
// POSICAO DA MUSICA
// =====================================

void positionCallback(uint32_t position) {

  positionMs = position;
}

// =====================================
// CONEXAO BLUETOOTH
// =====================================

void connectionCallback(bool connected) {

  bluetoothConnected = connected;

  if (connected) {
    metadataReceived = false;
    serialMetadataActive = false;
    lastMetadataRequest = millis();
    lastPositionTick = 0;
  }

  Serial.println(
    connected
      ? "Bluetooth conectado"
      : "Bluetooth desconectado"
  );

  if (!connected) {

    isPlaying = false;
    positionMs = 0;

    durationMs = 0;

    strcpy(musicTitle, "Aguardando musica");
    strcpy(musicArtist, "Desconhecido");
    musicAlbum[0] = '\0';
  }
}

// =====================================
// EXIBIR OLED
// =====================================

void updateDisplay() {

  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);

  // CABECALHO

  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print("ESP32 MUSIC PLAYER");

  display.setCursor(111, 0);
  display.print(bluetoothConnected ? "BT" : "--");

  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  // TITULO

  display.setCursor(0, 14);
  display.print("Musica:");

  display.setCursor(0, 24);

  display.print(musicTitle);

  // ARTISTA

  display.setCursor(0, 36);

  display.print("Artista: ");

  display.print(musicArtist);

  // PROGRESSO

  display.setCursor(0, 48);

  char current[12];
  char total[12];

  formatTime(positionMs, current, sizeof(current));
  formatTime(durationMs, total, sizeof(total));

  display.print(current);
  display.print(" / ");
  display.print(total);

  // STATUS

  display.setCursor(92, 48);

  display.print(isPlaying ? "PLAY" : "PAUSE");

  // BARRA DE PROGRESSO

  display.drawRect(0, 58, 128, 5, SSD1306_WHITE);

  if (durationMs > 0) {

    uint32_t progressWidth =
      ((uint64_t)positionMs * 126) / durationMs;

    if (progressWidth > 126) progressWidth = 126;

    display.fillRect(
      1,
      59,
      progressWidth,
      3,
      SSD1306_WHITE
    );
  }

  display.display();
}

// =====================================
// SETUP
// =====================================

void setup() {

  Serial.begin(115200);

  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(
    SSD1306_SWITCHCAPVCC,
    OLED_ADDRESS
  )) {

    Serial.println("Erro OLED");

    while (true) {
      delay(1000);
    }
  }

  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(10, 25);
  display.println("Iniciando Bluetooth...");

  display.display();

  // Solicitar metadados AVRCP

  a2dp_sink.set_avrc_metadata_attribute_mask(
    ESP_AVRC_MD_ATTR_TITLE |
    ESP_AVRC_MD_ATTR_ARTIST |
    ESP_AVRC_MD_ATTR_ALBUM |
    ESP_AVRC_MD_ATTR_PLAYING_TIME
  );

  // Callback metadados

  a2dp_sink.set_avrc_metadata_callback(
    metadataCallback
  );

  // Callback reproducao

  a2dp_sink.set_avrc_rn_playstatus_callback(
    playbackCallback
  );

  // Callback posicao

  a2dp_sink.set_avrc_rn_play_pos_callback(
    positionCallback,
    1
  );

  // Callback conexao

  a2dp_sink.set_avrc_connection_state_callback(
    connectionCallback
  );

  // Iniciar Bluetooth

  a2dp_sink.start("ESP32 MUSIC PLAYER");

  // Este dispositivo so exibe metadados: desativa saida de audio
  // para evitar bloqueio da tarefa Bluetooth (watchdog).
  a2dp_sink.set_output_active(false);

  Serial.println();
  Serial.println("============================");
  Serial.println("ESP32 MUSIC PLAYER");
  Serial.println("Bluetooth aguardando conexao");
  Serial.println("============================");
}

// =====================================
// LOOP
// =====================================

void handleSerialCommand(const String &command) {

  if (!command.startsWith("META|")) return;

  String payload = command.substring(5);

  int firstSep = payload.indexOf('|');
  int secondSep = payload.indexOf('|', firstSep + 1);

  String title = firstSep >= 0
    ? payload.substring(0, firstSep)
    : payload;

  String artist = (firstSep >= 0 && secondSep >= 0)
    ? payload.substring(firstSep + 1, secondSep)
    : "Desconhecido";

  String duration = secondSep >= 0
    ? payload.substring(secondSep + 1)
    : "0";

  if (title.length() == 0) title = "Sem titulo";
  if (artist.length() == 0) artist = "Desconhecido";

  strncpy(musicTitle, title.c_str(), sizeof(musicTitle) - 1);
  musicTitle[sizeof(musicTitle) - 1] = '\0';

  strncpy(musicArtist, artist.c_str(), sizeof(musicArtist) - 1);
  musicArtist[sizeof(musicArtist) - 1] = '\0';

  durationMs = (uint32_t)duration.toInt();
  positionMs = 0;
  isPlaying = true;
  serialMetadataActive = true;
  lastPositionTick = millis();

  Serial.printf("Serial metadata: %s - %s\n", musicTitle, musicArtist);
}

void loop() {

  while (Serial.available()) {

    static String serialBuffer;

    char c = (char)Serial.read();

    if (c == '\n') {

      serialBuffer.trim();

      if (serialBuffer.length() > 0) {
        handleSerialCommand(serialBuffer);
      }

      serialBuffer = "";

    } else if (c != '\r') {

      serialBuffer += c;
    }
  }

  if (serialMetadataActive && isPlaying) {
    unsigned long now = millis();
    if (lastPositionTick == 0) {
      lastPositionTick = now;
    } else {
      unsigned long delta = now - lastPositionTick;
      lastPositionTick = now;
      if (durationMs > 0 && positionMs + delta >= durationMs) {
        positionMs = durationMs;
        isPlaying = false;
        serialMetadataActive = false;
      } else {
        positionMs += (uint32_t)delta;
      }
    }
  }

  if (!bluetoothConnected && !serialMetadataActive) {
    if (isPlaying || positionMs != 0) {
      isPlaying = false;
      positionMs = 0;
      lastPositionTick = 0;
    }
  }

  if (millis() - lastDisplayUpdate >= 500) {

    lastDisplayUpdate = millis();

    updateDisplay();
  }

  delay(10);
}
