import sqlite3 # pip install db-sqlite3

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

# Verificar se a tabela está vazia
cursor.execute("SELECT COUNT(*) FROM pacote")
resultado = cursor.fetchone()

# Se a tabela estiver vazia, redefinir o autoincremento para começar em 1 novamente
if resultado[0] == 0:
    cursor.execute("DELETE FROM sqlite_sequence WHERE name = 'pacote'")

# Inserir dados
# dados = (10, 1, 10, 1, 1, "Ola mundo 0")
# cursor.execute("INSERT INTO pacote (contador, id_dispositivo, qtd_fila, tipo_mensagem, comando, mensagem) VALUES (?, ?, ?, ?, ?, ?)", dados)
# cursor.executemany("INSERT INTO pacote (contador, id_dispositivo, qtd_fila, tipo_mensagem, comando, mensagem) VALUES (?, ?, ?, ?, ?, ?)", [
#     (11, 1, 10, 1, 1, "Ola mundo 1"),
#     (12, 1, 10, 1, 1, "Ola mundo 2"),
#     (13, 1, 10, 1, 1, "Ola mundo 3")
# ])
# connection.commit()

# Remover apenas o primeiro registro inserido (com o menor id)
# cursor.execute("""
#     DELETE FROM pacote
#     WHERE id = (SELECT id FROM pacote ORDER BY id ASC LIMIT 1)
# """)
# connection.commit()

# Verifica a quantidade de pacotes cadastrados
cursor.execute("SELECT COUNT(*) FROM pacote")
resultado = cursor.fetchone()
print(f"Quantidade: {resultado[0]}")

# Consultar dados
cursor.execute("SELECT * FROM pacote")
pacotes = cursor.fetchall()
if len(pacotes) > 0:
    print("Pacotes cadastrados:")
    for pacote in pacotes:
        print(pacote)

# Consultar apenas o primeiro registro inserido
# cursor.execute("""
# SELECT * FROM pacote
# ORDER BY id ASC
# LIMIT 1
# """)

primeiro = cursor.fetchone()  # Recuperar o único registro retornado
# if primeiro != None:
#     print("\nPrimeiro pacote inserido:")
#     print(primeiro)

# Encerrar conexão
connection.close()