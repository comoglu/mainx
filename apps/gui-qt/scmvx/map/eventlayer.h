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


#ifndef SEISCOMP_MAPVIEWX_LAYERS_EVENTLAYER_H
#define SEISCOMP_MAPVIEWX_LAYERS_EVENTLAYER_H


#ifndef Q_MOC_RUN
#include <seiscomp/datamodel/publicobjectcache.h>
#include <seiscomp/gui/datamodel/originsymbol.h>
#include <seiscomp/gui/datamodel/eventlayer.h>
#include <seiscomp/gui/core/gradient.h>
#endif

#include <QColor>
#include <QMap>
#include <QVector>

#include <vector>


namespace Seiscomp::MapViewX {


class EventColorLegend;


class EventLayer : public Gui::EventLayer {
	Q_OBJECT


	// ----------------------------------------------------------------------
	//  Public types
	// ----------------------------------------------------------------------
	public:
		//! The attribute used to color the event symbols
		enum class ColorMode {
			Depth,
			Magnitude,
			Age,
			Evaluation,
			Type
		};

		//! A named color class, e.g. an evaluation state or event type
		using NamedColors = QVector<QPair<QString, QColor>>;

		struct ColorScheme {
			//! Keys are magnitude values
			Gui::Gradient magnitude;
			//! Keys are seconds since origin time
			Gui::Gradient age;
			//! automatic, manual, confirmed, rejected
			NamedColors   evaluation;
			//! Event type names as used in the data model
			NamedColors   types;
			QColor        unsetType;
			QColor        otherType;
		};


	// ----------------------------------------------------------------------
	//  X'truction
	// ----------------------------------------------------------------------
	public:
		EventLayer(QObject* parent, DataModel::PublicObjectCache *cache);


	// ----------------------------------------------------------------------
	//  Color modes
	// ----------------------------------------------------------------------
	public:
		void setColorScheme(const ColorScheme &scheme);
		const ColorScheme &colorScheme() const { return _colorScheme; }

		void setColorMode(ColorMode mode);
		ColorMode colorMode() const { return _colorMode; }

		//! Parses a mode name: depth, magnitude, age, evaluation or type
		static bool parseColorMode(const std::string &name, ColorMode &mode);

		//! The default color scheme for the non-depth modes
		static ColorScheme defaultColorScheme();

		/**
		 * @brief Returns the color of an event in the current color mode.
		 * @return An invalid color in depth mode or if the event is unknown
		 */
		QColor modeColor(const std::string &eventID) const;


	// ----------------------------------------------------------------------
	//  Layer interface
	// ----------------------------------------------------------------------
	public:
		/**
		 * @brief Sets the current event if available as symbol. This event
		 *        will be drawn on-top and toggle its colors when calling
		 *        tick (blinking).
		 * @param evt The pointer to the current event. Actually only the
		 *            publicID will be read and looked up in the local store.
		 */
		void setCurrentEvent(DataModel::Event *evt);
		int eventCount() const;

		//! Returns true if a symbol for the given event is currently present
		//! (i.e. the event is shown on the map).
		bool hasEvent(const std::string &eventID) const;

		//! Shows only the given event's symbol and hides all other event
		//! symbols. Used to reveal a single event while the layer would
		//! otherwise be switched off.
		void showOnlyEvent(const std::string &eventID);

		//! Makes all event symbols visible again.
		void showAllEvents();

		//! Returns the public IDs of the visible events under the given widget
		//! position, topmost first.
		std::vector<std::string> eventsUnder(int x, int y) const;

		//! Requests the event details window for the given event.
		void selectEvent(const std::string &eventID);

		//! The public ID of the event currently under the cursor (used to
		//! highlight the chooser entry).
		const std::string &hoveredId() const { return _hoverId; }

		//! While suppressed, a release click is ignored (used when a combined
		//! station/event chooser is shown instead).
		void setClickSuppressed(bool suppressed) { _clickSuppressed = suppressed; }


	// ----------------------------------------------------------------------
	//  Layer interface
	// ----------------------------------------------------------------------
	public:
		void draw(const Gui::Map::Canvas *, QPainter &) override;
		bool isInside(const QMouseEvent *event, const QPointF &geoPos) override;

		void handleLeaveEvent() override;
		bool filterMouseDoubleClickEvent(QMouseEvent *event, const QPointF &geoPos) override;
		bool filterMousePressEvent(QMouseEvent *event, const QPointF &geoPos) override;
		bool filterMouseReleaseEvent(QMouseEvent *event, const QPointF &geoPos) override;


	// ----------------------------------------------------------------------
	//  Slots
	// ----------------------------------------------------------------------
	public slots:
		void clear() override;
		void addEvent(Seiscomp::DataModel::Event*,bool) override;
		void updateEvent(Seiscomp::DataModel::Event*) override;
		void removeEvent(Seiscomp::DataModel::Event*) override;
		void tick();


	// ----------------------------------------------------------------------
	//  Signals
	// ----------------------------------------------------------------------
	signals:
		void eventHovered(const std::string &eventID);
		void eventSelected(const std::string &eventID);


	// ----------------------------------------------------------------------
	//  Private members
	// ----------------------------------------------------------------------
	private:
		void setHoverId(const std::string &id);

		//! Remembers the attributes needed for the color modes
		void updateColorInfo(DataModel::Event *event, DataModel::Origin *origin);

		//! Resets the symbol colors of an event according to the color mode
		void applyColor(const std::string &eventID);
		void applyColors();

		QColor colorFor(const std::string &eventID) const;


	// ----------------------------------------------------------------------
	//  Protected members
	// ----------------------------------------------------------------------
	protected:
		struct ColorInfo {
			OPT(double) magnitude;
			Core::Time  time;
			QString     evaluation;
			QString     type;
		};

		Gui::OriginSymbol            *_currentEvent;
		std::string                   _currentEventID;
		DataModel::PublicObjectCache *_cache;
		bool                          _clickSuppressed{false};

		ColorMode                     _colorMode{ColorMode::Depth};
		ColorScheme                   _colorScheme;
		QMap<std::string, ColorInfo>  _colorInfo;
		EventColorLegend             *_colorLegend{nullptr};
		int                           _ageTicks{0};
};


}


#endif
