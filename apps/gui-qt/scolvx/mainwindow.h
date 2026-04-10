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


#ifndef SEISCOMP_OLOCX_MAINWINDOW_H
#define SEISCOMP_OLOCX_MAINWINDOW_H

#include <vector>


#include <seiscomp/gui/core/mainwindow.h>
#ifndef Q_MOC_RUN
#include <seiscomp/datamodel/databasequery.h>
#include <seiscomp/datamodel/origin.h>
#include <seiscomp/datamodel/eventparameters.h>
#include <seiscomp/datamodel/journaling.h>
#endif

#include <QProcess>
#include <QSystemTrayIcon>

#include "ui_mainwindow.h"


namespace Seiscomp {
namespace OLocX {

struct JsonLocation {
	std::string name;
	std::string type;
	std::string state;
	std::string country;
	double lat{0.0};
	double lon{0.0};
	double population{0.0};
};

}

namespace Gui {

class EventListView;
class EventSummary;
class EventEdit;
class OriginLocatorView;
class MagnitudeView;

}


namespace OLocX {


class MainWindow : public Gui::MainWindow {
	Q_OBJECT


	public:
		MainWindow();
		~MainWindow();

		void setEventID(const std::string &eventID);
		void setOriginID(const std::string &originID);
		void loadEvents(float days);
		void setOffline(bool offline);
		void openFile(const std::string &filename);


	protected:
		void toggledFullScreen(bool) override;
		void closeEvent(QCloseEvent *e) override;


	protected slots:
		void originAdded();
		void objectAdded(const QString &parentID, Seiscomp::DataModel::Object*);
		void objectRemoved(const QString &parentID, Seiscomp::DataModel::Object*);
		void objectUpdated(const QString &parentID, Seiscomp::DataModel::Object*);


	private slots:
		void eventAdded(Seiscomp::DataModel::Event*, bool fromNotification);
		void updateEventTabText();

		// Called by OriginLocatorView::newOriginSet — locator already has the
		// origin, just sync summaries/magnitudes/eventEdit
		void setOrigin(Seiscomp::DataModel::Origin*,
		               Seiscomp::DataModel::Event*, bool, bool);

		// Called by EventListView::originSelected and EventSummary::selected
		// — loads the origin INTO the locator, then updates the rest of the UI
		void loadOrigin(Seiscomp::DataModel::Origin*,
		                Seiscomp::DataModel::Event*);
		void updateOrigin(Seiscomp::DataModel::Origin*,
		                  Seiscomp::DataModel::Event*);
		void onUpdatedOrigin(Seiscomp::DataModel::Origin*);
		void onMagnitudesAdded(Seiscomp::DataModel::Origin*,
		                       Seiscomp::DataModel::Event*);
		void onCommittedOrigin(Seiscomp::DataModel::Origin*,
		                       Seiscomp::DataModel::Event*);
		void committedNewOrigin(Seiscomp::DataModel::Origin*,
		                        Seiscomp::DataModel::Event*);
		void setArtificialOrigin(Seiscomp::DataModel::Origin*);

		void originReferenceAdded(const std::string &,
		                          Seiscomp::DataModel::OriginReference*);
		void originReferenceRemoved(const std::string &,
		                            Seiscomp::DataModel::OriginReference*);

		void showMagnitude(const std::string &magnitudeID);
		void tabChanged(int index);
		void showWaveforms();
		void publishEvent();

		void hoverEvent(const std::string &eventID);
		void selectEvent(const std::string &eventID);
		void showEventList();

		void fileOpen();
		void fileSave();

		void configureAcquisition();

		void trayIconActivated(QSystemTrayIcon::ActivationReason);
		void trayIconMessageClicked();

		void onCitySelectionChanged();
		void onSetRegionName();


	private:
		bool populateOrigin(Seiscomp::DataModel::Origin*,
		                    Seiscomp::DataModel::Event*, bool);
		void updateCitiesTab(Seiscomp::DataModel::Origin *origin);
		void loadJsonLocations();
		void updateRegionPreview();
		QString formatRegionName(const QString &name, const QString &state,
		                         const QString &country, int distKm,
		                         const QString &dir) const;
		void updateCurrentRegionLabel(Seiscomp::DataModel::Event *event);

		Seiscomp::DataModel::EventParametersPtr
		    _createEventParametersForPublication(
		        const Seiscomp::DataModel::Event *event);


	private:
		Ui::MainWindow                    _ui;

		QSystemTrayIcon                  *_trayIcon{nullptr};
		std::string                       _trayMessageEventID;

		Gui::EventListView               *_eventList{nullptr};
		Gui::EventSummary                *_eventSummaryPreferred{nullptr};
		Gui::EventSummary                *_eventSummaryCurrent{nullptr};
		Gui::EventEdit                   *_eventEdit{nullptr};
		Gui::OriginLocatorView           *_originLocator{nullptr};
		Gui::MagnitudeView               *_magnitudes{nullptr};

		DataModel::OriginPtr              _currentOrigin;
		DataModel::EventParametersPtr     _offlineData;
		DataModel::JournalingPtr          _offlineJournal;

		bool                              _offline{false};
		std::string                       _eventID;
		std::vector<JsonLocation>         _jsonLocations;
		QWidget                          *_currentTabWidget{nullptr};
		QProcess                          _exportProcess;
		QAction                          *_actionConfigureAcquisition{nullptr};
};


}
}


#endif
