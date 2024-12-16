import os
import serial
import struct
import time
from collections import deque
import serial.tools.list_ports
import sqlite3

porta_serial = None
terminal_so = '' # O tipo de terminal para fazer comandos
porta = '' # Nome da porta
TAMANHO_MAXIMO_FILA = 10 # Tamanho máximo da fila
formato_estrutura = 'iiiBB20s'  # 'i' para int, 'B' para byte, '20s' para char[20]
tamanho_estrutura = struct.calcsize(formato_estrutura) # Define o formato da estrutura conforme o pacote de dados em C

# Conexão com o banco de dados
connection = sqlite3.connect('banco_de_dados.db')
cursor = connection.cursor()

# Criar tabela
cursor.execute("""
CREATE TABLE IF NOT EXISTS pacote (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    contador INTEGER NOT NULL,
    id_dispositivo INTEGER NOT NULL,
    qtd_fila INTEGER NOT NULL,
    tipo_mensagem INTEGER NOT NULL,
    comando INTEGER NOT NULL,
    mensagem TEXT NOT NULL
)
""")

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

# Verifica a quantidade de pacotes cadastrados
def quantidade_fila():
    cursor.execute("SELECT COUNT(*) FROM pacote")
    resultado = cursor.fetchone()

    return resultado[0]

# Função para desenfileirar dados
def desenfileirar():
    if quantidade_fila() > 0:
        # Remover apenas o primeiro registro inserido (com o menor id)
        cursor.execute("""
            DELETE FROM pacote
            WHERE id = (SELECT id FROM pacote ORDER BY id ASC LIMIT 1)
        """)
        connection.commit()

# Função para enfileirar dados
def enfileirar(dados):
    qtd = quantidade_fila()
    if qtd < TAMANHO_MAXIMO_FILA:
        # Se a tabela estiver vazia, redefinir o autoincremento para começar em 1 novamente
        if qtd == 0:
            cursor.execute("DELETE FROM sqlite_sequence WHERE name = 'pacote'")
            connection.commit()
        
        # Inserir dados
        cursor.execute("INSERT INTO pacote (contador, id_dispositivo, qtd_fila, tipo_mensagem, comando, mensagem) VALUES (?, ?, ?, ?, ?, ?)", dados)
        connection.commit()
    else:
        desenfileirar()
        enfileirar(dados)

intervalo = 2  # Tempo em segundos
ultimo_tempo = time.time()

# Imprime todos os pacotes inseridos
def imprimir_tabela():
    cursor.execute("SELECT * FROM pacote")
    pacotes = cursor.fetchall()

    if len(pacotes) > 0:
        print(f"Quantidade: {quantidade_fila()}")
        for pacote in pacotes:
            print(pacote)

        print("")

while True:
    try:
        if porta_serial.in_waiting == tamanho_estrutura:
            # Lê a quantidade de bytes necessária para a estrutura
            dados_recebidos = porta_serial.read(tamanho_estrutura)
            
            # Desempacota os dados recebidos
            contador, id_dispositivo, qtd_fila, tipo_mensagem, comando, mensagem = struct.unpack(formato_estrutura, dados_recebidos)
            
            # Decodifica a string e remove bytes vazios
            mensagem = mensagem.decode('utf-8').rstrip('\x00')

            # Uma tupla para armazenar a estrutura
            pacote = (contador, id_dispositivo, qtd_fila, tipo_mensagem, comando, mensagem)

            # Enfileirar o pacote
            if comando == 1:
                enfileirar(pacote)
            elif comando == 2:
                desenfileirar()
            
            limpar_terminal()
            imprimir_tabela()
    except serial.SerialException:
        print("Porta serial desconectada. Tentando reconectar...")
        conectar_porta()

    # Executando o bloco de código a cada intervalo de tempo definido...
    if time.time() - ultimo_tempo >= intervalo:
        qtd = quantidade_fila()
        if qtd > 0:
            # Obtém o primeiro elemento da fila
            cursor.execute("""
                SELECT * FROM pacote
                ORDER BY id ASC
                LIMIT 1
            """)
            primeiro_elemento = cursor.fetchone()
            
            # Empacotando os dados em formato binário
            dados_enviar = struct.pack(formato_estrutura,
                                    primeiro_elemento[1],
                                    primeiro_elemento[2],
                                    qtd, # Atualiza a quantidade de pacotes cadastrados
                                    primeiro_elemento[4],
                                    primeiro_elemento[5],
                                    primeiro_elemento[6].encode('utf-8').ljust(20, b'\x00'))  # Preenche com '\x00' até 20 bytes
            # Envia os dados via Serial
            porta_serial.write(dados_enviar)
        # Atualiza o tempo da última execução
        ultimo_tempo = time.time()