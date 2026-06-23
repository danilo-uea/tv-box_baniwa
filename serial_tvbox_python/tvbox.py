"""
tvbox_rede_lora.py
TV-Box para Transmissor, Intermediário ou Receptor da rede LoRa.

Uso sugerido:
1) Ajuste DEVICE_ID e DEVICE_TYPE abaixo, OU use variáveis de ambiente.
2) Execute na TV-Box conectada ao ESP32 pela USB/Serial.

Exemplos:
    DEVICE_ID=1 DEVICE_TYPE=1 python3 tvbox_rede_lora.py   # TV-Box do transmissor
    DEVICE_ID=2 DEVICE_TYPE=2 python3 tvbox_rede_lora.py   # TV-Box do intermediário
    DEVICE_ID=3 DEVICE_TYPE=3 python3 tvbox_rede_lora.py   # TV-Box do receptor

Tipos:
    1 = Transmissor
    2 = Intermediário
    3 = Receptor

Função:
- Para transmissor/intermediário:
    guarda pacotes pendentes em SQLite e envia o primeiro pendente ao ESP32 periodicamente.
- Para intermediário/receptor:
    registra pacotes recebidos em SQLite e informa se são novos ou duplicados.
- Para receptor:
    a tabela pacotes_recebidos representa o armazenamento final do fluxo.
"""

import os
import sqlite3
import struct
import time
import serial
import serial.tools.list_ports

# -------------------- Configuração do dispositivo --------------------

TIPO_TRANSMISSOR = 1
TIPO_INTERMEDIARIO = 2
TIPO_RECEPTOR = 3

# Pode editar diretamente aqui ou passar por variável de ambiente.
DEVICE_ID = int(os.environ.get("DEVICE_ID", "3"))
DEVICE_TYPE = int(os.environ.get("DEVICE_TYPE", str(TIPO_TRANSMISSOR)))

# Porta Serial: se vazio, tenta usar a primeira porta encontrada.
SERIAL_PORT = os.environ.get("SERIAL_PORT", "")
BAUDRATE = int(os.environ.get("BAUDRATE", "115200"))

# Banco separado por dispositivo para evitar misturar dados ao testar várias TV-Boxes.
DB_PATH = os.environ.get("DB_PATH", f"banco_de_dados_lora_{DEVICE_ID}.db")

# Quantidade máxima de pacotes que podem ficar aguardando retransmissão.
# Usado em transmissores e intermediários.
TAMANHO_MAXIMO_FILA = int(os.environ.get("TAMANHO_MAXIMO_FILA", "20000"))

# Quantidade máxima de registros mantidos na tabela pacotes_recebidos.
# Essa tabela é usada para controle de duplicidade em intermediários/receptores.
# Valor recomendado maior que a fila de pendentes, pois ela funciona como histórico recente.
TAMANHO_MAXIMO_RECEBIDOS = int(os.environ.get("TAMANHO_MAXIMO_RECEBIDOS", "40000"))

# Intervalo em que a TV-Box entrega o primeiro pacote pendente ao ESP32.
# Deve ser menor que o intervalo de geração de dados do transmissor para esvaziar a fila.
INTERVALO_ENVIO_PENDENTE = float(os.environ.get("INTERVALO_ENVIO_PENDENTE", "2.0"))

# Quando ativo, a TV-Box avisa ao ESP32 que a fila está vazia.
# Isso permite que o transmissor saiba quando pode voltar a transmitir dados novos diretamente.
ENVIAR_AVISO_FILA_VAZIA = os.environ.get("ENVIAR_AVISO_FILA_VAZIA", "1") != "0"

# -------------------- Constantes do protocolo --------------------

VERSAO_PROTOCOLO = 1

MSG_DADO = 1
MSG_ACK = 2
MSG_NACK = 3
MSG_RESPOSTA_TVBOX = 4

CMD_NENHUM = 0
CMD_ARMAZENAR_PENDENTE = 1
CMD_REMOVER_PENDENTE = 2
CMD_REGISTRAR_RECEBIDO = 3
CMD_RESPOSTA_TVBOX = 4

