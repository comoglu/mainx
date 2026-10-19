/***************************************************************************
 * Copyright (C) gempa GmbH                                                *
 * All rights reserved.                                                    *
 * Contact: gempa GmbH (seiscomp-dev@gempa.de)                             *
 *                                                                         *
 * GNU Affero General Public License Usage                                 *
 * This file may be used under the terms of the GNU Affero                 *
 * Public License version 3.0 as published by the Free Software Foundation *
 * and appearing in the file LICENSE included in the packaging of this     *
 * file. Please review the following information to ensure the GNU Affero  *
 * Public License version 3.0 requirements will be met:                    *
 * https://www.gnu.org/licenses/agpl-3.0.html.                             *
 *                                                                         *
 * Other Usage                                                             *
 * Alternatively, this file may be used in accordance with the terms and   *
 * conditions contained in a signed written agreement between you and      *
 * gempa GmbH.                                                             *
 ***************************************************************************/


#define SEISCOMP_COMPONENT MapView

#include <seiscomp/logging/log.h>
#include <algorithm>

#include <seiscomp/core/datamessage.h>
#include <seiscomp/core/message.h>
#include <seiscomp/datamodel/configmodule.h>
#include <seiscomp/datamodel/configstation.h>
#include <seiscomp/datamodel/creationinfo.h>
#include <seiscomp/datamodel/notifier.h>
#include <seiscomp/gui/core/application.h>
#include <seiscomp/gui/core/utils.h>
#include <seiscomp/gui/datamodel/eventlayer.h>
#include <seiscomp/gui/datamodel/origindialog.h>
#include <seiscomp/datamodel/utils.h>
#include <seiscomp/io/archive/xmlarchive.h>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QList>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QScreen>
#include <QScrollArea>
#include <QStringList>
#include <QStyle>
#include <QTextStream>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <QWidgetAction>

#include "mainwindow.h"
#include "searchwidget.h"
#include "eventinfodialog.h"
#include "stationinfodialog.h"
#include "map/networklayer.h"
#include "map/eventlayer.h"
#include "map/eventheatlayer.h"
#include "map/recenteventslayer.h"
#include "map/currenteventlayer.h"
#include "map/scalelayer.h"


using namespace std;
using namespace Seiscomp;

namespace dm = Seiscomp::DataModel;


// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
namespace Seiscomp {
namespace MapViewX {
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::Event::setEvent(DataModel::Event *evt, ObjectCache &cache) {
	event = evt;
	preferredOrigin = cache.get<dm::Origin>(evt->preferredOriginID());
	preferredMagnitude = cache.get<dm::Magnitude>(evt->preferredMagnitudeID());
	preferredFocalMechanism = cache.get<dm::FocalMechanism>(evt->preferredFocalMechanismID());
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool MainWindow::Event::isMoreRecent(const Event &other) const {
	if ( !isValid() ) {
		return false;
	}
	if ( !other ) {
		return true;
	}
	if ( event->publicID() == other.event->publicID() ) {
		return false;
	}
	return preferredOrigin->time().value() > other.preferredOrigin->time().value();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::Event::reset() {
	event.reset();
	preferredOrigin.reset();
	preferredMagnitude.reset();
	preferredFocalMechanism.reset();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
QPointF MainWindow::Event::location() const {
	if ( preferredOrigin )
		return QPointF(preferredOrigin->longitude(), preferredOrigin->latitude());
	else
		return QPointF();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
MainWindow::MainWindow(QWidget *parent, Qt::WindowFlags f)
: Gui::MainWindow(parent, f)
, _currentTraces(nullptr) {
	SCScheme.colors.legend.background.setAlpha(192);

	qRegisterMetaType<string>("std::string");

	_ui.setupUi(this);
	_ui.actionCenterMapOnEventUpdate->setChecked(global.centerOrigins);

	_cache.setDatabaseArchive(SCApp->query());
	_cache.setBufferSize(100);

	connect(SCApp, SIGNAL(messageAvailable(Seiscomp::Core::Message*,Seiscomp::Client::Packet*)),
	        this, SLOT(handleMessage(Seiscomp::Core::Message*)));
	connect(SCApp, SIGNAL(addObject(QString,Seiscomp::DataModel::Object*)),
	        this, SLOT(addObject(QString,Seiscomp::DataModel::Object*)));

	_actionToggleFullScreen->disconnect();
	connect(_actionToggleFullScreen, SIGNAL(triggered()), this, SLOT(toggleFullScreen()));

	connect(_ui.actionOpenFile, SIGNAL(triggered()), this, SLOT(openFile()));
	connect(_ui.actionQuit, SIGNAL(triggered()), this, SLOT(close()));
	connect(_ui.tabWidget, SIGNAL(currentChanged(int)), this, SLOT(switchTab(int)));

	_mapWidget = new MapWidget(SCApp->mapsDesc());
	_mapWidget->installEventFilter(this);
	_mapWidget->canvas().setLegendMargin(9);

	_eventListView = new Gui::EventListView(SCApp->query(), false);
	connect(SCApp, SIGNAL(messageAvailable(Seiscomp::Core::Message*,Seiscomp::Client::Packet*)),
	        _eventListView, SLOT(messageAvailable(Seiscomp::Core::Message*,Seiscomp::Client::Packet*)));

	connect(SCApp, SIGNAL(notifierAvailable(Seiscomp::DataModel::Notifier*)),
	        _eventListView, SLOT(notifierAvailable(Seiscomp::DataModel::Notifier*)));

	connect(_eventListView, SIGNAL(eventSelected(Seiscomp::DataModel::Event*)),
	        this, SLOT(selectEventFromList(Seiscomp::DataModel::Event*)));

	_ui.tabNetwork->setLayout(new QVBoxLayout);
	_ui.tabGM->setLayout(new QVBoxLayout);
	_ui.tabQC->setLayout(new QVBoxLayout);
	_ui.tabEvents->setLayout(new QVBoxLayout);

	_ui.tabEvents->layout()->addWidget(_eventListView);

	_ui.actionShowLatestEvent->setChecked(global.showLatestEvent);

	_currentEventLayer = new CurrentEventLayer(_mapWidget);
	_currentEventLayer->setVisible(_ui.actionShowLatestEvent->isChecked());
	connect(_currentEventLayer, SIGNAL(clicked(std::string)),
	        this, SLOT(showCurrentEventDetails(std::string)),
	        Qt::QueuedConnection);

	connect(_ui.actionShowGrayscale, SIGNAL(toggled(bool)), _mapWidget, SLOT(setGrayScale(bool)));
	connect(_ui.actionShowLatestEvent, SIGNAL(toggled(bool)), _currentEventLayer, SLOT(setVisible(bool)));

	_recentEventsLayer = new RecentEventsLayer(_mapWidget);
	_recentEventsLayer->setVisible(global.recentEventCount > 0);
	if ( global.recentEventPosition == "topleft" ) {
		_recentEventsLayer->legend()->setArea(Qt::AlignLeft | Qt::AlignTop);
	}
	else if ( global.recentEventPosition == "topright" ) {
		_recentEventsLayer->legend()->setArea(Qt::AlignRight | Qt::AlignTop);
	}
	else if ( global.recentEventPosition == "bottomright" ) {
		_recentEventsLayer->legend()->setArea(Qt::AlignRight | Qt::AlignBottom);
	}
	connect(_recentEventsLayer, SIGNAL(clicked(std::string)),
	        this, SLOT(centerOnEvent(std::string)),
	        Qt::QueuedConnection);
	connect(_recentEventsLayer, SIGNAL(doubleClicked(std::string)),
	        this, SLOT(showCurrentEventDetails(std::string)),
	        Qt::QueuedConnection);

	auto actionShowRecentEvents = new QAction(tr("Show recent events on map"), this);
	actionShowRecentEvents->setCheckable(true);
	actionShowRecentEvents->setChecked(_recentEventsLayer->isVisible());
	actionShowRecentEvents->setEnabled(global.recentEventCount > 0);
	_ui.menuView->insertAction(_ui.actionShowMapLegend, actionShowRecentEvents);
	connect(actionShowRecentEvents, SIGNAL(toggled(bool)), _recentEventsLayer, SLOT(setVisible(bool)));
	connect(_ui.actionShowMapLegend, SIGNAL(toggled(bool)), _mapWidget, SLOT(setDrawLegends(bool)));
	connect(&_mapWidget->canvas(), SIGNAL(legendVisibilityChanged(bool)), _ui.actionShowMapLegend, SLOT(setChecked(bool)));
	_mapWidget->addAction(_actionToggleFullScreen);

	_eventHeatLayer = new EventHeatLayer(_mapWidget);
	_eventHeatLayer->setVisible(false);

	connect(_eventListView, SIGNAL(reset()), _eventHeatLayer, SLOT(clear()));
	connect(_eventListView, SIGNAL(eventAddedToList(Seiscomp::DataModel::Event*,bool)),
	        _eventHeatLayer, SLOT(addEvent(Seiscomp::DataModel::Event*,bool)));
	connect(_eventListView, SIGNAL(eventUpdatedInList(Seiscomp::DataModel::Event*)),
	        _eventHeatLayer, SLOT(updateEvent(Seiscomp::DataModel::Event*)));
	connect(_eventListView, SIGNAL(eventRemovedFromList(Seiscomp::DataModel::Event*)),
	        _eventHeatLayer, SLOT(removeEvent(Seiscomp::DataModel::Event*)));

	_eventLayer = new EventLayer(_mapWidget, &_cache);
	_eventLayer->setVisible(true);

	if ( global.eventLegendPosition == "topleft" ) {
		_eventLayer->legend(0)->setArea(Qt::AlignLeft | Qt::AlignTop);
	}
	else if ( global.eventLegendPosition == "topright" ) {
		_eventLayer->legend(0)->setArea(Qt::AlignRight | Qt::AlignTop);
	}
	else if ( global.eventLegendPosition == "bottomright" ) {
		_eventLayer->legend(0)->setArea(Qt::AlignRight | Qt::AlignBottom);
	}
	else if ( global.eventLegendPosition == "bottomleft" ) {
		_eventLayer->legend(0)->setArea(Qt::AlignLeft | Qt::AlignBottom);
	}

	connect(_eventListView, SIGNAL(reset()), _eventLayer, SLOT(clear()));
	connect(_eventListView, SIGNAL(eventAddedToList(Seiscomp::DataModel::Event*,bool)),
	        _eventLayer, SLOT(addEvent(Seiscomp::DataModel::Event*,bool)));
	connect(_eventListView, SIGNAL(eventUpdatedInList(Seiscomp::DataModel::Event*)),
	        _eventLayer, SLOT(updateEvent(Seiscomp::DataModel::Event*)));
	connect(_eventListView, SIGNAL(eventRemovedFromList(Seiscomp::DataModel::Event*)),
	        _eventLayer, SLOT(removeEvent(Seiscomp::DataModel::Event*)));
	connect(_eventLayer, SIGNAL(eventHovered(std::string)),
	        this, SLOT(hoverEvent(std::string)));
	connect(_eventLayer, SIGNAL(eventHovered(std::string)),
	        _recentEventsLayer, SLOT(setHighlightedEvent(std::string)));
	connect(_eventLayer, SIGNAL(eventSelected(std::string)),
	        this, SLOT(selectEvent(std::string)),
	        Qt::QueuedConnection);

	connect(_eventListView, SIGNAL(visibleEventCountChanged()),
	        this, SLOT(updateEventTabText()));

	_annotationLayer = new Gui::Map::AnnotationLayer(_mapWidget, new Gui::Map::Annotations);

	_ui.actionShowStationAnnotations->setChecked(global.annotations);

	_annotationLayer->setVisible(_ui.actionShowStationAnnotations->isChecked());

	_networkLayer = new NetworkLayer(_mapWidget);
	_mapWidget->setNetworkLayer(_networkLayer);
	_mapWidget->setEventLayer(_eventLayer);
	_mapWidget->setContextMenuExtender([this](QMenu *menu) {
		QMenu *stationsStatusMenu = menu->addMenu(tr("Stations status"));
		populateStationsStatusMenu(stationsStatusMenu);

		StayOpenMenu *networksMenu = new StayOpenMenu(tr("Networks"), menu);
		populateNetworksMenu(networksMenu);
		menu->addMenu(networksMenu);
	});

	if ( global.statusLegendPosition == "topleft" ) {
		_networkLayer->statusLegend()->setArea(Qt::AlignLeft | Qt::AlignTop);
	}
	else if ( global.statusLegendPosition == "bottomright" ) {
		_networkLayer->statusLegend()->setArea(Qt::AlignRight | Qt::AlignBottom);
	}
	else if ( global.statusLegendPosition == "bottomleft" ) {
		_networkLayer->statusLegend()->setArea(Qt::AlignLeft | Qt::AlignBottom);
	}

	if ( global.stationLegendPosition == "topleft" ) {
		_networkLayer->mainLegend()->setArea(Qt::AlignLeft | Qt::AlignTop);
	}
	else if ( global.stationLegendPosition == "topright" ) {
		_networkLayer->mainLegend()->setArea(Qt::AlignRight | Qt::AlignTop);
	}
	else if ( global.stationLegendPosition == "bottomright" ) {
		_networkLayer->mainLegend()->setArea(Qt::AlignRight | Qt::AlignBottom);
	}
	else if ( global.stationLegendPosition == "bottomleft" ) {
		_networkLayer->mainLegend()->setArea(Qt::AlignLeft | Qt::AlignBottom);
	}

	_ui.actionShowChannelCodes->setChecked(global.annotationsWithChannels);
	_ui.actionShowUnboundStations->setChecked(global.showUnboundStations);

	_networkLayer->setInventory(Client::Inventory::Instance()->inventory(), _annotationLayer->annotations());
	_networkLayer->setShowChannelCodes(_ui.actionShowChannelCodes->isChecked());
	_networkLayer->setShowIssues(_ui.actionShowStationIssues->isChecked());
	_networkLayer->setShowUnbound(_ui.actionShowUnboundStations->isChecked());
	_networkLayer->setShowClosed(_ui.actionShowClosedStations->isChecked());

	connect(_networkLayer, SIGNAL(stationEntered(Seiscomp::DataModel::Station*)),
	        this, SLOT(stationEntered(Seiscomp::DataModel::Station*)));
	connect(_networkLayer, SIGNAL(stationLeft()), this, SLOT(stationLeft()));
	connect(_networkLayer, SIGNAL(stationClicked(Seiscomp::DataModel::Station*)),
	        this, SLOT(stationClicked(Seiscomp::DataModel::Station*)));

	connect(_networkLayer, &NetworkLayer::filterAlertClicked, this, [this]() {
		StayOpenMenu menu(tr("Networks"), this);
		populateNetworksMenu(&menu);
		menu.exec(QCursor::pos());
	});

	connect(_ui.actionShowStationAnnotations, SIGNAL(toggled(bool)), _annotationLayer, SLOT(setVisible(bool)));
	connect(_ui.actionShowStationAnnotations, SIGNAL(toggled(bool)), _mapWidget, SLOT(update()));
	connect(_ui.actionShowChannelCodes, SIGNAL(toggled(bool)), _networkLayer, SLOT(setShowChannelCodes(bool)));
	connect(_ui.actionShowChannelCodes, SIGNAL(toggled(bool)), _mapWidget, SLOT(update()));
	connect(_ui.actionShowStationIssues, SIGNAL(toggled(bool)), _networkLayer, SLOT(setShowIssues(bool)));
	connect(_ui.actionShowUnboundStations, SIGNAL(toggled(bool)), _networkLayer, SLOT(setShowUnbound(bool)));
	connect(_ui.actionShowClosedStations, SIGNAL(toggled(bool)), _networkLayer, SLOT(setShowClosed(bool)));
	connect(_ui.actionSearchStation, SIGNAL(triggered()), this, SLOT(searchStation()));
	connect(_ui.actionCenterMapOnEventUpdate, SIGNAL(toggled(bool)), this, SLOT(toggleCentering(bool)));
	connect(_ui.actionResetView, SIGNAL(triggered()), this, SLOT(resetView()));

	{
		// Add the network selection menu to the "View" menu, above "QC".
		StayOpenMenu *networksMenu = new StayOpenMenu(tr("Networks"), _ui.menuView);
		_ui.menuView->insertMenu(_ui.menuQC->menuAction(), networksMenu);
		connect(networksMenu, &QMenu::aboutToShow, this, [this, networksMenu]() {
			populateNetworksMenu(networksMenu);
		});
	}

	{
		// Add the stations-status exports to the "File" menu, above the
		// separator that precedes "Quit".
		QMenu *stationsStatusMenu = new QMenu(tr("Stations status"), _ui.menuFile);
		auto fileActions = _ui.menuFile->actions();
		int quitIndex = fileActions.indexOf(_ui.actionQuit);
		QAction *before = quitIndex > 0 ? fileActions[quitIndex - 1] : _ui.actionQuit;
		_ui.menuFile->insertMenu(before, stationsStatusMenu);
		connect(stationsStatusMenu, &QMenu::aboutToShow, this, [this, stationsStatusMenu]() {
			populateStationsStatusMenu(stationsStatusMenu);
		});
	}

	{
		QActionGroup *qcActions = new QActionGroup(_ui.menuQC);
		QAction *action;

		action = _ui.menuQC->addAction(tr("Delay")); action->setData("delay");
		action->setCheckable(true);
		action->setActionGroup(qcActions);
		action->setChecked(action->data().toString() == _networkLayer->activeQCParameter().c_str());

		action = _ui.menuQC->addAction(tr("Latency"));
		action->setData("latency"); action->setCheckable(true);
		action->setActionGroup(qcActions);
		action->setChecked(action->data().toString() == _networkLayer->activeQCParameter().c_str());

		action = _ui.menuQC->addAction(tr("Timing Quality"));
		action->setData("timing quality");
		action->setCheckable(true);
		action->setActionGroup(qcActions);
		action->setChecked(action->data().toString() == _networkLayer->activeQCParameter().c_str());

		action = _ui.menuQC->addAction(tr("Gaps Interval"));
		action->setData("gaps interval");
		action->setCheckable(true);
		action->setActionGroup(qcActions);
		action->setChecked(action->data().toString() == _networkLayer->activeQCParameter().c_str());

		action = _ui.menuQC->addAction(tr("Gaps Length"));
		action->setData("gaps length");
		action->setCheckable(true);
		action->setActionGroup(qcActions);
		action->setChecked(action->data().toString() == _networkLayer->activeQCParameter().c_str());

		action = _ui.menuQC->addAction(tr("Overlaps Interval"));
		action->setData("overlaps interval");
		action->setCheckable(true);
		action->setActionGroup(qcActions);
		action->setChecked(action->data().toString() == _networkLayer->activeQCParameter().c_str());

		action = _ui.menuQC->addAction(tr("Availability"));
		action->setData("availability");
		action->setCheckable(true);
		action->setActionGroup(qcActions);
		action->setChecked(action->data().toString() == _networkLayer->activeQCParameter().c_str());

		action = _ui.menuQC->addAction(tr("Offset"));
		action->setData("offset");
		action->setCheckable(true);
		action->setActionGroup(qcActions);
		action->setChecked(action->data().toString() == _networkLayer->activeQCParameter().c_str());

		action = _ui.menuQC->addAction(tr("RMS"));
		action->setData("rms");
		action->setCheckable(true);
		action->setActionGroup(qcActions);
		action->setChecked(action->data().toString() == _networkLayer->activeQCParameter().c_str());
	}

	connect(_ui.menuQC, SIGNAL(triggered(QAction*)), this, SLOT(applyQCMode(QAction*)));

	_mapWidget->canvas().addLayer(_eventHeatLayer);
	_mapWidget->canvas().addLayer(_networkLayer);
	_mapWidget->canvas().addLayer(_eventLayer);
	_mapWidget->canvas().addLayer(_annotationLayer);
	_mapWidget->canvas().addLayer(new ScaleLayer);
	_mapWidget->canvas().addLayer(_currentEventLayer);
	_mapWidget->canvas().addLayer(_recentEventsLayer);

	_ui.menuSettings->addAction(this->_actionShowSettings);
	_ui.menuView->insertAction(_ui.actionShowChannelCodes, this->_actionToggleFullScreen);

	_mapDirty = false;

	connect(_eventListView, SIGNAL(eventAddedToList(Seiscomp::DataModel::Event*,bool)),
	        this, SLOT(eventAdded(Seiscomp::DataModel::Event*,bool)));
	connect(_eventListView, SIGNAL(eventUpdatedInList(Seiscomp::DataModel::Event*)),
	        this, SLOT(eventUpdated(Seiscomp::DataModel::Event*)));
	connect(_eventListView, SIGNAL(eventRemovedFromList(Seiscomp::DataModel::Event*)),
	        this, SLOT(eventRemoved(Seiscomp::DataModel::Event*)));
	connect(_eventListView, SIGNAL(reset()), this, SLOT(eventsCleared()));
	connect(_eventListView, SIGNAL(eventsUpdated()), this, SLOT(eventsUpdated()));

	switchTab(_ui.tabWidget->currentIndex());

	connect(&_updateTimer, SIGNAL(timeout()), this, SLOT(timeout()));

	connect(_ui.actionOpenEventTable, SIGNAL(triggered()), this, SLOT(showEventList()));

	_updateTimer.setInterval(1000);
	_updateTimer.start();

	_eventHeatLayer->setCompositionMode(true);
	for ( int i = 0; i < _networkLayer->legendCount(); ++i ) {
		_networkLayer->legend(i)->bringToFront();
	}

	Core::TimeWindow tw;
	tw.setEndTime(Core::Time::UTC());
	tw.setStartTime(tw.endTime() - global.eventTimeSpan);
	_eventListView->setInterval(tw);
	if ( !global.inputFile.empty() ) {
		readEventParameters(global.inputFile);
	}
	else {
		_eventListView->readFromDatabase();
	}
	updateEventTabText();
	resetView();

	if ( SCApp->query() ) {
		cerr << "= Total read events from database =" << endl;
		cerr << " * Number of events: " << _eventListView->eventCount() << endl;
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::toggleFullScreen() {
	if ( _mapWidget->isFullScreen() )
		leaveFullScreen();
	else {
		if ( _mapWidget->parentWidget() )
			_mapWidget->parentWidget()->layout()->removeWidget(_mapWidget);

		_mapWidget->setWindowFlags(Qt::Window);
		_mapWidget->showFullScreen();
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::leaveFullScreen() {
	_mapWidget->hide();
	_mapWidget->showNormal();
	_mapWidget->setWindowFlags(_mapWidget->windowFlags() & ~Qt::Window);
	switchTab(_ui.tabWidget->currentIndex());
	_mapWidget->show();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::resetView() {
	if ( !global.initialRegion.isNull() ) {
		_mapWidget->canvas().displayRect(global.initialRegion);
	}
	else {
		_mapWidget->canvas().displayRect(QRectF(-180, -90, 360, 180));
	}

	if ( global.displayMode == "groundmotion" ) {
		_ui.tabWidget->setCurrentWidget(_ui.tabGM);
	}
	else if ( global.displayMode == "qualitycontrol" ) {
		_ui.tabWidget->setCurrentWidget(_ui.tabQC);
	}
	else {
		_ui.tabWidget->setCurrentWidget(_ui.tabNetwork);
	}

	bool showLegend = SCScheme.map.showLegends;

	if ( global.showLegend ) {
		showLegend = true;
	}

	_ui.actionShowMapLegend->setChecked(showLegend);
	_mapWidget->setDrawLegends(_ui.actionShowMapLegend->isChecked());
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::showEventList() {
	_ui.tabWidget->setCurrentWidget(_ui.tabEvents);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::switchTab(int index) {
	QWidget *w = _ui.tabWidget->widget(index);
	if ( w == _ui.tabGM || w == _ui.tabQC || w == _ui.tabNetwork ) {
		if ( !_mapWidget->isFullScreen() ) {
			// Reparent _mapWidget
			if ( _mapWidget->parentWidget() ) {
				_mapWidget->parentWidget()->layout()->removeWidget(_mapWidget);
			}
			w->layout()->addWidget(_mapWidget);
		}

		if ( w == _ui.tabNetwork ) {
			_networkLayer->setColorMode(NetworkLayer::Network);
		}
		else if ( w == _ui.tabGM ) {
			_networkLayer->setColorMode(NetworkLayer::GroundMotion);
		}
		else if ( w == _ui.tabQC ) {
			_networkLayer->setColorMode(NetworkLayer::QC);
		}
		else {
			_networkLayer->setColorMode(NetworkLayer::Default);
		}
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::timeout() {
	global.tickToggleState = !global.tickToggleState;

	if ( _mapDirty ) {
		_mapWidget->update();
		_mapDirty = false;
	}

	for ( auto & [model, data] : global.stationConfig ) {
		if ( data->infoData ) {
			static_cast<StationInfoDialog*>(data->infoData)->shiftData();
		}
	}

	_networkLayer->tick();
	_eventLayer->tick();
	_currentEventLayer->tick();

	if ( _recentEventsDirty ) {
		updateRecentEvents();
	}
	_recentEventsLayer->tick();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::openFile() {
	QString filename = QFileDialog::getOpenFileName(this,
		tr("Open XML file"), "", tr("XML files (*.xml);;All (*.*)"));

	if ( filename.isEmpty() ) {
		return;
	}

	readEventParameters(filename.toStdString());
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::handleMessage(Core::Message* message) {
	auto dataMessage = Core::DataMessage::Cast(message);
	if ( dataMessage ) {
		for ( auto &obj : *dataMessage ) {
			DataModel::WaveformQuality *wfq = DataModel::WaveformQuality::Cast(obj);
			if ( wfq ) {
				addObject(QString(), wfq);
			}
		}
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::addObject(const QString &, DataModel::Object *obj) {
	dm::Pick *pick = dm::Pick::Cast(obj);
	if ( pick ) {
		SEISCOMP_INFO("Received pick at %s on %s.%s.%s.%s",
		              pick->time().value().iso().c_str(),
		              pick->waveformID().networkCode().c_str(),
		              pick->waveformID().stationCode().c_str(),
		              pick->waveformID().locationCode().c_str(),
		              pick->waveformID().channelCode().c_str());
		string sta_id = pick->waveformID().networkCode() + "." + pick->waveformID().stationCode();
		auto it = global.stationIDConfig.find(sta_id);
		if ( it == global.stationIDConfig.end() ) {
			SEISCOMP_DEBUG("Station not registered, ignoring pick");
			return;
		}

		if ( !it->second->triggerTime ||
		     (pick->time().value() > *it->second->triggerTime) ) {
			it->second->triggerTime = pick->time().value();
		}

		bool known = false;
		for ( auto &p : it->second->picks ) {
			if ( p->publicID() == pick->publicID() ) {
				p = pick;
				known = true;
				break;
			}
		}
		if ( !known ) {
			it->second->picks.push_back(pick);
			if ( it->second->infoData ) {
				static_cast<StationInfoDialog*>(it->second->infoData)->addPick(pick);
			}
		}

		return;
	}

	dm::WaveformQuality *wfq = dm::WaveformQuality::Cast(obj);
	if ( wfq ) {
		string sta_id = wfq->waveformID().networkCode() + "." + wfq->waveformID().stationCode();
		auto it = global.stationIDConfig.find(sta_id);
		if ( it == global.stationIDConfig.end() ) {
			SEISCOMP_DEBUG("%s: station not registered, ignoring waveform quality",
			               sta_id.c_str());
			return;
		}

		if ( !it->second->channel ) {
			SEISCOMP_DEBUG("%s.%s.%s: station channel not configured, ignoring waveform quality",
			               sta_id.c_str(),
			               wfq->waveformID().locationCode().c_str(),
			               wfq->waveformID().channelCode().c_str());
			return;
		}

		if ( wfq->waveformID().locationCode() != it->second->channel->sensorLocation()->code()
		  || wfq->waveformID().channelCode() != it->second->channel->code() ) {
			SEISCOMP_DEBUG("%s.%s.%s: channel not preferred, ignoring waveform quality",
			               sta_id.c_str(),
			               wfq->waveformID().locationCode().c_str(),
			               wfq->waveformID().channelCode().c_str());
			return;
		}

		it->second->qc[wfq->parameter()] = wfq;
		updateQC(it->second.get(), wfq);

		return;
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::eventAdded(DataModel::Event *obj, bool) {
	_recentEventsDirty = true;

	try {
		if ( SCApp->isAgencyIDBlocked(obj->creationInfo().agencyID()) ) {
			return;
		}
	}
	catch ( ... ) {}

	if ( _latestEvent ) {
		Event evt;
		evt.setEvent(obj, _cache);
		if ( _latestEvent.isMoreRecent(evt) ) {
			return;
		}
		_latestEvent = evt;
	}
	else {
		_latestEvent.setEvent(obj, _cache);
	}

	updateCurrentEvent();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::eventUpdated(DataModel::Event *evt) {
	eventAdded(evt, true);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::eventRemoved(DataModel::Event *evt) {
	_recentEventsDirty = true;

	if ( _latestEvent && _latestEvent.event->publicID() == evt->publicID() ) {
		_latestEvent.reset();
		updateCurrentEvent();
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::eventsUpdated() {
	_recentEventsDirty = true;

	if ( _latestEvent ) return;

	// Check for a new latest event after the event list finished to update
	// its events

	QTreeWidget *eventTree = _eventListView->eventTree();
	int count = eventTree->topLevelItemCount();

	for ( int i = 0; i < count; ++i ) {
		if ( eventTree->topLevelItem(i)->isHidden() ) continue;
		DataModel::Event *evt = _eventListView->eventFromTreeItem(eventTree->topLevelItem(i));

		try {
			if ( SCApp->isAgencyIDBlocked(evt->creationInfo().agencyID()) )
				continue;
		}
		catch ( ... ) {}

		if ( _latestEvent ) {
			Event evtObj;
			evtObj.setEvent(evt, _cache);
			if ( _latestEvent.isMoreRecent(evtObj) ) {
				continue;
			}
			_latestEvent = evtObj;
		}
		else
			_latestEvent.setEvent(evt, _cache);
	}

	updateCurrentEvent();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::eventsCleared() {
	_recentEventsDirty = true;

	_latestEvent.reset();
	updateCurrentEvent();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::updateCurrentEvent() {
	if ( _latestEvent ) {
		_eventLayer->setCurrentEvent(_latestEvent.event.get());
		_currentEventLayer->setEvent(_latestEvent.event.get());
		if ( global.centerOrigins ) {
			_mapWidget->canvas().setMapCenter(_latestEvent.location());
		}
	}
	else {
		_eventLayer->setCurrentEvent(nullptr);
		_currentEventLayer->setEvent(nullptr);
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::updateRecentEvents() {
	_recentEventsDirty = false;

	if ( global.recentEventCount <= 0 ) {
		return;
	}

	QVector<RecentEventsLayer::Item> items;

	// Use the events of the event list including its filters
	QTreeWidget *eventTree = _eventListView->eventTree();
	int count = eventTree->topLevelItemCount();

	for ( int i = 0; i < count; ++i ) {
		if ( eventTree->topLevelItem(i)->isHidden() ) {
			continue;
		}

		auto *evt = _eventListView->eventFromTreeItem(eventTree->topLevelItem(i));
		if ( !evt ) {
			continue;
		}

		try {
			if ( SCApp->isAgencyIDBlocked(evt->creationInfo().agencyID()) ) {
				continue;
			}
		}
		catch ( ... ) {}

		dm::OriginPtr origin = _cache.get<dm::Origin>(evt->preferredOriginID());
		if ( !origin ) {
			continue;
		}

		RecentEventsLayer::Item item;
		item.eventID = evt->publicID();

		try {
			item.originTime = origin->time().value();
		}
		catch ( ... ) {
			continue;
		}

		try {
			item.depth = origin->depth().value();
		}
		catch ( ... ) {}

		dm::MagnitudePtr magnitude = _cache.get<dm::Magnitude>(evt->preferredMagnitudeID());
		if ( magnitude ) {
			try {
				item.magnitude = magnitude->magnitude().value();
			}
			catch ( ... ) {}
			item.magnitudeType = magnitude->type().c_str();
		}

		item.region = dm::eventRegion(evt).c_str();
		items.append(item);
	}

	std::sort(items.begin(), items.end(),
	          [](const RecentEventsLayer::Item &a, const RecentEventsLayer::Item &b) {
	          	return a.originTime > b.originTime;
	          });

	if ( items.size() > global.recentEventCount ) {
		items.resize(global.recentEventCount);
	}

	_recentEventsLayer->setItems(items);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::centerOnEvent(const std::string &eventID) {
	dm::EventPtr event = _cache.get<dm::Event>(eventID);
	if ( !event ) {
		return;
	}

	dm::OriginPtr origin = _cache.get<dm::Origin>(event->preferredOriginID());
	if ( origin ) {
		_mapWidget->canvas().setMapCenter(
			QPointF(origin->longitude().value(), origin->latitude().value())
		);
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::hoverEvent(const std::string &eventID) {
	setToolTip(eventID.c_str());
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::showCurrentEventDetails(const std::string &eventID) {
	dm::EventPtr event = _cache.get<dm::Event>(eventID);
	if ( event ) {
		// Center the map on the event.
		dm::OriginPtr origin = _cache.get<dm::Origin>(event->preferredOriginID());
		if ( origin ) {
			_mapWidget->canvas().setMapCenter(
			            QPointF(origin->longitude().value(), origin->latitude().value())
			);
		}

		// If an earlier click forced a different event onto the map, hide it
		// again before forcing the new one.
		if ( _forcedEvent && (_forcedEvent->publicID() != eventID) ) {
			_eventLayer->removeEvent(_forcedEvent.get());
			_forcedEvent = nullptr;
		}

		// Show the event on the map if it is not currently displayed, e.g.
		// because the event layer does not hold it.
		if ( !_eventLayer->hasEvent(eventID) ) {
			_eventLayer->addEvent(event.get(), false);
			_forcedEvent = event;
		}

		// The event symbols may be switched off via the map's Layers menu.
		// In that case reveal only the selected latest event and keep the
		// other events hidden; the layer is switched off again on close.
		if ( !_eventLayer->isVisible() ) {
			_eventLayer->showOnlyEvent(eventID);
			_eventLayer->setVisible(true);
			_eventLayerForcedVisible = true;
		}

		// Highlight it as the current event so its symbol is shown even when
		// the event symbols are otherwise not displayed.
		_eventLayer->setCurrentEvent(event.get());
		_mapWidget->update();
	}

	selectEvent(eventID);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::selectEvent(const std::string &eventID) {
	dm::EventPtr event = _cache.get<dm::Event>(eventID);
	if ( event ) {
		dm::OriginPtr origin = _cache.get<dm::Origin>(event->preferredOriginID());
		dm::MagnitudePtr magnitude = _cache.get<dm::Magnitude>(event->preferredMagnitudeID());
		if ( !_eventDetails ) {
			_eventDetails = new EventInfoDialog(this, Qt::Tool);
			connect(_eventDetails, SIGNAL(destroyed(QObject*)),
			        this, SLOT(objectDestroyed(QObject*)));
			_eventDetails->setAttribute(Qt::WA_DeleteOnClose);
			_eventDetails->restoreGeometry(_eventDetailsState);
		}

		_eventDetails->setEvent(event.get(), origin.get(), magnitude.get());
		_eventDetails->show();
		_eventDetails->activateWindow();
	}
	else {
		QMessageBox::critical(nullptr, tr("Error"), tr("Event %1 could not be found").arg(eventID.c_str())
		);
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::selectEventFromList(Seiscomp::DataModel::Event *evt) {
	dm::OriginPtr org = _cache.get<dm::Origin>(evt->preferredOriginID());
	if ( org ) {
		_mapWidget->canvas().setMapCenter(QPointF(org->longitude().value(), org->latitude().value()));
	}
	_ui.tabWidget->setCurrentWidget(_ui.tabNetwork);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::stationEntered(DataModel::Station *station) {
	if ( statusBar() ) {
		statusBar()->showMessage((station->network()->code() + "." + station->code()).c_str());
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::stationLeft() {
	if ( statusBar() )
		statusBar()->clearMessage();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::stationClicked(DataModel::Station *station) {
	Settings::StationData *data = nullptr;

	auto it = global.stationConfig.find(station);
	if ( it != global.stationConfig.end() ) {
		data = it->second.get();
	}

	StationInfoDialog dlg(station, data);

	if ( data ) {
		data->infoData = &dlg;
		connect(&dlg, &StationInfoDialog::setStationEnabled, this,
		        [this, station](bool enable) {
			setStationEnabled(station->network()->code(), station->code(), enable);
		});
	}
	dlg.restoreGeometry(_lastInfoGeometry);
	dlg.exec();
	_lastInfoGeometry = dlg.saveGeometry();
	if ( data ) {
		data->infoData = nullptr;
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::readEventParameters(const std::string &file) {
	QApplication::setOverrideCursor(Qt::WaitCursor);

	IO::XMLArchive ar;
	if ( !ar.open(file.c_str()) ) {
		QApplication::restoreOverrideCursor();
		QMessageBox::critical(this, tr("Read EventParameters"),
			QString("Could not open file\n%1").arg(file.c_str())
		);
		return;
	}

	_eventListView->clear();
	_localEP = nullptr;
	ar >> _localEP;

	if ( !_localEP ) {
		QApplication::restoreOverrideCursor();
		QMessageBox::critical(this, tr("Read EventParameters"),
			QString("Invalid file\n%1").arg(file.c_str())
		);
		return;
	}

	for ( size_t i = 0; i < _localEP->eventCount(); ++i ) {
		_eventListView->add(_localEP->event(i), nullptr);
	}

	cerr << "= Total read events from file =" << endl;
	cerr << " * Number of events: " << _eventLayer->eventCount() << endl;

	QApplication::restoreOverrideCursor();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::updateQC(Settings::StationData *data,
                          DataModel::WaveformQuality *wfq) {
	if ( _networkLayer->colorMode() != NetworkLayer::QC )
		return;

	auto symbol = reinterpret_cast<NetworkLayerSymbol*>(data->viewData);
	if ( !symbol ) return;

	if ( wfq->parameter() == _networkLayer->activeQCParameter() ) {
		symbol->setColorFromValue(wfq->value());
		_mapDirty = true;
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::updateGroundMotion(Settings::StationData *data) {
	auto symbol = reinterpret_cast<NetworkLayerSymbol*>(data->viewData);
	if ( !symbol ) {
		return;
	}

	if ( _networkLayer->colorMode() == NetworkLayer::GroundMotion ) {
		symbol->setColorFromValue(data->maximumAmplitude);
		_mapDirty = true;
	}

	if ( data->infoData ) {
		static_cast<StationInfoDialog*>(data->infoData)->processingDataUpdated(data);
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::updateStation(DataModel::ConfigStation *cs, DataModel::Operation op) {
	switch ( op ) {
		case DataModel::OP_ADD:
		{
			// TODO
			break;
		}
		case DataModel::OP_UPDATE:
		{
			auto staID = cs->networkCode() + "." + cs->stationCode();
			auto it = global.stationIDConfig.find(staID);
			if ( it != global.stationIDConfig.end() ) {
				if ( it->second->enabled != cs->enabled() ) {
					it->second->enabled = cs->enabled();
					_networkLayer->updateStation(staID);
				}
			}
			break;
		}
		case DataModel::OP_REMOVE:
		{
			auto staID = cs->networkCode() + "." + cs->stationCode();
			auto it = global.stationIDConfig.find(staID);
			if ( it != global.stationIDConfig.end() ) {
				it->second->enabled = false;
				it->second->bindings = nullptr;
				it->second->state = Settings::Unconfigured;
				_networkLayer->updateStation(staID);
			}
			break;
		}
		default:
			break;
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
DataModel::ConfigStation *MainWindow::configStation(const std::string &net,
                                                    const std::string &sta) const {
	DataModel::ConfigModule *module = SCApp->configModule();
	if ( !module ) {
		return nullptr;
	}

	for ( size_t i = 0; i < module->configStationCount(); ++i ) {
		DataModel::ConfigStation *cs = module->configStation(i);
		if ( cs->networkCode() == net && cs->stationCode() == sta ) {
			return cs;
		}
	}

	return nullptr;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::setStationEnabled(const std::string &net, const std::string &sta,
                                   bool enable) {
	std::string staID = net + "." + sta;

	// Update the local configuration state and recolor the map immediately.
	auto it = global.stationIDConfig.find(staID);
	if ( it != global.stationIDConfig.end() ) {
		it->second->enabled = enable;
		_networkLayer->updateStation(staID);
	}

	// Mirror the change into the config module and send it to the CONFIG
	// message group as a notifier, the same way scrttv does.
	DataModel::ConfigStation *cs = configStation(net, sta);
	if ( !cs ) {
		DataModel::ConfigModule *module = SCApp->configModule();
		if ( !module ) {
			return;
		}

		DataModel::ConfigStationPtr newCs = DataModel::ConfigStation::Create(
			"Config/" + module->name() + "/" + net + "/" + sta);
		newCs->setNetworkCode(net);
		newCs->setStationCode(sta);
		newCs->setEnabled(enable);

		DataModel::CreationInfo ci;
		ci.setAuthor(SCApp->author());
		ci.setAgencyID(SCApp->agencyID());
		ci.setCreationTime(Core::Time::UTC());
		newCs->setCreationInfo(ci);

		DataModel::Notifier::Enable();
		module->add(newCs.get());
		DataModel::Notifier::Disable();

		cs = newCs.get();
	}

	if ( cs->enabled() != enable ) {
		cs->setEnabled(enable);

		DataModel::CreationInfo *ci;
		try {
			ci = &cs->creationInfo();
			ci->setModificationTime(Core::Time::UTC());
		}
		catch ( ... ) {
			cs->setCreationInfo(DataModel::CreationInfo());
			ci = &cs->creationInfo();
			ci->setCreationTime(Core::Time::UTC());
		}

		ci->setAuthor(SCApp->author());
		ci->setAgencyID(SCApp->agencyID());

		DataModel::Notifier::Enable();
		cs->update();
		DataModel::Notifier::Disable();
	}

	Core::MessagePtr msg = DataModel::Notifier::GetMessage(true);
	if ( msg ) {
		SCApp->sendMessage("CONFIG", msg.get());
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool MainWindow::eventFilter(QObject *object, QEvent *event) {
	if ( object == _mapWidget ) {
		if ( event->type() == QEvent::MouseButtonRelease ) {
			auto mouseEvent = static_cast<QMouseEvent*>(event);

			if ( mouseEvent->button() == Qt::LeftButton	&& mouseEvent->modifiers() == Qt::ShiftModifier ) {
				showMapCoordinates(mouseEvent->pos());
			}
			else if ( mouseEvent->button() == Qt::MiddleButton ) {
				sendArtificialOrigin(mouseEvent->pos());
				return true;
			}
		}
	}

	return Gui::MainWindow::eventFilter(object, event);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::showMapCoordinates(const QPoint &pos) {
	QPointF mapPos;
	if ( _mapWidget->canvas().projection()->unproject(mapPos, pos) ) {
		statusBar()->showMessage(
			QString("Pos: %1 %2")
			.arg(
				Gui::latitudeToString(mapPos.y(), true, true, -1),
				Gui::longitudeToString(mapPos.x(), true, true, -1)
			),
			2000
		);
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::sendArtificialOrigin(const QPoint &pos) {
	QPointF mapPos;
	if ( !_mapWidget->canvas().projection()->unproject(mapPos, pos) ) {
		return;
	}

	Gui::OriginDialog dialog(mapPos.x(), mapPos.y(), this);
	dialog.move(pos);

	if ( dialog.exec() != QDialog::Accepted ) {
		return;
	}

	DataModel::Origin* origin = DataModel::Origin::Create();
	DataModel::CreationInfo ci;

	ci.setAgencyID(SCApp->agencyID());
	ci.setAuthor(SCApp->author());
	ci.setCreationTime(Core::Time::UTC());

	origin->setCreationInfo(ci);
	origin->setLongitude(dialog.longitude());
	origin->setLatitude(dialog.latitude());
	origin->setDepth(DataModel::RealQuantity(dialog.depth()));
	origin->setTime(Core::Time(dialog.getTime_t(), 0));

	SCApp->sendCommand(Gui::CM_OBSERVE_LOCATION, "", origin);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::updateEventTabText() {
	int idx = _ui.tabWidget->indexOf(_ui.tabEvents);
	_ui.tabWidget->setTabText(idx, QString("Events (%1/%2)")
	                          .arg(_eventListView->visibleEventCount())
	                          .arg(_eventListView->eventCount()));
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::applyQCMode(QAction *action) {
	_networkLayer->setActiveQCParameter(action->data().toString().toStdString());
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::searchStation() {
	if ( !_currentSearch ) {
		_currentSearch = new SearchWidget(_networkLayer->closedStationCodes(),
		                                  _networkLayer->visibleStationCodes(),
		                                  this);
		connect(_currentSearch, SIGNAL(destroyed(QObject*)),
		        this, SLOT(objectDestroyed(QObject*)));
		connect(_currentSearch, SIGNAL(filterView()),
		        this, SLOT(filterStations()));
	}

	_currentSearch->show();
	_currentSearch->activateWindow();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::filterStations() {
	if ( !_currentSearch ) {
		return;
	}

	auto result = _currentSearch->visibleData();

	if ( !result.isEmpty() ) {
		// Center on the geographic center of the selected stations (on the
		// station itself when only one is selected).
		double sumLon = 0.0;
		double sumLat = 0.0;
		for ( const DataModel::Station *s : result ) {
			sumLon += s->longitude();
			sumLat += s->latitude();
		}

		_mapWidget->canvas().setMapCenter(
			QPointF(sumLon / result.size(), sumLat / result.size())
		);
	}

	_networkLayer->setStationsVisible(&result);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::populateNetworksMenu(QMenu *menu) {
	menu->clear();

	QAction *selectAll = menu->addAction(tr("Select all"));
	QAction *unselectAll = menu->addAction(tr("Unselect all"));
	menu->addSeparator();

	QWidget *grid = new QWidget(menu);
	QGridLayout *gridLayout = new QGridLayout(grid);
	gridLayout->setContentsMargins(4, 2, 4, 2);
	gridLayout->setHorizontalSpacing(12);
	gridLayout->setVerticalSpacing(2);

	QList<QCheckBox*> boxes;
	const QStringList codes = _networkLayer->networkCodes();
	for ( int i = 0; i < codes.size(); ++i ) {
		const QString code = codes[i];
		QCheckBox *box = new QCheckBox(code, grid);
		box->setChecked(_networkLayer->isNetworkVisible(code));
		connect(box, &QCheckBox::toggled, this,
		        [this, code](bool on) { _networkLayer->setNetworkVisible(code, on); });
		gridLayout->addWidget(box, i % 10, i / 10);
		boxes.append(box);
	}

	QWidgetAction *gridAction = new QWidgetAction(menu);
	gridAction->setDefaultWidget(grid);
	menu->addAction(gridAction);

	connect(selectAll, &QAction::triggered, this, [boxes]() {
		for ( QCheckBox *b : boxes ) {
			b->setChecked(true);
		}
	});
	connect(unselectAll, &QAction::triggered, this, [boxes]() {
		for ( QCheckBox *b : boxes ) {
			b->setChecked(false);
		}
	});
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::populateStationsStatusMenu(QMenu *menu) {
	menu->clear();

	QLabel *header = new QLabel(
		tr("Visible stations: %1").arg(_networkLayer->visibleStationCount()), menu);
	header->setAlignment(Qt::AlignCenter);
	header->setContentsMargins(6, 2, 6, 2);
	QWidgetAction *headerAction = new QWidgetAction(menu);
	headerAction->setDefaultWidget(header);
	menu->addAction(headerAction);
	menu->addSeparator();

	struct Entry {
		QString label;
		int     count;
		QString saveTitle;
		QString defaultName;
		QString content;
	};

	auto byLabel = [](const Entry &a, const Entry &b) {
		return a.label.localeAwareCompare(b.label) < 0;
	};

	QList<Entry> statusEntries = {
		{ tr("Disabled"), _networkLayer->disabledStationCount(),
		  tr("Save disabled stations"), "stations-disabled",
		  _networkLayer->disabledStationList() },
		{ tr("Enabled"), _networkLayer->enabledStationCount(),
		  tr("Save enabled stations"), "stations-enabled",
		  _networkLayer->enabledStationList() },
	};

	QList<Entry> issueEntries = {
		{ tr("Bindings mismatching inventory"), _networkLayer->mismatchStationCount(),
		  tr("Save stations with binding mismatch"), "stations-bindings-mismatch",
		  _networkLayer->mismatchStationList() },
		{ tr("Closed"), _networkLayer->closedStationCount(),
		  tr("Save closed stations"), "stations-closed",
		  _networkLayer->closedStationList() },
		{ tr("Missing detecStream configuration"), _networkLayer->noDetecStreamStationCount(),
		  tr("Save stations missing detecStream"), "stations-no-detecstream",
		  _networkLayer->noDetecStreamStationList() },
		{ tr("Missing global bindings"), _networkLayer->unboundStationCount(),
		  tr("Save unbound stations"), "stations-unbound",
		  _networkLayer->unboundStationList() },
	};

	std::sort(statusEntries.begin(), statusEntries.end(), byLabel);
	std::sort(issueEntries.begin(), issueEntries.end(), byLabel);

	auto addEntries = [this, menu](const QList<Entry> &entries) {
		for ( const Entry &e : entries ) {
			addStationsStatusMenu(menu, e.label, e.count, e.saveTitle, e.defaultName, e.content);
		}
	};

	// Enable/disable status on top, separated from the issue categories.
	addEntries(statusEntries);
	menu->addSeparator();
	addEntries(issueEntries);

	// CSV report: one row per "networkCode.stationCode", one column per
	// category (1 = the station is a member of that category).
	QList<Entry> allEntries = statusEntries + issueEntries;

	QStringList headerRow;
	headerRow << "#station";
	for ( const Entry &e : allEntries ) {
		headerRow << e.label;
	}

	QMap<QString, QList<bool>> table;
	for ( int c = 0; c < allEntries.size(); ++c ) {
		const QStringList codes = allEntries[c].content.isEmpty()
		                          ? QStringList() : allEntries[c].content.split('\n');
		for ( const QString &code : codes ) {
			QList<bool> &row = table[code];
			while ( row.size() < allEntries.size() ) {
				row << false;
			}
			row[c] = true;
		}
	}

	QStringList lines;
	lines << headerRow.join(',');
	for ( auto it = table.constBegin(); it != table.constEnd(); ++it ) {
		QStringList row;
		row << it.key();
		for ( bool member : it.value() ) {
			row << (member ? "1" : "0");
		}
		lines << row.join(',');
	}
	QString report = lines.join('\n');

	menu->addSeparator();
	QMenu *reportMenu = menu->addMenu(tr("Create report"));
	connect(reportMenu->addAction(tr("Copy to clipboard")), &QAction::triggered, this,
	        [this, report]() { copyStationsToClipboard(report); });
	connect(reportMenu->addAction(tr("Save to file")), &QAction::triggered, this,
	        [this, report]() {
	        	saveStationsToFile(tr("Save stations status report"), report,
	        	                   "stations-status-report");
	        });
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::addStationsStatusMenu(QMenu *menu, const QString &label, int count,
                                     const QString &saveTitle, const QString &defaultName,
                                     const QString &content) {
	QMenu *sub = menu->addMenu(label + QString(":\t%1").arg(count));
	sub->setEnabled(count > 0);
	connect(sub->addAction(tr("Copy to clipboard")), &QAction::triggered, this,
	        [this, content]() { copyStationsToClipboard(content); });
	connect(sub->addAction(tr("Save to file")), &QAction::triggered, this,
	        [this, saveTitle, content, defaultName]() {
	        	saveStationsToFile(saveTitle, content, defaultName);
	        });

	QMenu *searchMenu = sub->addMenu(tr("Search"));
	connect(searchMenu, &QMenu::aboutToShow, this, [this, searchMenu, content]() {
		populateStationSearchMenu(searchMenu, content);
	});
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::populateStationSearchMenu(QMenu *menu, const QString &content) {
	menu->clear();

	const QStringList codes = content.isEmpty() ? QStringList()
	                                            : content.split('\n');

	QWidget *container = new QWidget(menu);
	QVBoxLayout *containerLayout = new QVBoxLayout(container);
	containerLayout->setContentsMargins(6, 4, 6, 4);
	containerLayout->setSpacing(2);

	QLabel *header = new QLabel(tr("stations"), container);
	header->setAlignment(Qt::AlignCenter);
	containerLayout->addWidget(header);

	QFrame *separator = new QFrame(container);
	separator->setFrameShape(QFrame::HLine);
	separator->setFrameShadow(QFrame::Sunken);
	containerLayout->addWidget(separator);

	QWidget *list = new QWidget;
	QVBoxLayout *listLayout = new QVBoxLayout(list);
	listLayout->setContentsMargins(0, 0, 0, 0);
	listLayout->setSpacing(2);

	for ( const QString &code : codes ) {
		QToolButton *button = new QToolButton(list);
		button->setAutoRaise(true);
		button->setToolButtonStyle(Qt::ToolButtonTextOnly);
		button->setText(code);
		connect(button, &QToolButton::clicked, this, [this, code]() {
			QPointF location;
			if ( _networkLayer->stationLocation(code, location) ) {
				_mapWidget->canvas().setMapCenter(location);
				_mapWidget->update();
			}

			// Dismiss the whole menu chain, mirroring a normal menu action.
			while ( QWidget *popup = QApplication::activePopupWidget() ) {
				popup->close();
			}
		});
		listLayout->addWidget(button);
	}

	QScrollArea *scroll = new QScrollArea(container);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scroll->setWidget(list);

	// Reserve room for the vertical scrollbar so the codes are not truncated.
	scroll->setMinimumWidth(list->sizeHint().width() +
	                        style()->pixelMetric(QStyle::PM_ScrollBarExtent));

	// Cap at ~20 rows (or the available screen height), scrolling beyond that.
	int rows = codes.size();
	int fullHeight = list->sizeHint().height();
	int maxHeight = fullHeight;
	if ( rows > 20 ) {
		maxHeight = fullHeight * 20 / rows;
	}
	int screenHeight = QGuiApplication::primaryScreen()->availableGeometry().height();
	maxHeight = qMin(maxHeight, screenHeight - 160);
	scroll->setMaximumHeight(maxHeight);
	containerLayout->addWidget(scroll);

	QWidgetAction *action = new QWidgetAction(menu);
	action->setDefaultWidget(container);
	menu->addAction(action);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::copyStationsToClipboard(const QString &content) {
	QApplication::clipboard()->setText(content);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::saveStationsToFile(const QString &title, const QString &content,
                                    const QString &defaultName) {
	QString fileName = QFileDialog::getSaveFileName(
		this, title, QDir("/tmp").filePath(defaultName),
		tr("Text files (*.txt);;All files (*)"));

	if ( fileName.isEmpty() ) {
		return;
	}

	QFile file(fileName);
	if ( !file.open(QIODevice::WriteOnly | QIODevice::Text) ) {
		QMessageBox::critical(this, tr("Error"),
		                      tr("Failed to open file for writing:\n%1").arg(fileName));
		return;
	}

	QTextStream stream(&file);
	stream << content << '\n';
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::objectDestroyed(QObject *obj) {
	if ( _currentSearch == obj ) {
		_currentSearch = nullptr;
		_networkLayer->setStationsVisible(nullptr);
	}
	else if ( _eventDetails == obj ) {
		_eventDetailsState = _eventDetails->saveGeometry();
		_eventDetails = nullptr;

		// Hide the event again if it was only shown to display its details.
		if ( _forcedEvent ) {
			_eventLayer->removeEvent(_forcedEvent.get());
			_forcedEvent = nullptr;
		}

		// Switch the event layer off again if it was only shown for this,
		// restoring the visibility of the other event symbols.
		if ( _eventLayerForcedVisible ) {
			_eventLayer->showAllEvents();
			_eventLayer->setVisible(false);
			_eventLayerForcedVisible = false;
		}

		_mapWidget->update();
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::toggleCentering(bool enable) {
	global.centerOrigins = enable;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
