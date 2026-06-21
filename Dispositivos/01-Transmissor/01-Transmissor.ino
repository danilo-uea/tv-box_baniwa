/*
  01-Transmissor.ino
  Dispositivo: ESP32 LoRa v2 + TV-Box via Serial

  Função na rede:
  - Gera dados fictícios de sensores: temperatura, umidade, latitude e longitude.
  - Tenta transmitir os dados via LoRa.
  - Aguarda ACK do próximo nó: intermediário ou receptor.
  - Se receber ACK positivo, descarta definitivamente o pacote.
  - Se não receber ACK no tempo definido, envia o pacote para a TV-Box armazenar.
  - Também retransmite pacotes pendentes enviados pela TV-Box via Serial.

  Bibliotecas mantidas conforme o projeto original:
  Wire, Adafruit_GFX, Adafruit_SSD1306, LoRa, SPI e Preferences.
*/

/* Bibliotecas para o Display OLED */
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

/* Bibliotecas para comunicação LoRa */
#include <LoRa.h>
#include <SPI.h>

/* Biblioteca para salvar a sequência no NVS do ESP32 */
#include <Preferences.h>

/* Pinagem para o Display OLED */
#define OLED_SDA 4
#define OLED_SCL 15
#define OLED_RST 16
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

/* Pinagem para comunicação com rádio LoRa */
#define SCK_LORA           5
#define MISO_LORA          19
#define MOSI_LORA          27
#define RESET_PIN_LORA     14
#define SS_PIN_LORA        18
#define HIGH_GAIN_LORA     20
#define BAND               915E6

/* Configuração deste nó */
#define ID_DISPOSITIVO     1
#define TIPO_DISPOSITIVO   TIPO_TRANSMISSOR

/* Tipos de dispositivo */
#define TIPO_TRANSMISSOR   1
#define TIPO_INTERMEDIARIO 2
#define TIPO_RECEPTOR      3

/* Tipos de mensagem LoRa */
#define MSG_DADO           1
#define MSG_ACK            2
#define MSG_NACK           3
#define MSG_RESPOSTA_TVBOX 4

/* Comandos usados somente entre ESP32 e TV-Box via Serial */
#define CMD_NENHUM             0
#define CMD_ARMAZENAR_PENDENTE 1
#define CMD_REMOVER_PENDENTE   2
#define CMD_REGISTRAR_RECEBIDO 3
#define CMD_RESPOSTA_TVBOX     4

/* Status usado nas respostas da TV-Box e ACK/NACK */
#define STATUS_ERRO       0
#define STATUS_OK         1
#define STATUS_DUPLICADO  2
#define STATUS_VAZIO      3

/* Configurações do protocolo */
#define VERSAO_PROTOCOLO  1
#define MAX_SALTOS        200
#define TAM_PACOTE        sizeof(TPacoteRede)

/* Tempos principais */
const unsigned long INTERVALO_GERACAO_MS = 8000; /* Gera mock a cada 8 segundos: mais lento que o reenvio da fila */
const unsigned long TIMEOUT_ACK_MS       = 1800;  /* Tempo para considerar falha na transmissão LoRa */
const unsigned long TIMEOUT_TVBOX_MS     = 700;   /* Tempo máximo aguardando resposta serial da TV-Box */

/*
  Pacote único do protocolo.
  A estrutura é empacotada para que Arduino e Python usem exatamente os mesmos bytes.

  Formato equivalente no Python:
  '<BBBBHHHIBBffffI'

  Campos principais:
  - origem_id: identificador do transmissor que gerou o dado.
  - remetente_id: identificador do dispositivo que transmitiu no último salto.
  - destino_id: 0 para broadcast; no ACK, aponta para quem deve receber a confirmação.
  - sequencia: número único gerado pelo transmissor.
  - temperatura/umidade/latitude/longitude: dados fictícios dos sensores.
*/
typedef struct __attribute__((__packed__))
{
  uint8_t  versao;
  uint8_t  tipo_dispositivo;
  uint8_t  tipo_mensagem;
  uint8_t  comando_serial;
  uint16_t origem_id;
  uint16_t remetente_id;
  uint16_t destino_id;
  uint32_t sequencia;
  uint8_t  saltos;
  uint8_t  status;
  float    temperatura;
  float    umidade;
  float    latitude;
  float    longitude;
  uint32_t timestamp;
} TPacoteRede;

