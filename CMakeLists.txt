cmake_minimum_required(VERSION 3.16)

project(MagicCounting VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 REQUIRED COMPONENTS Widgets)
find_program(LRELEASE_EXECUTABLE lrelease REQUIRED)

set(TS_FILES
    translations/magiccounting_fi.ts
)

set(QM_FILES)
foreach(TS_FILE ${TS_FILES})
    get_filename_component(TS_NAME ${TS_FILE} NAME_WE)
    set(QM_FILE "${CMAKE_CURRENT_BINARY_DIR}/${TS_NAME}.qm")
    add_custom_command(
        OUTPUT ${QM_FILE}
        COMMAND ${LRELEASE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/${TS_FILE} -qm ${QM_FILE}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${TS_FILE}
        VERBATIM
    )
    list(APPEND QM_FILES ${QM_FILE})
endforeach()

add_executable(magiccounting
    src/AccountListDialog.cpp
    src/AccountListDialog.h
    src/AppConfig.cpp
    src/AppConfig.h
    src/DataStore.cpp
    src/DataStore.h
    src/LanguageManager.cpp
    src/LanguageManager.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/SplitEditorDialog.cpp
    src/SplitEditorDialog.h
    src/main.cpp
)

target_link_libraries(magiccounting PRIVATE Qt5::Widgets)
target_compile_definitions(magiccounting PRIVATE
    MAGICCOUNTING_TRANSLATIONS_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)

add_custom_target(translations ALL DEPENDS ${QM_FILES})
add_dependencies(magiccounting translations)

configure_file(
    share/applications/magiccounting.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/magiccounting.desktop
    @ONLY
)

install(TARGETS magiccounting RUNTIME DESTINATION bin)
install(FILES ${QM_FILES} DESTINATION share/magiccounting/translations)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/magiccounting.desktop DESTINATION share/applications)
install(FILES share/mime/packages/magiccounting.xml DESTINATION share/mime/packages)
