cmake_minimum_required(VERSION 3.20)
project(TinyWebServer)

set(CMAKE_CXX_STANDARD 11)

add_executable(TinyWebServer main.cpp ./lock/locker.h ./log/block_queue.h ./log/log.h ./log/log.cpp ./CGImysql/sql_connection_pool.h ./CGImysql/sql_connection_pool.cpp ./threadpool/threadpool.h ./timer/lst_timer.h ./timer/lst_timer.cpp ./http/http_conn.h ./http/http_conn.cpp ./webserver.h ./webserver.cpp ./config.h ./config.cpp)

TARGET_LINK_LIBRARIES(TinyWebServer -lpthread -lmysqlclient)