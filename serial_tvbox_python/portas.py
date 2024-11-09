import serial.tools.list_ports

ports = serial.tools.list_ports.comports()

if len(ports) > 0: # Imprime a primeira porta
    print(f"Nome da primeira porta: {ports[0].device}")

print(f"Quantidade de portas: {len(ports)}\n")

for port in ports: # Imprime todas as portas
    print(port.device)
