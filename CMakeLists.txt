cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})

project(blink)

target_include_directories(app PRIVATE src)

target_sources(app PRIVATE
  src/status_led.cpp
  src/main.cpp
)