STATUS_ERRO = 0
STATUS_OK = 1
STATUS_DUPLICADO = 2
STATUS_VAZIO = 3

# Mesmo formato da struct TPacoteRede dos códigos Arduino.
# little-endian:
# B B B B H H H I B B f f f f I
FORMATO_PACOTE = "<BBBBHHHIBBffffI"
TAMANHO_PACOTE = struct.calcsize(FORMATO_PACOTE)

CAMPOS = [
    "versao",
    "tipo_dispositivo",
    "tipo_mensagem",
    "comando_serial",
    "origem_id",
    "remetente_id",
    "destino_id",
    "sequencia",
    "saltos",
    "status",
    "temperatura",
    "umidade",
    "latitude",
    "longitude",
    "timestamp",
]

porta_serial = None


# -------------------- Banco de dados --------------------

def conectar_banco():
    connection = sqlite3.connect(DB_PATH)
    connection.row_factory = sqlite3.Row
    cursor = connection.cursor()

    cursor.execute("""
        CREATE TABLE IF NOT EXISTS pacotes_pendentes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            versao INTEGER NOT NULL,
            tipo_dispositivo INTEGER NOT NULL,
            tipo_mensagem INTEGER NOT NULL,
            origem_id INTEGER NOT NULL,
            remetente_id INTEGER NOT NULL,
            destino_id INTEGER NOT NULL,
            sequencia INTEGER NOT NULL,
            saltos INTEGER NOT NULL,
            status INTEGER NOT NULL,
            temperatura REAL NOT NULL,
            umidade REAL NOT NULL,
            latitude REAL NOT NULL,
            longitude REAL NOT NULL,
            timestamp INTEGER NOT NULL,
            criado_em REAL NOT NULL,
            UNIQUE(origem_id, sequencia)
        )
    """)

    cursor.execute("""
        CREATE TABLE IF NOT EXISTS pacotes_recebidos (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            id_tvbox_local INTEGER NOT NULL,
            tipo_tvbox_local INTEGER NOT NULL,
            versao INTEGER NOT NULL,
            tipo_dispositivo INTEGER NOT NULL,
            tipo_mensagem INTEGER NOT NULL,
            origem_id INTEGER NOT NULL,
            remetente_id INTEGER NOT NULL,
            destino_id INTEGER NOT NULL,
            sequencia INTEGER NOT NULL,
            saltos INTEGER NOT NULL,
            status INTEGER NOT NULL,
            temperatura REAL NOT NULL,
            umidade REAL NOT NULL,
            latitude REAL NOT NULL,
            longitude REAL NOT NULL,
            timestamp INTEGER NOT NULL,
            recebido_em REAL NOT NULL,
            UNIQUE(origem_id, sequencia)
        )
    """)

    connection.commit()
    return connection


connection = conectar_banco()


def quantidade_pendentes():
    cursor = connection.cursor()
    cursor.execute("SELECT COUNT(*) AS total FROM pacotes_pendentes")
    return int(cursor.fetchone()["total"])


def quantidade_recebidos():
    cursor = connection.cursor()
    cursor.execute("SELECT COUNT(*) AS total FROM pacotes_recebidos")
    return int(cursor.fetchone()["total"])


def limitar_fila_se_necessario():
    """
    Mantém a fila com no máximo TAMANHO_MAXIMO_FILA registros.
    Se ultrapassar o limite, remove o mais antigo.
    """
    while quantidade_pendentes() > TAMANHO_MAXIMO_FILA:
        cursor = connection.cursor()
        cursor.execute("""
            DELETE FROM pacotes_pendentes
            WHERE id = (
                SELECT id
                FROM pacotes_pendentes
                ORDER BY id ASC
                LIMIT 1
            )
        """)
        connection.commit()


