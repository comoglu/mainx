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


#ifndef SEISCOMP_MAPVIEWX_LAYERS_RECENTEVENTSLAYER_H
#define SEISCOMP_MAPVIEWX_LAYERS_RECENTEVENTSLAYER_H


#ifndef Q_MOC_RUN
#include <seiscomp/core/datetime.h>
#include <seiscomp/gui/map/layer.h>
#endif

#include <QFont>
#include <QVector>

#include <string>


namespace Seiscomp::MapViewX {


class RecentEventsLegend;


/**
 * @brief Shows a list of the most recent events as map legend. The legend
 *        is stacked with other legends in the same map corner. Its rows
 *        can be clicked to center the map on an event.
 */
class RecentEventsLayer : public Gui::Map::Layer {
	Q_OBJECT

	// ----------------------------------------------------------------------
	//  Public types
	// ----------------------------------------------------------------------
	public:
		struct Item {
			std::string     eventID;
			Core::Time      originTime;
			OPT(double)     magnitude;
			QString         magnitudeType;
			QString         region;
			OPT(double)     depth;
		};


	// ----------------------------------------------------------------------
	//  X'truction
	// ----------------------------------------------------------------------
	public:
		RecentEventsLayer(QObject *parent);


	// ----------------------------------------------------------------------
	//  Public interface
	// ----------------------------------------------------------------------
	public:
		//! Sets the events to show, most recent first.
		void setItems(const QVector<Item> &items);

		//! The legend showing the list
		Gui::Map::Legend *legend() const;

		/**
		 * @brief Requests a repaint if the relative times have changed.
		 *        Should be called periodically.
		 */
		void tick();


	// ----------------------------------------------------------------------
	//  Public slots
	// ----------------------------------------------------------------------
	public slots:
		//! Highlights an event, e.g. when hovered on the map.
		void setHighlightedEvent(const std::string &eventID);


	// ----------------------------------------------------------------------
	//  Layer interface
	// ----------------------------------------------------------------------
	public:
		void draw(const Gui::Map::Canvas *, QPainter &) override;
		bool isInside(const QMouseEvent *event, const QPointF &geoPos) override;
		void handleLeaveEvent() override;
		bool filterMouseMoveEvent(QMouseEvent *event, const QPointF &geoPos) override;
		bool filterMouseReleaseEvent(QMouseEvent *event, const QPointF &geoPos) override;
		bool filterMouseDoubleClickEvent(QMouseEvent *event, const QPointF &geoPos) override;


	// ----------------------------------------------------------------------
	//  Signals
	// ----------------------------------------------------------------------
	signals:
		//! Emitted when an event row is clicked.
		void clicked(const std::string &eventID);

		//! Emitted when an event row is double clicked.
		void doubleClicked(const std::string &eventID);


	// ----------------------------------------------------------------------
	//  Private methods
	// ----------------------------------------------------------------------
	private:
		int rowAt(const QPoint &pos) const;


	// ----------------------------------------------------------------------
	//  Private members
	// ----------------------------------------------------------------------
	private:
		RecentEventsLegend *_legend;

	friend class RecentEventsLegend;
};


}


#endif