/* Objetos globais */
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);
Preferences preferences;

uint32_t fatorE = 7;
uint32_t proximaSequencia = 1;

TPacoteRede pacoteEmEspera;
bool aguardandoAck = false;
bool pacoteVeioDaTvBox = false;

/*
  Indica se há pacotes pendentes na TV-Box.
  Enquanto essa flag estiver ativa, o transmissor NÃO envia dados novos diretamente via LoRa.
  Os dados novos são apenas salvos na TV-Box para preservar a ordem da fila.
*/
bool filaTvBoxAtiva = false;

unsigned long momentoEnvio = 0;
unsigned long momentoUltimaGeracao = 0;

/* -------------------- Funções utilitárias -------------------- */

bool mesmaChavePacote(TPacoteRede a, TPacoteRede b)
{
  return (a.origem_id == b.origem_id && a.sequencia == b.sequencia);
}

/*
  A TV-Box envia um pacote de controle com STATUS_VAZIO quando a fila está vazia.
  Esse aviso é usado para liberar a transmissão direta de dados novos.
*/
bool ehAvisoFilaVazia(TPacoteRede pacote)
{
  return (pacote.comando_serial == CMD_RESPOSTA_TVBOX &&
          pacote.tipo_mensagem == MSG_RESPOSTA_TVBOX &&
          pacote.status == STATUS_VAZIO &&
          pacote.sequencia == 0);
}

void atualizarEstadoFilaTvBox(TPacoteRede pacote)
{
  if (pacote.comando_serial == CMD_NENHUM && pacote.tipo_mensagem == MSG_DADO)
  {
    filaTvBoxAtiva = true;
  }
  else if (ehAvisoFilaVazia(pacote))
  {
    filaTvBoxAtiva = false;
  }
}

void salvarProximaSequencia()
{
  preferences.begin("rede_lora", false);
  preferences.putUInt("seq", proximaSequencia);
  preferences.end();
}

void carregarProximaSequencia()
{
  preferences.begin("rede_lora", false);
  proximaSequencia = preferences.getUInt("seq", 1);
  preferences.end();
}

void mostrarDisplay(String linha1, String linha2, TPacoteRede pacote)
{
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("TRANSMISSOR");

  display.setCursor(0, 10);
  display.println(linha1);

  display.setCursor(0, 20);
  display.println(linha2);

  display.setCursor(0, 30);
  display.print("Orig: ");
  display.print(pacote.origem_id);
  display.print(" Seq: ");
  display.println(pacote.sequencia);

  display.setCursor(0, 40);
  display.print("T:");
  display.print(pacote.temperatura, 1);
  display.print(" U:");
  display.print(pacote.umidade, 1);

  display.setCursor(0, 50);
  display.print("GPS ");
  display.print(pacote.latitude, 2);
  display.print(",");
  display.print(pacote.longitude, 2);

  display.display();
}

void mostrarAguardando()
{
  TPacoteRede vazio = {};
  mostrarDisplay("Aguardando...", "Rede LoRa", vazio);
}

bool initComunicacaoLoRa()
{
  SPI.begin(SCK_LORA, MISO_LORA, MOSI_LORA, SS_PIN_LORA);
  LoRa.setPins(SS_PIN_LORA, RESET_PIN_LORA, LORA_DEFAULT_DIO0_PIN);

  if (!LoRa.begin(BAND))
  {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Radio LoRa");
    display.setCursor(0, 10);
    display.println("Conectando...");
    display.display();
    delay(1000);
    return false;
  }

  LoRa.setSpreadingFactor(fatorE);
  LoRa.setTxPower(HIGH_GAIN_LORA);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(0x55);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Radio LoRa OK");
  display.display();
  delay(1000);
  return true;
}

void enviarPacoteLoRa(TPacoteRede pacote)
{
  LoRa.beginPacket();
  LoRa.write((uint8_t *)&pacote, sizeof(TPacoteRede));
  LoRa.endPacket();
}

bool lerPacoteSerial(TPacoteRede *pacote)
{
  if (Serial.available() >= (int)sizeof(TPacoteRede))
  {
    Serial.readBytes((char *)pacote, sizeof(TPacoteRede));
    return true;
  }
  return false;
}