def limitar_recebidos_se_necessario():
    """
    Mantém a tabela pacotes_recebidos com no máximo TAMANHO_MAXIMO_RECEBIDOS registros.

    A remoção é feita pelos registros mais antigos, usando o menor id, da mesma forma
    que a fila de pacotes pendentes. Isso mantém um histórico recente para controle
    de duplicidade sem deixar o banco crescer indefinidamente.

    Se TAMANHO_MAXIMO_RECEBIDOS for 0 ou negativo, a limpeza fica desativada.
    """
    if TAMANHO_MAXIMO_RECEBIDOS <= 0:
        return

    while quantidade_recebidos() > TAMANHO_MAXIMO_RECEBIDOS:
        cursor = connection.cursor()
        cursor.execute("""
            DELETE FROM pacotes_recebidos
            WHERE id = (
                SELECT id
                FROM pacotes_recebidos
                ORDER BY id ASC
                LIMIT 1
            )
        """)
        connection.commit()


def inserir_pendente(pacote):
    """
    Insere pacote na fila de pendentes.
    Se origem_id + sequencia já existir, considera duplicado.
    """
    cursor = connection.cursor()

    cursor.execute("""
        INSERT OR IGNORE INTO pacotes_pendentes (
            versao, tipo_dispositivo, tipo_mensagem,
            origem_id, remetente_id, destino_id, sequencia,
            saltos, status, temperatura, umidade, latitude, longitude,
            timestamp, criado_em
        )
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    """, (
        pacote["versao"],
        pacote["tipo_dispositivo"],
        MSG_DADO,
        pacote["origem_id"],
        pacote["remetente_id"],
        pacote["destino_id"],
        pacote["sequencia"],
        pacote["saltos"],
        pacote["status"],
        pacote["temperatura"],
        pacote["umidade"],
        pacote["latitude"],
        pacote["longitude"],
        pacote["timestamp"],
        time.time(),
    ))

    connection.commit()

    if cursor.rowcount == 0:
        return STATUS_DUPLICADO

    limitar_fila_se_necessario()
    return STATUS_OK


def remover_pendente(pacote):
    cursor = connection.cursor()
    cursor.execute("""
        DELETE FROM pacotes_pendentes
        WHERE origem_id = ? AND sequencia = ?
    """, (pacote["origem_id"], pacote["sequencia"]))
    connection.commit()

    return STATUS_OK if cursor.rowcount > 0 else STATUS_VAZIO


def obter_primeiro_pendente():
    cursor = connection.cursor()
    cursor.execute("""
        SELECT *
        FROM pacotes_pendentes
        ORDER BY id ASC
        LIMIT 1
    """)
    row = cursor.fetchone()

    if row is None:
        return None

    return {
        "versao": int(row["versao"]),
        "tipo_dispositivo": int(row["tipo_dispositivo"]),
        "tipo_mensagem": MSG_DADO,
        "comando_serial": CMD_NENHUM,
        "origem_id": int(row["origem_id"]),
        "remetente_id": int(row["remetente_id"]),
        "destino_id": int(row["destino_id"]),
        "sequencia": int(row["sequencia"]),
        "saltos": int(row["saltos"]),
        "status": int(row["status"]),
        "temperatura": float(row["temperatura"]),
        "umidade": float(row["umidade"]),
        "latitude": float(row["latitude"]),
        "longitude": float(row["longitude"]),
        "timestamp": int(row["timestamp"]),
    }


def registrar_recebido(pacote):
    """
    Registra pacote recebido pela TV-Box local.
    Em receptor, esta é a gravação final do fluxo.
    Em intermediário, esta tabela serve também para bloquear duplicados.
    """
    cursor = connection.cursor()

    cursor.execute("""
        INSERT OR IGNORE INTO pacotes_recebidos (
            id_tvbox_local, tipo_tvbox_local,
            versao, tipo_dispositivo, tipo_mensagem,
            origem_id, remetente_id, destino_id, sequencia,
            saltos, status, temperatura, umidade, latitude, longitude,
            timestamp, recebido_em
        )
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    """, (
        DEVICE_ID,
        DEVICE_TYPE,
        pacote["versao"],
        pacote["tipo_dispositivo"],
        MSG_DADO,
        pacote["origem_id"],
        pacote["remetente_id"],
        pacote["destino_id"],
        pacote["sequencia"],
        pacote["saltos"],
        pacote["status"],
        pacote["temperatura"],
        pacote["umidade"],
        pacote["latitude"],
        pacote["longitude"],
        pacote["timestamp"],
        time.time(),
    ))

    connection.commit()

    if cursor.rowcount == 0:
        return STATUS_DUPLICADO

    limitar_recebidos_se_necessario()
    return STATUS_OK


