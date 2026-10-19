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


#ifndef SEISCOMP_MAPVIEWX_MAINWINDOW_H
#define SEISCOMP_MAPVIEWX_MAINWINDOW_H


#ifndef Q_MOC_RUN
#include <seiscomp/datamodel/eventparameters_package.h>
#include <seiscomp/datamodel/waveformquality.h>
#include <seiscomp/datamodel/publicobjectcache.h>
#include <seiscomp/gui/core/recordwidget.h>
#include <seiscomp/gui/core/mainwindow.h>
#include <seiscomp/gui/datamodel/eventlistview.h>
#include <seiscomp/gui/map/mapwidget.h>
#include <seiscomp/gui/map/layers/annotationlayer.h>
#endif

#include <QTimer>

#include "ui_mainwindow.h"
#include "mapwidget.h"
#include "settings.h"


namespace Seiscomp {
namespace MapViewX {


class NetworkLayer;
class EventLayer;
class EventHeatLayer;
class CurrentEventLayer;
class RecentEventsLayer;
class SearchWidget;
class EventInfoDialog;


class MainWindow : public Gui::MainWindow {
	Q_OBJECT

	public:
		MainWindow(QWidget *parent = 0, Qt::WindowFlags = Qt::WindowFlags());


	public:
		void readEventParameters(const std::string &file);
		void updateQC(Settings::StationData *data,
		              DataModel::WaveformQuality *wfq);
		void updateGroundMotion(Settings::StationData *data);
		void updateStation(DataModel::ConfigStation *cs, DataModel::Operation op);

		//! Returns the config-module station configuration for the given
		//! network/station code, or nullptr if none exists.
		DataModel::ConfigStation *configStation(const std::string &net,
		                                        const std::string &sta) const;

		//! Enables or disables a station: updates the local state and map and
		//! sends the change to the CONFIG message group as a notifier.
		void setStationEnabled(const std::string &net, const std::string &sta,
		                       bool enable);


	protected:
		bool eventFilter(QObject *object, QEvent *event) override;


	public slots:
		void addObject(const QString &parentID, Seiscomp::DataModel::Object*);


	private slots:
		void openFile();

		void handleMessage(Seiscomp::Core::Message*);

		void toggleFullScreen();
		void leaveFullScreen();

		void resetView();

		void showEventList();
		void switchTab(int index);

		void timeout();

		void eventAdded(Seiscomp::DataModel::Event*, bool fromNotification);
		void eventUpdated(Seiscomp::DataModel::Event*);
		void eventRemoved(Seiscomp::DataModel::Event*);
		void eventsUpdated();
		void eventsCleared();

		void hoverEvent(const std::string &);
		void selectEvent(const std::string &);
		void showCurrentEventDetails(const std::string &);
		void centerOnEvent(const std::string &);
		void selectEventFromList(Seiscomp::DataModel::Event *);

		void stationEntered(Seiscomp::DataModel::Station *station);
		void stationLeft();
		void stationClicked(Seiscomp::DataModel::Station *station);

		void updateEventTabText();

		void applyQCMode(QAction*);
		void searchStation();
		void filterStations();
		void toggleCentering(bool);

		void objectDestroyed(QObject*);


	private:
		void updateCurrentEvent();
		void updateRecentEvents();
		void showMapCoordinates(const QPoint &pos);
		void sendArtificialOrigin(const QPoint &pos);

		//! Builds the per-network visibility selection menu (checkboxes plus
		//! select all / unselect all).
		void populateNetworksMenu(QMenu *menu);

		//! Builds the stations-status menu (one category submenu each with a
		//! count and copy/save actions).
		void populateStationsStatusMenu(QMenu *menu);
		void addStationsStatusMenu(QMenu *menu, const QString &label, int count,
		                         const QString &saveTitle, const QString &defaultName,
		                         const QString &content);

		//! Fills a submenu with a scrollable, clickable list of the given
		//! "networkCode.stationCode" stations; clicking one centers the map.
		void populateStationSearchMenu(QMenu *menu, const QString &content);
		void copyStationsToClipboard(const QString &content);
		void saveStationsToFile(const QString &title, const QString &content,
		                        const QString &defaultName);


	private:
		typedef DataModel::PublicObjectRingBuffer ObjectCache;

		struct Event {
			void setEvent(DataModel::Event *evt, ObjectCache &cache);
			void reset();

			bool isMoreRecent(const Event &other) const;

			QPointF location() const;

			bool isValid() const { return preferredOrigin.get(); }
			operator bool() const { return isValid(); }


			DataModel::EventPtr          event;
			DataModel::OriginPtr         preferredOrigin;
			DataModel::MagnitudePtr      preferredMagnitude;
			DataModel::FocalMechanismPtr preferredFocalMechanism;
		};

		Ui::MainWindow                 _ui;
		QTimer                         _updateTimer;
		MapWidget                     *_mapWidget;
		Gui::EventListView            *_eventListView;
		NetworkLayer                  *_networkLayer;
		Gui::Map::AnnotationLayer     *_annotationLayer;
		EventLayer                    *_eventLayer;
		EventHeatLayer                *_eventHeatLayer;
		CurrentEventLayer             *_currentEventLayer;
		RecentEventsLayer             *_recentEventsLayer;
		bool                           _recentEventsDirty{true};
		bool                           _mapDirty;
		Gui::RecordWidget             *_currentTraces;
		ObjectCache                    _cache;
		QByteArray                     _lastInfoGeometry;
		Event                          _latestEvent;
		SearchWidget                  *_currentSearch{nullptr};
		EventInfoDialog               *_eventDetails{nullptr};
		QByteArray                     _eventDetailsState;
		//! Event that was shown on the map only to display its details; it is
		//! hidden again when the details window closes.
		DataModel::EventPtr            _forcedEvent;
		//! True if the event layer was switched off and temporarily shown to
		//! display the forced event; it is hidden again on close.
		bool                           _eventLayerForcedVisible{false};
		DataModel::EventParametersPtr  _localEP;
};


}
}


#endif
