CREATE DATABASE IF NOT EXISTS takeaway;
USE takeaway;

CREATE TABLE IF NOT EXISTS products (
    id    INT PRIMARY KEY AUTO_INCREMENT,
    name  VARCHAR(64) NOT NULL,
    price DECIMAL(8,2) NOT NULL,
    stock INT NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS orders (
    id         INT PRIMARY KEY AUTO_INCREMENT,
    user_id    INT NOT NULL,
    total      DECIMAL(10,2) NOT NULL,
    status     TINYINT NOT NULL DEFAULT 0  -- 0:待支付 1:已支付
);

CREATE TABLE IF NOT EXISTS order_items (
    id         INT PRIMARY KEY AUTO_INCREMENT,
    order_id   INT NOT NULL,
    product_id INT NOT NULL,
    qty        INT NOT NULL,
    price      DECIMAL(8,2) NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_oi_order   ON order_items(order_id);
CREATE INDEX IF NOT EXISTS idx_oi_product ON order_items(product_id);

-- 测试数据
INSERT IGNORE INTO products(id, name, price, stock) VALUES
(1, '黄焖鸡米饭', 18.00, 100),
(2, '麻辣烫',     22.00, 80),
(3, '煎饼果子',   10.00, 200);
