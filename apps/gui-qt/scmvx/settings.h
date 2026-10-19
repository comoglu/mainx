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


#ifndef SEISCOMP_MAPVIEWX_CONFIG_H
#define SEISCOMP_MAPVIEWX_CONFIG_H


#include <seiscomp/datamodel/station.h>
#include <seiscomp/datamodel/stream.h>
#include <seiscomp/datamodel/waveformquality.h>
#include <seiscomp/datamodel/pick.h>
#include <seiscomp/system/application.h>
#include <seiscomp/utils/bindings.h>

#include <QRectF>
#include <map>
#include <vector>

#include "processor.h"


namespace Seiscomp {
namespace MapViewX {


struct Settings : System::Application::AbstractSettings {
	void accept(System::Application::SettingsLinker &linker) override;

	enum State {
		OK,
		Unknown,
		Unconfigured,
		NoPrimaryStream,
		NoChannelGroupMetaData,
		NoVerticalChannelMetaData,
		QCWarning,
		QCError
	};

	using QCParameters = std::map<std::string, DataModel::WaveformQualityPtr>;

	DEFINE_SMARTPOINTER(StationData);
	class StationData : public Core::BaseObject {
		public:
			StationData() = default;

			DataModel::Stream        *channel{nullptr};
			const Util::KeyValues    *bindings{nullptr};

			std::string               detecLocid;
			std::string               detecStream;

			GroundMotionProcessorPtr  proc;
			State                     state{OK};

			Core::Time                maximumAmplitudeTimeStamp;
			double                    maximumAmplitude{-1};

			OPT(Core::Time)           triggerTime;

			std::vector<DataModel::PickPtr>  picks;

			bool                      enabled{false};
			QCParameters              qc;

			void                     *viewData{nullptr}; //! The map symbol
			void                     *infoData{nullptr}; //! The info widget
	};

	//! Maps a station to its configuration
	using StationConfigs = std::map<DataModel::Station*, StationDataPtr>;
	using StationIDConfigs = std::map<std::string, StationDataPtr>;

	std::string       inputFile;
	bool              offline{false};
	Util::BindingsPtr bindings;
	StationConfigs    stationConfig;
	StationIDConfigs  stationIDConfig;
	std::string       filter{"ITAPER(60)>>BW_HP(4,0.5)"};
	Core::TimeSpan    eventTimeSpan{86400, 0};
	Core::TimeSpan    maximumAmplitudeTimeSpan{10, 0};
	Core::TimeSpan    ringBuffer{60 * 10, 0};
	Core::TimeSpan    triggerTimeout{900, 0};
	int               triggerFrameSize{3};
	bool              tickToggleState{false};
	bool              centerOrigins{false};
	std::string       displayMode{"network"};
	bool              showLatestEvent{true};
	bool              showLegend{false};
	std::string       stationLegendPosition;
	std::string       eventLegendPosition;
	bool              annotations{false};
	bool              annotationsWithChannels{true};
	bool              showUnboundStations{true};

	//! Fixed network colors as "NET:color" where color is any color
	//! string understood by QColor, e.g. "AU:#1f77b4" or "S1:orange".
	std::vector<std::string> networkColors;

	struct {
		void accept(System::Application::SettingsLinker &linker) {
			linker
			& cfg(left, "lonmin")
			& cfg(right, "lonmax")
			& cfg(bottom, "latmin")
			& cfg(top, "latmax");
		}

		bool isNull() {
			return left == right && top == bottom;
		}

		operator QRectF() const {
			QRectF r;
			r.setLeft(left); r.setRight(right);
			r.setBottom(bottom); r.setTop(top);
			return r;
		}

		double left{-180};
		double right{180};
		double bottom{-90};
		double top{90};
	}                 initialRegion;
};


extern Settings global;


}
}


#endif
