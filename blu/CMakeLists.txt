cmake_minimum_required(VERSION 3.12)

# Change your executable name to something creative!
set(NAME blu) # <-- Name your project/executable here!

set(PICO_BOARD pico_w)

include(pico_sdk_import.cmake)

# Gooey boilerplate
project(${NAME} C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(PICO_CXX_ENABLE_EXCEPTIONS 1)

# Initialize the SDK
pico_sdk_init()

SET(LWIP_PORT_DIR "${CMAKE_CURRENT_LIST_DIR}/../port/lwip" CACHE STRING "Common Lib")
include(lwip_import.cmake)

SET(FREERTOS_KERNEL_PATH "${CMAKE_CURRENT_LIST_DIR}/../lib/FreeRTOS-Kernel" CACHE STRING "Common Lib")
SET(FREERTOS_CONFIG_FILE_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../port/FreeRTOS-Kernel" CACHE STRING "Local Config")
include(${CMAKE_CURRENT_LIST_DIR}/../port/FreeRTOS-Kernel/freertos_config.cmake)
include_directories("${FREERTOS_CONFIG_FILE_DIRECTORY}") 
include(FreeRTOS_Kernel_import.cmake)

set(COREHTTP_PATH "${CMAKE_CURRENT_LIST_DIR}/../lib/coreHTTP" CACHE STRING "Common Lib")
set(COREHTTP_PORT "${CMAKE_CURRENT_LIST_DIR}/../port/coreHTTP/" CACHE STRING "Common Lib")
include(coreHTTP.cmake)

SET(URILIB_PATH "${CMAKE_CURRENT_LIST_DIR}/../lib/uri-library" CACHE STRING "Common Lib")
include(urilib.cmake)

SET(JSON_MAKER_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/json-maker" CACHE STRING "Common Lib")
include(json-maker.cmake)

SET(TINY_JSON_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/tiny-json" CACHE STRING "Common Lib")
include(tiny-json.cmake)

set(MD5_PATH "${CMAKE_CURRENT_LIST_DIR}/../lib/md5")
include(md5.cmake)

########################################
# pico_fota_bootloader options
#
# The bootloader adds a SHA256 of the image to every FOTA image, which the
# application validates with pfb_firmware_sha256_check() before the update is
# applied, so leave PFB_WITH_SHA256_HASHING on.
#
# AES image encryption is off by default: it additionally requires the pycryptodome
# python package and a key which has to be kept in sync between the bootloader and
# every future firmware build, e.g.
#   cmake -DPFB_WITH_IMAGE_ENCRYPTION=ON -DPFB_AES_KEY=<key> ..
########################################
set(PFB_WITH_BOOTLOADER_LOGS ON CACHE BOOL "" FORCE)
set(PFB_REDIRECT_BOOTLOADER_LOGS_TO_UART ON CACHE BOOL "" FORCE)
set(PFB_WITH_IMAGE_ENCRYPTION OFF CACHE BOOL "" FORCE)
set(PFB_WITH_SHA256_HASHING ON CACHE BOOL "" FORCE)

add_subdirectory(pico_fota_bootloader)

# The bootloader logs over the same UART pins as the application, so that a
# single UART cable sees the whole boot sequence.
target_compile_definitions(pico_fota_bootloader PRIVATE
    PICO_DEFAULT_UART_RX_PIN=17
    PICO_DEFAULT_UART_TX_PIN=16
)

add_subdirectory(src)

#Set up files for the release packages
install(CODE "execute_process(COMMAND $ENV{HOME}/bin/picoDeploy.sh ${CMAKE_CURRENT_BINARY_DIR}/src/${NAME}.elf)")

# Set up files for the release packages
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/src/${NAME}.uf2
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR}
)

set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_GENERATOR "ZIP" "TGZ")
include(CPack)
