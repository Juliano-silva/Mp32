
#include <BleKeyboard.h>

BleKeyboard bleKeyboard("ESP32 Play Pause", "Juliano", 100);

const int BOTAO_PROXIMO = 12;
const int BOTAO_ANTIGA = 14;

int estadoAnterior = HIGH;
int estadoAnterior2 = HIGH;

void setup() {
  Serial.begin(115200);

  pinMode(BOTAO_PROXIMO, INPUT_PULLUP);
  pinMode(BOTAO_ANTIGA, INPUT_PULLUP);

  bleKeyboard.begin();
}

void loop() {

  if (bleKeyboard.isConnected()) {

    int next = digitalRead(BOTAO_PROXIMO);
    int prev = digitalRead(BOTAO_ANTIGA);

    if (next == LOW && estadoAnterior == HIGH) {

      Serial.println("Proximo video/musica");

      bleKeyboard.write(KEY_MEDIA_NEXT_TRACK);
      bleKeyboard.press(KEY_LEFT_SHIFT);
      bleKeyboard.press('n');

      delay(100);

      // Solta as teclas
      bleKeyboard.releaseAll();

      delay(300);
    }
    if (prev == LOW && estadoAnterior2 == HIGH) {

      Serial.println("Antigo video/musica");

      bleKeyboard.write(KEY_MEDIA_PREVIOUS_TRACK);
      bleKeyboard.press(KEY_LEFT_SHIFT);
      bleKeyboard.press('p');

      delay(100);

      // Solta as teclas
      bleKeyboard.releaseAll();

      delay(300);
    }

    estadoAnterior = next;
    estadoAnterior2 = prev;
  }
}
