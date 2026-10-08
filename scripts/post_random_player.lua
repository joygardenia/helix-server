-- wrk 脚本：生成随机用户名进行角色创建测试
math.randomseed(os.time())

request = function()
    local user_id = math.random(1, 10000000)
    local body = '{"name":"user_' .. user_id .. '"}'
    return wrk.format("POST", "/players", {["Content-Type"] = "application/json"}, body)
end
