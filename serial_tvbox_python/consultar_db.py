import sqlite3
import os
import sys

def conectar_banco(db_path):
    """Conecta ao banco de dados SQLite e verifica se ele existe."""
    if not os.path.exists(db_path):
        print(f"❌ Erro: O banco de dados '{db_path}' não foi encontrado.")
        print("💡 Dica: Certifique-se de que o tvbox.py já foi executado para criar o banco.")
        sys.exit(1)
    
    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row  # Permite acessar colunas pelo nome
    return conn

def exibir_tabela(conn, nome_tabela):
    """Busca e exibe todos os registros de uma tabela específica."""
    print(f"\n{'='*25} TABELA: {nome_tabela.upper()} {'='*25}")
    cursor = conn.cursor()
    
    # Verifica se a tabela existe no banco
    cursor.execute("SELECT name FROM sqlite_master WHERE type='table' AND name=?;", (nome_tabela,))
    if not cursor.fetchone():
        print(f"⚠️ A tabela '{nome_tabela}' não existe neste banco de dados.")
        return

    # Faz o SELECT *
    cursor.execute(f"SELECT * FROM {nome_tabela}")
    registros = cursor.fetchall()
    
    if not registros:
        print(f"📭 Nenhum registro encontrado na tabela '{nome_tabela}'.")
        return
        
    print(f"📊 Total de registros: {len(registros)}\n")
    
    # Imprime os registros formatados
    for idx, row in enumerate(registros, 1):
        print(f"--- Registro {idx} ---")
        for key in row.keys():
            # Formatação simples para alinhar os valores
            print(f"  {key:<20}: {row[key]}")
        print("-" * 40)

def main():
    # Permite passar o caminho do banco como argumento na linha de comando
    # Exemplo: python consultar_db.py banco_de_dados_lora_2.db
    if len(sys.argv) > 1:
        db_path = sys.argv[1]
    else:
        # Se não passar argumento, usa o padrão do DEVICE_ID=1 do tvbox.py
        device_id = os.environ.get("DEVICE_ID", "3")
        db_path = os.environ.get("DB_PATH", f"banco_de_dados_lora_{device_id}.db")
        
    print(f"🔌 Conectando ao banco de dados: {db_path}")
    conn = conectar_banco(db_path)
    
    try:
        # Consulta as tabelas criadas no tvbox.py
        exibir_tabela(conn, "pacotes_pendentes")
        exibir_tabela(conn, "pacotes_recebidos")
    finally:
        conn.close()
        print("\n✅ Conexão com o banco de dados encerrada.")

if __name__ == "__main__":
    main()