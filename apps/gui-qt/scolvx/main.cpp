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


#define SEISCOMP_COMPONENT OriginLocatorX

#include <seiscomp/logging/log.h>

#include "app.h"


// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
namespace Seiscomp {
namespace OLocX {
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
Application::Application(int &argc, char **argv)
: Gui::Kicker<MainWindow>(argc, argv,
      DEFAULT | LOAD_INVENTORY | LOAD_CONFIGMODULE)
, _preloadDays(1.0f) {
	setLoadRegionsEnabled(true);
	setPrimaryMessagingGroup("LOCATION");
	addMessagingSubscription("EVENT");
	addMessagingSubscription("LOCATION");
	addMessagingSubscription("FOCMECH");
	addMessagingSubscription("MAGNITUDE");
	addMessagingSubscription("PICK");
	addMessagingSubscription("CONFIG");
	addMessagingSubscription("GUI");
	bindSettings(&global);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void Application::printUsage() const {
	std::cout
		<< "Usage:\n  " << name() << " [options]\n\n"
		<< "Review events and origins.\n";

	Gui::Application::printUsage();

	std::cout
		<< "Examples:\n"
		<< "  " << name() << " --debug\n\n"
		<< "  " << name() << " -E gempa2022abcd\n\n"
		<< "  " << name() << " --offline -i events.xml\n\n";
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Application::initConfiguration() {
	if ( !Gui::Kicker<MainWindow>::initConfiguration() )
		return false;

	try { _preloadDays = configGetDouble("loadEventDB"); }
	catch ( ... ) {}

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void Application::createCommandLineDescription() {
	Gui::Kicker<MainWindow>::createCommandLineDescription();

	commandline().addGroup("Options");
	commandline().addOption("Options", "origin,O",
	                        "Preload origin by public ID", &_originID);
	commandline().addOption("Options", "event,E",
	                        "Preload event by public ID",  &_eventID);
	commandline().addOption("Options", "offline",
	                        "Switch to offline mode (no messaging)");
	commandline().addOption("Options", "input-file,i",
	                        "Load events from XML file and switch to offline mode",
	                        &_inputFile);
	commandline().addOption("Options", "load-event-db",
	                        "Number of days to load from database",
	                        &_preloadDays);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Application::validateParameters() {
	if ( !Gui::Kicker<MainWindow>::validateParameters() ) return false;

	if ( commandline().hasOption("offline") || !_inputFile.empty() ) {
		setMessagingEnabled(false);
		if ( !isInventoryDatabaseEnabled() )
			setDatabaseEnabled(false, false);
	}

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Application::handleInitializationError(int stage) {
	if ( stage == CONFIGMODULE || stage == INVENTORY )
		return commandline().hasOption("offline");
	return Gui::Kicker<MainWindow>::handleInitializationError(stage);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void Application::setupUi(MainWindow *w) {
	if ( commandline().hasOption("offline") || !_inputFile.empty() ) {
		w->setOffline(true);
		if ( !_inputFile.empty() )
			w->openFile(_inputFile);
	}
	else if ( !_eventID.empty() )
		w->setEventID(_eventID);
	else if ( !_originID.empty() )
		w->setOriginID(_originID);
	else
		w->loadEvents(_preloadDays);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
int main(int argc, char **argv) {
	int retCode;
	{
		Seiscomp::OLocX::Application app(argc, argv);
		retCode = app();
		SEISCOMP_DEBUG("Objects remaining before destroy: %d",
		               Seiscomp::Core::BaseObject::ObjectCount());
	}
	SEISCOMP_DEBUG("Objects remaining after destroy: %d",
	               Seiscomp::Core::BaseObject::ObjectCount());
	return retCode;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
