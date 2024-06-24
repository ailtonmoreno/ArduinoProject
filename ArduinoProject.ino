#include <SPI.h>
#include <MFRC522.h>
#include <Keypad.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>

// Configuração do teclado matricial
const byte ROWS = 4; // quatro linhas
const byte COLS = 4; // quatro colunas
char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

// Defina os pinos de linha e coluna
byte rowPins[ROWS] = {22, 24, 26, 28}; // R1, R2, R3, R4
byte colPins[COLS] = {30, 32, 34, 36}; // C1, C2, C3, C4

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Configuração do relé, LED verde, LED vermelho, buzzer e LED adicional
const int pinRelay = 13; // Pino do relé
const int pinBuzzerTone = 9; // Pino para controle do tom -> PRW ~
const int pinBuzzerControl = 8; // Pino de controle do buzzer (Base do transistor) -> PRW ~
const int pinGreenLED = 3; // Pino do LED verde -> PRW ~
const int pinRedLED = 2; // Pino do LED vermelho -> PRW ~
const int pinAdditionalLED = 4; // Pino para o LED adicional -> PRW ~
const int ledPin = 5; // Pino para o LED de inatividade -> PRW ~
const int SS_PIN = 40; // Pino SS do RFID
const int RST_PIN = 42; // Pino RST do RFID
const int relayActiveState = LOW; // Define o estado ativo do relé (LOW para acionar, HIGH para desligar)

// Configuração do DFPlayer Mini
SoftwareSerial mySoftwareSerial(10, 11); // RX, TX
DFRobotDFPlayerMini myDFPlayer;

// Configuração do RFID
MFRC522 rfid(SS_PIN, RST_PIN);

// Senhas armazenadas
String storedPasswords[] = {"1234", "5678", "9012"}; // Senhas permitidas
const String masterPassword = "4321"; // Senha mestra

// UID do cartão administrador
byte masterCard[4] = {0x14, 0x44, 0x10, 0x0B}; // Número do cartão administrador

// Variáveis de controle
String enteredPassword = ""; // Armazena a senha digitada
const int passwordLength = 4; // Comprimento da senha
int wrongAttempts = 0; // Contador de tentativas erradas
unsigned long lastAttemptTime = 0; // Tempo da última tentativa
const unsigned long resetDelay = 20000; // Delay para resetar as tentativas (20 segundos)
const unsigned long lockoutDuration = 300000; // Duração do bloqueio (5 minutos)
bool lockoutActive = false; // Flag de bloqueio
unsigned long lockoutStartTime = 0; // Tempo de início do bloqueio
unsigned long lastKeyPressTime = 0; // Tempo da última tecla pressionada
const unsigned long inactivityTimeout = 3000; // Tempo de inatividade (3 segundos)
unsigned long ledTurnOffTime = 0; // Tempo para desligar o LED adicional

// Lista de cartões cadastrados
byte registeredCards[10][4]; // Armazena até 10 cartões
int registeredCardCount = 0; // Contador de cartões cadastrados

void setup() {
  pinMode(ledPin, OUTPUT); // Configura o pino do LED como saída
  pinMode(pinRelay, OUTPUT); // Configura o pino do relé como saída
  pinMode(pinGreenLED, OUTPUT); // Configura o pino do LED verde como saída
  pinMode(pinRedLED, OUTPUT); // Configura o pino do LED vermelho como saída
  pinMode(pinBuzzerControl, OUTPUT); // Configura o pino de controle do buzzer como saída
  pinMode(pinBuzzerTone, OUTPUT); // Configura o pino de tom do buzzer como saída
  pinMode(pinAdditionalLED, OUTPUT); // Configura o pino do LED adicional como saída

  digitalWrite(pinRelay, !relayActiveState); // Garante que o relé esteja desligado inicialmente
  digitalWrite(pinGreenLED, LOW); // Garante que o LED verde esteja desligado inicialmente
  digitalWrite(pinRedLED, HIGH); // Garante que o LED vermelho esteja ligado inicialmente
  digitalWrite(pinBuzzerControl, LOW); // Garante que o controle do buzzer esteja desligado inicialmente
  digitalWrite(pinAdditionalLED, LOW); // Garante que o LED adicional esteja desligado inicialmente

  Serial.begin(9600); // Inicia a comunicação serial
  Serial.println("Sistema iniciado");

  // Inicialização do DFPlayer Mini
  mySoftwareSerial.begin(9600);
  if (!myDFPlayer.begin(mySoftwareSerial)) {
    Serial.println("Não foi possível iniciar o DFPlayer Mini.");
    while (true);
  }
  Serial.println("DFPlayer Mini iniciado com sucesso.");
  myDFPlayer.setTimeOut(500);
  myDFPlayer.volume(26); // Volume de 0 a 30
  myDFPlayer.EQ(DFPLAYER_EQ_NORMAL);
  myDFPlayer.outputDevice(DFPLAYER_DEVICE_SD);

  // Inicialização do RFID
  SPI.begin(); // Inicia o barramento SPI
  rfid.PCD_Init(); // Inicia o módulo RFID
  Serial.println("RFID iniciado com sucesso.");
}

