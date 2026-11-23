
#include <Arduino.h>
#include <Wire.h>
#include <BluetoothA2DPSink.h>
#include <esp_avrc_api.h>
#include <ESP_I2S.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// =================================================
// PINOS
// =================================================

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C

#define OLED_SDA 33
#define OLED_SCL 32

// I2S - AMPLIFICADOR
#define I2S_BCLK 26
#define I2S_LRC  25
#define I2S_DOUT 27

// Controlador dos botões
#define BUTTON_PIN      12
#define NEXT_BUTTON_PIN 14
#define PREV_BUTTON_PIN 13

#define VOLUME_POT_PIN 35

// =================================================
// OBJETOS
// =================================================

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);

I2SClass i2s;

BluetoothA2DPSink a2dp_sink(i2s);

// =================================================
// INFORMACOES DA MUSICA
// =================================================

char musicTitle[100] = "Aguardando musica";
char musicArtist[70] = "Desconhecido";
char musicAlbum[70] = "";

uint32_t durationMs = 0;
uint32_t positionMs = 0;

bool bluetoothConnected = false;
bool isPlaying = false;
bool metadataReceived = false;

// =================================================
// CONTROLES
// =================================================

struct Button {
  uint8_t pin;
  bool lastReading;
  bool stableState;
  unsigned long lastChange;
};

Button playButton = {BUTTON_PIN, HIGH, HIGH, 0};
Button nextButton = {NEXT_BUTTON_PIN, HIGH, HIGH, 0};
Button prevButton = {PREV_BUTTON_PIN, HIGH, HIGH, 0};

int lastVolume = -1;
unsigned long lastVolumeRead = 0;
unsigned long lastDisplayUpdate = 0;

// =================================================
// BOTAO COM DEBOUNCE
// =================================================

bool buttonPressed(Button &button) {

  bool reading = digitalRead(button.pin);

  if (reading != button.lastReading) {
    button.lastChange = millis();
    button.lastReading = reading;
  }

  if (millis() - button.lastChange >= 50) {

    if (reading != button.stableState) {

      button.stableState = reading;

      if (button.stableState == LOW) {
        return true;
      }
    }
  }

  return false;
}

// =================================================
// FORMATAR TEMPO
// =================================================

void formatTime(
  uint32_t milliseconds,
  char *buffer,
  size_t size
) {

  uint32_t seconds = milliseconds / 1000;

  snprintf(
    buffer,
    size,
    "%02lu:%02lu",
    (unsigned long)(seconds / 60),
    (unsigned long)(seconds % 60)
  );
}

// =================================================
// METADADOS BLUETOOTH AVRCP
// =================================================

void metadataCallback(uint8_t id, const uint8_t *text) {

  if (text == nullptr) return;

  const char *value = (const char *)text;

  metadataReceived = true;

  Serial.printf(
    "Metadata recebida: %s\n",
    value
  );

  switch (id) {

    case ESP_AVRC_MD_ATTR_TITLE:

      if (value[0] != '\0') {
        strncpy(
          musicTitle,
          value,
          sizeof(musicTitle) - 1
        );

        musicTitle[sizeof(musicTitle) - 1] = '\0';
      }

      break;

    case ESP_AVRC_MD_ATTR_ARTIST:

      if (value[0] != '\0') {
        strncpy(
          musicArtist,
          value,
          sizeof(musicArtist) - 1
        );

        musicArtist[sizeof(musicArtist) - 1] = '\0';
      }

      break;

    case ESP_AVRC_MD_ATTR_ALBUM:

      if (value[0] != '\0') {
        strncpy(
          musicAlbum,
          value,
          sizeof(musicAlbum) - 1
        );

        musicAlbum[sizeof(musicAlbum) - 1] = '\0';
      }

      break;

    case ESP_AVRC_MD_ATTR_PLAYING_TIME:

      durationMs = strtoul(value, nullptr, 10);

      break;
  }
}

// =================================================
// ESTADO DA REPRODUCAO
// =================================================

void playbackCallback(esp_avrc_playback_stat_t status) {

  switch (status) {

    case ESP_AVRC_PLAYBACK_PLAYING:

      isPlaying = true;
      Serial.println("Musica reproduzindo");

      break;

    case ESP_AVRC_PLAYBACK_PAUSED:

      isPlaying = false;
      Serial.println("Musica pausada");

      break;

    case ESP_AVRC_PLAYBACK_STOPPED:

      isPlaying = false;
      positionMs = 0;

      Serial.println("Musica parada");

      break;

    default:
      break;
  }
}

