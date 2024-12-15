import os
import serial
import struct
import time
from collections import deque
import serial.tools.list_ports

porta_serial = None
terminal_so = '' # O tipo de terminal para fazer comandos
porta = '' # Nome da porta
TAMANHO_MAXIMO_FILA = 10 # Tamanho máximo da fila
formato_estrutura = 'iiiBB20s'  # 'i' para int, 'B' para byte, '20s' para char[20]
tamanho_estrutura = struct.calcsize(formato_estrutura) # Define o formato da estrutura conforme o pacote de dados em C
fila = deque(maxlen=TAMANHO_MAXIMO_FILA) # Criar a fila

# Verifica qual é o nome da porta no sistema operacional: 'COM3' (no Windows) ou '/dev/ttyUSB0' (no Linux)
ports = serial.tools.list_ports.comports()
if len(ports) > 0:
    porta = ports[0].device # Atribui sempre a primeira porta

# Verifica o sistema operacional
if os.name == 'nt':  # Se for Windows
    terminal_so = 'cls'
else:  # Se for Linux ou Mac
    terminal_so = 'clear'

def limpar_terminal():
    os.system(terminal_so)

# Função para reconectar à porta serial
def conectar_porta():
    global porta_serial
    while True:
        try:
            porta_serial = serial.Serial(porta, 115200, timeout=1)
            print("Porta serial conectada.")
            return
        except serial.SerialException:
            print("Tentando reconectar...")
            time.sleep(2)

# Conecte à porta inicialmente
conectar_porta()

# Função para desenfileirar dados
def desenfileirar():
    if fila:
        dados = fila.popleft()
        print("Dados desenfileirados:", dados)
        return dados
    else:
        print("A fila está vazia.")
        return None

# Função para enfileirar dados
def enfileirar(dados):
    if len(fila) < TAMANHO_MAXIMO_FILA:
        fila.append(dados)
    else:
        desenfileirar()
        enfileirar(dados)

# Função para verificar se a fila está cheia
def fila_cheia():
    return len(fila) == TAMANHO_MAXIMO_FILA

intervalo = 2  # Tempo em segundos
ultimo_tempo = time.time()

while True:
    try:
        if porta_serial.in_waiting == tamanho_estrutura:
            # Lê a quantidade de bytes necessária para a estrutura
            dados_recebidos = porta_serial.read(tamanho_estrutura)
            
            # Desempacota os dados recebidos
            contador, id_dispositivo, qtd_fila, tipo_mensagem, comando, mensagem = struct.unpack(formato_estrutura, dados_recebidos)
            
            # Decodifica a string e remove bytes vazios
            mensagem = mensagem.decode('utf-8').rstrip('\x00')

            # Criar um dicionário para armazenar a estrutura de forma compreensível
            pacote = {
                'contador': contador,
                'id_dispositivo': id_dispositivo,
                'qtd_fila': qtd_fila,
                'tipo_mensagem': tipo_mensagem,
                'comando': comando,
                'mensagem': mensagem
            }

            # Enfileirar o pacote
            if comando == 1:
                enfileirar(pacote)
            elif comando == 2:
                desenfileirar()
            
            limpar_terminal()
             # Mostrar o status da fila
            for objeto in fila:
                print(f"Fila atual: {objeto}")
            print("")
    except serial.SerialException:
        print("Porta serial desconectada. Tentando reconectar...")
        conectar_porta()

    # Executando o bloco de código a cada intervalo de tempo definido...
    if time.time() - ultimo_tempo >= intervalo:
        # Obtém o primeiro elemento da fila
        primeiro_elemento = fila[0] if fila else None  # Verifica se a fila não está vazia
        if primeiro_elemento != None:
            primeiro_elemento['qtd_fila'] = len(fila)
            # Empacotando os dados em formato binário
            dados_enviar = struct.pack(formato_estrutura,
                                    primeiro_elemento['contador'],
                                    primeiro_elemento['id_dispositivo'],
                                    primeiro_elemento['qtd_fila'],
                                    primeiro_elemento['tipo_mensagem'],
                                    primeiro_elemento['comando'],
                                    primeiro_elemento['mensagem'].encode('utf-8').ljust(20, b'\x00'))  # Preenche com '\x00' até 20 bytes
            # Envia os dados via Serial
            porta_serial.write(dados_enviar)
        # Atualiza o tempo da última execução
        ultimo_tempo = time.time()