bool lerPacoteLoRa(TPacoteRede *pacote)
{
  int tamPacote = LoRa.parsePacket();

  if (tamPacote == sizeof(TPacoteRede))
  {
    uint8_t *ptr = (uint8_t *)pacote;
    int i = 0;

    while (LoRa.available() && i < (int)sizeof(TPacoteRede))
    {
      ptr[i] = (uint8_t)LoRa.read();
      i++;
    }

    return (i == (int)sizeof(TPacoteRede));
  }

  return false;
}

/*
  Envia um comando para a TV-Box e aguarda uma resposta com a mesma chave
  origem_id + sequencia. A TV-Box responde usando CMD_RESPOSTA_TVBOX.
*/
uint8_t enviarComandoTvBox(uint8_t comando, TPacoteRede pacote)
{
  pacote.comando_serial = comando;
  Serial.write((uint8_t *)&pacote, sizeof(TPacoteRede));

  unsigned long inicio = millis();

  while (millis() - inicio < TIMEOUT_TVBOX_MS)
  {
    TPacoteRede resposta;

    if (lerPacoteSerial(&resposta))
    {
      if (resposta.comando_serial == CMD_RESPOSTA_TVBOX && mesmaChavePacote(resposta, pacote))
      {
        return resposta.status;
      }

      atualizarEstadoFilaTvBox(resposta);

      /*
        Caso chegue um pacote pendente ou um aviso de fila vazia durante a espera,
        apenas atualizamos o estado local. O pendente permanece salvo no banco
        e será enviado novamente pela TV-Box em outro ciclo.
      */
    }
  }

  return STATUS_ERRO;
}

void armazenarPendenteNaTvBox(TPacoteRede pacote)
{
  pacote.tipo_dispositivo = TIPO_DISPOSITIVO;
  pacote.remetente_id = ID_DISPOSITIVO;
  pacote.destino_id = 0;
  pacote.comando_serial = CMD_ARMAZENAR_PENDENTE;
  pacote.status = STATUS_OK;

  uint8_t status = enviarComandoTvBox(CMD_ARMAZENAR_PENDENTE, pacote);

  if (status == STATUS_OK || status == STATUS_DUPLICADO)
  {
    filaTvBoxAtiva = true;
    mostrarDisplay("Salvo na TV-Box", "Pendente", pacote);
  }
  else
  {
    mostrarDisplay("Falha TV-Box", "Nao armazenou", pacote);
  }
}

void removerPendenteDaTvBox(TPacoteRede pacote)
{
  pacote.comando_serial = CMD_REMOVER_PENDENTE;
  enviarComandoTvBox(CMD_REMOVER_PENDENTE, pacote);
}

/*
  Inicia uma transmissão LoRa e passa a aguardar ACK.
  O pacote só será removido da TV-Box quando o ACK for recebido.
*/
bool iniciarEnvioComAck(TPacoteRede pacote, bool veioDaTvBox)
{
  if (aguardandoAck)
  {
    return false;
  }

  pacote.versao = VERSAO_PROTOCOLO;
  pacote.tipo_dispositivo = TIPO_DISPOSITIVO;
  pacote.tipo_mensagem = MSG_DADO;
  pacote.comando_serial = CMD_NENHUM;
  pacote.remetente_id = ID_DISPOSITIVO;
  pacote.destino_id = 0;
  pacote.status = STATUS_OK;

  pacoteEmEspera = pacote;
  pacoteVeioDaTvBox = veioDaTvBox;
  if (veioDaTvBox)
  {
    filaTvBoxAtiva = true;
  }
  aguardandoAck = true;
  momentoEnvio = millis();

  enviarPacoteLoRa(pacoteEmEspera);

  if (veioDaTvBox)
  {
    mostrarDisplay("Retransmitindo", "Fila TV-Box", pacoteEmEspera);
  }
  else
  {
    mostrarDisplay("Transmitindo", "Dado novo", pacoteEmEspera);
  }

  return true;
}

