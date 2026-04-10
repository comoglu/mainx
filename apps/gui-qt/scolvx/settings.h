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


#ifndef SEISCOMP_OLOCX_SETTINGS_H
#define SEISCOMP_OLOCX_SETTINGS_H


#include <seiscomp/system/application.h>

#include <string>


namespace Seiscomp {
namespace OLocX {


struct Settings : System::Application::AbstractSettings {
	void accept(System::Application::SettingsLinker &linker) override;

	bool        computeMagnitudesAfterRelocate{false};
	bool        computeMagnitudesSilently{false};
	bool        enableMagnitudeSelection{true};
	std::string exportScript;
	bool        exportScriptSilentTerminate{false};
	bool        systemTray{true};

	// Locator display config
	double      reductionVelocityP{6.0};
	bool        drawMapLines{true};
	bool        drawGridLines{true};
	bool        computeMissingTakeOffAngles{true};

	// Nearby cities tab
	double      citiesMaxDist{1000.0};
	int         citiesMaxCount{20};
	int         citiesMinPopulation{10000};
	std::string citiesJsonFile;
};


extern Settings global;


}
}


#endif
