CMAKE_MINIMUM_REQUIRED( VERSION 3.15 )
PROJECT( RiftRay3 )

INCLUDE(CTest)

INCLUDE(cmake_modules/InvokePython.cmake)
INVOKEPYTHON( "tools/hardcode_shaders.py" )
# Python script will dump generated headers to autogen/
INCLUDE_DIRECTORIES("autogen/")

ADD_DEFINITIONS( -DPROJECT_NAME="${PROJECT_NAME}" )
MESSAGE( "CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}" )

SET( USE_ANTTWEAKBAR TRUE CACHE BOOL "Use AntTweakBar" )

#
# Platform-dependent section
#
IF( WIN32 )
    MESSAGE(STATUS "MSVC_VERSION=${MSVC_VERSION}")

    #
    # Windows include and lib dirs: I drop everything into C:\lib\.
    #
    SET( LIBS_HOME "C:/lib" CACHE STRING "Location of support libraries Glut, Glew and others"  )

    # Use vcpkg for dependencies
    find_package(glm CONFIG REQUIRED)
    ADD_DEFINITIONS( -DGLM_FORCE_RADIANS )
    ADD_DEFINITIONS( -DGLM_ENABLE_EXPERIMENTAL )

    MESSAGE("Using GLFW Framework.")
    find_package(glfw3 CONFIG REQUIRED)
    SET( FRAMEWORK_LIBS glfw )

    find_package(GLEW REQUIRED)
    # Using dynamic GLEW from vcpkg

    find_package(OpenXR CONFIG REQUIRED)
    SET( OPENXR_LIBS OpenXR::openxr_loader )
    ADD_DEFINITIONS( -DRIFTRAY_HAS_OPENXR )

    IF( USE_ANTTWEAKBAR )
        MESSAGE("Using AntTweakBar.")
        SET( ANTTWEAKBAR_ROOT "${PROJECT_SOURCE_DIR}/AntTweakBar" CACHE STRING "AntTweakBar root" )
        INCLUDE_DIRECTORIES( "${ANTTWEAKBAR_ROOT}/include" )
        LINK_DIRECTORIES   ( "${ANTTWEAKBAR_ROOT}/lib" )
        SET( ANT_LIBS AntTweakBar64.lib )
        ADD_DEFINITIONS( -DUSE_ANTTWEAKBAR )
    ENDIF( USE_ANTTWEAKBAR )

    SET( PLATFORM_LIBS
        opengl32.lib glu32.lib
        ${OPENXR_LIBS}
        ${ANT_LIBS}
        Winmm.lib
        )

    ADD_DEFINITIONS( /W4 -D_CRT_SECURE_NO_WARNINGS )

    # Copy DLLs of the included libraries into output directories.
    INVOKEPYTHON( "tools/copyDLLs.py"
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_BINARY_DIR}
        "GLEW_ROOT=${GLEW_ROOT}"
        "ANTTWEAKBAR_ROOT=${ANTTWEAKBAR_ROOT}"
        )
ENDIF()

#
# Platform-independent section
#
FILE( GLOB_RECURSE UTIL_SOURCE_FILES
    src/Util/*.cpp
    src/Util/*.h
    )

FILE( GLOB_RECURSE SCENE_SOURCE_FILES
    src/Scene/*.cpp
    src/Scene/*.h
    )

INCLUDE_DIRECTORIES("src/Util")
INCLUDE_DIRECTORIES("src/Util/GL")
INCLUDE_DIRECTORIES("src/Util/third_party")
INCLUDE_DIRECTORIES("src/Scene")

ADD_LIBRARY( Util        ${UTIL_SOURCE_FILES} )
TARGET_LINK_LIBRARIES( Util PUBLIC glm::glm GLEW::glew glfw )

ADD_LIBRARY( Scene       ${SCENE_SOURCE_FILES} )
TARGET_LINK_LIBRARIES( Scene PUBLIC glm::glm GLEW::glew glfw )

ADD_EXECUTABLE( ${PROJECT_NAME} src/main_glfw_openxr.cpp )

TARGET_LINK_LIBRARIES( ${PROJECT_NAME}
    Scene
    Util
    glm::glm
    GLEW::glew
    glfw
    ${PLATFORM_LIBS}
    )

IF( BUILD_TESTING )
    ADD_EXECUTABLE( MatrixFunctionsTests tests/MatrixFunctionsTests.cpp )
    TARGET_LINK_LIBRARIES( MatrixFunctionsTests PRIVATE Util )
    ADD_TEST( NAME MatrixFunctions COMMAND MatrixFunctionsTests )
ENDIF()

# Copy required DLLs to output directory
ADD_CUSTOM_COMMAND(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/AntTweakBar/lib/AntTweakBar64.dll"
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VCPKG_INSTALLED_DIR}/x64-windows/bin/glew32.dll"
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VCPKG_INSTALLED_DIR}/x64-windows/bin/glfw3.dll"
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMENT "Copying DLLs to output directory"
)

ADD_CUSTOM_COMMAND(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VCPKG_INSTALLED_DIR}/x64-windows/bin/openxr_loader.dll"
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMENT "Copying OpenXR loader to output directory"
)

# Copy shaders and textures directories for both Debug and Release builds
ADD_CUSTOM_COMMAND(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_SOURCE_DIR}/textures"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/textures"
    COMMENT "Copying shaders and textures to output directory"
)
