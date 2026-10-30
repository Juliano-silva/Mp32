#include <BleKeyboard.h>

BleKeyboard bleKeyboard("ESP32 Play Pause", "Juliano", 100);

const int BOTAO = 2; // Pino do Botão 

int estadoAnterior = HIGH;
int ultimaLeitura = HIGH;

unsigned long tempoDebounce = 0;
const unsigned long debounce = 50;

void setup() {
  Serial.begin(115200);

  pinMode(BOTAO, INPUT_PULLUP);

  bleKeyboard.begin();

  Serial.println("ESP32 pronta!");
  Serial.println("Pareie com o Bluetooth do Windows.");
}

void loop() {
  int leitura = digitalRead(BOTAO);

  if (leitura != ultimaLeitura) {
    tempoDebounce = millis();
  }

  if ((millis() - tempoDebounce) > debounce) {
    if (leitura != estadoAnterior) {
      estadoAnterior = leitura;

      // Botao pressionado
      if (estadoAnterior == LOW) {
        if (bleKeyboard.isConnected()) {
          bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
          Serial.println("Play/Pause enviado!");
        } else {
          Serial.println("Bluetooth desconectado!");
        }
      }
    }
  }

  ultimaLeitura = leitura;
}
