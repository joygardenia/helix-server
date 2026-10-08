#!/usr/bin/env bash
# 会话：对 player_id=1,item_id=1 开事务 FOR UPDATE 并持锁 N 秒
DUR="${1:-3}"
mysql -h127.0.0.1 -P3306 -utakeaway -ptakeaway123 game_db 2>/dev/null <<SQL
BEGIN;
SELECT qty FROM inventory WHERE player_id=1 AND item_id=1 FOR UPDATE;
SELECT SLEEP($DUR);
COMMIT;
SQL
