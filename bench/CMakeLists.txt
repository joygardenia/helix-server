# ping-pong 压测：服务端复用 src_lib，客户端仅需 pthread
add_executable(pingpong_server pingpong_server.cc)
target_link_libraries(pingpong_server src_lib memory_lib log_lib ${LIBS} mariadb)

add_executable(pingpong_client pingpong_client.cc)
target_link_libraries(pingpong_client ${LIBS})