// =================================================
// POSICAO DA MUSICA
// =================================================

void positionCallback(uint32_t position) {

  positionMs = position;
}

// =================================================
// CONEXAO BLUETOOTH
// =================================================

void connectionCallback(bool connected) {

  bluetoothConnected = connected;

  Serial.println(
    connected
      ? "Bluetooth conectado!"
      : "Bluetooth desconectado!"
  );

  if (connected) {

    metadataReceived = false;
    positionMs = 0;
    durationMs = 0;

    strcpy(musicTitle, "Aguardando musica");
    strcpy(musicArtist, "Desconhecido");

  } else {

    isPlaying = false;
    positionMs = 0;
    durationMs = 0;

    strcpy(musicTitle, "Aguardando musica");
    strcpy(musicArtist, "Desconhecido");

    musicAlbum[0] = '\0';
  }
}

// =================================================
// OLED
// =================================================

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
  char displayTitle[22];
  if (strlen(musicTitle) > 18) {
    strncpy(displayTitle, musicTitle, 15);
    displayTitle[15] = '\0';
    strcat(displayTitle, "...");
  } else {
    strcpy(displayTitle, musicTitle);
  }
  display.print(displayTitle);

  // ARTISTA

  display.setCursor(0, 36);
  display.print("Artista:");

  display.setCursor(45, 36);
  char displayArtist[14];
  if (strlen(musicArtist) > 12) {
    strncpy(displayArtist, musicArtist, 9);
    displayArtist[9] = '\0';
    strcat(displayArtist, "...");
  } else {
    strcpy(displayArtist, musicArtist);
  }
  display.print(displayArtist);

  // TEMPO

  char current[12];
  char total[12];

  formatTime(positionMs, current, sizeof(current));
  formatTime(durationMs, total, sizeof(total));

  display.setCursor(0, 48);
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

    if (progressWidth > 126) {
      progressWidth = 126;
    }

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

// =================================================
// PLAY / PAUSE
// =================================================

void togglePlayback() {

  if (!bluetoothConnected) return;

  if (isPlaying) {

    a2dp_sink.pause();

    isPlaying = false;

    Serial.println("PAUSE solicitado");

  } else {

    a2dp_sink.play();

    isPlaying = true;

    Serial.println("PLAY solicitado");
  }
}

// =================================================
// PROXIMA MUSICA
// =================================================

void nextTrack() {

  if (!bluetoothConnected) return;

  a2dp_sink.next();

  Serial.println("Proxima musica");
}

// =================================================
// MUSICA ANTERIOR
// =================================================

void previousTrack() {

  if (!bluetoothConnected) return;

  a2dp_sink.previous();

  Serial.println("Musica anterior");
}

// =================================================
// CONTROLE DE VOLUME
// =================================================

void updateVolume() {

  if (millis() - lastVolumeRead < 200) return;

  lastVolumeRead = millis();

  int raw = analogRead(VOLUME_POT_PIN);

  int volume = map(raw, 0, 4095, 0, 127);

  volume = constrain(volume, 0, 127);

  if (lastVolume < 0 || abs(volume - lastVolume) >= 2) {

    lastVolume = volume;

    a2dp_sink.set_volume((uint8_t)volume);

    Serial.printf("Volume: %d/127\n", volume);
  }
}

// =================================================
// SETUP
// =================================================

