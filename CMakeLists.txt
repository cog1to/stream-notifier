cmake_minimum_required(VERSION 3.9)
project(stream-notifier VERSION 0.1 DESCRIPTION "Super basic Twitch stream notifier in C.")
include(GNUInstallDirs)

# Find Glib
find_package(PkgConfig REQUIRED)
pkg_check_modules(NOTIFY REQUIRED libnotify)
pkg_check_modules(TWITCH REQUIRED libctwitch)

# Example target
add_executable(${PROJECT_NAME}
  src/daemon.c
  src/strings.c
)

target_link_libraries(${PROJECT_NAME} ${NOTIFY_LIBRARIES} ${TWITCH_LIBRARIES} -pthread)
target_include_directories(${PROJECT_NAME} PUBLIC ${NOTIFY_INCLUDE_DIRS} ${TWITCH_INCLUDE_DIRS})
target_compile_options(${PROJECT_NAME} PUBLIC ${NOTIFY_CFLAGS_OTHER} ${TWITCH_CFLAGS_OTHER})

# Project settings
set_target_properties(${PROJECT_NAME} PROPERTIES
  VERSION ${PROJECT_VERSION}
)

# Install executable
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

