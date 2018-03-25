package.path = package.path ..';lua/?.lua'
--[[
-- network
--]]
require "./config"
require "./util"

local users = map:new()

function broadcast(msg)
	for k,v in pairs(users) do
		if type(k) == "number" then
			cwrite(k, msg)
		end
	end
end

function onConnected(fd, addr)
	users:insert(fd, addr)
	print("lua:connected " .. fd .. " addr " .. addr .. " online " .. users.count)
	broadcast("fd:" .. fd .. " connect:" .. addr)
end

function onRecv(fd, msg)
	print("lua:recv " .. fd .. " len:" .. string.len(msg) .. " msg:" .. msg)
	broadcast("fd:" .. fd .. " say:" .. msg)
end

function onError(fd, err)
	users:remove(fd)
	print("lua:error " .. fd .. " online " .. users.count)
	broadcast("fd:" .. fd .. " disconnected")
end
