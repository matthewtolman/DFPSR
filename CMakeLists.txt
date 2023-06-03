cmake_minimum_required(VERSION 3.18)
project(DFPSR)

if( CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR )
    set(MAIN_PROJECT ON)
else()
    set(MAIN_PROJECT OFF)
endif ()

if (MAIN_PROJECT)
    set(CMAKE_CXX_STANDARD 14)
elseif (CMAKE_CXX_STANDARD LESS 14)
    message(FATAL_ERROR "DFPSR requires a CXX standard of 14 or higher")
endif()

option(NO_SOUND "If 'ON', then will disable sound output. Defaults to OFF" OFF)
option(NO_WINDOW_MANAGER "If 'ON', then will disable window manager. Defaults to OFF" OFF)

IF (NO_SOUND)
    set(SOUND_SOURCES Source/SoundManagers/NoSound.cpp)
ELSEIF (WIN32)
    # set stuff for windows
    set(SOUND_SOURCES Source/SoundManagers/WinMMSound.cpp)
ELSEIF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    # set stuff for Mac
    set(SOUND_SOURCES Source/SoundManagers/NoSound.cpp)
ELSE()
    # set stuff for other systems
    set(SOUND_SOURCES Source/SoundManagers/AlsaSound.cpp)
ENDIF()

IF (NO_WINDOW_MANAGER)
    set(WINDOW_MANAGER_SOURCES Source/windowManagers/NoWindow.cpp)
ELSEIF (WIN32)
    set(WINDOW_MANAGER_SOURCES Source/windowManagers/Win32Window.cpp)
ELSEIF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    # set stuff for Mac
    set(WINDOW_MANAGER_SOURCES Source/windowManagers/NoWindow.cpp)
ELSE()
    # set stuff for other systems
    set(WINDOW_MANAGER_SOURCES Source/windowManagers/X11Window.cpp)
ENDIF()

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    add_compile_options(-DDARWIN=1)
endif()

FIND_PACKAGE(Threads REQUIRED)

add_library(
        ${PROJECT_NAME}-sound STATIC
        ${SOUND_SOURCES}
)
target_include_directories(${PROJECT_NAME}-sound PUBLIC Source/soundManagers)

add_library(
        ${PROJECT_NAME}-window STATIC
        ${WINDOW_MANAGER_SOURCES}
)
target_include_directories(${PROJECT_NAME}-window PUBLIC Source/windowManagers)

