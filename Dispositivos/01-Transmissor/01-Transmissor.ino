/* Bibliotecas para o Display OLED*/
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

/* Bibliotecas para comunicação LoRa */
#include <LoRa.h>
#include <SPI.h>

/* Biblioteca para acessar o NVS */
#include <Preferences.h>

/* Pinagem para o Display Oled */
#define OLED_SDA 4
#define OLED_SCL 15 
#define OLED_RST 16
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

/* Pinagem para comunicação com radio LoRa */
#define SCK_LORA           5
#define MISO_LORA          19
#define MOSI_LORA          27
#define RESET_PIN_LORA     14
#define SS_PIN_LORA        18
#define HIGH_GAIN_LORA     20  /* dBm */
#define BAND               915E6  /* 915MHz de frequencia */

/* Instância para acessar o NVS */
Preferences preferences;

uint32_t fatorE = 7;     /* Valor do fator de espalhamento */
int id_dispositivo = 1;  /* O id do dispositivo deve ser único para cada dispositivo */
#define TAMANHO_FILA 10 /* Define o tamanho máximo da fila */
int qtdFila = 0; /* Número de elementos na fila */

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST); /* Definicões do Display OLED */

/* Estrutura do pacote de dados */
typedef struct __attribute__((__packed__))  
{
  int contador;
  int id_dispositivo; /* O dispositivo que enviou o pacote */
  int qtd_fila;
  byte tipo_mensagem; /* Tipos de Mensagem: 1 - Envio da mensagem, 2 - Confirmação de recebimento */
  byte comando;       /* Tipos de comandos: 1 - Enfileirar, 2 - Desenfila */
  char mensagem[20];
} TDadosLora;

TDadosLora dados_enviados = {0, id_dispositivo, qtdFila, 1, 0, ""}; /* Último pacote que foi transmitido */
TDadosLora dados_recebidos = {0, id_dispositivo, qtdFila, 2, 0, ""}; /* Último pacote que foi recebido */

void aguardando_dados_display() {
  /* Imprimir mensagem dizendo o tipo do dispositivo */
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Transmissor");
  display.setCursor(0, 10);
  display.println("Em espera...");
  display.display();
  
  delay(200);
}

void escreve_medicoes_display(TDadosLora dados_lora, int state)
{      
    display.clearDisplay();

    display.setCursor(0, 0);
    display.println("TRANSMISSOR");

    display.setCursor(0, 10);
    display.print("Cont: ");
    display.println(dados_lora.contador);

    display.setCursor(0, 20);
    display.print("Tipo: ");
    display.println(dados_lora.tipo_mensagem);
    
    display.setCursor(0, 30);
    display.print("Mensagem: ");
    display.println(dados_lora.mensagem);

    display.setCursor(0, 40);
    display.print("Status: ");
    if (state == 1){
      display.println("Esp32");  
    } else if (state == 2){
      display.println("Tv-Box");  
    }
    
    display.display();
}

TDadosLora enviar_dados_lora(TDadosLora dados_lora) /* Transmite os dados via LoRa */   
{
  LoRa.beginPacket();
  LoRa.write((unsigned char *)&dados_lora, sizeof(TDadosLora));
  LoRa.endPacket();
  return dados_lora;
}

bool init_comunicacao_lora(void)
{
    bool status_init = false;
    /* Serial.println("[LoRa Emissor] Tentando iniciar comunicacao com o radio LoRa..."); */
    SPI.begin(SCK_LORA, MISO_LORA, MOSI_LORA, SS_PIN_LORA);
    LoRa.setPins(SS_PIN_LORA, RESET_PIN_LORA, LORA_DEFAULT_DIO0_PIN);

    display.clearDisplay();
    
    if (!LoRa.begin(BAND)) 
    {
      /* Serial.println("[LoRa Emissor] Comunicacao com o radio LoRa falhou. Nova tentativa em 1 segundo..."); */
      status_init = false;

      display.setCursor(0, 0);
      display.println("Radio LoRa");
      display.setCursor(0, 10);
      display.println("Status: Conectando...");
      display.setCursor(0, 20);
      display.println("Tentativas: Cada 1s");
      display.display();

      delay(1000);
    }
    else
    {
      LoRa.setSpreadingFactor(fatorE); /* Fator de Espalhamento */
      LoRa.setTxPower(HIGH_GAIN_LORA); /* Configura o ganho do receptor LoRa para 20dBm, o maior ganho possível (visando maior alcance possível) */ 
      LoRa.setSignalBandwidth(125E3);  /* Largura de banda fixa de 125 kHz - Suporta valores: 7.8E3, 10.4E3, 15.6E3, 20.8E3, 31.25E3, 41.7E3, 62.5E3, 125E3, 250E3 e 500E3 */
      LoRa.setCodingRate4(5);          /* Taxa de código - Suporta valores entre 5 e 8 */
      LoRa.setSyncWord(0x55);          /* Palavra de sincronização. Deve ser a mesma no transmissor e receptor */
      
      /*Serial.println("[LoRa Emissor] Comunicacao com o radio LoRa ok"); */
      status_init = true;

      display.setCursor(0, 0);
      display.println("Radio LoRa");
      display.setCursor(0, 10);
      display.println("Status: Ok");
      display.display();
    }

    return status_init;
}

