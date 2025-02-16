cmake_minimum_required(VERSION 3.15)
project(usb-monitor VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 COMPONENTS 
    Core 
    Widgets 
    Charts 
    Network 
    Sql 
    DBus
    REQUIRED
)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
find_package(Boost COMPONENTS system filesystem REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)

set(SOURCES
    src/main.cpp
    src/core/DeviceManager.cpp
    src/core/UsbDevice.cpp
    src/core/PowerManager.cpp
    src/core/BandwidthMonitor.cpp
    src/gui/MainWindow.cpp
    src/gui/DeviceTreeWidget.cpp
    src/gui/TopologyView.cpp
    src/gui/SystemTrayIcon.cpp
    src/security/DeviceAuthorizer.cpp
    src/security/SecurityManager.cpp
    src/analysis/ProtocolAnalyzer.cpp
    src/analysis/BenchmarkTool.cpp
    src/utils/ConfigManager.cpp
    src/utils/ExportManager.cpp
)

set(RESOURCES
    resources/resources.qrc
)

add_executable(${PROJECT_NAME} 
    ${SOURCES}
    ${RESOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${LIBUSB_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${SQLite3_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt5::Core
    Qt5::Widgets
    Qt5::Charts
    Qt5::Network
    Qt5::Sql
    Qt5::DBus
    ${LIBUSB_LIBRARIES}
    ${Boost_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    SQLite::SQLite3
)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

install(DIRECTORY resources/config/
    DESTINATION etc/${PROJECT_NAME}
)

enable_testing()
add_subdirectory(tests)

find_package(Doxygen)
if(DOXYGEN_FOUND)
    add_subdirectory(docs)
endif()
