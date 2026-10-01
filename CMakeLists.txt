cmake_minimum_required(VERSION 3.21)

project(
    PlayerKnockdownImmunity
    VERSION 0.1.0
    LANGUAGES CXX)

find_package(directxtk CONFIG REQUIRED)
find_package(CommonLibSSE CONFIG REQUIRED)

add_commonlibsse_plugin(
    ${PROJECT_NAME}
    NAME "PlayerKnockdownImmunity"
    AUTHOR "Raziell74"
    VERSION ${PROJECT_VERSION}
    USE_ADDRESS_LIBRARY
    SOURCES
        src/plugin.cpp
        src/Settings/Settings.cpp)

# SKSEPlugin_Version / SKSEPlugin_Query are declared in src/plugin.cpp as
# PluginVersionData so AE 1.7.99 gets AddressLibraryV5. The generated
# SKSEPluginInfo uses PluginDeclaration, which overwrites that flag.
set_source_files_properties(
    "${CMAKE_CURRENT_BINARY_DIR}/__${PROJECT_NAME}Plugin.cpp"
    PROPERTIES HEADER_FILE_ONLY TRUE)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /EHsc /permissive- /Zc:preprocessor /W4)
endif()

set(OUTPUT_FOLDER "G:/MO2SSE/mods/Player Knockdown Immunity SKSE")
set(DLL_FOLDER "${OUTPUT_FOLDER}/SKSE/Plugins")
message(STATUS "SKSE plugin output folder: ${DLL_FOLDER}")

add_custom_command(
    TARGET "${PROJECT_NAME}"
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:${PROJECT_NAME}>"
        "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_SOURCE_DIR}/PlayerKnockdownImmunity.ini"
        "${DLL_FOLDER}/PlayerKnockdownImmunity.ini"
    VERBATIM
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_PDB_FILE:${PROJECT_NAME}>"
            "${DLL_FOLDER}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
        VERBATIM
    )
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(RELEASES_DIR "${CMAKE_SOURCE_DIR}/releases")
    set(RELEASE_STAGING "${RELEASES_DIR}/${PROJECT_NAME} v${PROJECT_VERSION}")
    set(RELEASE_ZIP "${RELEASES_DIR}/${PROJECT_NAME} v${PROJECT_VERSION}.zip")
    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${RELEASES_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${RELEASE_STAGING}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${RELEASE_STAGING}"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${OUTPUT_FOLDER}"
            "${RELEASE_STAGING}"
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${RELEASE_STAGING}/meta.ini"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${RELEASE_STAGING}/.git"
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${RELEASE_STAGING}/.gitignore"
        VERBATIM
    )
    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${RELEASE_ZIP}"
        COMMAND "${CMAKE_COMMAND}" -E tar cf "${RELEASE_ZIP}" --format=zip -- .
        WORKING_DIRECTORY "${RELEASE_STAGING}"
        VERBATIM
    )
    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${RELEASE_STAGING}"
        VERBATIM
    )
endif()
