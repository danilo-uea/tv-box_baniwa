import serial

# Substitua 'COM3' (no Windows) ou '/dev/ttyUSB0' (no Linux) pela porta correta
ser = serial.Serial('COM3', 115200, timeout=1)

while True:
    if ser.in_waiting > 0:
        line = ser.readline().decode('utf-8').rstrip()
        lista = line.split(';')
        print(f"Contador: {lista[0]}, Temperatura: {lista[1]}, Umidade: {lista[2]}")