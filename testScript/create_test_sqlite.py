import sqlite3
import os

db_path = r'd:\vc\ObjectPRX\PostgreSQL_Backend\sqlite_data\e2e_test.sqlite'
os.makedirs(os.path.dirname(db_path), exist_ok=True)
if os.path.exists(db_path):
    os.remove(db_path)

conn = sqlite3.connect(db_path)
conn.execute('CREATE TABLE test_table (id INTEGER PRIMARY KEY, name TEXT, value REAL, flag INTEGER)')
conn.execute("INSERT INTO test_table (name, value, flag) VALUES ('alpha', 1.5, 1)")
conn.execute("INSERT INTO test_table (name, value, flag) VALUES ('beta', 2.5, 0)")
conn.execute("INSERT INTO test_table (name, value, flag) VALUES ('gamma', 3.5, 1)")
conn.commit()

cur = conn.execute('SELECT count(*) FROM test_table')
print('Created test SQLite at:', db_path)
print('Row count:', cur.fetchone()[0])
conn.close()
