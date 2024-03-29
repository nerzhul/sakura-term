CMAKE_MINIMUM_REQUIRED (VERSION 2.9 FATAL_ERROR)
CMAKE_POLICY(SET CMP0005 NEW)
PROJECT (sakura)
SET (AUTHOR "David Gómez Espinosa" INTERNAL "Author")
SET (VERSION "3.6.0")
SET(CMAKE_CXX_STANDARD 17)

INCLUDE (FindPkgConfig)

IF (NOT PKG_CONFIG_FOUND)
	MESSAGE (FATAL_ERROR "pkg-config not found...")
ENDIF (NOT PKG_CONFIG_FOUND)

pkg_check_modules (GLIB REQUIRED glib-2.0>=2.40)
IF (NOT GLIB_FOUND)
	MESSAGE(FATAL_ERROR "You don't seem to have glib >= 2.40 development libraries installed...")
ENDIF (NOT GLIB_FOUND)

pkg_check_modules (GTK REQUIRED gtk+-3.0>=3.20)
IF (NOT GTK_FOUND)
	MESSAGE(FATAL_ERROR "You don't seem to have gtk >= 3.20 development libraries installed...")
ENDIF (NOT GTK_FOUND)

pkg_check_modules (GTKMM REQUIRED gtkmm-3.0>=3.0)
IF (GTKMM_FOUND)
	MESSAGE("Found gtkmm")
ELSE()
	MESSAGE(FATAL_ERROR "You don't seem to have gtkmm >= 3.0 development libraries installed...")
ENDIF()

pkg_check_modules (VTE REQUIRED vte-2.91>=0.50)
IF (NOT VTE_FOUND)
	MESSAGE(FATAL_ERROR "You don't seem to have vte >= 0.50 development libraries installed...")
ENDIF (NOT VTE_FOUND)

pkg_check_modules (X11 REQUIRED x11)
IF (NOT X11_FOUND)
	MESSAGE(FATAL_ERROR "You don't seem to have x11 development libraries installed...")
ENDIF (NOT X11_FOUND)

pkg_check_modules (YAMLCPP REQUIRED yaml-cpp)
IF (NOT YAMLCPP_FOUND)
	MESSAGE(FATAL_ERROR "You don't seem to have yaml-cpp library installed...")
ENDIF (NOT YAMLCPP_FOUND)

FIND_PROGRAM(POD2MAN pod2man)
MESSAGE ("pod2man executable is" ${POD2MAN})

ADD_DEFINITIONS (-DVERSION="${VERSION}")
ADD_DEFINITIONS (-DDATADIR="${CMAKE_INSTALL_PREFIX}/share")
ADD_DEFINITIONS (-DBUILDTYPE="${CMAKE_BUILD_TYPE}")

IF (${CMAKE_BUILD_TYPE} MATCHES "Debug")
	SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
ELSE (${CMAKE_BUILD_TYPE} NOT MATCHES "Debug")
	SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -Wno-deprecated-declarations")
ENDIF (${CMAKE_BUILD_TYPE} MATCHES "Debug")

include_directories(. ${GTK_INCLUDE_DIRS} ${GTKMM_INCLUDE_DIRS} ${VTE_INCLUDE_DIRS})
link_directories(
	${GTK_LIBRARY_DIRS}
	${GTKMM_LIBRARY_DIRS}
	${VTE_LIBRARY_DIRS}
	${X11_LIBRARY_DIRS}
)

add_compile_options(-Wall)

add_executable(sakura
	src/config.cpp
	src/main.cpp
	src/notebook.cpp
	src/sakura.cpp
	src/sakuraold.cpp
	src/terminal.cpp
	src/window.cpp)

target_link_libraries (sakura
	${GTK_LIBRARIES}
	${GTKMM_LIBRARIES}
	${VTE_LIBRARIES}
	${X11_LIBRARIES}
	${YAMLCPP_LIBRARIES}
	m
	stdc++fs)

#ADD_SUBDIRECTORY (po)

INSTALL (TARGETS sakura RUNTIME DESTINATION bin)
INSTALL (FILES sakura.desktop DESTINATION share/applications)
INSTALL (FILES terminal-tango.svg DESTINATION share/pixmaps)
IF (POD2MAN)
	INSTALL (FILES ${sakura_BINARY_DIR}/sakura.1 DESTINATION share/man/man1)
ENDIF (POD2MAN)
INSTALL (FILES INSTALL DESTINATION share/doc/sakura)

FILE (GLOB MO_FILES po/*.mo)

SET_DIRECTORY_PROPERTIES (PROPERTIES
	ADDITIONAL_MAKE_CLEAN_FILES "${MO_FILES};po/sakura.pot")
SET (EXECUTABLE_OUTPUT_PATH src)

ADD_CUSTOM_TARGET (distclean
	rm -f Makefile cmake_install.cmake CMakeCache.txt install_manifest.txt sakura.1
	COMMAND rm -f po/*.mo po/sakura.pot po/Makefile po/cmake_install.cmake
	COMMAND rm -rf CMakeFiles po/CMakeFiles)

IF (POD2MAN)
ADD_CUSTOM_TARGET(man ALL
	COMMAND ${POD2MAN} -u --section=1 --release=${VERSION} --center '' ${sakura_SOURCE_DIR}/sakura.pod sakura.1)
ENDIF (POD2MAN)
