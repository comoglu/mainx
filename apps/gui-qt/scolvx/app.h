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


#ifndef SEISCOMP_OLOCX_APP_H
#define SEISCOMP_OLOCX_APP_H


#include <seiscomp/gui/core/application.h>

#include "mainwindow.h"
#include "settings.h"


namespace Seiscomp {
namespace OLocX {


class Application : public Gui::Kicker<MainWindow> {
	Q_OBJECT


	public:
		Application(int &argc, char **argv);


	public:
		void printUsage() const override;
		void createCommandLineDescription() override;
		bool initConfiguration() override;
		bool validateParameters() override;
		bool handleInitializationError(int stage) override;
		void setupUi(MainWindow *w) override;


	private:
		std::string _originID;
		std::string _eventID;
		std::string _inputFile;
		float       _preloadDays{1.0f};
};


}
}


#endif
