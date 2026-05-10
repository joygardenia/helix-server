-- 游戏服务端演示库（本地开发）
--
-- 首次初始化（任选其一）：
--   mysql -u root -p < db/init.sql
-- 若程序使用 takeaway/takeaway123 连接，请先授权（MySQL 8 示例）：
--   mysql -u root -p -e "CREATE USER IF NOT EXISTS 'takeaway'@'localhost' IDENTIFIED BY 'takeaway123'; GRANT ALL PRIVILEGES ON game_db.* TO 'takeaway'@'localhost'; FLUSH PRIVILEGES;"
--
CREATE DATABASE IF NOT EXISTS game_db CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE game_db;

CREATE TABLE IF NOT EXISTS players (
    id         INT PRIMARY KEY AUTO_INCREMENT,
    name       VARCHAR(64) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_players_name (name)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS item_defs (
    id          INT PRIMARY KEY AUTO_INCREMENT,
    name        VARCHAR(64) NOT NULL,
    description VARCHAR(256) NOT NULL DEFAULT ''
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS inventory (
    player_id INT NOT NULL,
    item_id   INT NOT NULL,
    qty       INT NOT NULL DEFAULT 0,
    PRIMARY KEY (player_id, item_id),
    CONSTRAINT fk_inv_player FOREIGN KEY (player_id) REFERENCES players(id),
    CONSTRAINT fk_inv_item   FOREIGN KEY (item_id)   REFERENCES item_defs(id)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS audit_log (
    id         BIGINT PRIMARY KEY AUTO_INCREMENT,
    player_id  INT NOT NULL,
    action     VARCHAR(32) NOT NULL,
    detail     VARCHAR(512) NOT NULL DEFAULT '',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    KEY idx_audit_player (player_id)
) ENGINE=InnoDB;

-- 物品目录
INSERT IGNORE INTO item_defs (id, name, description) VALUES
(1, '小型生命药水', '恢复少量 HP'),
(2, '魔力结晶',     '用于技能升级');

-- 便于一键 curl 联调的测试角色与背包
INSERT IGNORE INTO players (id, name) VALUES (1, 'Tester');
INSERT IGNORE INTO inventory (player_id, item_id, qty) VALUES
(1, 1, 10),
(1, 2, 3);
