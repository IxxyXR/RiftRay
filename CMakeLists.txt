CMAKE_MINIMUM_REQUIRED( VERSION 2.6 )
PROJECT( RiftRay3 )

INCLUDE(cmake_modules/InvokePython.cmake)
INVOKEPYTHON( "tools/hardcode_shaders.py" )
# Python script will dump generated headers to autogen/
INCLUDE_DIRECTORIES("autogen/")

ADD_DEFINITIONS( -DPROJECT_NAME="${PROJECT_NAME}" )
MESSAGE( "CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}" )

SET( USE_ANTTWEAKBAR TRUE CACHE BOOL "Use AntTweakBar" )
SET( USE_OCULUSSDK TRUE CACHE BOOL "Use Oculus SDK" )

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

    IF( USE_OCULUSSDK )
        SET( OCULUSSDK_ROOT "${PROJECT_SOURCE_DIR}/ovr_sdk_win_32.0.0" CACHE STRING "OculusSDK root" ) # OVR SDK 32.0
        INCLUDE_DIRECTORIES( "${OCULUSSDK_ROOT}/LibOVR/Include" )
        INCLUDE_DIRECTORIES( "${OCULUSSDK_ROOT}/LibOVR/Src" )
        INCLUDE_DIRECTORIES( "${OCULUSSDK_ROOT}/LibOVRKernel/Src" )
        IF( MSVC_VERSION STREQUAL "1600" )
            SET( VS_DIR "VS2010" )
        ELSEIF( MSVC_VERSION STREQUAL "1700" )
            SET( VS_DIR "VS2012" )
        ELSEIF( MSVC_VERSION STREQUAL "1800" )
            SET( VS_DIR "VS2013" )
        ELSEIF( MSVC_VERSION STREQUAL "1900" )
            SET( VS_DIR "VS2015" )
        ELSEIF( MSVC_VERSION STREQUAL "1916" OR MSVC_VERSION STREQUAL "1920" OR MSVC_VERSION STREQUAL "1930" OR MSVC_VERSION STREQUAL "1940" OR MSVC_VERSION GREATER_EQUAL "1930" )
            SET( VS_DIR "VS2017" )
        ENDIF()
        IF( DEBUG )
            LINK_DIRECTORIES( "${OCULUSSDK_ROOT}/LibOVR/Lib/Windows/x64/Release/${VS_DIR}" )
        ELSE()
            LINK_DIRECTORIES( "${OCULUSSDK_ROOT}/LibOVR/Lib/Windows/x64/Release/${VS_DIR}" )
        ENDIF()
        SET( OVR_LIBS LibOVR.lib )
        ADD_DEFINITIONS( -DUSE_OCULUSSDK )
    ENDIF()

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
        ${OVR_LIBS}
        ${ANT_LIBS}
        Winmm.lib
        )

    ADD_DEFINITIONS( /W4 -D_CRT_SECURE_NO_WARNINGS )

    # Copy DLLs of the included libraries into output directories.
    INVOKEPYTHON( "tools/copyDLLs.py"
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_BINARY_DIR}
        "OCULUSSDK_ROOT=${OCULUSSDK_ROOT}"
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

ADD_EXECUTABLE( ${PROJECT_NAME} src/main_glfw_ovrsdk13.cpp )

TARGET_LINK_LIBRARIES( ${PROJECT_NAME}
    Scene
    Util
    glm::glm
    GLEW::glew
    glfw
    ${PLATFORM_LIBS}
    )

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