void loop() {
  // Verifica se um cartão RFID foi detectado
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    byte readCard[4];
    for (byte i = 0; i < 4; i++) {
      readCard[i] = rfid.uid.uidByte[i];
    }
    rfid.PICC_HaltA(); // Para a comunicação com o cartão

    if (isMasterCard(readCard)) {
      handleMasterCard();
    } else if (isRegisteredCard(readCard)) {
      handleRegisteredCard();
    } else {
      handleUnknownCard();
    }
  }

  char key = keypad.getKey(); // Lê a tecla pressionada

  if (key) { // Se uma tecla foi pressionada
    Serial.print("Tecla pressionada: ");
    Serial.println(key);

    // Acende o LED adicional por 10 segundos
    digitalWrite(pinAdditionalLED, HIGH);
    ledTurnOffTime = millis() + 15000;

    // Feedback visual e sonoro ao pressionar uma tecla
    digitalWrite(pinGreenLED, HIGH); // Acende o LED verde
    digitalWrite(pinBuzzerControl, HIGH); // Liga o transistor, alimentando o buzzer
    tone(pinBuzzerTone, 1000); // Emite um som curto de 1 kHz
    delay(100); // Espera 100 ms
    noTone(pinBuzzerTone); // Para o som
    digitalWrite(pinBuzzerControl, LOW); // Desliga o transistor, cortando a alimentação do buzzer
    digitalWrite(pinGreenLED, LOW); // Apaga o LED verde

    if (enteredPassword.length() < passwordLength) { // Verifica se a senha ainda não está completa
      enteredPassword += key; // Adiciona a tecla pressionada à senha
      Serial.print("Senha digitada: ");
      Serial.println(enteredPassword);
      lastKeyPressTime = millis(); // Atualiza o tempo da última tecla pressionada

      if (enteredPassword.length() == passwordLength) { // Se a senha está completa
        if (lockoutActive) { // Se o bloqueio está ativo
          if (enteredPassword == masterPassword) { // Se a senha digitada é a senha mestra
            Serial.println("Senha mestra correta! Sistema desbloqueado.");
            playBuzzerCorrectMaster(); // Toca os bips de senha mestra correta
            lockoutActive = false; // Desativa o bloqueio
            wrongAttempts = 0; // Reseta o contador de tentativas erradas
            enteredPassword = ""; // Reseta a senha digitada
            return;
          } else {
            Serial.println("Sistema bloqueado. Aguarde o desbloqueio ou use a senha mestra.");
            playBuzzerIncorrectMaster(); // Toca o bip longo de senha mestra incorreta
            enteredPassword = ""; // Reseta a senha digitada
            return;
          }
        }
        checkPassword(); // Verifica a senha
        lastAttemptTime = millis(); // Atualiza o tempo da última tentativa
        delay(500); // Pequeno delay para dar tempo de ler a última tecla
        enteredPassword = ""; // Reseta a senha digitada após a verificação
        Serial.println("Senha limpa automaticamente"); // Informa que a senha foi limpa
        digitalWrite(ledPin, HIGH); // Acende o LED de inatividade
        delay(100); // Mantém o LED aceso por 100 ms
        digitalWrite(ledPin, LOW); // Apaga o LED de inatividade
      }
    }
  }

  // Desliga o LED adicional após 10 segundos
  if (millis() > ledTurnOffTime) {
    digitalWrite(pinAdditionalLED, LOW);
  }

  // Reseta as tentativas erradas se 20 segundos se passaram desde a última tentativa
  if (millis() - lastAttemptTime > resetDelay) {
    wrongAttempts = 0;
  }

  // Reseta a senha digitada se 7 segundos se passaram desde a última tecla pressionada
  if (enteredPassword.length() > 0 && millis() - lastKeyPressTime > inactivityTimeout) {
    enteredPassword = "";
    Serial.println("Senha resetada por inatividade.");

    digitalWrite(ledPin, HIGH); // Acende o LED de inatividade
    delay(100); // Mantém o LED aceso por 100 ms
    digitalWrite(ledPin, LOW); // Apaga o LED de inatividade
  }

  // Verifica se o bloqueio deve ser desativado
  if (lockoutActive && millis() - lockoutStartTime > lockoutDuration) {
    lockoutActive = false;
    Serial.println("Bloqueio de tentativas terminado. Você pode tentar novamente.");
  }
}

