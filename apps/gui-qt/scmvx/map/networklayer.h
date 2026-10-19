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


#ifndef SEISCOMP_MAPVIEWX_LAYERS_NETWORKLAYER_H
#define SEISCOMP_MAPVIEWX_LAYERS_NETWORKLAYER_H


#ifndef Q_MOC_RUN
#include <seiscomp/datamodel/inventory.h>
#include <seiscomp/gui/core/gradient.h>
#include <seiscomp/gui/datamodel/stationsymbol.h>
#include <seiscomp/gui/map/annotations.h>
#include <seiscomp/gui/map/layer.h>

#include <map>
#include <set>

#include <QRect>
#include <QStringList>

#include "../settings.h"
#include "stationsymbol.h"
#endif


namespace Seiscomp::MapViewX {


class NetworkLayer;


class NetworkLayerGradient : public Gui::Gradient {
	public:
		NetworkLayerGradient() = default;

	public:
		QString title;
		QColor  unsetColor;
};


class NetworkLayerSymbol : public StationSymbol {
	public:
		explicit NetworkLayerSymbol(NetworkLayer *layer,
		                            DataModel::Station *station,
		                            Gui::Map::AnnotationItem *annotation);
		virtual ~NetworkLayerSymbol() override;


	public:
		DataModel::Station *model() const { return _model; }
		Settings::StationData *data() const { return _data; }

		void setDefaultVisibility();

		//! Sets the symbol visibility and keeps the annotation in sync.
		//! Intentionally hides the non-virtual base class method: all
		//! symbols of this layer are handled as NetworkLayerSymbol.
		void setVisible(bool visible);

		//! Synchronizes the annotation visibility with the current symbol
		//! visibility and clipping state.
		void updateAnnotationVisibility();

		bool setSelected(bool);
		bool isSelected() const { return _selected; }

		void setColor(QColor c);
		void setColorFromValue(double value);
		QColor color() const { return _color; }

		void setValue(double v) { _value = v; }
		double value() const { return _value; }

		void updateColor();

		void setAnnotation(const QString &a) { _annotation->text = a; }
		const QString &annotation() const { return _annotation->text; }

		void setState(Settings::State state) { _state = state; }
		Settings::State state() const { return _state; }

		//! Marks a station whose epoch/streams are closed before the
		//! reference time. Such stations are rendered with a cross.
		void setClosed(bool closed) { _closed = closed; }
		bool isClosed() const { return _closed; }

		void calculateMapPosition(const Seiscomp::Gui::Map::Canvas *canvas) override;
		void customDraw(const Seiscomp::Gui::Map::Canvas *canvas, QPainter& painter) override;


	private:
		Seiscomp::Gui::Map::AnnotationItem *_annotation;
		DataModel::Station                 *_model;
		Settings::StationData              *_data{nullptr};
		bool                                _selected;
		bool                                _closed{false};
		double                              _value;
		QColor                              _color;
		NetworkLayer                       *_layer;
		Settings::State                     _state;

	friend class NetworkLayer;
};


class NetworkLayerLegend : public Gui::Map::Legend {
	public:
		NetworkLayerLegend(QObject *parent = nullptr);

		virtual void draw(const QRect &rect, QPainter &painter);
		virtual void contextResizeEvent(const QSize &size);

		void updateFrom(NetworkLayer *layer);

	private:
		void updateLayout();

	private:
		QVector< QPair<QString, QColor> > _items;
		int                               _columns;
		int                               _columnWidth;
		int                               _maxColumns;
};


/**
 * @brief The NetworkLayer class
 */
class NetworkLayer : public Gui::Map::Layer {
	Q_OBJECT


	// ----------------------------------------------------------------------
	//  Public types
	// ----------------------------------------------------------------------
	public:
		//! Associative container to map network codes to colors
		using NetworkColors = std::map<std::string, QColor>;

		enum ColorMode {
			Default,
			Network,
			GroundMotion,
			QC
		};



	// ----------------------------------------------------------------------
	//  X'truction
	// ----------------------------------------------------------------------
	public:
		NetworkLayer(QObject* parent = nullptr);
		~NetworkLayer();