# -------------------- Serial e empacotamento --------------------

def localizar_primeira_porta():
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        return ""
    return ports[0].device


def conectar_porta():
    global porta_serial

    tentativas = 0

    while True:
        porta = SERIAL_PORT or localizar_primeira_porta()

        if not porta:
            tentativas += 1
            print(f"Nenhuma porta serial encontrada. Tentativa {tentativas}.")
            time.sleep(2)
            continue

        try:
            porta_serial = serial.Serial(porta, BAUDRATE, timeout=0.2)
            print(f"Porta serial conectada: {porta}")
            return
        except serial.SerialException as exc:
            tentativas += 1
            print(f"Falha ao conectar em {porta}. Tentativa {tentativas}. Erro: {exc}")
            time.sleep(2)


def desempacotar_pacote(dados):
    valores = struct.unpack(FORMATO_PACOTE, dados)
    return dict(zip(CAMPOS, valores))


def empacotar_pacote(pacote):
    return struct.pack(
        FORMATO_PACOTE,
        int(pacote["versao"]),
        int(pacote["tipo_dispositivo"]),
        int(pacote["tipo_mensagem"]),
        int(pacote["comando_serial"]),
        int(pacote["origem_id"]),
        int(pacote["remetente_id"]),
        int(pacote["destino_id"]),
        int(pacote["sequencia"]),
        int(pacote["saltos"]),
        int(pacote["status"]),
        float(pacote["temperatura"]),
        float(pacote["umidade"]),
        float(pacote["latitude"]),
        float(pacote["longitude"]),
        int(pacote["timestamp"]),
    )


def enviar_pacote_serial(pacote):
    porta_serial.write(empacotar_pacote(pacote))


def enviar_resposta_tvbox(pacote, status):
    resposta = dict(pacote)
    resposta["versao"] = VERSAO_PROTOCOLO
    resposta["tipo_dispositivo"] = DEVICE_TYPE
    resposta["tipo_mensagem"] = MSG_RESPOSTA_TVBOX
    resposta["comando_serial"] = CMD_RESPOSTA_TVBOX
    resposta["remetente_id"] = DEVICE_ID
    resposta["destino_id"] = pacote["remetente_id"]
    resposta["status"] = status
    enviar_pacote_serial(resposta)


def montar_aviso_fila_vazia():
    """
    Cria um pacote de controle para informar ao ESP32 que não há pendentes.

    Esse pacote não representa um dado de sensor. Ele serve apenas para controle
    de fluxo entre TV-Box e ESP32. O transmissor usa esse aviso para liberar
    a transmissão direta de novos dados somente quando a fila da TV-Box estiver vazia.
    """
    return {
        "versao": VERSAO_PROTOCOLO,
        "tipo_dispositivo": DEVICE_TYPE,
        "tipo_mensagem": MSG_RESPOSTA_TVBOX,
        "comando_serial": CMD_RESPOSTA_TVBOX,
        "origem_id": DEVICE_ID,
        "remetente_id": DEVICE_ID,
        "destino_id": 0,
        "sequencia": 0,
        "saltos": 0,
        "status": STATUS_VAZIO,
        "temperatura": 0.0,
        "umidade": 0.0,
        "latitude": 0.0,
        "longitude": 0.0,
        "timestamp": int(time.time()),
    }


def enviar_aviso_fila_vazia():
    enviar_pacote_serial(montar_aviso_fila_vazia())


# -------------------- Processamento --------------------

