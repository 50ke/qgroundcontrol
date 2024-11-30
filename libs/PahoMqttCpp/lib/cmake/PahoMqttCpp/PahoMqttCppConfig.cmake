# save build-time options
set(PAHO_BUILD_STATIC ON)
set(PAHO_BUILD_SHARED OFF)
set(PAHO_WITH_SSL OFF)
set(PAHO_WITH_MQTT_C ON)

include(CMakeFindDependencyMacro)

find_dependency(Threads REQUIRED)

if (NOT PAHO_WITH_MQTT_C)
  find_dependency(eclipse-paho-mqtt-c REQUIRED)
endif()

if (PAHO_WITH_SSL)
  find_dependency(OpenSSL REQUIRED)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/PahoMqttCppTargets.cmake")