void recebe_informacoes(){ /* Recebe os pacotes de confirmação de recepção */
  TDadosLora dados_temp;   /* Inicializando o pacote */
  char byte_recebido;
  int tam_pacote = LoRa.parsePacket();          /* Verifica se chegou alguma informação do tamanho esperado */
  char * ptInformaraoRecebida = NULL;
  
  if (tam_pacote == sizeof(TDadosLora)) {       /* Recebe a confirmação de recebimento da mensagem */
    ptInformaraoRecebida = (char *)&dados_temp; /* Recebe os dados conforme protocolo */
    
    while (LoRa.available()) 
    {
        byte_recebido = (char)LoRa.read();
        *ptInformaraoRecebida = byte_recebido;
        ptInformaraoRecebida++;
    }

    /* Ele só aceita os pacotes que ele enviou do tipo de confirmação de recepção */
    if(dados_temp.id_dispositivo == id_dispositivo && dados_temp.tipo_mensagem == 2){
      dados_recebidos = dados_temp;
    }
  }
}

/* Função para verificar se a fila está cheia */
bool isFilaCheia() {
  return (qtdFila == TAMANHO_FILA);
}

/* Função para verificar se a fila está vazia */
bool isFilaVazia() {
  return (qtdFila == 0);
}

/* Função para remover um elemento da fila */
void desenfileirar() {
  if (isFilaVazia()) {
    return;
  }
  qtdFila--;
  TDadosLora novoDado = {0, id_dispositivo, qtdFila, 0, 2, ""};
  Serial.write((byte*)&novoDado, sizeof(TDadosLora)); /* Envia o comando para desenfileirar na TV-BOX */
}

/* Função para adicionar um elemento à fila */
void enfileirar(TDadosLora novoDado) {
  if (isFilaCheia()) {
    desenfileirar();
    delay(10);
  }
  
  novoDado.comando = 1; /* Comando para enfileirar na TV-BOX */
  Serial.write((byte*)&novoDado, sizeof(TDadosLora)); /* Envia o pacote para ser armazenado na TV-BOX */
  qtdFila++;
}

int cont = 1;

/* Função para salvar o contador antes do desligamento */
void salvarContador() {
  preferences.begin("dados", false); // Acessa o NVS
  preferences.putInt("contador", cont); // Salva o contador
  preferences.end();
}

/* Função para recupera o valor do contador ao iniciar */
void pegarContador() {
  preferences.begin("dados", false);
  cont = preferences.getInt("contador", 1); // Valor padrão é 1
  preferences.end();
}

TDadosLora capturar_dados(){ /* Captura as informações dos sensores para transmissão a cada período de tempo definido */
  TDadosLora dados_transmitir;
  dados_transmitir.contador = cont;
  dados_transmitir.id_dispositivo = id_dispositivo;
  dados_transmitir.qtd_fila = qtdFila;
  dados_transmitir.tipo_mensagem = 1;
  dados_transmitir.comando = 0;
  strcpy(dados_transmitir.mensagem, "Ola mundo");
  cont++;
  salvarContador();

  return dados_transmitir;
}

void setup() 
{
  /* Monitor Serial */
  Serial.begin(115200);

  /* Preparando a inicialização do display OLED */
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(20);
  digitalWrite(OLED_RST, HIGH);

  /* Inicialização do display OLED */
  Wire.begin(OLED_SDA, OLED_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false)) { // Address 0x3C for 128x32
    for(;;); /* Don't proceed, loop forever */
  }

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0,0);
  display.print("Transmissor LoRa");
  display.display();
  delay(2000);
  
  while(init_comunicacao_lora() == false); /* Tenta, até obter sucesso na comunicacao com o chip LoRa */

  delay(2000);

  /* Imprimir mensagem dizendo que está aguardando o funcionamento */
  aguardando_dados_display();

  /* Recupera o valor do contador ao iniciar */
  pegarContador();
  
  /* Registra o hook para salvar antes do reinício/desligamento */
  esp_register_shutdown_handler(salvarContador);
}

unsigned long tempoAnterior = 0;
const long intervalo = 4000;
bool isInit = true; /* Verifica se está na primeira iteração */

void loop()
{
  if (millis() - tempoAnterior >= intervalo) { /* Captura os dados a cada intervalo de tempo e decide se vai enviar ou armazenar */
    TDadosLora dados_enviar = capturar_dados(); /* Dados capturados para envio ou armazenamento */
    tempoAnterior = millis();
    
    if(qtdFila == 0) { /* O armazenamento na TV-BOX está vazio */  
      if(isInit || dados_enviados.contador == dados_recebidos.contador) { /* Significa que o pacote foi recebido com sucesso */
        dados_enviados = enviar_dados_lora(dados_enviar); /* Transmite os pacotes diretamente */
        isInit = false;
        escreve_medicoes_display(dados_enviados, 1);
      } else { /* O pacote não foi recebido com sucesso, então é necessário armazená-lo na TV-BOX */
        enfileirar(dados_enviados);
        delay(intervalo / 2);
        enfileirar(dados_enviar);
      }
    } else if(qtdFila > 0) { /* O armazenamento na TV-BOX possui dados para enviar */
      enfileirar(dados_enviar);
    }
  }

  /* Verifica se há dados disponíveis na serial */
  if (Serial.available() >= sizeof(TDadosLora)) { 
    TDadosLora dados_enviar;
    Serial.readBytes((char *)&dados_enviar, sizeof(TDadosLora)); /* Lê os dados e os armazena na estrutura */
    qtdFila = dados_enviar.qtd_fila;
    
    if (dados_enviados.contador == dados_recebidos.contador){
      desenfileirar();
    }
    
    dados_enviados = enviar_dados_lora(dados_enviar);
    escreve_medicoes_display(dados_enviar, 2);
  }
  
  recebe_informacoes();
}
