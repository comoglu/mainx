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


#ifndef SEISCOMP_MAPVIEWX_LAYERS_CURRENTEVENTLAYER_H
#define SEISCOMP_MAPVIEWX_LAYERS_CURRENTEVENTLAYER_H


#include "seiscomp/datamodel/event.h"
#ifndef Q_MOC_RUN
#include <seiscomp/gui/map/layer.h>
#endif

#include <QColor>

#include <functional>


namespace Seiscomp::MapViewX {


class CurrentEventLayer : public Gui::Map::Layer {
	Q_OBJECT

	// ----------------------------------------------------------------------
	//  X'truction
	// ----------------------------------------------------------------------
	public:
		CurrentEventLayer(QObject* parent);


	// ----------------------------------------------------------------------
	//  Layer interface
	// ----------------------------------------------------------------------
	public:
		/**
		 * @brief Sets the event.
		 * @param evt The pointer to the current event.
		 */
		void setEvent(DataModel::Event *evt);

		/**
		 * @brief Requests a repaint if the relative origin time text has
		 *        changed. Should be called periodically.
		 */
		void tick();

		/**
		 * @brief Sets a function returning the color of an event. If it
		 *        returns an invalid color, the depth color is used.
		 */
		void setColorProvider(std::function<QColor (const std::string &)> provider);


	// ----------------------------------------------------------------------
	//  Layer interface
	// ----------------------------------------------------------------------
	public:
		void draw(const Gui::Map::Canvas *, QPainter &) override;
		void calculateMapPosition(const Gui::Map::Canvas *canvas) override;
		bool isInside(const QMouseEvent *event, const QPointF &geoPos) override;
		bool filterMouseReleaseEvent(QMouseEvent *event, const QPointF &geoPos) override;


	// ----------------------------------------------------------------------
	//  Signals
	// ----------------------------------------------------------------------
	signals:
		//! Emitted when the "Latest event" box is clicked.
		void clicked(const std::string &eventID);


	// ----------------------------------------------------------------------
	//  Private members
	// ----------------------------------------------------------------------
	private:
		bool            _isValid{false};
		std::string     _eventID;
		QSize           _magSize;
		int             _minWidth;
		QRect           _boxRect;

		QFont           _magFont;
		QFont           _magTypeFont;
		QFont           _boldFont;
		QFont           _normalFont;

		OPT(double)     _magnitude;
		QString         _magnitudeType;
		QString         _region;
		OPT(Core::Time) _originTime;
		OPT(double)     _depth;
		QString         _status;
		QString         _lastAgo;
		std::function<QColor (const std::string &)> _colorProvider;
};


}


#endif
