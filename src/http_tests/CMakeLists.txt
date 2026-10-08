add_executable(http_test http_test.cc)
target_link_libraries(http_test src_lib log_lib ${LIBS} mariadb)
add_test(NAME http_test COMMAND http_test)

add_executable(threadpool_test threadpool_test.cc)
target_link_libraries(threadpool_test src_lib log_lib ${LIBS} mariadb)
add_test(NAME threadpool_test COMMAND threadpool_test)

add_executable(loopback_test loopback_test.cc)
target_link_libraries(loopback_test src_lib log_lib ${LIBS} mariadb)
add_test(NAME loopback_test COMMAND loopback_test)