void setup() {

  Serial.begin(115200);

  delay(500);

  Serial.println();
  Serial.println("================================");
  Serial.println("ESP32 MUSIC PLAYER");
  Serial.println("INICIALIZANDO...");
  Serial.println("================================");

  // -----------------------------------------------
  // BOTOES
  // -----------------------------------------------

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(NEXT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(PREV_BUTTON_PIN, INPUT_PULLUP);

  // -----------------------------------------------
  // POTENCIOMETRO
  // -----------------------------------------------

  analogReadResolution(12);

  // -----------------------------------------------
  // OLED
  // -----------------------------------------------

  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(
    SSD1306_SWITCHCAPVCC,
    OLED_ADDRESS
  )) {

    Serial.println("ERRO: OLED nao encontrado!");

    while (true) {
      delay(1000);
    }
  }

  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(10, 25);
  display.println("Iniciando sistema...");

  display.display();

  // -----------------------------------------------
  // I2S
  // -----------------------------------------------

  Serial.println("Configurando I2S...");

  i2s.setPins(
    I2S_BCLK,
    I2S_LRC,
    I2S_DOUT
  );

  bool i2sStarted = i2s.begin(
    I2S_MODE_STD,
    44100,
    I2S_DATA_BIT_WIDTH_16BIT,
    I2S_SLOT_MODE_STEREO,
    I2S_STD_SLOT_BOTH
  );

  if (!i2sStarted) {

    Serial.println("ERRO AO INICIAR I2S!");

    display.clearDisplay();
    display.setCursor(0, 25);
    display.println("ERRO I2S!");
    display.display();

    while (true) {
      delay(1000);
    }
  }

  Serial.println("I2S iniciado com sucesso!");

  // -----------------------------------------------
  // CALLBACKS AVRCP
  // -----------------------------------------------

  a2dp_sink.set_avrc_metadata_attribute_mask(
    ESP_AVRC_MD_ATTR_TITLE |
    ESP_AVRC_MD_ATTR_ARTIST |
    ESP_AVRC_MD_ATTR_ALBUM |
    ESP_AVRC_MD_ATTR_PLAYING_TIME
  );

  a2dp_sink.set_avrc_metadata_callback(
    metadataCallback
  );

  a2dp_sink.set_avrc_rn_playstatus_callback(
    playbackCallback
  );

  a2dp_sink.set_avrc_rn_play_pos_callback(
    positionCallback,
    1
  );

  a2dp_sink.set_avrc_connection_state_callback(
    connectionCallback
  );

  // -----------------------------------------------
  // BLUETOOTH + AUDIO
  // -----------------------------------------------

  Serial.println("Iniciando Bluetooth A2DP...");

  // O I2SClass sera utilizado como saida de audio.
  // Nao desativar set_output_active!

  a2dp_sink.start("ESP32 MUSIC PLAYER");

  a2dp_sink.set_volume(80);

  Serial.println("Bluetooth aguardando conexao!");

  display.clearDisplay();
  display.setCursor(0, 20);
  display.println("TESTE DISPLAY");
  display.println("ESP32 MUSIC PLAYER");
  display.display();
  delay(2000);
}

// =================================================
// COMANDO SERIAL (META|titulo|artista|duracao_ms)
// =================================================

void handleSerialCommand(const String &command) {

  if (!command.startsWith("META|")) return;

  String payload = command.substring(5);

  int firstSep = payload.indexOf('|');
  int secondSep = payload.indexOf('|', firstSep + 1);

  String title = firstSep >= 0 ? payload.substring(0, firstSep) : payload;
  String artist = (firstSep >= 0 && secondSep >= 0) ? payload.substring(firstSep + 1, secondSep) : "Desconhecido";
  String duration = secondSep >= 0 ? payload.substring(secondSep + 1) : "0";

  if (title.length() == 0) title = "Sem titulo";
  if (artist.length() == 0) artist = "Desconhecido";

  strncpy(musicTitle, title.c_str(), sizeof(musicTitle) - 1);
  musicTitle[sizeof(musicTitle) - 1] = '\0';

  strncpy(musicArtist, artist.c_str(), sizeof(musicArtist) - 1);
  musicArtist[sizeof(musicArtist) - 1] = '\0';

  durationMs = (uint32_t)duration.toInt();
  positionMs = 0;
  isPlaying = true;

  Serial.printf("Serial metadata: %s - %s\n", musicTitle, musicArtist);
}

// =================================================
// LOOP
// =================================================

void loop() {

  // COMANDO SERIAL
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

  // PLAY / PAUSE

  if (buttonPressed(playButton)) {

    togglePlayback();
  }

  // PROXIMA FAIXA

  if (buttonPressed(nextButton)) {

    nextTrack();
  }

  // FAIXA ANTERIOR

  if (buttonPressed(prevButton)) {

    previousTrack();
  }

  // VOLUME

  updateVolume();

  // OLED

  if (millis() - lastDisplayUpdate >= 500) {

    lastDisplayUpdate = millis();

    updateDisplay();
  }

  delay(5);
}
