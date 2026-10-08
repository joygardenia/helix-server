-- wrk 脚本：生成随机 player_id 进行一致性哈希发现测试
math.randomseed(os.time())

request = function()
    local player_id = math.random(1, 1000000)
    local path = "/discover?key=player_" .. player_id
    return wrk.format("GET", path)
end

-- 记录响应统计（可选）
-- response = function(status, headers, body)
--    if status ~= 200 then
--        print("Error: " .. status)
--    end
-- end