void checkPassword() {
  bool match = false; // Variável para verificar se a senha está correta
  for (int i = 0; i < sizeof(storedPasswords) / sizeof(storedPasswords[0]); i++) { // Loop para comparar a senha digitada com as armazenadas
    if (enteredPassword == storedPasswords[i]) { // Se a senha digitada corresponde a uma das armazenadas
      match = true; // A senha está correta
      break; // Sai do loop
    }
  }

  if (match) { // Se a senha está correta
    Serial.println("Senha correta!");
    digitalWrite(pinRedLED, LOW); // Apaga o LED vermelho
    digitalWrite(pinBuzzerControl, HIGH); // Liga o transistor, alimentando o buzzer
    tone(pinBuzzerTone, 2000); // Emite um tom de 2 kHz
    delay(200); // Espera 200 ms
    tone(pinBuzzerTone, 2000); // Emite um tom de 2 kHz
    delay(600); // Espera de 500 ms
    noTone(pinBuzzerTone); // Para o som
    digitalWrite(pinBuzzerControl, LOW); // Desliga o transistor, cortando a alimentação do buzzer
    myDFPlayer.play(1); // Toca o áudio 001.mp3

    unsigned long startTime = millis();
    while (millis() - startTime < 47000) { // Pisca o LED verde durante 47 segundos
      digitalWrite(pinGreenLED, HIGH); // Liga o LED verde
      delay(500); // Espera 500 ms
      digitalWrite(pinGreenLED, LOW); // Desliga o LED verde
      delay(500); // Espera 500 ms
    }

    digitalWrite(pinRelay, relayActiveState); // Aciona o relé
    digitalWrite(pinGreenLED, HIGH); // Liga o LED verde
    delay(3000); // Mantém o LED e o relé acionados por 3000 ms (3 segundos)
    digitalWrite(pinRelay, !relayActiveState); // Desliga o relé
    digitalWrite(pinGreenLED, LOW); // Garante que o LED verde esteja desligado
    digitalWrite(pinRedLED, HIGH); // Religa o LED vermelho
    wrongAttempts = 0; // Reseta o contador de tentativas erradas
  } else { // Se a senha está incorreta
    Serial.println("Senha incorreta!");
    for (int i = 0; i < 3; i++) { // Três bipes para indicar senha incorreta
      digitalWrite(pinBuzzerControl, HIGH); // Liga o transistor, alimentando o buzzer
      tone(pinBuzzerTone, 500); // Emite um tom de 500 Hz
      digitalWrite(pinRedLED, HIGH); // Acende o LED Vermelho enquanto o buzzer toca
      delay(200); // Espera 200 ms
      noTone(pinBuzzerTone); // Para o som
      digitalWrite(pinBuzzerControl, LOW); // Desliga o transistor, cortando a alimentação do buzzer
      digitalWrite(pinRedLED, LOW); // Apaga o LED Vermelho
      delay(200); // Espera 200 ms
    }

    wrongAttempts++; // Incrementa o contador de tentativas erradas
    myDFPlayer.play(2); // Toca o áudio 002.mp3
    unsigned long startTime = millis();
    while (millis() - startTime < 13000) { // Pisca o LED Vermelho durante 13 segundos
      digitalWrite(pinRedLED, HIGH); // Liga o LED VERMELHO
      delay(500); // Espera 500 ms
      digitalWrite(pinRedLED, LOW); // Desliga o LED VERMELHO
      delay(500); // Espera 500 ms
      digitalWrite(pinRedLED, HIGH); // Liga o LED VERMELHO
    }

    if (wrongAttempts >= 3) { // Se houver três tentativas erradas consecutivas
      myDFPlayer.play(3); // Toca o áudio 003.mp3
      lockoutActive = true; // Ativa o bloqueio
      lockoutStartTime = millis(); // Registra o tempo de início do bloqueio
      Serial.println("Sistema bloqueado por tentativas excessivas. Use a senha mestra para desbloquear.");
      delay(15000);
      unsigned long startTime = millis();
      while (millis() - startTime < 5000) { // LEDs piscam durante os últimos 5 segundos do áudio 003.mp3
        digitalWrite(pinGreenLED, HIGH); // Acende o LED Vermelho enquanto o buzzer toca
        delay(200); // Espera 200 ms
        digitalWrite(pinGreenLED, LOW); // Apaga o LED Vermelho
        digitalWrite(pinRedLED, HIGH); // Acende o LED Vermelho enquanto o buzzer toca
        delay(200); // Espera 200 ms
        digitalWrite(pinRedLED, LOW);

        char key = keypad.getKey(); // Verifica se uma tecla foi pressionada
        if (key) { // Se uma tecla foi pressionada
          if (enteredPassword.length() < passwordLength) { // Verifica se a senha ainda não está completa
            enteredPassword += key; // Adiciona a tecla pressionada à senha
            Serial.print("Senha digitada: ");
            Serial.println(enteredPassword);
            lastKeyPressTime = millis(); // Atualiza o tempo da última tecla pressionada

            if (enteredPassword.length() == passwordLength) { // Se a senha está completa
              if (enteredPassword == masterPassword) { // Se a senha digitada é a senha mestra
                Serial.println("Senha mestra correta! Sistema desbloqueado.");
                playBuzzerCorrectMaster(); // Toca os bips de senha mestra correta
                lockoutActive = false; // Desativa o bloqueio
                wrongAttempts = 0; // Reseta o contador de tentativas erradas
                enteredPassword = ""; // Reseta a senha digitada
                //break; // Sai do loop de piscagem dos LED
              } else {
                Serial.println("Senha incorreta!");
                playBuzzerIncorrectMaster(); // Toca o bip longo de senha mestra incorreta
                enteredPassword = ""; // Reseta a senha digitada
              }
            }
          }
        }
      }

      digitalWrite(pinGreenLED, LOW); // Desliga o Led Verde
      digitalWrite(pinRedLED, HIGH); // Garante que o LED vermelho esteja ligado
    }
  }
}

