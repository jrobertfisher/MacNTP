cmake_minimum_required(VERSION 3.15)
project(MacNTP C)

set(WARNINGS "-Wall;-Wno-pointer-sign")

# --- MacNTP Startup: the application that does the network work ---------
add_application(MacNTPStartup
    startup.c
    net.c
    common.c
    mpwstubs.c
    startup.r
    CREATOR "MNtp"
    )
target_link_libraries(MacNTPStartup OpenTransportApp OpenTransport OpenTptInet)
set_target_properties(MacNTPStartup PROPERTIES
    COMPILE_OPTIONS "-ffunction-sections;-fdata-sections;-Os;${WARNINGS}"
    LINK_FLAGS "-Wl,-gc-sections")

# --- MacNTP control panel: a 'cdev' code resource ------------------------
add_executable(MacNTPcdev cdev.c common.c)
set_target_properties(MacNTPcdev PROPERTIES
    OUTPUT_NAME MacNTPcdev.flt
    COMPILE_OPTIONS "-ffunction-sections;-Os;${WARNINGS}"
    LINK_FLAGS "-Wl,--mac-flat -Wl,-eMACNTPCDEV -Wl,-gc-sections")

add_custom_command(
    OUTPUT MacNTP.bin MacNTP.dsk
    COMMAND ${REZ} -I${REZ_INCLUDE_PATH} -I${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/cdev.r
            -o MacNTP.bin --cc MacNTP.dsk -t cdev -c MNtc
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS MacNTPcdev cdev.r icons.r)
add_custom_target(MacNTP_cdev ALL DEPENDS MacNTP.bin)

# --- Set the Finder's hasBundle flag so both files show their icons ------
add_custom_target(MacNTP_bundle_flags ALL
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/set_bundle_bit.py MacNTP.bin MacNTPStartup.bin
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_dependencies(MacNTP_bundle_flags MacNTP_cdev MacNTPStartup_APPL)

# --- MacNTP Installer: one application containing both files -----------
add_custom_command(
    OUTPUT MacNTP.rsrc MacNTP.data MacNTPStartup.rsrc MacNTPStartup.data
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/extract_fork.py MacNTP.bin MacNTP
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/extract_fork.py MacNTPStartup.bin MacNTPStartup
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/MacNTP.bin ${CMAKE_CURRENT_BINARY_DIR}/MacNTPStartup.bin)
add_custom_command(
    OUTPUT installer_res.rsrc.bin
    COMMAND ${REZ} ${CMAKE_CURRENT_SOURCE_DIR}/installer.r -I${REZ_INCLUDE_PATH} -I${CMAKE_CURRENT_SOURCE_DIR}
            -o installer_res.rsrc.bin
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS installer.r icons.r MacNTP.rsrc MacNTP.data MacNTPStartup.rsrc MacNTPStartup.data)
add_custom_target(MacNTP_installer_res DEPENDS installer_res.rsrc.bin)
add_dependencies(MacNTP_installer_res MacNTP_cdev MacNTPStartup_APPL)

add_application(MacNTPInstaller
    installer.c
    common.c
    ${CMAKE_CURRENT_BINARY_DIR}/installer_res.rsrc.bin
    CREATOR "MNti")
set_target_properties(MacNTPInstaller PROPERTIES
    COMPILE_OPTIONS "-ffunction-sections;-Os;${WARNINGS}"
    LINK_FLAGS "-Wl,-gc-sections")
add_dependencies(MacNTPInstaller_APPL MacNTP_installer_res)

add_custom_target(MacNTP_installer_flags ALL
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/set_bundle_bit.py MacNTPInstaller.bin
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_dependencies(MacNTP_installer_flags MacNTPInstaller_APPL)
