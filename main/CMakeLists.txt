set(TAIKO_PS4_AUTH_DIR "" CACHE PATH
    "Directory containing a PS4 private key, serial.txt, and credential signature")

set(TAIKO_PS4_AUTH_ENABLED OFF)
if(NOT "${TAIKO_PS4_AUTH_DIR}" STREQUAL "")
    get_filename_component(TAIKO_PS4_AUTH_DIR "${TAIKO_PS4_AUTH_DIR}"
        ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    set(TAIKO_PS4_SERIAL "${TAIKO_PS4_AUTH_DIR}/serial.txt")

    if(EXISTS "${TAIKO_PS4_AUTH_DIR}/private.pem")
        set(TAIKO_PS4_PRIVATE_KEY "${TAIKO_PS4_AUTH_DIR}/private.pem")
    elseif(EXISTS "${TAIKO_PS4_AUTH_DIR}/key.pem")
        set(TAIKO_PS4_PRIVATE_KEY "${TAIKO_PS4_AUTH_DIR}/key.pem")
    else()
        message(FATAL_ERROR
            "Missing PS4 private key: expected private.pem or key.pem in ${TAIKO_PS4_AUTH_DIR}")
    endif()

    if(EXISTS "${TAIKO_PS4_AUTH_DIR}/signature.bin")
        set(TAIKO_PS4_SIGNATURE "${TAIKO_PS4_AUTH_DIR}/signature.bin")
    elseif(EXISTS "${TAIKO_PS4_AUTH_DIR}/sig.bin")
        set(TAIKO_PS4_SIGNATURE "${TAIKO_PS4_AUTH_DIR}/sig.bin")
    else()
        message(FATAL_ERROR
            "Missing PS4 credential signature: expected signature.bin or sig.bin in ${TAIKO_PS4_AUTH_DIR}")
    endif()

    if(NOT EXISTS "${TAIKO_PS4_SERIAL}")
        message(FATAL_ERROR "Missing PS4 authentication file: ${TAIKO_PS4_SERIAL}")
    endif()

    file(SIZE "${TAIKO_PS4_SIGNATURE}" ps4_signature_size)
    if(NOT ps4_signature_size EQUAL 256)
        message(FATAL_ERROR
            "PS4 credential signature must contain exactly 256 bytes; found ${ps4_signature_size}")
    endif()

    file(READ "${TAIKO_PS4_SERIAL}" ps4_serial_text)
    string(STRIP "${ps4_serial_text}" ps4_serial_text)
    string(LENGTH "${ps4_serial_text}" ps4_serial_length)
    if(NOT ps4_serial_length EQUAL 16 OR
       NOT ps4_serial_text MATCHES "^[0-9A-Fa-f]+$")
        message(FATAL_ERROR
            "PS4 serial.txt must contain exactly 16 hexadecimal characters")
    endif()

    file(READ "${TAIKO_PS4_PRIVATE_KEY}" ps4_private_key_prefix LIMIT 128)
    string(FIND "${ps4_private_key_prefix}" "PRIVATE KEY-----"
        ps4_private_key_marker)
    if(ps4_private_key_marker EQUAL -1)
        message(FATAL_ERROR "PS4 private key must be an unencrypted PEM file")
    endif()

    set(TAIKO_PS4_AUTH_ENABLED ON)
endif()

set(TAIKO_PRIVATE_REQUIRES
    esp_adc
    esp_timer
    driver
    esp_driver_gpio
    esp_driver_rmt
    bootloader_support
    esp_hw_support
    mbedtls)

idf_component_register(
    SRCS
        "taiko_controller.c"
        "taiko_hit_led.c"
        "taiko_hit_processor.c"
        "taiko_ps4_auth.c"
        "taiko_ps4_auth_protocol.c"
        "taiko_reports.c"
        "taiko_usb.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES ${TAIKO_PRIVATE_REQUIRES}
    )

if(TAIKO_PS4_AUTH_ENABLED)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        TAIKO_PS4_AUTH_EMBEDDED=1)
    target_add_binary_data(${COMPONENT_LIB} "${TAIKO_PS4_PRIVATE_KEY}" TEXT
        RENAME_TO taiko_ps4_private_key)
    target_add_binary_data(${COMPONENT_LIB} "${TAIKO_PS4_SERIAL}" TEXT
        RENAME_TO taiko_ps4_serial_text)
    target_add_binary_data(${COMPONENT_LIB} "${TAIKO_PS4_SIGNATURE}" BINARY
        RENAME_TO taiko_ps4_signature)
    message(STATUS
        "PS4 credential authentication enabled from ${TAIKO_PS4_AUTH_DIR}")
else()
    message(STATUS
        "PS4 credential authentication disabled (TAIKO_PS4_AUTH_DIR is empty)")
endif()
