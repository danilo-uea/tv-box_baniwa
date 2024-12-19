const int ledPin = 25;

void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT); /* Led */
}

int cont = 0;
float temperatura = 0;
float f_latitude = 0.650982;

char mensagem[200];

unsigned long tempoAntes = millis();

void loop() {
  if (Serial.available() > 0) {
    String comando = Serial.readStringUntil('\n');

    if (comando == "ligar") {
      digitalWrite(ledPin, HIGH);
    } else if (comando == "desligar") {
      digitalWrite(ledPin, LOW);
    }
  }
  if (millis() - tempoAntes > 1000)
  {
    memset(mensagem, 0, sizeof(mensagem));
    sprintf(mensagem, "%d;%.2f;%.6f", cont, temperatura, f_latitude);
    Serial.println(mensagem);
    
    cont++;
    temperatura = temperatura + 0.5;

    tempoAntes = millis();
  }
}
