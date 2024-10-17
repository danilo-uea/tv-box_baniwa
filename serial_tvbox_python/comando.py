import serial
import time

# Substitua 'COM3' (no Windows) ou '/dev/ttyUSB0' (no Linux) pela porta correta
ser = serial.Serial('COM3', 115200, timeout=1)

# Tempo para garantir que a conexão serial esteja estável
time.sleep(2)

# Função para enviar comandos para o ESP32
def enviar_comando(comando):
    ser.write((comando + '\n').encode())  # Envia o comando seguido de nova linha
    time.sleep(0.1)  # Aguarda um pequeno intervalo

while True:
    comando = input("Digite um comando: ")

    if comando.lower() == 'sair':
        print("Encerrando a conexão...")
        break

    enviar_comando(comando)

ser.close()