def processar_pacote_serial(pacote):
    comando = pacote["comando_serial"]

    if comando == CMD_ARMAZENAR_PENDENTE:
        status = inserir_pendente(pacote)
        enviar_resposta_tvbox(pacote, status)
        print(f"ARMAZENAR_PENDENTE origem={pacote['origem_id']} seq={pacote['sequencia']} status={status}")

    elif comando == CMD_REMOVER_PENDENTE:
        status = remover_pendente(pacote)
        enviar_resposta_tvbox(pacote, status)
        print(f"REMOVER_PENDENTE origem={pacote['origem_id']} seq={pacote['sequencia']} status={status}")

    elif comando == CMD_REGISTRAR_RECEBIDO:
        status = registrar_recebido(pacote)
        enviar_resposta_tvbox(pacote, status)
        print(f"REGISTRAR_RECEBIDO origem={pacote['origem_id']} seq={pacote['sequencia']} status={status}")

    else:
        # Comando desconhecido ou CMD_NENHUM vindo do ESP32.
        enviar_resposta_tvbox(pacote, STATUS_ERRO)
        print(f"COMANDO_INVALIDO={comando} origem={pacote['origem_id']} seq={pacote['sequencia']}")


def imprimir_resumo():
    cursor = connection.cursor()

    cursor.execute("SELECT COUNT(*) AS total FROM pacotes_pendentes")
    pendentes = cursor.fetchone()["total"]

    cursor.execute("SELECT COUNT(*) AS total FROM pacotes_recebidos")
    recebidos = cursor.fetchone()["total"]

    limite_recebidos = "sem limite" if TAMANHO_MAXIMO_RECEBIDOS <= 0 else TAMANHO_MAXIMO_RECEBIDOS

    print(
        f"TV-Box ID={DEVICE_ID} Tipo={DEVICE_TYPE} | "
        f"Pendentes={pendentes}/{TAMANHO_MAXIMO_FILA} | "
        f"Recebidos={recebidos}/{limite_recebidos}"
    )


def main():
    conectar_porta()

    print("TV-Box Rede LoRa iniciada")
    print(f"DEVICE_ID={DEVICE_ID} DEVICE_TYPE={DEVICE_TYPE} DB_PATH={DB_PATH}")
    print(f"TAMANHO_MAXIMO_FILA={TAMANHO_MAXIMO_FILA}")
    print(f"TAMANHO_MAXIMO_RECEBIDOS={TAMANHO_MAXIMO_RECEBIDOS}")
    print(f"INTERVALO_ENVIO_PENDENTE={INTERVALO_ENVIO_PENDENTE}")
    print(f"ENVIAR_AVISO_FILA_VAZIA={ENVIAR_AVISO_FILA_VAZIA}")
    print(f"Formato={FORMATO_PACOTE} Tamanho={TAMANHO_PACOTE} bytes")

    ultimo_envio_pendente = time.time()
    ultimo_resumo = 0

    while True:
        try:
            # Lê todos os pacotes completos acumulados na Serial.
            while porta_serial.in_waiting >= TAMANHO_PACOTE:
                dados = porta_serial.read(TAMANHO_PACOTE)
                pacote = desempacotar_pacote(dados)
                processar_pacote_serial(pacote)

            agora = time.time()

            # Receptores finais não precisam retransmitir pendentes.
            if DEVICE_TYPE != TIPO_RECEPTOR and (agora - ultimo_envio_pendente) >= INTERVALO_ENVIO_PENDENTE:
                pendente = obter_primeiro_pendente()

                if pendente is not None:
                    enviar_pacote_serial(pendente)
                    print(f"ENVIAR_PENDENTE origem={pendente['origem_id']} seq={pendente['sequencia']}")
                elif ENVIAR_AVISO_FILA_VAZIA:
                    enviar_aviso_fila_vazia()

                ultimo_envio_pendente = agora

            if agora - ultimo_resumo >= 5:
                imprimir_resumo()
                ultimo_resumo = agora

            time.sleep(0.05)

        except serial.SerialException:
            print("Porta serial desconectada. Tentando reconectar...")
            conectar_porta()


if __name__ == "__main__":
    main()