	// ----------------------------------------------------------------------
	//  Public interface
	// ----------------------------------------------------------------------
	public:
		/**
		 * @brief Creates station symbols from all stations in the passed
		 *        inventory where the epoch is open or valid for a passed
		 *        reference time.
		 * @param inv The inventory pointer
		 * @param annotations Instance to add station annotations to
		 * @param time The reference time for which the station must be
		 *             operational
		 */
		void setInventory(DataModel::Inventory *inv,
		                  Gui::Map::Annotations *annotations,
		                  const Core::Time *time = nullptr);

		/**
		 * @brief Removes all symbols
		 */
		void clear();

		/**
		 * @brief Returns the current network code color map
		 * @return The string:QColor map
		 */
		const NetworkColors &networkColors() const { return _networkColors; }

		void setGMGradient(const NetworkLayerGradient &);
		const NetworkLayerGradient *gmGradient() const { return &_gmGradient; }

		const NetworkLayerGradient *qcGradient() const;

		/**
		 * @brief Sets the station symbol color mode.
		 * @param mode The mode flag
		 * @param force If true then the values will be recomputed even if the
		 *        requested color mode is already active
		 */
		void setColorMode(ColorMode mode, bool force = false);
		ColorMode colorMode() const { return _colorMode; }

		void setActiveQCParameter(const std::string &);
		const std::string &activeQCParameter() const { return _activeQCParameter; }

		void setStationsVisible(QSet<const DataModel::Station *> *);

		//! Returns the sorted, distinct network codes of all known stations.
		QStringList networkCodes() const;

		//! Whether stations of the given network are currently selected for
		//! display.
		bool isNetworkVisible(const QString &code) const;

		//! Whether any network is currently hidden, i.e. the stations are
		//! filtered by network code.
		bool hasHiddenNetworks() const { return !_hiddenNetworks.empty(); }

		//! Total number of currently visible stations.
		int visibleStationCount() const;

		//! Number of stations known to the layer, excluding those whose
		//! epochs/streams are currently closed.
		int stationCount() const;

		//! Number of currently visible stations that are not closed.
		int visibleOpenStationCount() const;

		//! Number of currently visible stations in each export category.
		int closedStationCount() const;
		int unboundStationCount() const;
		int mismatchStationCount() const;
		int noDetecStreamStationCount() const;
		int disabledStationCount() const;
		int enabledStationCount() const;

		//! Newline-separated "networkCode.stationCode" list of the currently
		//! visible stations in each export category.
		QString closedStationList() const;
		QString unboundStationList() const;

		//! "networkCode.stationCode" of every station rendered as closed,
		//! regardless of its current visibility.
		std::set<std::string> closedStationCodes() const;

		//! "networkCode.stationCode" of every station currently visible on
		//! the map (respecting all active filters).
		std::set<std::string> visibleStationCodes() const;
		QString mismatchStationList() const;
		QString noDetecStreamStationList() const;
		QString disabledStationList() const;
		QString enabledStationList() const;

		//! Looks up the geographic location (longitude, latitude) of the
		//! station identified by "networkCode.stationCode". Returns false when
		//! no such station is known.
		bool stationLocation(const QString &netSta, QPointF &location) const;

		//! Selects or deselects a network for display, updating the map
		//! immediately.
		void setNetworkVisible(const QString &code, bool visible);

		//! Returns the visible station symbols under the given widget
		//! position, topmost first.
		QVector<NetworkLayerSymbol*> symbolsUnder(int x, int y) const;

		//! The station symbol currently under the cursor and the most recently
		//! selected one (used to highlight the chooser entry).
		NetworkLayerSymbol *currentSymbol() const { return _currentSymbol; }
		NetworkLayerSymbol *selectedSymbol() const { return _selectedSymbol; }

		//! Selects the given station and requests its information window.
		void selectStation(NetworkLayerSymbol *symbol);

		//! While suppressed, a release click is ignored (used when a combined
		//! station/event chooser is shown instead).
		void setClickSuppressed(bool suppressed) { _clickSuppressed = suppressed; }

		Gui::Map::Legend *mainLegend() const;

		void updateStation(const std::string &staID);

		//! Forces a recomputation of the render order, e.g. after a symbol
		//! value has changed.
		void invalidateDrawOrder() { _drawOrderDirty = true; }


	// ----------------------------------------------------------------------
	//  Signals
	// ----------------------------------------------------------------------
	signals:
		void stationEntered(Seiscomp::DataModel::Station *station);
		void stationLeft();
		void stationClicked(Seiscomp::DataModel::Station *station);

		//! Emitted when the station-filter alert box is clicked.
		void filterAlertClicked();


