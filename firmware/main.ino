
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

#define OLED_SDA 21
#define OLED_SCL 22

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);

// =====================================
// BLUETOOTH
// =====================================

BluetoothA2DPSink a2dp_sink;

// =====================================
// INFORMACOES DA MUSICA
// =====================================

char musicTitle[100] = "Aguardando musica";
char musicArtist[70] = "Desconhecido";
char musicAlbum[70] = "";

uint32_t durationMs = 0;
uint32_t positionMs = 0;

bool bluetoothConnected = false;
bool isPlaying = false;

unsigned long lastDisplayUpdate = 0;

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

  const char *value = (const char *)text;

  switch (id) {

    case ESP_AVRC_MD_ATTR_TITLE:

      strncpy(musicTitle, value, sizeof(musicTitle) - 1);
      musicTitle[sizeof(musicTitle) - 1] = '\0';

      Serial.printf("Titulo: %s\n", musicTitle);

      break;

    case ESP_AVRC_MD_ATTR_ARTIST:

      strncpy(musicArtist, value, sizeof(musicArtist) - 1);
      musicArtist[sizeof(musicArtist) - 1] = '\0';

      Serial.printf("Artista: %s\n", musicArtist);

      break;

    case ESP_AVRC_MD_ATTR_ALBUM:

      strncpy(musicAlbum, value, sizeof(musicAlbum) - 1);
      musicAlbum[sizeof(musicAlbum) - 1] = '\0';

      Serial.printf("Album: %s\n", musicAlbum);

      break;

    case ESP_AVRC_MD_ATTR_PLAYING_TIME:

      durationMs = strtoul(value, nullptr, 10);

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
      Serial.println("PLAYING");

      break;

    case ESP_AVRC_PLAYBACK_PAUSED:

      isPlaying = false;
      Serial.println("PAUSED");

      break;

    case ESP_AVRC_PLAYBACK_STOPPED:

      isPlaying = false;
      positionMs = 0;

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

  Serial.println();
  Serial.println("============================");
  Serial.println("ESP32 MUSIC PLAYER");
  Serial.println("Bluetooth aguardando conexao");
  Serial.println("============================");
}

// =====================================
// LOOP
// =====================================

void loop() {

  if (millis() - lastDisplayUpdate >= 500) {

    lastDisplayUpdate = millis();

    updateDisplay();
  }

  delay(10);
}