void handleMasterCard() {
  Serial.println("Cartão administrador reconhecido.");
  digitalWrite(pinGreenLED, HIGH); // Acende o LED verde
  playBuzzerCorrectMaster(); // Emite som indicando sucesso
  delay(100);

  // Aguarda novo cartão para cadastro
  while (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {}

  byte newCard[4];
  for (byte i = 0; i < 4; i++) {
    newCard[i] = rfid.uid.uidByte[i];
  }
  rfid.PICC_HaltA(); // Para a comunicação com o cartão

  Serial.println("Novo cartão lido para cadastro.");
  digitalWrite(pinGreenLED, HIGH); // Acende o LED verde
  tone(pinBuzzerTone, 1000); // Emite som curto
  delay(100);
  noTone(pinBuzzerTone);
  digitalWrite(pinGreenLED, LOW); // Apaga o LED verde

  // Aguarda confirmação do cartão administrador para finalizar o cadastro
  while (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {}

  if (isMasterCard(rfid.uid.uidByte)) {
    registerCard(newCard); // Cadastra o novo cartão
    Serial.println("Cartão cadastrado com sucesso.");
    for (int i = 0; i < 3; i++) {
      digitalWrite(pinGreenLED, HIGH);
      playBuzzerCorrectMaster();
      delay(100);
      digitalWrite(pinGreenLED, LOW);
      delay(100);
    }
  } else {
    Serial.println("Falha ao finalizar cadastro. Cartão administrador não foi utilizado.");
  }
}

void handleRegisteredCard() {
  Serial.println("Cartão cadastrado reconhecido.");
  digitalWrite(pinRedLED, LOW); // Apaga o LED vermelho
  digitalWrite(pinBuzzerControl, HIGH); // Liga o transistor, alimentando o buzzer
  tone(pinBuzzerTone, 2000); // Emite um tom de 2 kHz
  delay(200); // Espera 200 ms
  tone(pinBuzzerTone, 2000); // Emite um tom de 2 kHz
  delay(600); // Espera de 500 ms
  noTone(pinBuzzerTone); // Para o som
  digitalWrite(pinBuzzerControl, LOW); // Desliga o transistor, cortando a alimentação do buzzer
  myDFPlayer.play(4); // Toca o áudio 004.mp3
  delay(46000); // Toca o áudio por 46 segundos
  digitalWrite(pinRelay, relayActiveState); // Aciona o relé
  digitalWrite(pinGreenLED, HIGH); // Liga o LED verde
  delay(3000); // Mantém o LED e o relé acionados por 3000 ms (3 segundos)
  digitalWrite(pinRelay, !relayActiveState); // Desliga o relé
  digitalWrite(pinGreenLED, LOW); // Garante que o LED verde esteja desligado
  digitalWrite(pinRedLED, HIGH); // Religa o LED vermelho
}

void handleUnknownCard() {
  Serial.println("Cartão não cadastrado.");
  digitalWrite(pinBuzzerControl, HIGH); // Liga o transistor, alimentando o buzzer
  tone(pinBuzzerTone, 500); // Emite um tom de 500 Hz
  digitalWrite(pinRedLED, LOW); // Apaga o LED vermelho enquanto o buzzer toca
  delay(200); // Espera 200 ms
  noTone(pinBuzzerTone); // Para o som
  digitalWrite(pinBuzzerControl, LOW); // Desliga o transistor, cortando a alimentação do buzzer
  digitalWrite(pinRedLED, HIGH); // Acende o LED vermelho
  myDFPlayer.play(5); // Toca o áudio 005.mp3
  delay(10000); // Toca o áudio por 10 segundos
}

void registerCard(byte *newCard) {
  if (registeredCardCount < 10) {
    for (byte i = 0; i < 4; i++) {
      registeredCards[registeredCardCount][i] = newCard[i];
    }
    registeredCardCount++;
  } else {
    Serial.println("Limite de cartões cadastrados atingido.");
  }
}

bool isMasterCard(byte *card) {
  for (byte i = 0; i < 4; i++) {
    if (card[i] != masterCard[i]) {
      return false;
    }
  }
  return true;
}

bool isRegisteredCard(byte *card) {
  for (int i = 0; i < registeredCardCount; i++) {
    bool match = true;
    for (byte j = 0; j < 4; j++) {
      if (registeredCards[i][j] != card[j]) {
        match = false;
        break;
      }
    }
    if (match) {
      return true;
    }
  }
  return false;
}

void playBuzzerCorrectMaster() {
  for (int i = 0; i < 2; i++) { // Dois bipes para senha mestra correta
    digitalWrite(pinBuzzerControl, HIGH); // Liga o transistor, alimentando o buzzer
    tone(pinBuzzerTone, 2000); // Emite um tom de 1 kHz
    delay(600); // Espera de 500 ms
    noTone(pinBuzzerTone); // Para o som
    digitalWrite(pinBuzzerControl, LOW); // Desliga o transistor, cortando a alimentação do buzzer
    delay(100); // Espera 100 ms entre os bipes
  }
}

void playBuzzerIncorrectMaster() {
  digitalWrite(pinBuzzerControl, HIGH); // Liga o transistor, alimentando o buzzer
  tone(pinBuzzerTone, 500); // Emite um tom de 500 Hz
  delay(1000); // Espera 1000 ms
  noTone(pinBuzzerTone); // Para o som
  digitalWrite(pinBuzzerControl, LOW); // Desliga o transistor, cortando a alimentação do buzzer
}