add_library(
        ${PROJECT_NAME}-DFPSR STATIC
        Source/DFPSR/base/allocator.cpp
        Source/DFPSR/base/SafePointer.cpp
        Source/DFPSR/base/threading.cpp
        Source/DFPSR/api/bufferAPI.cpp
        Source/DFPSR/api/configAPI.cpp
        Source/DFPSR/api/drawAPI.cpp
        Source/DFPSR/api/fileAPI.cpp
        Source/DFPSR/api/filterAPI.cpp
        Source/DFPSR/api/fontAPI.cpp
        Source/DFPSR/api/guiAPI.cpp
        Source/DFPSR/api/imageAPI.cpp
        Source/DFPSR/api/mediaMachineAPI.cpp
        Source/DFPSR/api/modelAPI.cpp
        Source/DFPSR/api/stringAPI.cpp
        Source/DFPSR/api/timeAPI.cpp
        Source/DFPSR/api/types.cpp
        Source/DFPSR/collection/collections.cpp
        Source/DFPSR/font/Font.cpp
        Source/DFPSR/gui/BackendWindow.cpp
        Source/DFPSR/gui/DsrWindow.cpp
        Source/DFPSR/gui/FlexRegion.cpp
        Source/DFPSR/gui/InputEvent.cpp
        Source/DFPSR/gui/VisualComponent.cpp
        Source/DFPSR/gui/VisualTheme.cpp
        Source/DFPSR/gui/components/Button.cpp
        Source/DFPSR/gui/components/Label.cpp
        Source/DFPSR/gui/components/ListBox.cpp
        Source/DFPSR/gui/components/Menu.cpp
        Source/DFPSR/gui/components/Panel.cpp
        Source/DFPSR/gui/components/Picture.cpp
        Source/DFPSR/gui/components/TextBox.cpp
        Source/DFPSR/gui/components/Toolbar.cpp
        Source/DFPSR/gui/components/helpers/ScrollBarImpl.cpp
        Source/DFPSR/image/Color.cpp
        Source/DFPSR/image/draw.cpp
        Source/DFPSR/image/Image.cpp
        Source/DFPSR/image/ImageF32.cpp
        Source/DFPSR/image/ImageRgbaU8.cpp
        Source/DFPSR/image/ImageU8.cpp
        Source/DFPSR/image/ImageU16.cpp
        Source/DFPSR/image/stbImage/stbImageWrapper.cpp
        Source/DFPSR/machine/mediaFilters.cpp
        Source/DFPSR/machine/VirtualMachine.cpp
        Source/DFPSR/math/FixedPoint.cpp
        Source/DFPSR/persistent/ClassFactory.cpp
        Source/DFPSR/persistent/atomic/PersistentBoolean.cpp
        Source/DFPSR/persistent/atomic/PersistentColor.cpp
        Source/DFPSR/persistent/atomic/PersistentImage.cpp
        Source/DFPSR/persistent/atomic/PersistentInteger.cpp
        Source/DFPSR/persistent/atomic/PersistentString.cpp
        Source/DFPSR/persistent/atomic/PersistentStringList.cpp
        Source/DFPSR/render/ITriangle2D.cpp
        Source/DFPSR/render/renderCore.cpp
        Source/DFPSR/render/ResourcePool.cpp
        Source/DFPSR/render/model/Model.cpp
        Source/DFPSR/render/model/format/dmf1.cpp
        Source/DFPSR/render/shader/Shader.cpp
)
target_include_directories(${PROJECT_NAME}-DFPSR PUBLIC Source/DFPSR)
target_link_libraries(${PROJECT_NAME}-DFPSR PUBLIC Threads::Threads ${PROJECT_NAME}-sound ${PROJECT_NAME}-window)

if (MAIN_PROJECT)
    enable_testing()
    macro(DFPSR_TEST TEST_NAME)
        add_executable(${PROJECT_NAME}-tests-${TEST_NAME} ${ARGN})

        target_link_libraries(${PROJECT_NAME}-tests-${TEST_NAME} PRIVATE ${PROJECT_NAME}-DFPSR)
        add_test(
                NAME ${TEST_NAME}
                COMMAND ${PROJECT_NAME}-tests-${TEST_NAME}
        )
    endmacro()

    add_custom_command(TARGET ${PROJECT_NAME}-DFPSR POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/Source/test/tests/resources ${CMAKE_BINARY_DIR}/test/tests/resources
    )

    DFPSR_TEST(buffer Source/test/tests/BufferTest.cpp)
    DFPSR_TEST(dataLoop Source/test/tests/DataLoopTest.cpp)
    DFPSR_TEST(draw Source/test/tests/DrawTest.cpp)
    DFPSR_TEST(file Source/test/tests/FileTest.cpp)
    DFPSR_TEST(fixedPoint Source/test/tests/FixedPointTest.cpp)
    DFPSR_TEST(flex Source/test/tests/FlexTest.cpp)
    DFPSR_TEST(imageProcessing Source/test/tests/ImageProcessingTest.cpp)
    DFPSR_TEST(image Source/test/tests/ImageTest.cpp)
    DFPSR_TEST(list Source/test/tests/ListTest.cpp)
    DFPSR_TEST(persistent Source/test/tests/PersistentTest.cpp)
    DFPSR_TEST(pixel Source/test/tests/PixelTest.cpp)
    DFPSR_TEST(safePointer Source/test/tests/SafePointerTest.cpp)
    DFPSR_TEST(simd Source/test/tests/SimdTest.cpp)
    DFPSR_TEST(string Source/test/tests/StringTest.cpp)
    DFPSR_TEST(textEncoding Source/test/tests/TextEncodingTest.cpp)
    DFPSR_TEST(thread Source/test/tests/ThreadTest.cpp)
    DFPSR_TEST(vector Source/test/tests/VectorTest.cpp)
endif()