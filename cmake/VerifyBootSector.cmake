if(NOT DEFINED INPUT)
    message(FATAL_ERROR "INPUT is required")
endif()

if(NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "Boot sector does not exist: ${INPUT}")
endif()

file(SIZE "${INPUT}" BOOT_SECTOR_SIZE)
if(NOT BOOT_SECTOR_SIZE EQUAL 512)
    message(FATAL_ERROR "Boot sector must be exactly 512 bytes, got ${BOOT_SECTOR_SIZE}")
endif()

file(READ "${INPUT}" BOOT_SECTOR_HEX HEX)
string(SUBSTRING "${BOOT_SECTOR_HEX}" 1020 4 BOOT_SIGNATURE)
string(TOLOWER "${BOOT_SIGNATURE}" BOOT_SIGNATURE)
if(NOT BOOT_SIGNATURE STREQUAL "55aa")
    message(FATAL_ERROR "Boot sector signature must be 55aa, got ${BOOT_SIGNATURE}")
endif()

message(STATUS "Boot sector verified: 512 bytes, signature 55aa")