	// ----------------------------------------------------------------------
	//  Public slots
	// ----------------------------------------------------------------------
	public slots:
		/**
		 * @brief Whether to include the preferred channel code. The default
		 *        is false.
		 * @param enable The visibility state
		 */
		void setShowChannelCodes(bool enable);

		/**
		 * @brief Sets if station issues should be indicated with an
		 *        additional symbol or not.
		 * @param enable The visibility state
		 */
		void setShowIssues(bool enable);

		/**
		 * @brief Sets if stations should be shown which do not have
		 *        station bindings.
		 * @param enable The visibility state
		 */
		void setShowUnbound(bool enable);

		/**
		 * @brief Sets if stations should be shown whose streams are all
		 *        closed before the current time. Disabled by default.
		 * @param enable The visibility state
		 */
		void setShowClosed(bool enable);

		/**
		 * @brief Updates the internal render state for each station symbol.
		 */
		void tick();


	// ----------------------------------------------------------------------
	//  Layer interface
	// ----------------------------------------------------------------------
	public:
		bool isInside(const QMouseEvent *event, const QPointF &geoPos) override;
		void calculateMapPosition(const Gui::Map::Canvas *canvas) override;
		void draw(const Gui::Map::Canvas *canvas, QPainter &p) override;


	// ----------------------------------------------------------------------
	//  Event interface
	// ----------------------------------------------------------------------
	protected:
		void handleLeaveEvent() override;
		bool filterMousePressEvent(QMouseEvent *event, const QPointF &geoPos) override;
		bool filterMouseReleaseEvent(QMouseEvent *event, const QPointF &geoPos) override;
		bool filterMouseMoveEvent(QMouseEvent *event, const QPointF &geoPos) override;
		bool filterMouseDoubleClickEvent(QMouseEvent *event, const QPointF &geoPos) override;


	// ----------------------------------------------------------------------
	//  Private methods
	// ----------------------------------------------------------------------
	private:
		void updateAnnotations();
		void disposeSymbols();
		void updateColor(NetworkLayerSymbol *symbol);

		//! Assigns a color to each network: configured colors first, then
		//! perceptually distinct automatic colors.
		void assignNetworkColors();

		//! Recomputes a single symbol's visibility from all active filters
		//! (network selection, closed and unbound toggles).
		void updateSymbolVisibility(NetworkLayerSymbol *s) const;

		//! Closed stations are only shown on the Network tab and only when
		//! the "show closed" toggle is enabled.
		bool closedStationsVisible() const { return _showClosed && (_colorMode == Network); }

		//! Returns the symbols in render order (bottom first). In ground motion
		//! and QC mode the most significant stations are rendered last so
		//! that they are not hidden by neighbouring stations.
		const QVector<NetworkLayerSymbol*> &drawOrder() const;

		//! The sort key used to compute the render order.
		double drawRank(const NetworkLayerSymbol *s) const;


	// ----------------------------------------------------------------------
	//  Private members
	// ----------------------------------------------------------------------
	private:
		using Symbols = QVector<NetworkLayerSymbol*>;
		using StationSymbolMap = std::map<std::string, NetworkLayerSymbol*>;

		bool                                     _showChannelCodes;
		bool                                     _showIssues;
		bool                                     _showUnbound{true};
		bool                                     _showClosed{false};
		std::set<std::string>                    _hiddenNetworks;
		ColorMode                                _colorMode;
		std::string                              _activeQCParameter;
		Symbols                                  _stationSymbols;
		NetworkColors                            _networkColors;
		StationSymbolMap                         _stationSymbolLookup;
		NetworkLayerSymbol                      *_currentSymbol;
		NetworkLayerSymbol                      *_currentClickSymbol;
		NetworkLayerSymbol                      *_selectedSymbol{nullptr};
		bool                                     _clickSuppressed{false};
		NetworkLayerLegend                      *_legend;
		NetworkLayerGradient                     _gmGradient;
		QMap<std::string, NetworkLayerGradient>  _qcGradients;

		mutable NetworkLayerSymbol              *_isInsideSymbol;

		//! Cached render order, see drawOrder()
		mutable Symbols                          _drawOrder;
		mutable bool                             _drawOrderDirty{true};

		//! Screen rectangle of the station-filter alert box (empty when it is
		//! not shown) and whether the cursor is currently over it.
		mutable QRect                            _filterAlertRect;
		mutable bool                             _isInsideAlert{false};
};


}


#endif
