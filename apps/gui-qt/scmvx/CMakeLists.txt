SET(PACKAGE_NAME MAPVIEWX)

SUBDIRS(seiscomp plugins)

SET(APP_NAME scmvx)

INCLUDE_DIRECTORIES(.)

SET(
	${PACKAGE_NAME}_SOURCES
		map/stationsymbol.cpp
		map/eventheatlayer.cpp
		map/networklayer.cpp
		map/eventlayer.cpp
		map/currenteventlayer.cpp
		map/recenteventslayer.cpp
		map/scalelayer.cpp
		settings.cpp
		eventinfodialog.cpp
		main.cpp
		mainwindow.cpp
		processor.cpp
		searchwidget.cpp
		stationinfodialog.cpp
)

SET(
	${PACKAGE_NAME}_HEADERS
)


SET(
	${PACKAGE_NAME}_MOC_HEADERS
		map/eventheatlayer.h
		map/networklayer.h
		map/eventlayer.h
		map/currenteventlayer.h
		map/recenteventslayer.h
		map/scalelayer.h
		app.h
		eventinfodialog.h
		mainwindow.h
		searchwidget.h
		stationinfodialog.h
)

SET(
	${PACKAGE_NAME}_UI
		eventinfodialog.ui
		mainwindow.ui
		searchwidget.ui
		stationinfodialog.ui
)


SET(
	${PACKAGE_NAME}_RESOURCES
		resources.qrc
)


FILE(GLOB descs "${CMAKE_CURRENT_SOURCE_DIR}/descriptions/*.xml")
INSTALL(FILES ${descs} DESTINATION ${SC3_PACKAGE_APP_DESC_DIR})
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR})

SC_ADD_GUI_EXECUTABLE(${PACKAGE_NAME} ${APP_NAME})
SC_ADD_VERSION(${PACKAGE_NAME} ${APP_NAME})
SC_LINK_LIBRARIES_INTERNAL(${APP_NAME} qt mvx)
#SC_LINK_LIBRARIES(${APP_NAME} ${PYTHON_LIBRARIES})
