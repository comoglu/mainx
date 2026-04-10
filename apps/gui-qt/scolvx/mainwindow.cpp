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
 ***************************************************************************/


#define SEISCOMP_COMPONENT Gui::OriginLocatorX

#include "mainwindow.h"

#include <seiscomp/logging/log.h>
#include <seiscomp/core/strings.h>
#include <seiscomp/system/environment.h>
#include <seiscomp/datamodel/pick.h>
#include <seiscomp/datamodel/origin.h>
#include <seiscomp/datamodel/amplitude.h>
#include <seiscomp/datamodel/event.h>
#include <seiscomp/datamodel/magnitude.h>
#include <seiscomp/datamodel/originreference.h>
#include <seiscomp/datamodel/journalentry.h>
#include <seiscomp/datamodel/messages.h>
#include <seiscomp/datamodel/utils.h>
#include <seiscomp/io/archive/xmlarchive.h>
#include <seiscomp/gui/core/application.h>
#include <seiscomp/gui/core/icon.h>
#include <seiscomp/gui/datamodel/eventsummary.h>
#include <seiscomp/gui/datamodel/originlocatorview.h>
#include <seiscomp/gui/datamodel/magnitudeview.h>
#include <seiscomp/gui/datamodel/pickerview.h>
#include <seiscomp/gui/datamodel/amplitudeview.h>
#include <seiscomp/gui/datamodel/eventlistview.h>
#include <seiscomp/gui/datamodel/eventedit.h>
#include <seiscomp/gui/datamodel/pickersettings.h>
#include <seiscomp/gui/core/processmanager.h>
#include <seiscomp/gui/map/imagetree.h>

#include <QCloseEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPainter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <seiscomp/math/geo.h>
#include <seiscomp/datamodel/eventdescription.h>
#include <seiscomp/datamodel/notifier.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cstdlib>

#include "settings.h"

Q_DECLARE_METATYPE(std::string)


using namespace std;
using namespace Seiscomp;
using namespace Seiscomp::DataModel;


// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
namespace Seiscomp {
namespace OLocX {
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
MainWindow::MainWindow() {
	qRegisterMetaType<std::string>("std::string");

	_ui.setupUi(this);

	SCApp->settings().beginGroup(objectName());

	// -----------------------------------------------------------------------
	// System tray
	// -----------------------------------------------------------------------
	if ( global.systemTray ) {
		_trayIcon = new QSystemTrayIcon(this);
		_trayIcon->setIcon(Gui::icon("seiscomp-logo").pixmap(64));
		_trayIcon->setToolTip(tr("%1").arg(SCApp->name().c_str()));
		_trayIcon->show();

		connect(_trayIcon, &QSystemTrayIcon::messageClicked,
		        this, &MainWindow::trayIconMessageClicked);
		connect(_trayIcon, &QSystemTrayIcon::activated,
		        this, &MainWindow::trayIconActivated);
	}

	// -----------------------------------------------------------------------
	// Map tree (shared between summary and locator widgets)
	// -----------------------------------------------------------------------
	Gui::Map::ImageTreePtr mapTree = new Gui::Map::ImageTree(SCApp->mapsDesc());

	// -----------------------------------------------------------------------
	// Left panel: event summary (Preferred / Current tabs)
	// -----------------------------------------------------------------------
	_ui.frameSummary->setFrameShape(QFrame::NoFrame);

	_eventSummaryPreferred = new Gui::EventSummary(mapTree.get(), SCApp->query(), this);
	_eventSummaryCurrent   = new Gui::EventSummary(mapTree.get(), SCApp->query(), this);

	QTabWidget *summaryTabs = new QTabWidget(_ui.frameSummary);
	{
		QWidget *w = new QWidget;
		QVBoxLayout *l = new QVBoxLayout(w);
		l->setContentsMargins(0, 0, 0, 0);
		l->addWidget(_eventSummaryPreferred);
		summaryTabs->addTab(w, "Preferred");
	}
	{
		QWidget *w = new QWidget;
		QVBoxLayout *l = new QVBoxLayout(w);
		l->setContentsMargins(0, 0, 0, 0);
		l->addWidget(_eventSummaryCurrent);
		summaryTabs->addTab(w, "Current");
	}
	QHBoxLayout *summaryLayout = new QHBoxLayout(_ui.frameSummary);
	summaryLayout->setContentsMargins(0, 0, 0, 0);
	summaryLayout->addWidget(summaryTabs);

	// Publish button (export script)
	if ( !global.exportScript.empty() ) {
		QPushButton *btn = _eventSummaryPreferred->exportButton();
		btn->setVisible(true);
		btn->setText("");
		btn->setIcon(Gui::icon("publish_event"));
		btn->setFlat(true);
		btn->setToolTip("Publish event");
		connect(btn, &QPushButton::clicked, this, &MainWindow::publishEvent);
	}

	// -----------------------------------------------------------------------
	// OriginLocatorView config (read from settings)
	// -----------------------------------------------------------------------
	Gui::OriginLocatorView::Config locatorConfig;
	Gui::PickerView::Config        pickerConfig;
	Gui::AmplitudeView::Config     amplitudeConfig;

	pickerConfig.recordURL    = SCApp->recordStreamURL().c_str();
	amplitudeConfig.recordURL = SCApp->recordStreamURL().c_str();

	locatorConfig.reductionVelocityP          = global.reductionVelocityP;
	locatorConfig.drawMapLines                = global.drawMapLines;
	locatorConfig.drawGridLines               = global.drawGridLines;
	locatorConfig.computeMissingTakeOffAngles = global.computeMissingTakeOffAngles;

	// -----------------------------------------------------------------------
	// Picker config — mirrors scolv's mainframe.cpp config loading
	// -----------------------------------------------------------------------
	try { pickerConfig.showCrossHair = SCApp->configGetBool("picker.showCrossHairCursor"); }
	catch ( ... ) {}
	try { pickerConfig.ignoreUnconfiguredStations = SCApp->configGetBool("picker.ignoreUnconfiguredStations"); }
	catch ( ... ) {}
	try { pickerConfig.loadAllComponents = SCApp->configGetBool("picker.loadAllComponents"); }
	catch ( ... ) {}
	try { pickerConfig.loadAllPicks = SCApp->configGetBool("picker.loadAllPicks"); }
	catch ( ... ) {}
	try { pickerConfig.loadStrongMotionData = SCApp->configGetBool("picker.loadStrongMotion"); }
	catch ( ... ) {}
	try { pickerConfig.showAllComponents = SCApp->configGetBool("picker.showAllComponents"); }
	catch ( ... ) {}
	try { pickerConfig.repickerSignalStart = SCApp->configGetDouble("picker.repickerStart"); }
	catch ( ... ) {}
	try { pickerConfig.repickerSignalEnd = SCApp->configGetDouble("picker.repickerEnd"); }
	catch ( ... ) {}
	try { pickerConfig.defaultAddStationsDistance = SCApp->configGetDouble("olv.defaultAddStationsDistance"); }
	catch ( ... ) {}
	try { pickerConfig.loadStationsWithinDistanceInitially = SCApp->configGetBool("olv.loadAdditionalStations"); }
	catch ( ... ) {}
	try { pickerConfig.hideStationsWithoutData = SCApp->configGetBool("olv.hideStationsWithoutData"); }
	catch ( ... ) {}
	try { pickerConfig.hideDisabledStations = SCApp->configGetBool("olv.hideDisabledStations"); }
	catch ( ... ) {}
	try { pickerConfig.ignoreDisabledStations = SCApp->configGetBool("olv.ignoreDisabledStations"); }
	catch ( ... ) {}
	try { pickerConfig.defaultDepth = SCApp->configGetDouble("olv.defaultDepth"); }
	catch ( ... ) {}

	// Phase groups and favourites
	try {
		vector<string> phases = SCApp->configGetStrings("picker.phases.favourites");
		for ( const auto &ph : phases )
			pickerConfig.favouritePhases.append(ph.c_str());
	}
	catch ( ... ) {}

	try {
		vector<string> phases = SCApp->configGetStrings("picker.showPhases");
		for ( const auto &ph : phases )
			pickerConfig.addShowPhase(ph.c_str());
	}
	catch ( ... ) {}

	// Picker filters
	try {
		vector<string> filters = SCApp->configGetStrings("picker.filters");
		for ( const auto &f : filters ) {
			if ( f.empty() ) continue;
			QString qs = f.c_str();
			QStringList tok = qs.split(";");
			if ( tok.size() != 2 ) continue;
			pickerConfig.addFilter(tok[0], tok[1]);
		}
	}
	catch ( ... ) {
		pickerConfig.addFilter("Teleseismic", "BW(3,0.7,2)");
		pickerConfig.addFilter("Regional",    "BW(3,2,6)");
		pickerConfig.addFilter("Local",       "BW(3,4,10)");
	}

	// Auxiliary channel profiles
	try {
		auto patterns = SCApp->configGetStrings("picker.auxiliary.channels");
		double minD = 0, maxD = 1000;
		try { minD = SCApp->configGetDouble("picker.auxiliary.minimumDistance"); } catch ( ... ) {}
		try { maxD = SCApp->configGetDouble("picker.auxiliary.maximumDistance"); } catch ( ... ) {}
		pickerConfig.auxiliaryChannelProfiles.push_back({ QString(), patterns, minD, maxD });
	}
	catch ( ... ) {}

	try {
		auto profiles = SCApp->configGetStrings("picker.auxiliary.profiles");
		for ( const auto &profile : profiles ) {
			auto patterns = SCApp->configGetStrings("picker.auxiliary.profiles." + profile + ".channels");
			double minD = 0, maxD = 1000;
			try { minD = SCApp->configGetDouble("picker.auxiliary.profiles." + profile + ".minimumDistance"); } catch ( ... ) {}
			try { maxD = SCApp->configGetDouble("picker.auxiliary.profiles." + profile + ".maximumDistance"); } catch ( ... ) {}
			pickerConfig.auxiliaryChannelProfiles.push_back({ profile.data(), patterns, minD, maxD });
		}
	}
	catch ( ... ) {}

	// -----------------------------------------------------------------------
	// Amplitude picker config
	// -----------------------------------------------------------------------
	try { amplitudeConfig.preOffset = Core::TimeSpan(SCApp->configGetDouble("amplitudePicker.preOffset")); }
	catch ( ... ) { amplitudeConfig.preOffset = Core::TimeSpan(300, 0); }
	try { amplitudeConfig.postOffset = Core::TimeSpan(SCApp->configGetDouble("amplitudePicker.postOffset")); }
	catch ( ... ) { amplitudeConfig.postOffset = Core::TimeSpan(300, 0); }
	try { amplitudeConfig.defaultNoiseBegin = SCApp->configGetDouble("amplitudePicker.defaultNoiseBegin"); } catch ( ... ) {}
	try { amplitudeConfig.defaultNoiseEnd = SCApp->configGetDouble("amplitudePicker.defaultNoiseEnd"); } catch ( ... ) {}
	try { amplitudeConfig.defaultSignalBegin = SCApp->configGetDouble("amplitudePicker.defaultSignalBegin"); } catch ( ... ) {}
	try { amplitudeConfig.defaultSignalEnd = SCApp->configGetDouble("amplitudePicker.defaultSignalEnd"); } catch ( ... ) {}

	try {
		vector<string> filters = SCApp->configGetStrings("amplitudePicker.filters");
		for ( const auto &f : filters ) {
			if ( f.empty() ) continue;
			QString qs = f.c_str();
			QStringList tok = qs.split(";");
			if ( tok.size() != 2 ) continue;
			amplitudeConfig.addFilter(tok[0], tok[1]);
		}
	}
	catch ( ... ) {}

	// -----------------------------------------------------------------------
	// OriginLocatorView — the core location widget
	// -----------------------------------------------------------------------
	_originLocator = new Gui::OriginLocatorView(mapTree.get(), pickerConfig, this);
	_originLocator->setConfig(locatorConfig);
	_originLocator->setDatabase(SCApp->query());

	try {
		_originLocator->setScript0(
			Environment::Instance()->absolutePath(
				SCApp->configGetString("scripts.script0")));
	}
	catch ( ... ) {}

	try {
		_originLocator->setScript1(
			Environment::Instance()->absolutePath(
				SCApp->configGetString("scripts.script1")));
	}
	catch ( ... ) {}

	QVBoxLayout *locLayout = new QVBoxLayout(_ui.tabLocation);
	locLayout->setContentsMargins(0, 0, 0, 0);
	locLayout->addWidget(_originLocator);

	// Shared fields from pickerConfig (mirrors scolv)
	amplitudeConfig.auxiliaryChannelProfiles  = pickerConfig.auxiliaryChannelProfiles;
	amplitudeConfig.defaultAddStationsDistance = pickerConfig.defaultAddStationsDistance;
	amplitudeConfig.hideStationsWithoutData   = pickerConfig.hideStationsWithoutData;
	amplitudeConfig.loadStrongMotionData      = pickerConfig.loadStrongMotionData;

	// -----------------------------------------------------------------------
	// MagnitudeView
	// -----------------------------------------------------------------------
	_magnitudes = new Gui::MagnitudeView(mapTree.get(), SCApp->query(), this);
	_magnitudes->setAmplitudeConfig(amplitudeConfig);
	_magnitudes->setComputeMagnitudesSilently(global.computeMagnitudesSilently);
	_magnitudes->setMagnitudeTypeSelectionEnabled(global.enableMagnitudeSelection);
	_magnitudes->setDrawGridLines(locatorConfig.drawGridLines);

	try {
		if ( !_magnitudes->setDefaultAggregationType(SCApp->configGetString("olv.defaultMagnitudeAggregation")) ) {
			SEISCOMP_ERROR("Unknown aggregation in olv.defaultMagnitudeAggregation: %s",
			               SCApp->configGetString("olv.defaultMagnitudeAggregation").c_str());
		}
	}
	catch ( ... ) {}

	QVBoxLayout *magLayout = new QVBoxLayout(_ui.tabMagnitudes);
	magLayout->setContentsMargins(0, 0, 0, 0);
	magLayout->addWidget(_magnitudes);

	// -----------------------------------------------------------------------
	// EventEdit (event-level review: event type, comments, publication)
	// -----------------------------------------------------------------------
	_eventEdit = new Gui::EventEdit(SCApp->query(), mapTree.get(), this);

	QVBoxLayout *evtLayout = new QVBoxLayout(_ui.tabEvent);
	evtLayout->setContentsMargins(0, 0, 0, 0);
	evtLayout->addWidget(_eventEdit);

	// -----------------------------------------------------------------------
	// EventListView — the event catalogue browser
	// -----------------------------------------------------------------------
	_eventList = new Gui::EventListView(SCApp->query(), true, false, this);

	QVBoxLayout *evtListLayout = new QVBoxLayout(_ui.tabEvents);
	evtListLayout->setContentsMargins(0, 0, 0, 0);
	evtListLayout->addWidget(_eventList);

	// -----------------------------------------------------------------------
	// Signal connections: OriginLocatorView
	// -----------------------------------------------------------------------
	connect(_originLocator, SIGNAL(undoStateChanged(bool)),
	        _ui.actionUndo, SLOT(setEnabled(bool)));
	connect(_originLocator, SIGNAL(redoStateChanged(bool)),
	        _ui.actionRedo, SLOT(setEnabled(bool)));

	connect(_ui.actionUndo, SIGNAL(triggered(bool)),
	        _originLocator, SLOT(undo()));
	connect(_ui.actionRedo, SIGNAL(triggered(bool)),
	        _originLocator, SLOT(redo()));

	// New origin selected from list → update locator display
	connect(_originLocator,
	        SIGNAL(newOriginSet(Seiscomp::DataModel::Origin*,
	                            Seiscomp::DataModel::Event*, bool, bool)),
	        this,
	        SLOT(setOrigin(Seiscomp::DataModel::Origin*,
	                       Seiscomp::DataModel::Event*, bool, bool)));

	// Locator relocated an origin → update magnitudes and summaries
	connect(_originLocator,
	        SIGNAL(updatedOrigin(Seiscomp::DataModel::Origin*)),
	        this,
	        SLOT(onUpdatedOrigin(Seiscomp::DataModel::Origin*)));

	// Magnitudes computed after relocation → switch to magnitude tab
	connect(_originLocator,
	        SIGNAL(magnitudesAdded(Seiscomp::DataModel::Origin*,
	                               Seiscomp::DataModel::Event*)),
	        this,
	        SLOT(onMagnitudesAdded(Seiscomp::DataModel::Origin*,
	                               Seiscomp::DataModel::Event*)));

	// committedOrigin 4th param is AmplitudePtr, not StationMagnitude (Qt prefix match)
	connect(_originLocator,
	        SIGNAL(committedOrigin(Seiscomp::DataModel::Origin*,
	                               Seiscomp::DataModel::Event*,
	                               const Seiscomp::Gui::ObjectChangeList<Seiscomp::DataModel::Pick>&,
	                               const std::vector<Seiscomp::DataModel::AmplitudePtr>&)),
	        this,
	        SLOT(onCommittedOrigin(Seiscomp::DataModel::Origin*,
	                               Seiscomp::DataModel::Event*)));

	connect(_originLocator,
	        SIGNAL(artificalOriginCreated(Seiscomp::DataModel::Origin*)),
	        this,
	        SLOT(setArtificialOrigin(Seiscomp::DataModel::Origin*)));

	connect(_originLocator, SIGNAL(waveformsRequested()),
	        this, SLOT(showWaveforms()));
	connect(_originLocator, SIGNAL(eventListRequested()),
	        this, SLOT(showEventList()));

	_originLocator->setMagnitudeCalculationEnabled(true);

	connect(_originLocator, SIGNAL(computeMagnitudesRequested()),
	        _magnitudes, SLOT(computeMagnitudes()));

	// -----------------------------------------------------------------------
	// Signal connections: MagnitudeView signals → OriginLocatorView slots
	// (magnitudeRemoved/Selected are slots on OriginLocatorView, not signals)
	// -----------------------------------------------------------------------
	connect(_magnitudes,
	        SIGNAL(localAmplitudesAvailable(Seiscomp::DataModel::Origin*,
	                                        AmplitudeSet*, StringSet*)),
	        _originLocator,
	        SLOT(setLocalAmplitudes(Seiscomp::DataModel::Origin*,
	                                AmplitudeSet*, StringSet*)));

	connect(_magnitudes,
	        SIGNAL(magnitudeRemoved(const QString &, Seiscomp::DataModel::Object*)),
	        _originLocator,
	        SLOT(magnitudeRemoved(const QString &, Seiscomp::DataModel::Object*)));

	connect(_magnitudes,
	        SIGNAL(magnitudeSelected(const QString &, Seiscomp::DataModel::Magnitude*)),
	        _originLocator,
	        SLOT(magnitudeSelected(const QString &, Seiscomp::DataModel::Magnitude*)));

	// After relocation magnitudes are added → reload magnitude view
	connect(_originLocator,
	        SIGNAL(magnitudesAdded(Seiscomp::DataModel::Origin*,
	                               Seiscomp::DataModel::Event*)),
	        _magnitudes, SLOT(reload()));

	// Disable rework after update or commit
	connect(_originLocator,
	        SIGNAL(updatedOrigin(Seiscomp::DataModel::Origin*)),
	        _magnitudes, SLOT(disableRework()));

	connect(_originLocator,
	        SIGNAL(committedOrigin(Seiscomp::DataModel::Origin*,
	                               Seiscomp::DataModel::Event*,
	                               const Seiscomp::Gui::ObjectChangeList<Seiscomp::DataModel::Pick>&,
	                               const std::vector<Seiscomp::DataModel::AmplitudePtr>&)),
	        _magnitudes, SLOT(disableRework()));

	// Send committed origin + picks + amplitudes to messaging/database
	connect(_originLocator,
	        SIGNAL(committedOrigin(Seiscomp::DataModel::Origin*,
	                               Seiscomp::DataModel::Event*,
	                               const Seiscomp::Gui::ObjectChangeList<Seiscomp::DataModel::Pick>&,
	                               const std::vector<Seiscomp::DataModel::AmplitudePtr>&)),
	        _eventList,
	        SLOT(insertOrigin(Seiscomp::DataModel::Origin*,
	                          Seiscomp::DataModel::Event*,
	                          const Seiscomp::Gui::ObjectChangeList<Seiscomp::DataModel::Pick>&,
	                          const std::vector<Seiscomp::DataModel::AmplitudePtr>&)));

	// -----------------------------------------------------------------------
	// Signal connections: EventListView
	// originSelected → loadOrigin (loads origin INTO the locator from DB)
	// -----------------------------------------------------------------------
	connect(_eventList,
	        SIGNAL(originSelected(Seiscomp::DataModel::Origin*,
	                              Seiscomp::DataModel::Event*)),
	        this,
	        SLOT(loadOrigin(Seiscomp::DataModel::Origin*,
	                        Seiscomp::DataModel::Event*)));

	connect(_eventList, SIGNAL(originAdded()),
	        this,       SLOT(originAdded()));

	connect(_eventList,
	        SIGNAL(originReferenceAdded(const std::string &,
	                                    Seiscomp::DataModel::OriginReference*)),
	        this,
	        SLOT(originReferenceAdded(const std::string &,
	                                  Seiscomp::DataModel::OriginReference*)));

	connect(_eventList, SIGNAL(visibleEventCountChanged()),
	        this,       SLOT(updateEventTabText()));

	connect(_eventList,
	        SIGNAL(eventAddedToList(Seiscomp::DataModel::Event*, bool)),
	        this,
	        SLOT(updateEventActionIndicator(Seiscomp::DataModel::Event*)));

	connect(_eventList,
	        SIGNAL(eventUpdatedInList(Seiscomp::DataModel::Event*)),
	        this,
	        SLOT(updateEventActionIndicator(Seiscomp::DataModel::Event*)));

	// EventSummary::selected → also goes through loadOrigin
	connect(_eventSummaryPreferred,
	        SIGNAL(selected(Seiscomp::DataModel::Origin*,
	                        Seiscomp::DataModel::Event*)),
	        this,
	        SLOT(loadOrigin(Seiscomp::DataModel::Origin*,
	                        Seiscomp::DataModel::Event*)));

	// -----------------------------------------------------------------------
	// Signal connections: messaging / objects
	// SCApp signals are addObject/removeObject/updateObject
	// -----------------------------------------------------------------------
	connect(SCApp,
	        SIGNAL(addObject(const QString &, Seiscomp::DataModel::Object*)),
	        this,
	        SLOT(objectAdded(const QString &, Seiscomp::DataModel::Object*)));

	connect(SCApp,
	        SIGNAL(removeObject(const QString &, Seiscomp::DataModel::Object*)),
	        this,
	        SLOT(objectRemoved(const QString &, Seiscomp::DataModel::Object*)));

	connect(SCApp,
	        SIGNAL(updateObject(const QString &, Seiscomp::DataModel::Object*)),
	        this,
	        SLOT(objectUpdated(const QString &, Seiscomp::DataModel::Object*)));

	// Forward messaging objects to MagnitudeView so it receives real-time updates
	connect(SCApp,
	        SIGNAL(addObject(const QString &, Seiscomp::DataModel::Object*)),
	        _magnitudes,
	        SLOT(addObject(const QString &, Seiscomp::DataModel::Object*)));

	connect(SCApp,
	        SIGNAL(updateObject(const QString &, Seiscomp::DataModel::Object*)),
	        _magnitudes,
	        SLOT(updateObject(const QString &, Seiscomp::DataModel::Object*)));

	connect(SCApp,
	        SIGNAL(removeObject(const QString &, Seiscomp::DataModel::Object*)),
	        _magnitudes,
	        SLOT(removeObject(const QString &, Seiscomp::DataModel::Object*)));

	// -----------------------------------------------------------------------
	// Signal connections: tab widget and menu actions
	// -----------------------------------------------------------------------
	connect(_ui.tabWidget, &QTabWidget::currentChanged,
	        this, &MainWindow::tabChanged);

	connect(_ui.actionOpen, &QAction::triggered, this, &MainWindow::fileOpen);
	connect(_ui.actionSave, &QAction::triggered, this, &MainWindow::fileSave);

	connect(_ui.actionShowSummary, &QAction::toggled,
	        _ui.frameSummary, &QWidget::setVisible);
	connect(_ui.actionShowEventList, &QAction::triggered,
	        this, &MainWindow::showEventList);

	connect(_ui.actionShowStations, SIGNAL(toggled(bool)),
	        _originLocator, SLOT(drawStations(bool)));
	connect(_ui.actionShowStations, SIGNAL(toggled(bool)),
	        _magnitudes, SLOT(drawStations(bool)));
	connect(_ui.actionShowStations, SIGNAL(toggled(bool)),
	        _eventEdit, SLOT(drawStations(bool)));

	connect(_ui.actionShowStationAnnotations, SIGNAL(toggled(bool)),
	        _originLocator, SLOT(drawStationAnnotations(bool)));
	connect(_ui.actionShowStationAnnotations, SIGNAL(toggled(bool)),
	        _magnitudes, SLOT(drawStationAnnotations(bool)));
	connect(_ui.actionShowStationAnnotations, SIGNAL(toggled(bool)),
	        _eventEdit, SLOT(drawStationAnnotations(bool)));

	// Apply initial state
	_originLocator->drawStations(_ui.actionShowStations->isChecked());
	_originLocator->drawStationAnnotations(_ui.actionShowStationAnnotations->isChecked());
	_magnitudes->drawStations(_ui.actionShowStations->isChecked());
	_magnitudes->drawStationAnnotations(_ui.actionShowStationAnnotations->isChecked());
	_eventEdit->drawStations(_ui.actionShowStations->isChecked());
	_eventEdit->drawStationAnnotations(_ui.actionShowStationAnnotations->isChecked());

	connect(_ui.actionPreviousEvent, SIGNAL(triggered(bool)),
	        _eventList, SLOT(setPreviousEvent()));
	connect(_ui.actionNextEvent, SIGNAL(triggered(bool)),
	        _eventList, SLOT(setNextEvent()));

	connect(_ui.actionCreateArtificialOrigin, SIGNAL(triggered(bool)),
	        _originLocator, SLOT(createArtificialOrigin()));

	connect(&_exportProcess,
	        SIGNAL(finished(int, QProcess::ExitStatus)),
	        this, SLOT(updateEventTabText()));

	// -----------------------------------------------------------------------
	// Toolbar icons
	// -----------------------------------------------------------------------
	_ui.actionUndo->setIcon(Gui::icon("undo"));
	_ui.actionRedo->setIcon(Gui::icon("redo"));

	// -----------------------------------------------------------------------
	// Settings menu
	// -----------------------------------------------------------------------
	_actionConfigureAcquisition = new QAction(this);
	_actionConfigureAcquisition->setObjectName(QString::fromUtf8("configureAcquisition"));
	_actionConfigureAcquisition->setShortcut(QKeySequence(Qt::Key_F3));
	_actionConfigureAcquisition->setText(tr("Configure &OriginLocatorView..."));
	addAction(_actionConfigureAcquisition);
	connect(_actionConfigureAcquisition, SIGNAL(triggered(bool)),
	        this, SLOT(configureAcquisition()));
	_ui.menuSettings->addAction(_actionConfigureAcquisition);

	if ( SCApp->isMessagingEnabled() || SCApp->isDatabaseEnabled() )
		_ui.menuSettings->addAction(_actionShowSettings);

	// Process Manager — shows running SeisComP modules
	{
		QAction *actionProcessManager = new QAction(this);
		actionProcessManager->setText(tr("&Process Manager"));
		actionProcessManager->setIcon(Gui::icon("process_manager"));
		addAction(actionProcessManager);
		connect(actionProcessManager, &QAction::triggered, []() {
			SCApp->processManager()->show();
		});
		_ui.menuView->addSeparator();
		_ui.menuView->addAction(actionProcessManager);
	}

	// Full-screen toggle provided by Gui::MainWindow
	_ui.menuView->insertAction(_ui.actionShowSummary, _actionToggleFullScreen);

	// -----------------------------------------------------------------------
	// Nearby Cities tab wiring
	// -----------------------------------------------------------------------
	connect(_ui.citiesTable->selectionModel(),
	        &QItemSelectionModel::selectionChanged,
	        this, &MainWindow::onCitySelectionChanged);
	connect(_ui.regionFormatCombo,
	        QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &MainWindow::updateRegionPreview);
	connect(_ui.setRegionBtn, &QPushButton::clicked,
	        this, &MainWindow::onSetRegionName);
	connect(_ui.citiesHamletFilter, &QCheckBox::toggled,
	        this, [this](bool){ updateCitiesTab(_currentOrigin.get()); });
	connect(_ui.citiesUseFullState, &QCheckBox::toggled,
	        this, [this](bool){ updateCitiesTab(_currentOrigin.get()); });

	_ui.citiesUseFullState->setChecked(global.citiesUseFullState);
	loadJsonLocations();

	SCApp->settings().endGroup();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
MainWindow::~MainWindow() {}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::setEventID(const std::string &eventID) {
	PublicObjectPtr po = SCApp->query()->loadObject(Event::TypeInfo(), eventID);
	EventPtr event = Event::Cast(po);
	if ( !event ) {
		QMessageBox::critical(this, tr("Load event"),
		    tr("Event %1 not found.").arg(eventID.c_str()));
		return;
	}
	_eventList->add(event.get(), nullptr);
	_eventList->selectEventID(event->publicID());
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::setOriginID(const std::string &originID) {
	PublicObjectPtr po = SCApp->query()->loadObject(Origin::TypeInfo(), originID);
	OriginPtr origin = Origin::Cast(po);
	if ( !origin ) {
		QMessageBox::critical(this, tr("Load origin"),
		    tr("Origin %1 not found.").arg(originID.c_str()));
		return;
	}
	EventPtr event = Event::Cast(SCApp->query()->getEvent(originID));
	_eventList->add(event.get(), origin.get());
	loadOrigin(origin.get(), event.get());
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::loadEvents(float days) {
	if ( days <= 0 ) return;

	SCApp->showMessage("Load event database");

	Core::TimeWindow tw;
	tw.setEndTime(Core::Time::UTC());
	tw.setStartTime(tw.endTime() - Core::TimeSpan(days * 86400.0));

	_eventList->setInterval(tw);
	_eventList->readFromDatabase();
	_eventList->selectFirstEnabledEvent();

	if ( _trayIcon && _eventList->eventCount() > 0 ) {
		_trayIcon->showMessage(
		    tr("Finished"),
		    tr("%1 loaded %2 events")
		    .arg(SCApp->name().c_str())
		    .arg(_eventList->eventCount()));
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::setOffline(bool offline) {
	_offline = offline;
	_ui.actionOpen->setEnabled(offline);
	_ui.actionSave->setEnabled(offline);
	if ( _eventList ) _eventList->setMessagingEnabled(!offline);
	if ( _eventEdit ) _eventEdit->setMessagingEnabled(!offline);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::openFile(const std::string &filename) {
	if ( filename.empty() ) return;

	IO::XMLArchive ar;
	if ( !ar.open(filename.c_str()) ) {
		QMessageBox::critical(this, tr("Error"),
		    tr("Cannot open file: %1").arg(filename.c_str()));
		return;
	}

	_offlineData = nullptr;
	_offlineJournal = nullptr;
	_currentOrigin = nullptr;

	_originLocator->clear();
	_magnitudes->setOrigin(nullptr, nullptr);
	_eventEdit->setEvent(nullptr, nullptr);
	_eventSummaryPreferred->setEvent(nullptr);
	_eventSummaryCurrent->setEvent(nullptr);
	_eventList->clear();

	EventParametersPtr ep;
	ar >> ep;

	if ( !ep ) {
		QMessageBox::critical(this, tr("Error"),
		    tr("No EventParameters found in: %1").arg(filename.c_str()));
		return;
	}

	ar >> _offlineJournal;
	_offlineData = ep;

	if ( !_offlineData->registered() ) {
		if ( !_offlineData->setPublicID(_offlineData->publicID()) ) {
			_offlineData = nullptr;
			QMessageBox::critical(this, tr("Error"),
			    tr("Unable to register EventParameters globally."));
			return;
		}
	}

	std::set<std::string> associatedOriginIDs;
	for ( size_t i = 0; i < ep->eventCount(); ++i ) {
		DataModel::Event *ev = ep->event(i);
		_eventList->add(ev, nullptr);
		for ( size_t j = 0; j < ev->originReferenceCount(); ++j )
			associatedOriginIDs.insert(ev->originReference(j)->originID());
	}

	for ( size_t i = 0; i < ep->originCount(); ++i ) {
		DataModel::Origin *org = ep->origin(i);
		if ( associatedOriginIDs.find(org->publicID()) == associatedOriginIDs.end() )
			_eventList->add(nullptr, org);
	}

	_eventList->selectEvent(0);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::toggledFullScreen(bool fs) {
	if ( _trayIcon )
		_trayIcon->setVisible(!fs);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::closeEvent(QCloseEvent *e) {
	if ( _exportProcess.state() != QProcess::NotRunning ) {
		if ( global.exportScriptSilentTerminate ) {
			_exportProcess.terminate();
		}
		else {
			int ret = QMessageBox::question(
			    this, tr("Export running"),
			    tr("An export script is still running.\n"
			       "Do you want to terminate it and close?"),
			    QMessageBox::Yes | QMessageBox::No);
			if ( ret == QMessageBox::No ) {
				e->ignore();
				return;
			}
			_exportProcess.terminate();
		}
	}

	Gui::MainWindow::closeEvent(e);
	if ( !e->isAccepted() ) return;

	_originLocator->close();
	_magnitudes->close();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::objectAdded(const QString &parentID, DataModel::Object *obj) {
	Pick *pick = Pick::Cast(obj);
	if ( pick ) {
		_originLocator->addPick(pick);
		return;
	}

	OriginReference *ref = OriginReference::Cast(obj);
	if ( ref ) {
		originReferenceAdded(parentID.toStdString(), ref);
		return;
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::objectRemoved(const QString &parentID, DataModel::Object *obj) {
	OriginReference *ref = OriginReference::Cast(obj);
	if ( ref ) {
		// EventListView only has originReferenceAdded; removal is handled
		// automatically via notifiers in the event list model.
		return;
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::objectUpdated(const QString &, DataModel::Object *obj) {
	Event *event = Event::Cast(obj);
	if ( event ) {
		if ( event->publicID() == _eventID ) {
			if ( _currentOrigin &&
			     _currentOrigin->publicID() == event->preferredOriginID() ) {
				_magnitudes->setPreferredMagnitudeID(event->preferredMagnitudeID());
			}
		}
		// Refresh the action indicator whenever any event record changes
		// (type, typeCertainty, preferredOriginID, etc.)
		updateEventActionIndicator(event);
		return;
	}

	// When an origin's evaluation status/mode changes, find its parent event
	// and refresh the indicator for that event row.
	Origin *origin = Origin::Cast(obj);
	if ( origin ) {
		Event *ev = nullptr;
		// Walk all top-level tree items to find which event owns this origin
		QTreeWidget *tree = _eventList->eventTree();
		if ( tree ) {
			for ( int i = 0; i < tree->topLevelItemCount(); ++i ) {
				Event *candidate = Gui::EventListView::eventFromTreeItem(
				    tree->topLevelItem(i));
				if ( candidate &&
				     candidate->preferredOriginID() == origin->publicID() ) {
					ev = candidate;
					break;
				}
			}
		}
		if ( ev ) updateEventActionIndicator(ev);
		return;
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::eventAdded(DataModel::Event *event, bool fromNotification) {
	if ( fromNotification && _trayIcon ) {
		_trayMessageEventID = event->publicID();
		_trayIcon->showMessage(
		    tr("New event"),
		    tr("%1").arg(event->publicID().c_str()),
		    QSystemTrayIcon::Information, 5000);
	}
	updateEventTabText();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::updateEventTabText() {
	int idx = _ui.tabWidget->indexOf(_ui.tabEvents);
	if ( idx < 0 ) return;
	int count = _eventList->visibleEventCount();
	_ui.tabWidget->setTabText(
	    idx, count > 0 ? tr("Events (%1)").arg(count) : tr("Events"));
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
static std::string expandEnvVars(const std::string &path) {
	std::string result;
	result.reserve(path.size());
	size_t i = 0;
	while ( i < path.size() ) {
		if ( path[i] == '$' && i + 1 < path.size() ) {
			size_t start, end;
			if ( path[i+1] == '{' ) {
				start = i + 2;
				end   = path.find('}', start);
				if ( end == std::string::npos ) { result += path[i]; ++i; continue; }
				std::string var = path.substr(start, end - start);
				const char *val = std::getenv(var.c_str());
				if ( val ) result += val;
				i = end + 1;
			}
			else {
				start = i + 1;
				end   = start;
				while ( end < path.size() &&
				        (std::isalnum((unsigned char)path[end]) || path[end] == '_') )
					++end;
				std::string var = path.substr(start, end - start);
				const char *val = std::getenv(var.c_str());
				if ( val ) result += val;
				i = end;
			}
		}
		else {
			result += path[i++];
		}
	}
	return result;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::loadJsonLocations() {
	_jsonLocations.clear();
	if ( global.citiesJsonFile.empty() ) return;

	// Resolve SeisComP path variables (@DATADIR@ etc.) then env vars ($VAR)
	std::string resolved =
	    expandEnvVars(Environment::Instance()->resolvePath(global.citiesJsonFile));

	QFile f(QString::fromStdString(resolved));
	if ( !f.open(QIODevice::ReadOnly) ) {
		SEISCOMP_WARNING("cities.jsonFile: cannot open '%s'", resolved.c_str());
		return;
	}

	QJsonParseError err;
	QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
	if ( doc.isNull() ) {
		SEISCOMP_WARNING("cities.jsonFile: JSON parse error: %s",
		                 err.errorString().toStdString().c_str());
		return;
	}
	if ( !doc.isArray() ) {
		SEISCOMP_WARNING("cities.jsonFile: root element is not an array");
		return;
	}

	for ( const QJsonValue &val : doc.array() ) {
		if ( !val.isObject() ) continue;
		QJsonObject obj = val.toObject();

		JsonLocation loc;
		loc.name       = obj.value("name").toString().toStdString();
		loc.type       = obj.value("type").toString().toStdString();
		loc.state      = obj.value("state").toString().toStdString();
		loc.stateFull  = obj.value("state_full").toString().toStdString();
		loc.country    = obj.value("country").toString().toStdString();
		loc.lat        = obj.value("lat").toDouble();
		loc.lon        = obj.value("lon").toDouble();
		loc.population = obj.value("population").toDouble();

		if ( loc.name.empty() ) continue;
		_jsonLocations.push_back(std::move(loc));
	}

	SEISCOMP_INFO("Loaded %zu locations from cities.jsonFile", _jsonLocations.size());
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::updateCurrentRegionLabel(DataModel::Event *event) {
	if ( !event ) {
		_ui.currentRegionLabel->setText(tr("Region: —"));
		return;
	}
	for ( size_t i = 0; i < event->eventDescriptionCount(); ++i ) {
		auto *d = event->eventDescription(i);
		if ( d->type() == DataModel::REGION_NAME ) {
			_ui.currentRegionLabel->setText(
			    tr("Region: %1").arg(d->text().c_str()));
			return;
		}
	}
	_ui.currentRegionLabel->setText(tr("Region: —"));
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
QString MainWindow::formatRegionName(const QString &name, const QString &state,
                                     const QString &country, int distKm,
                                     const QString &dir) const {
	// state already holds the best available value (state_full > state > "")
	// fall back to country if neither is available
	QString loc = !state.isEmpty() ? state : country;

	switch ( _ui.regionFormatCombo->currentIndex() ) {
		case 0: return QString("%1 of %2%3")
		            .arg(dir, name, loc.isEmpty() ? "" : ", " + loc);
		case 1: return QString("%1, %2 at %3 km")
		            .arg(name, dir).arg(distKm);
		case 2: return QString("%1 km %2 of %3")
		            .arg(distKm).arg(dir, name);
		case 3: return QString("%1 (%2, %3 km)")
		            .arg(name, dir).arg(distKm);
		case 4: return QString("%1 km %2 of %3%4")
		            .arg(distKm).arg(dir, name,
		                             loc.isEmpty() ? "" : ", " + loc);
		default: return name;
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::onCitySelectionChanged() {
	updateRegionPreview();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::updateRegionPreview() {
	int row = _ui.citiesTable->currentRow();
	if ( row < 0 ) {
		_ui.regionPreview->clear();
		_ui.setRegionBtn->setEnabled(false);
		return;
	}

	auto cell = [&](int col) -> QString {
		auto *item = _ui.citiesTable->item(row, col);
		return item ? item->text() : QString();
	};

	QString name    = cell(0);
	// col 1 = Type (skip), col 2 = State, col 3 = Country
	QString state   = cell(2);
	QString country = cell(3);
	int     distKm  = static_cast<int>(cell(4).toDouble() + 0.5);
	QString dir     = cell(5);

	_ui.regionPreview->setText(
	    formatRegionName(name, state, country, distKm, dir));
	_ui.setRegionBtn->setEnabled(!_eventID.empty());
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::onSetRegionName() {
	QString regionText = _ui.regionPreview->text().trimmed();
	if ( regionText.isEmpty() ) return;
	if ( _eventID.empty() ) {
		QMessageBox::warning(this, tr("No Event"),
		                     tr("No event is currently loaded."));
		return;
	}

	if ( QMessageBox::question(
	         this, tr("Set Region Name"),
	         tr("Set region name for event %1 to:\n\n\"%2\"?")
	             .arg(_eventID.c_str(), regionText),
	         QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes )
		return;

	// Load event
	DataModel::EventPtr evt = DataModel::Event::Find(_eventID);
	if ( !evt && SCApp->query() )
		evt = DataModel::Event::Cast(
		    SCApp->query()->loadObject(DataModel::Event::TypeInfo(), _eventID));
	if ( !evt ) {
		QMessageBox::critical(this, tr("Error"),
		                      tr("Could not load event '%1'.")
		                          .arg(_eventID.c_str()));
		return;
	}

	// Load EventDescriptions (so we can find an existing REGION_NAME)
	if ( SCApp->query() )
		SCApp->query()->loadEventDescriptions(evt.get());

	// Find or create REGION_NAME EventDescription
	DataModel::EventDescription    *desc = nullptr;
	DataModel::Operation            op   = DataModel::OP_ADD;
	DataModel::EventDescriptionPtr  newDesc;

	for ( size_t i = 0; i < evt->eventDescriptionCount(); ++i ) {
		if ( evt->eventDescription(i)->type() == DataModel::REGION_NAME ) {
			desc = evt->eventDescription(i);
			op   = DataModel::OP_UPDATE;
			break;
		}
	}

	if ( !desc ) {
		newDesc = new DataModel::EventDescription;
		newDesc->setType(DataModel::REGION_NAME);
		evt->add(newDesc.get());
		desc = newDesc.get();
	}

	desc->setText(regionText.toStdString());

	// Build and send notifier message to the EVENT group
	DataModel::NotifierMessagePtr msg = new DataModel::NotifierMessage;
	msg->attach(new DataModel::Notifier(
	    "EventParameters", DataModel::OP_UPDATE, evt.get()));
	msg->attach(new DataModel::Notifier(
	    evt->publicID(), op, desc));

	if ( !SCApp->sendMessage(SCApp->messageGroups().event.c_str(), msg.get()) ) {
		QMessageBox::critical(this, tr("Error"),
		                      tr("Failed to send region name update.\n"
		                         "Check messaging connection."));
		return;
	}

	// Apply locally so the label updates immediately
	for ( DataModel::NotifierMessage::iterator it = msg->begin();
	      it != msg->end(); ++it )
		SCApp->emitNotifier(it->get());

	_ui.currentRegionLabel->setText(tr("Region: %1").arg(regionText));

	QMessageBox::information(this, tr("Region Name Set"),
	                         tr("Region name updated to:\n\"%1\"").arg(regionText));
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
namespace {

// Numeric QTableWidgetItem so distance and population columns sort correctly
class NumericItem : public QTableWidgetItem {
	public:
		NumericItem(double value, const QString &text)
		: QTableWidgetItem(text), _value(value) {}

		bool operator<(const QTableWidgetItem &other) const override {
			const NumericItem *o = dynamic_cast<const NumericItem *>(&other);
			return o ? _value < o->_value
			         : QTableWidgetItem::operator<(other);
		}

	private:
		double _value;
};

static const char *compassDir(double az) {
	static const char *dirs[16] = {
	    "N","NNE","NE","ENE","E","ESE","SE","SSE",
	    "S","SSW","SW","WSW","W","WNW","NW","NNW"
	};
	int idx = static_cast<int>((az + 11.25) / 22.5) % 16;
	return dirs[idx];
}

// Returns true if the event requires operator action.
// Logic mirrors the operational rules agreed for scolvx highlighting.
static bool requiresAction(DataModel::Event *event) {
	if ( !event ) return false;

	// Load preferred origin to check its evaluation fields
	Origin *pref = Origin::Find(event->preferredOriginID());

	OPT(EvaluationMode)   mode;
	OPT(EvaluationStatus) status;
	if ( pref ) {
		try { mode   = pref->evaluationMode();   } catch ( ... ) {}
		try { status = pref->evaluationStatus(); } catch ( ... ) {}
	}

	OPT(EventType)          evtType;
	OPT(EventTypeCertainty) certainty;
	try { evtType   = event->type();          } catch ( ... ) {}
	try { certainty = event->typeCertainty(); } catch ( ... ) {}

	// ---- No-action conditions (take priority) --------------------------------
	// Confirmed earthquake
	if ( evtType && *evtType == EARTHQUAKE &&
	     status  && *status  == CONFIRMED )
		return false;
	// Reviewed or Final
	if ( status && (*status == REVIEWED || *status == FINAL) )
		return false;
	// Not existing with known certainty
	if ( evtType    && *evtType    == NOT_EXISTING &&
	     certainty  && *certainty  == KNOWN )
		return false;
	// Outside of network interest
	if ( evtType && *evtType == OUTSIDE_OF_NETWORK_INTEREST )
		return false;
	// Quarry blast
	if ( evtType && *evtType == QUARRY_BLAST )
		return false;

	// ---- Requires-action conditions -----------------------------------------
	// Preliminary earthquake (assessed by DS, still needs action)
	if ( evtType && *evtType == EARTHQUAKE &&
	     status  && *status  == PRELIMINARY )
		return true;
	// Any automatic origin
	if ( mode && *mode == AUTOMATIC )
		return true;
	// Rejected with no certainty set
	if ( status && *status == REJECTED && !certainty )
		return true;

	return false;
}

static QIcon makeActionDot() {
	QPixmap pm(12, 12);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing);
	p.setBrush(QColor(255, 140, 0));   // orange, visible on light and dark themes
	p.setPen(Qt::NoPen);
	p.drawEllipse(1, 1, 10, 10);
	return QIcon(pm);
}

} // anonymous namespace
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::updateEventActionIndicator(DataModel::Event *event) {
	if ( !event ) return;

	static QIcon dotIcon = makeActionDot();
	static QIcon noIcon;

	QTreeWidget *tree = _eventList->eventTree();
	if ( !tree ) return;

	const bool needs = requiresAction(event);
	const std::string &eid = event->publicID();

	// Scan top-level items — each is an event row
	for ( int i = 0; i < tree->topLevelItemCount(); ++i ) {
		QTreeWidgetItem *item = tree->topLevelItem(i);
		Event *ev = Gui::EventListView::eventFromTreeItem(item);
		if ( !ev || ev->publicID() != eid ) continue;
		item->setIcon(0, needs ? dotIcon : noIcon);
		break;
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::updateCitiesTab(DataModel::Origin *origin) {
	QTableWidget *t = _ui.citiesTable;
	t->setSortingEnabled(false);
	t->setRowCount(0);
	_ui.regionPreview->clear();
	_ui.setRegionBtn->setEnabled(false);

	if ( !origin ) return;

	double originLat  = origin->latitude().value();
	double originLon  = origin->longitude().value();
	double maxDistDeg = Math::Geo::km2deg(global.citiesMaxDist);
	bool   hideHamlets = _ui.citiesHamletFilter->isChecked();

	// col indices: 0=Name, 1=Type, 2=State, 3=Country, 4=Dist, 5=Dir, 6=Pop, 7=★
	struct Entry {
		double  distKm;
		double  az;
		QString name;
		QString type;
		QString state;      // display value: stateFull ?? state ?? ""
		QString country;
		double  population;
		bool    isCapital;
	};
	std::vector<Entry> entries;

	// --- cities.xml source ---
	for ( const auto &city : SCApp->cities() ) {
		double dist, az;
		Math::Geo::delazi(originLat, originLon, city.lat, city.lon,
		                  &dist, &az);
		if ( dist > maxDistDeg ) continue;

		bool isCapital = city.category() == "C";
		if ( !isCapital &&
		     static_cast<int>(city.population()) < global.citiesMinPopulation )
			continue;

		entries.push_back({
		    Math::Geo::deg2km(dist), az,
		    QString::fromStdString(city.name()),
		    QString(),   // no type in cities.xml
		    QString(),   // no state in cities.xml
		    QString::fromStdString(city.countryID()),
		    city.population(),
		    isCapital
		});
	}

	// --- JSON source ---
	for ( const auto &loc : _jsonLocations ) {
		if ( hideHamlets && loc.type == "hamlet" ) continue;

		double dist, az;
		Math::Geo::delazi(originLat, originLon, loc.lat, loc.lon,
		                  &dist, &az);
		if ( dist > maxDistDeg ) continue;

		// Only apply population filter when population is known (> 0)
		if ( loc.population > 0 && loc.population < global.citiesMinPopulation ) continue;

		// State column: use full name or abbreviation per checkbox
		bool useFullState = _ui.citiesUseFullState->isChecked();
		QString stateDisplay = (useFullState && !loc.stateFull.empty())
		    ? QString::fromStdString(loc.stateFull)
		    : QString::fromStdString(loc.state);

		entries.push_back({
		    Math::Geo::deg2km(dist), az,
		    QString::fromStdString(loc.name),
		    QString::fromStdString(loc.type),
		    stateDisplay,
		    QString::fromStdString(loc.country),
		    loc.population,
		    false   // JSON locations have no capital flag
		});
	}

	std::sort(entries.begin(), entries.end(),
	          [](const Entry &a, const Entry &b){ return a.distKm < b.distKm; });

	if ( static_cast<int>(entries.size()) > global.citiesMaxCount )
		entries.resize(global.citiesMaxCount);

	t->setRowCount(static_cast<int>(entries.size()));

	for ( int i = 0; i < static_cast<int>(entries.size()); ++i ) {
		const Entry &e = entries[i];

		auto boldify = [&](QTableWidgetItem *item) {
			if ( e.isCapital ) {
				QFont f = item->font();
				f.setBold(true);
				item->setFont(f);
			}
			return item;
		};

		t->setItem(i, 0, boldify(new QTableWidgetItem(e.name)));
		t->setItem(i, 1, boldify(new QTableWidgetItem(e.type)));
		t->setItem(i, 2, boldify(new QTableWidgetItem(e.state)));
		t->setItem(i, 3, boldify(new QTableWidgetItem(e.country)));
		t->setItem(i, 4, boldify(new NumericItem(
		    e.distKm,
		    QString::number(static_cast<int>(e.distKm + 0.5)))));
		t->setItem(i, 5, boldify(new QTableWidgetItem(
		    QString::fromLatin1(compassDir(e.az)))));
		t->setItem(i, 6, boldify(new NumericItem(
		    e.population,
		    QString::number(static_cast<long long>(e.population)))));

		auto *capItem = new QTableWidgetItem(
		    e.isCapital ? QString("\u2605") : QString());
		capItem->setTextAlignment(Qt::AlignCenter);
		t->setItem(i, 7, capItem);
	}

	t->setSortingEnabled(true);
	t->horizontalHeader()->resizeSections(QHeaderView::ResizeToContents);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::setOrigin(DataModel::Origin *origin,
                           DataModel::Event  *event,
                           bool newOrigin, bool) {
	// Called from newOriginSet — the locator already owns the origin.
	// Do NOT call _originLocator->setOrigin() here (would re-trigger the signal).
	if ( !origin ) return;

	_currentOrigin = origin;
	_eventID       = event ? event->publicID() : std::string();

	_magnitudes->setOrigin(origin, event);
	_magnitudes->setReadOnly(!newOrigin);
	if ( event )
		_magnitudes->setPreferredMagnitudeID(event->preferredMagnitudeID());

	if ( _eventSummaryPreferred )
		_eventSummaryPreferred->setEvent(event);
	if ( _eventSummaryCurrent )
		_eventSummaryCurrent->setEvent(event);

	if ( _eventEdit )
		_eventEdit->setEvent(event, origin);

	updateCitiesTab(origin);
	updateCurrentRegionLabel(event);

	_ui.tabWidget->setCurrentWidget(_ui.tabLocation);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::loadOrigin(DataModel::Origin *origin,
                             DataModel::Event  *event) {
	// Entry point when user selects from event list or summary.
	// Loads origin INTO the locator; newOriginSet fires → setOrigin() syncs the rest.
	if ( !origin ) return;
	_originLocator->setOrigin(origin, event);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::updateOrigin(DataModel::Origin *origin,
                               DataModel::Event  *event) {
	if ( _currentOrigin &&
	     _currentOrigin->publicID() == origin->publicID() )
		setOrigin(origin, event, false, false);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::onUpdatedOrigin(DataModel::Origin *origin) {
	// After relocation, the origin object is the same — just tell magnitudes
	// to disable rework (handled by signal connection). Nothing more needed;
	// setOrigin was already called via newOriginSet when the locator loaded it.
	(void)origin;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::onMagnitudesAdded(DataModel::Origin *origin,
                                    DataModel::Event  *event) {
	_magnitudes->setOrigin(origin, event);
	_ui.tabWidget->setCurrentWidget(_ui.tabMagnitudes);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::onCommittedOrigin(DataModel::Origin *origin,
                                    DataModel::Event  *event) {
	updateOrigin(origin, event);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::committedNewOrigin(DataModel::Origin *origin,
                                     DataModel::Event  *event) {
	updateOrigin(origin, event);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::setArtificialOrigin(DataModel::Origin *origin) {
	_originLocator->setOrigin(origin, nullptr);
	_ui.tabWidget->setCurrentWidget(_ui.tabLocation);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::originReferenceAdded(const std::string &eventID,
                                       DataModel::OriginReference *ref) {
	if ( _currentOrigin && _currentOrigin->publicID() == ref->originID() ) {
		EventPtr evt = Event::Find(eventID);
		if ( !evt && SCApp->query() )
			evt = Event::Cast(SCApp->query()->loadObject(Event::TypeInfo(), eventID));
		if ( evt ) {
			_eventID = evt->publicID();
			if ( _magnitudes )
				_magnitudes->setPreferredMagnitudeID(evt->preferredMagnitudeID());
			if ( _eventEdit )
				_eventEdit->setEvent(evt.get(), _currentOrigin.get());
			if ( _eventSummaryPreferred )
				_eventSummaryPreferred->setEvent(evt.get());
			if ( _eventSummaryCurrent )
				_eventSummaryCurrent->setEvent(evt.get());
		}
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::originReferenceRemoved(const std::string &,
                                         DataModel::OriginReference *) {
	// EventListView does not expose a public originReferenceRemoved slot;
	// handled internally via DataModel notifiers.
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::showMagnitude(const std::string &) {
	if ( _currentOrigin )
		_magnitudes->setOrigin(_currentOrigin.get(), nullptr);
	_ui.tabWidget->setCurrentWidget(_ui.tabMagnitudes);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::tabChanged(int) {
	_currentTabWidget = _ui.tabWidget->currentWidget();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::showWaveforms() {
	// PickerView is opened as a separate window by OriginLocatorView itself.
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::publishEvent() {
	if ( global.exportScript.empty() ) return;

	DataModel::Event *event = _eventSummaryPreferred->currentEvent();
	if ( !event ) return;

	if ( _exportProcess.state() != QProcess::NotRunning ) {
		QMessageBox::warning(this, tr("Export running"),
		    tr("An export is already in progress."));
		return;
	}

	_exportProcess.start(
	    QString::fromStdString(
	        Environment::Instance()->absolutePath(global.exportScript)),
	    QStringList() << QString::fromStdString(event->publicID()));
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::hoverEvent(const std::string &eventID) {
	if ( !_eventID.empty() && _eventID == eventID )
		_originLocator->setToolTip(tr("%1\nCurrently loaded").arg(eventID.c_str()));
	else
		_originLocator->setToolTip(eventID.c_str());
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::selectEvent(const std::string &eventID) {
	_eventList->selectEventID(eventID);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::showEventList() {
	_ui.tabWidget->setCurrentWidget(_ui.tabEvents);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::fileOpen() {
	QString filename = QFileDialog::getOpenFileName(
	    this, tr("Open event file"), QString(),
	    tr("XML files (*.xml);;All files (*)"));
	if ( !filename.isEmpty() )
		openFile(filename.toStdString());
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::fileSave() {
	// TODO: serialize _offlineData to XML
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::configureAcquisition() {
	Gui::PickerSettings dlg(_originLocator->config(),
	                        _originLocator->pickerConfig(),
	                        _magnitudes->amplitudeConfig());

	dlg.ui().cbComputeMagnitudesAfterRelocate->setEnabled(true);
	dlg.ui().cbComputeMagnitudesAfterRelocate->setChecked(global.computeMagnitudesAfterRelocate);
	dlg.ui().cbComputeMagnitudesSilently->setEnabled(true);
	dlg.ui().cbComputeMagnitudesSilently->setChecked(global.computeMagnitudesSilently);
	dlg.ui().cbAskForMagnitudeTypes->setChecked(global.enableMagnitudeSelection);
	dlg.setSaveEnabled(true);

	if ( dlg.exec() != QDialog::Accepted )
		return;

	global.computeMagnitudesAfterRelocate = dlg.ui().cbComputeMagnitudesAfterRelocate->isChecked();
	global.computeMagnitudesSilently      = dlg.ui().cbComputeMagnitudesSilently->isChecked();
	global.enableMagnitudeSelection       = dlg.ui().cbAskForMagnitudeTypes->isChecked();

	_magnitudes->setComputeMagnitudesSilently(global.computeMagnitudesSilently);
	_magnitudes->setMagnitudeTypeSelectionEnabled(global.enableMagnitudeSelection);

	Gui::OriginLocatorView::Config lc = dlg.locatorConfig();
	Gui::PickerView::Config        pc = dlg.pickerConfig();
	Gui::AmplitudeView::Config     ac = dlg.amplitudeConfig();

	_originLocator->setConfig(lc);
	_originLocator->setPickerConfig(pc);
	_magnitudes->setDrawGridLines(lc.drawGridLines);
	_magnitudes->setAmplitudeConfig(ac);

	if ( dlg.saveSettings() ) {
		SCApp->configSetBool("olv.computeMagnitudesAfterRelocate", global.computeMagnitudesAfterRelocate);
		SCApp->configSetBool("olv.computeMagnitudesSilently",      global.computeMagnitudesSilently);
		SCApp->configSetBool("olv.enableMagnitudeSelection",        global.enableMagnitudeSelection);
		SCApp->configSetDouble("olv.Pvel",                          lc.reductionVelocityP);
		SCApp->configSetBool("olv.drawMapLines",                    lc.drawMapLines);
		SCApp->configSetBool("olv.drawGridLines",                   lc.drawGridLines);
		SCApp->configSetBool("olv.computeMissingTakeOffAngles",     lc.computeMissingTakeOffAngles);
		SCApp->configSetDouble("olv.defaultAddStationsDistance",    pc.defaultAddStationsDistance);
		SCApp->configSetBool("olv.hideStationsWithoutData",         pc.hideStationsWithoutData);
		SCApp->configSetBool("olv.hideDisabledStations",            pc.hideDisabledStations);
		SCApp->configSetBool("olv.ignoreDisabledStations",          pc.ignoreDisabledStations);

		SCApp->configSetBool("picker.showCrossHairCursor",           pc.showCrossHair);
		SCApp->configSetBool("picker.ignoreUnconfiguredStations",    pc.ignoreUnconfiguredStations);
		SCApp->configSetBool("picker.loadAllComponents",             pc.loadAllComponents);
		SCApp->configSetBool("picker.loadAllPicks",                  pc.loadAllPicks);
		SCApp->configSetBool("picker.showDataInSensorUnit",          pc.showDataInSensorUnit);
		SCApp->configSetBool("picker.loadStrongMotion",              pc.loadStrongMotionData);
		SCApp->configSetBool("picker.limitStationAcquisition",       pc.limitStations);
		SCApp->configSetInt("picker.limitStationAcquisitionCount",   pc.limitStationCount);
		SCApp->configSetBool("picker.showAllComponents",             pc.showAllComponents);
		SCApp->configSetDouble("picker.allComponentsMaximumDistance",pc.allComponentsMaximumStationDistance);
		SCApp->configSetBool("picker.usePerStreamTimeWindows",       pc.usePerStreamTimeWindows);
		SCApp->configSetDouble("picker.preOffset",                   static_cast<double>(pc.preOffset));
		SCApp->configSetDouble("picker.postOffset",                  static_cast<double>(pc.postOffset));
		SCApp->configSetDouble("picker.minimumTimeWindow",           static_cast<double>(pc.minimumTimeWindow));
		SCApp->configSetDouble("picker.alignmentPosition",           pc.alignmentPosition);
		SCApp->configSetBool("picker.removeAutomaticPicksFromStationAfterManualReview",
		                     pc.removeAutomaticStationPicks);
		SCApp->configSetBool("picker.removeAllAutomaticPicksAfterManualReview",
		                     pc.removeAutomaticPicks);
		SCApp->configSetString("recordstream",                       pc.recordURL.toStdString());

		std::vector<std::string> filters;
		for ( const auto &entry : pc.filters )
			filters.push_back(QString("%1;%2").arg(entry.first, entry.second).toStdString());
		SCApp->configSetStrings("picker.filters", filters);

		filters.clear();
		for ( const auto &entry : ac.filters )
			filters.push_back(QString("%1;%2").arg(entry.first, entry.second).toStdString());

		if ( pc.repickerSignalStart )
			SCApp->configSetDouble("picker.repickerStart", *pc.repickerSignalStart);
		else
			SCApp->configUnset("picker.repickerStart");

		if ( pc.repickerSignalEnd )
			SCApp->configSetDouble("picker.repickerEnd", *pc.repickerSignalEnd);
		else
			SCApp->configUnset("picker.repickerEnd");

		SCApp->configSetString("picker.integration.preFilter",  pc.integrationFilter.toStdString());
		SCApp->configSetBool("picker.integration.applyOnce",    pc.onlyApplyIntegrationFilterOnce);

		SCApp->configSetDouble("amplitudePicker.preOffset",  static_cast<double>(ac.preOffset));
		SCApp->configSetDouble("amplitudePicker.postOffset", static_cast<double>(ac.postOffset));

		SCApp->saveConfiguration();
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::trayIconActivated(QSystemTrayIcon::ActivationReason reason) {
	if ( reason == QSystemTrayIcon::DoubleClick ) {
		showNormal();
		raise();
		activateWindow();
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::trayIconMessageClicked() {
	if ( !_trayMessageEventID.empty() ) {
		selectEvent(_trayMessageEventID);
		showNormal();
		raise();
		activateWindow();
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void MainWindow::originAdded() {
	if ( statusBar() )
		statusBar()->showMessage(
		    tr("A new origin arrived at %1 (local time)")
		    .arg(Core::Time::LocalTime()
		         .toString("%F %T").c_str()));
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool MainWindow::populateOrigin(DataModel::Origin *,
                                 DataModel::Event  *, bool) {
	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
DataModel::EventParametersPtr
MainWindow::_createEventParametersForPublication(const DataModel::Event *) {
	return nullptr;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