TPacoteRede gerarMockSensores()
{
  TPacoteRede pacote = {};

  pacote.versao = VERSAO_PROTOCOLO;
  pacote.tipo_dispositivo = TIPO_DISPOSITIVO;
  pacote.tipo_mensagem = MSG_DADO;
  pacote.comando_serial = CMD_NENHUM;
  pacote.origem_id = ID_DISPOSITIVO;
  pacote.remetente_id = ID_DISPOSITIVO;
  pacote.destino_id = 0;
  pacote.sequencia = proximaSequencia;
  pacote.saltos = 0;
  pacote.status = STATUS_OK;

  /*
    Mock dos sensores. Depois, basta substituir estes valores
    pelas leituras reais do sensor de temperatura/umidade e GPS.
  */
  pacote.temperatura = 25.0 + (proximaSequencia % 70) / 10.0;
  pacote.umidade = 60.0 + (proximaSequencia % 30);
  pacote.latitude = -3.1000 + (proximaSequencia % 100) * 0.0001;
  pacote.longitude = -60.0200 - (proximaSequencia % 100) * 0.0001;
  pacote.timestamp = millis() / 1000;

  proximaSequencia++;
  salvarProximaSequencia();

  return pacote;
}

/* -------------------- Processamento principal -------------------- */

void processarSerialTvBox()
{
  TPacoteRede pacote;

  if (!lerPacoteSerial(&pacote))
  {
    return;
  }

  atualizarEstadoFilaTvBox(pacote);

  /*
    A TV-Box envia pacotes pendentes com CMD_NENHUM.
    O transmissor retransmite somente se não estiver aguardando ACK de outro pacote.
  */
  if (pacote.comando_serial == CMD_NENHUM && pacote.tipo_mensagem == MSG_DADO)
  {
    iniciarEnvioComAck(pacote, true);
  }
}

void processarLoRa()
{
  TPacoteRede recebido;

  if (!lerPacoteLoRa(&recebido))
  {
    return;
  }

  /*
    O transmissor não recebe dados de sensores, somente confirmações.
    O ACK é aceito apenas se for endereçado para este dispositivo.
  */
  if (recebido.tipo_mensagem == MSG_ACK &&
      recebido.destino_id == ID_DISPOSITIVO &&
      aguardandoAck &&
      mesmaChavePacote(recebido, pacoteEmEspera))
  {
    if (pacoteVeioDaTvBox)
    {
      removerPendenteDaTvBox(pacoteEmEspera);
    }

    aguardandoAck = false;
    pacoteVeioDaTvBox = false;

    mostrarDisplay("ACK recebido", "Descartado", recebido);
  }
}

void processarTimeoutAck()
{
  if (!aguardandoAck)
  {
    return;
  }

  if (millis() - momentoEnvio >= TIMEOUT_ACK_MS)
  {
    /*
      Se o pacote era novo e não recebeu ACK, grava na TV-Box.
      Se veio da TV-Box, ele já está salvo; apenas libera para tentar novamente depois.
    */
    if (!pacoteVeioDaTvBox)
    {
      armazenarPendenteNaTvBox(pacoteEmEspera);
    }
    else
    {
      mostrarDisplay("Sem ACK", "Mantido TV-Box", pacoteEmEspera);
    }

    aguardandoAck = false;
    pacoteVeioDaTvBox = false;
  }
}

void gerarEnviarOuArmazenar()
{
  if (millis() - momentoUltimaGeracao < INTERVALO_GERACAO_MS)
  {
    return;
  }

  momentoUltimaGeracao = millis();

  TPacoteRede novo = gerarMockSensores();

  /*
    Prioridade da fila:
    - Se a TV-Box tem pendentes, os dados novos NÃO são enviados diretamente.
    - Eles são salvos na TV-Box, no final da fila.
    - A TV-Box continuará entregando o primeiro pendente para transmissão,
      preservando o comportamento de fila FIFO.
  */
  if (aguardandoAck || filaTvBoxAtiva)
  {
    armazenarPendenteNaTvBox(novo);
    return;
  }

  iniciarEnvioComAck(novo, false);
}

void setup()
{
  Serial.begin(115200);

  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(20);
  digitalWrite(OLED_RST, HIGH);

  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false))
  {
    for (;;);
  }

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Transmissor LoRa");
  display.display();

  carregarProximaSequencia();

  while (initComunicacaoLoRa() == false);

  mostrarAguardando();
}

void loop()
{
  processarSerialTvBox();
  processarLoRa();
  processarTimeoutAck();
  gerarEnviarOuArmazenar();
}
