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


#define SEISCOMP_COMPONENT MapView
#include <seiscomp/logging/log.h>
#include <seiscomp/datamodel/event.h>
#include <seiscomp/datamodel/focalmechanism.h>
#include <seiscomp/datamodel/momenttensor.h>
#include <seiscomp/datamodel/magnitude.h>
#include <seiscomp/math/conversions.h>
#include <seiscomp/math/tensor.h>
#include <seiscomp/gui/core/application.h>
#include <seiscomp/gui/datamodel/eventlayer.h>
#include <seiscomp/gui/datamodel/ttdecorator.h>

#include <QApplication>
#include <QMenu>
#include <QPainter>
#include <QMouseEvent>

#include <algorithm>
#include <cmath>
#include <functional>

#include "eventlayer.h"


using namespace Seiscomp::DataModel;
using namespace Seiscomp::Gui;


namespace Seiscomp::MapViewX {

namespace {


void updateSymbol(DataModel::PublicObjectCache *cache,
                  Map::Canvas *canvas, Gui::EventLayer::EventSymbol &symbol,
                  Event *event, Origin *org, FocalMechanism *fm) {
	double latitude = org->latitude();
	double longitude = org->longitude();
	double depth = 10;
	double M = 0;

	MagnitudePtr mag = cache ? cache->get<Magnitude>(event->preferredMagnitudeID()) : Magnitude::Find(event->preferredMagnitudeID());
	if ( mag ) {
		try {
			M = mag->magnitude();
		}
		catch ( ... ) {}
	}

	const DataModel::NodalPlane *np{};

	if ( fm ) {
		int preferredNodalPlane = 0;

		try {
			preferredNodalPlane = fm->nodalPlanes().preferredPlane();
		}
		catch ( Core::ValueException& ) {}

		try {
			if ( preferredNodalPlane == 0 ) {
				np = &(fm->nodalPlanes().nodalPlane1());
			}
			else {
				np = &(fm->nodalPlanes().nodalPlane2());
			}
		}
		catch ( Core::ValueException& ) {}
	}

	symbol.free();

	symbol.origin = new OriginSymbol;

	try {
		depth = org->depth();
	}
	catch ( ... ) {}

	symbol.origin->setLocation(latitude, longitude);
	symbol.origin->setDepth(depth);
	symbol.origin->setPreferredMagnitudeValue(M);

	Core::Time originTime;
	originTime = org->time().value();

	if ( Core::Time::UTC() - originTime < Core::TimeSpan(30 * 60, 0) ) {
		// Consider the symbol for TT decorator
		if ( !symbol.origin->decorator() ) {
			symbol.origin->setDecorator(new TTDecorator());
		}

		TTDecorator *d = static_cast<TTDecorator*>(symbol.origin->decorator());
		d->setLatitude(latitude);
		d->setLongitude(longitude);
		d->setDepth(depth);
		d->setPreferredMagnitudeValue(M);
		d->setOriginTime(originTime);
	}
	else if ( symbol.origin->decorator() ) {
		if ( !symbol.origin->decorator()->isVisible() ) {
			symbol.origin->setDecorator(nullptr);
		}
	}

	if ( canvas ) {
		symbol.origin->calculateMapPosition(canvas);
	}

	if ( np ) {
		np->strike().value();
		np->dip().value();
		np->rake().value();

		Math::Tensor2Sd tensor;
		Math::np2tensor({
			np->strike().value(),
			np->dip().value(),
			np->rake().value()
		}, tensor);

		symbol.tensor = new Gui::TensorSymbol(tensor);

		OriginPtr origin;

		if ( fm->momentTensorCount() > 0 ) {
			auto mt = fm->momentTensor(0);
			origin = cache ? cache->get<Origin>(mt->derivedOriginID()) : Origin::Find(mt->derivedOriginID());
			if ( origin ) {
				org = origin.get();
			}

			mag = cache ? cache->get<Magnitude>(mt->momentMagnitudeID()) : Magnitude::Find(mt->momentMagnitudeID());
			if ( mag ) {
				try {
					M = mag->magnitude();
				}
				catch ( ... ) {}
			}
		}

		int size;
		if ( M > 0 ) {
			size = std::max(SCScheme.map.originSymbolMinSize, int(4.9 * (M - 1.2)));
		}
		else {
			size = SCScheme.map.originSymbolMinSize;
		}

		symbol.tensor->setSize(QSize(size * 3 / 2, size * 3 / 2));
		symbol.tensor->setShadingEnabled(true);
		symbol.tensor->setPosition(QPointF(org->longitude(), org->latitude()));
		symbol.tensor->setTColor(SCScheme.colors.originSymbol.depth.gradient.colorAt(depth, SCScheme.colors.originSymbol.depth.discrete));
		symbol.tensor->setOffset(QPoint(-16, -32));
		symbol.tensor->setDrawConnectorEnabled(true);
		symbol.tensor->setPriority(Gui::Map::Symbol::HIGH);

		if ( canvas ) {
			symbol.tensor->calculateMapPosition(canvas);
		}
	}
}


}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
/**
 * @brief Legend of the non-depth color modes: the color classes followed
 *        by the magnitude dependent symbol sizes.
 */
class EventColorLegend : public Gui::Map::Legend {
	public:
		explicit EventColorLegend(QObject *parent) : Gui::Map::Legend(parent) {
			int fromMag = static_cast<int>(SCScheme.map.originSymbolMinMag < 0 ?
				ceil(SCScheme.map.originSymbolMinMag) : floor(SCScheme.map.originSymbolMinMag));
			for ( int i = fromMag; i <= fromMag + 7; ++i ) {
				_magnitudes.append(i);
			}
		}

		void setContent(const QString &header, const EventLayer::NamedColors &items) {
			_header = header;
			_items = items;
			layout();
		}

		void draw(const QRect &rect, QPainter &p) override {
			p.save();

			QFont normal = p.font();
			QFont bold = normal;
			bold.setBold(true);
			QFontMetrics fm(normal);
			int fh = fm.height();
			int x0 = rect.left() + fh / 2;
			int y = rect.top() + fh / 2;

			p.setRenderHint(QPainter::Antialiasing, false);
			p.setFont(bold);
			p.setPen(SCScheme.colors.legend.headerText);
			p.drawText(QRect(rect.left(), y, rect.width(), fh), Qt::AlignHCenter | Qt::AlignTop, _header);
			y += fh + fh / 4;

			// Color classes in rows
			p.setFont(normal);
			for ( const auto &row : _rows ) {
				int x = x0 + (_contentWidth - row.second) / 2;
				for ( int i : row.first ) {
					const auto &item = _items[i];
					QRect swatch(x, y + fh / 8, fh * 3 / 4, fh * 3 / 4);
					p.setBrush(item.second);
					p.setPen(QColor(0, 0, 0, 128));
					p.drawRect(swatch);
					x += fh * 3 / 4 + fh / 4;
					p.setPen(SCScheme.colors.legend.text);
					int w = fm.horizontalAdvance(item.first);
					p.drawText(QRect(x, y, w, fh), Qt::AlignLeft | Qt::AlignVCenter, item.first);
					x += w + fh;
				}
				y += fh + fh / 4;
			}

			// Symbol sizes
			y += fh / 4;
			p.setFont(bold);
			p.setPen(SCScheme.colors.legend.headerText);
			p.drawText(QRect(rect.left(), y, rect.width(), fh), Qt::AlignHCenter | Qt::AlignTop, tr("Magnitudes"));
			y += fh + fh / 4;

			p.setFont(normal);
			p.setRenderHint(QPainter::Antialiasing, true);
			int maxSize = Gui::OriginSymbol::getSize(_magnitudes.back());
			int x = x0 + (_contentWidth - _magnitudesWidth) / 2;
			for ( int m : _magnitudes ) {
				int size = Gui::OriginSymbol::getSize(m);
				p.setPen(QPen(QColor(64, 64, 64), 1.5));
				p.setBrush(QColor(160, 160, 160));
				p.drawEllipse(QRect(x, y + (maxSize - size) / 2, size, size));
				x += size + fh / 4;
				p.setPen(SCScheme.colors.legend.text);
				QString label = QString::number(m);
				int w = fm.horizontalAdvance(label);
				p.drawText(QRect(x, y, w, maxSize), Qt::AlignLeft | Qt::AlignVCenter, label);
				x += w + fh / 2;
			}

			p.restore();
		}

	private:
		void layout() {
			QFontMetrics fm(qApp->font());
			int fh = fm.height();

			_magnitudesWidth = 0;
			for ( int m : _magnitudes ) {
				_magnitudesWidth += Gui::OriginSymbol::getSize(m) + fh / 4
				                  + fm.horizontalAdvance(QString::number(m)) + fh / 2;
			}

			int maxWidth = std::max(_magnitudesWidth, fh * 18);

			// Flow the items into rows
			_rows.clear();
			QVector<int> row;
			int rowWidth = 0;
			for ( int i = 0; i < _items.size(); ++i ) {
				int w = fh * 3 / 4 + fh / 4 + fm.horizontalAdvance(_items[i].first) + fh;
				if ( !row.isEmpty() && (rowWidth + w - fh > maxWidth) ) {
					_rows.append({ row, rowWidth - fh });
					row.clear();
					rowWidth = 0;
				}
				row.append(i);
				rowWidth += w;
			}
			if ( !row.isEmpty() ) {
				_rows.append({ row, rowWidth - fh });
			}

			_contentWidth = _magnitudesWidth;
			for ( const auto &r : _rows ) {
				_contentWidth = std::max(_contentWidth, r.second);
			}
			_contentWidth = std::max(_contentWidth, QFontMetrics(qApp->font()).horizontalAdvance(_header));

			int maxSize = Gui::OriginSymbol::getSize(_magnitudes.back());
			_size = QSize(_contentWidth + fh,
			              fh / 2 + (fh + fh / 4) + int(_rows.size()) * (fh + fh / 4)
			              + fh / 4 + (fh + fh / 4) + maxSize + fh / 2);
		}

	private:
		QString                          _header;
		EventLayer::NamedColors          _items;
		QVector<QPair<QVector<int>, int>> _rows;
		QVector<int>                     _magnitudes;
		int                              _magnitudesWidth{0};
		int                              _contentWidth{0};
};
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




namespace {


QString ageLabel(double seconds) {
	if ( seconds < 3600 ) {
		return QObject::tr("%1 min").arg(seconds / 60, 0, 'f', 0);
	}
	if ( seconds < 86400 ) {
		return QObject::tr("%1 h").arg(seconds / 3600, 0, 'g', 3);
	}
	return QObject::tr("%1 d").arg(seconds / 86400, 0, 'g', 3);
}


//! Legend items of a gradient: "< second key", second key, ...
EventLayer::NamedColors gradientItems(const Gui::Gradient &gradient,
                                      const std::function<QString (double)> &format) {
	EventLayer::NamedColors items;
	if ( gradient.isEmpty() ) {
		return items;
	}

	auto it = gradient.begin();
	QColor first = it.value().first;
	++it;
	if ( it == gradient.end() ) {
		items.append({ format(gradient.begin().key()), first });
		return items;
	}

	items.append({ QString("< %1").arg(format(it.key())), first });
	for ( ; it != gradient.end(); ++it ) {
		items.append({ format(it.key()), it.value().first });
	}

	return items;
}


}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
EventLayer::EventLayer(QObject* parent, DataModel::PublicObjectCache *cache)
: Gui::EventLayer(parent)
, _currentEvent(nullptr)
, _cache(cache) {
	_colorScheme = defaultColorScheme();
	_colorLegend = new EventColorLegend(this);
	_colorLegend->setEnabled(false);
	addLegend(_colorLegend);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
EventLayer::ColorScheme EventLayer::defaultColorScheme() {
	ColorScheme scheme;

	scheme.magnitude.setColorAt(0, QColor(0xB0, 0xB0, 0xB0));
	scheme.magnitude.setColorAt(3, QColor(0xFF, 0xD7, 0x00));
	scheme.magnitude.setColorAt(4, QColor(0xFF, 0xA5, 0x00));
	scheme.magnitude.setColorAt(5, QColor(0xFF, 0x00, 0x00));
	scheme.magnitude.setColorAt(6, QColor(0x8B, 0x00, 0x00));

	scheme.age.setColorAt(0, QColor(0xFF, 0x00, 0x00));
	scheme.age.setColorAt(3600, QColor(0xFF, 0xA5, 0x00));
	scheme.age.setColorAt(21600, QColor(0xFF, 0xFF, 0x00));
	scheme.age.setColorAt(86400, QColor(0xB0, 0xB0, 0xB0));

	scheme.evaluation = {
		{ "automatic", SCScheme.colors.originStatus.automatic },
		{ "manual", SCScheme.colors.originStatus.manual },
		{ "confirmed", QColor(0x1E, 0x64, 0xC8) },
		{ "rejected", QColor(0xA0, 0xA0, 0xA0) }
	};

	scheme.types = {
		{ "earthquake", QColor(0xFF, 0x00, 0x00) },
		{ "induced or triggered event", QColor(0x94, 0x00, 0xD3) },
		{ "quarry blast", QColor(0x8B, 0x45, 0x13) },
		{ "mining explosion", QColor(0xA0, 0x52, 0x2D) },
		{ "explosion", QColor(0xFF, 0x8C, 0x00) },
		{ "not existing", QColor(0xA0, 0xA0, 0xA0) }
	};
	scheme.unsetType = QColor(0xFF, 0xFF, 0xFF);
	scheme.otherType = QColor(0x00, 0xCE, 0xD1);

	return scheme;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool EventLayer::parseColorMode(const std::string &name, ColorMode &mode) {
	if ( name == "depth" ) {
		mode = ColorMode::Depth;
	}
	else if ( name == "magnitude" ) {
		mode = ColorMode::Magnitude;
	}
	else if ( name == "age" ) {
		mode = ColorMode::Age;
	}
	else if ( name == "evaluation" ) {
		mode = ColorMode::Evaluation;
	}
	else if ( name == "type" ) {
		mode = ColorMode::Type;
	}
	else {
		return false;
	}

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void EventLayer::setColorScheme(const ColorScheme &scheme) {
	_colorScheme = scheme;
	setColorMode(_colorMode);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void EventLayer::setColorMode(ColorMode mode) {
	_colorMode = mode;

	// The depth legend of the base class is used for the depth mode
	bool depth = _colorMode == ColorMode::Depth;
	if ( legendCount() > 0 ) {
		legend(0)->setEnabled(depth);
		_colorLegend->setArea(legend(0)->alignment());
	}
	_colorLegend->setEnabled(!depth);

	switch ( _colorMode ) {
		case ColorMode::Magnitude:
			_colorLegend->setContent(tr("Magnitude"), gradientItems(
				_colorScheme.magnitude, [](double v) { return QString::number(v, 'g', 3); }));
			break;
		case ColorMode::Age:
			_colorLegend->setContent(tr("Time since origin"), gradientItems(_colorScheme.age, ageLabel));
			break;
		case ColorMode::Evaluation:
			_colorLegend->setContent(tr("Evaluation"), _colorScheme.evaluation);
			break;
		case ColorMode::Type:
		{
			auto items = _colorScheme.types;
			items.append({ tr("unset"), _colorScheme.unsetType });
			items.append({ tr("other"), _colorScheme.otherType });
			_colorLegend->setContent(tr("Event type"), items);
			break;
		}
		default:
			break;
	}

	applyColors();
	emit updateRequested();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void EventLayer::updateColorInfo(Event *event, Origin *origin) {
	ColorInfo info;

	MagnitudePtr magnitude = _cache ? _cache->get<Magnitude>(event->preferredMagnitudeID())
	                                : Magnitude::Find(event->preferredMagnitudeID());
	if ( magnitude ) {
		try {
			info.magnitude = magnitude->magnitude().value();
		}
		catch ( ... ) {}
	}

	try {
		info.time = origin->time().value();
	}
	catch ( ... ) {}

	info.evaluation = "automatic";
	try {
		if ( origin->evaluationMode() == MANUAL ) {
			info.evaluation = "manual";
		}
	}
	catch ( ... ) {}

	try {
		switch ( origin->evaluationStatus() ) {
			case REJECTED:
				info.evaluation = "rejected";
				break;
			case CONFIRMED:
			case REVIEWED:
			case FINAL:
				info.evaluation = "confirmed";
				break;
			default:
				break;
		}
	}
	catch ( ... ) {}

	try {
		info.type = event->type().toString();
	}
	catch ( ... ) {}

	_colorInfo[event->publicID()] = info;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
QColor EventLayer::colorFor(const std::string &eventID) const {
	const QColor unknown(0xA0, 0xA0, 0xA0);

	auto it = _colorInfo.find(eventID);
	if ( it == _colorInfo.end() ) {
		return unknown;
	}

	const auto &info = it.value();

	auto lookup = [](const NamedColors &colors, const QString &name, const QColor &fallback) {
		for ( const auto &item : colors ) {
			if ( item.first == name ) {
				return item.second;
			}
		}
		return fallback;
	};

	switch ( _colorMode ) {
		case ColorMode::Magnitude:
			if ( !info.magnitude || _colorScheme.magnitude.isEmpty() ) {
				return unknown;
			}
			return _colorScheme.magnitude.colorAt(*info.magnitude, true);

		case ColorMode::Age:
			if ( _colorScheme.age.isEmpty() ) {
				return unknown;
			}
			return _colorScheme.age.colorAt(static_cast<double>(Core::Time::UTC() - info.time), true);

		case ColorMode::Evaluation:
			return lookup(_colorScheme.evaluation, info.evaluation, unknown);

		case ColorMode::Type:
			if ( info.type.isEmpty() ) {
				return _colorScheme.unsetType;
			}
			return lookup(_colorScheme.types, info.type, _colorScheme.otherType);

		default:
			return unknown;
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
QColor EventLayer::modeColor(const std::string &eventID) const {
	if ( (_colorMode == ColorMode::Depth) || !_colorInfo.contains(eventID) ) {
		return QColor();
	}

	return colorFor(eventID);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void EventLayer::applyColor(const std::string &eventID) {
	auto it = _eventSymbols.find(eventID);
	if ( it == _eventSymbols.end() || !it.value().origin ) {
		return;
	}

	auto &symbol = it.value();

	// Resets the colors to the depth coding
	symbol.origin->setDepth(symbol.origin->depth());

	QColor color = _colorMode == ColorMode::Depth ?
		SCScheme.colors.originSymbol.depth.gradient.colorAt(
			symbol.origin->depth(), SCScheme.colors.originSymbol.depth.discrete) :
		colorFor(eventID);

	if ( _colorMode != ColorMode::Depth ) {
		symbol.origin->setColor(color);
	}

	if ( symbol.tensor ) {
		symbol.tensor->setTColor(color);
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void EventLayer::applyColors() {
	for ( auto it = _eventSymbols.begin(); it != _eventSymbols.end(); ++it ) {
		applyColor(it.key());
	}

	// Keep the hover highlight
	if ( !_hoverId.empty() ) {
		auto it = _eventSymbols.find(_hoverId);
		if ( it != _eventSymbols.end() ) {
			it.value().origin->setFillColor(it.value().origin->color());
			it.value().origin->setColor(QColor(33,53,81));
		}
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void EventLayer::setCurrentEvent(DataModel::Event *evt) {
	auto lastCurrent = _currentEvent;

	if ( _currentEvent ) {
		_currentEvent->setType(0);
		applyColor(_currentEventID);
	}

	if ( !evt ) {
		_currentEvent = nullptr;
		_currentEventID.clear();
	}
	else {
		auto it = _eventSymbols.find(evt->publicID());
		if ( it == _eventSymbols.end() ) {
			_currentEvent = nullptr;
			_currentEventID.clear();
		}
		else {
			_currentEvent = it.value().origin;
			_currentEventID = evt->publicID();
		}
	}

	if ( lastCurrent != _currentEvent ) {
		emit updateRequested();
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
int EventLayer::eventCount() const {
	return _eventSymbols.size();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool EventLayer::hasEvent(const std::string &eventID) const {
	return _eventSymbols.contains(eventID);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void EventLayer::showOnlyEvent(const std::string &eventID) {
	for ( auto it = _eventSymbols.begin(); it != _eventSymbols.end(); ++it ) {
		bool visible = it.key() == eventID;
		it.value().origin->setVisible(visible);
		if ( it.value().tensor ) {
			it.value().tensor->setVisible(visible);
		}
	}

	emit updateRequested();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void EventLayer::showAllEvents() {
	for ( auto &symbol : _eventSymbols ) {
		symbol.origin->setVisible(true);
		if ( symbol.tensor ) {
			symbol.tensor->setVisible(true);
		}
	}

	emit updateRequested();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void EventLayer::draw(const Map::Canvas *canvas, QPainter &p) {
	EventSymbol *currentSymbol = nullptr;

	// Render all symbols
	for ( auto &symbol : _eventSymbols ) {
		if ( symbol.origin == _currentEvent ) {
			currentSymbol = &symbol;
			continue;
		}

		if ( !symbol.origin->isClipped() && symbol.origin->isVisible() ) {
			symbol.origin->draw(canvas, p);
		}

		if ( symbol.tensor ) {
			if ( !symbol.tensor->isClipped() && symbol.tensor->isVisible() ) {
				symbol.tensor->draw(canvas, p);
			}
		}
	}

	if ( currentSymbol ) {
		auto &symbol = *currentSymbol;

		if ( !symbol.origin->isClipped() && symbol.origin->isVisible() ) {
			symbol.origin->draw(canvas, p);
		}

		if ( symbol.tensor ) {
			if ( !symbol.tensor->isClipped() && symbol.tensor->isVisible() ) {
				symbol.tensor->draw(canvas, p);
			}
		}
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool EventLayer::isInside(const QMouseEvent *event, const QPointF &geoPos) {
	int x = event->pos().x();
	int y = event->pos().y();
	auto it = _eventSymbols.end();

	while ( it != _eventSymbols.begin() ) {
		--it;
		if ( it.value().origin->isClipped() || !it.value().origin->isVisible() ) {
			continue;
		}
		if ( it.value().origin->isInside(x, y) ) {
			_hoverChanged = _hoverId != it.key();
			if ( _hoverChanged ) {
				setHoverId(it.key());
			}
			return true;
		}
	}

	return false;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void EventLayer::handleLeaveEvent() {
	_hoverChanged = !_hoverId.empty();
	setHoverId(std::string());
	emit eventHovered(_hoverId);
	_hoverChanged = false;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool EventLayer::filterMouseDoubleClickEvent(QMouseEvent *event, const QPointF &geoPos) {
	return false;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool EventLayer::filterMousePressEvent(QMouseEvent *, const QPointF &) {
	return !_hoverId.empty();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool EventLayer::filterMouseReleaseEvent(QMouseEvent *event, const QPointF &) {
	if ( _clickSuppressed )
		return false;

	if ( _hoverId.empty() )
		return false;

	if ( event->button() != Qt::LeftButton )
		return false;

	// A single event here; overlapping events are resolved by the map
	// widget's chooser.
	selectEvent(_hoverId);

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void EventLayer::selectEvent(const std::string &eventID) {
	emit eventSelected(eventID);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
std::vector<std::string> EventLayer::eventsUnder(int x, int y) const {
	std::vector<std::string> result;

	auto it = _eventSymbols.end();
	while ( it != _eventSymbols.begin() ) {
		--it;
		const auto &symbol = it.value();
		if ( symbol.origin->isClipped() || !symbol.origin->isVisible() ) {
			continue;
		}
		if ( symbol.origin->isInside(x, y) ) {
			result.push_back(it.key());
		}
	}

	// Sort alphabetically by public ID.
	std::sort(result.begin(), result.end());

	return result;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void EventLayer::clear() {
	_currentEvent = nullptr;
	_currentEventID.clear();
	_colorInfo.clear();
	setHoverId(std::string());
	Gui::EventLayer::clear();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void EventLayer::addEvent(Event *e, bool) {
	auto it = _eventSymbols.find(e->publicID());

	OriginPtr org = _cache ? _cache->get<Origin>(e->preferredOriginID()) : Origin::Find(e->preferredOriginID());
	FocalMechanismPtr fm;
	if ( !e->preferredFocalMechanismID().empty() ) {
		fm = _cache ? _cache->get<FocalMechanism>(e->preferredFocalMechanismID()) : FocalMechanism::Find(e->preferredFocalMechanismID());
	}

	if ( org ) {
		EventSymbol symbol;

		if ( it != _eventSymbols.end() ) {
			symbol = it.value();
		}

		updateSymbol(_cache, canvas(), symbol, e, org.get(), fm.get());
		_eventSymbols[e->publicID()] = symbol;
		updateColorInfo(e, org.get());
		applyColor(e->publicID());

		// Create origin symbol and register it
		emit updateRequested();
	}
	else
		SEISCOMP_ERROR("Origin %s for event %s not found",
		               e->preferredOriginID().c_str(), e->publicID().c_str());
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void EventLayer::updateEvent(Event *e) {
	auto it = _eventSymbols.find(e->publicID());
	if ( it == _eventSymbols.end() ) {
		return;
	}

	OriginPtr org = _cache ? _cache->get<Origin>(e->preferredOriginID()) : Origin::Find(e->preferredOriginID());
	FocalMechanismPtr fm;
	if ( !e->preferredFocalMechanismID().empty() ) {
		fm = _cache ? _cache->get<FocalMechanism>(e->preferredFocalMechanismID()) : FocalMechanism::Find(e->preferredFocalMechanismID());
	}

	if ( org ) {
		auto oldOriginSymbol = it.value().origin;
		updateSymbol(_cache, canvas(), it.value(), e, org.get(), fm.get());
		if ( _currentEvent == oldOriginSymbol ) {
			_currentEvent = it.value().origin;
		}
		updateColorInfo(e, org.get());
		applyColor(e->publicID());
		emit updateRequested();
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void EventLayer::removeEvent(Event *e) {
	auto it = _eventSymbols.find(e->publicID());
	if ( it == _eventSymbols.end() ) {
		return;
	}
	if ( it.value().origin == _currentEvent ) {
		_currentEvent = nullptr;
		_currentEventID.clear();
	}
	it.value().free();
	_eventSymbols.erase(it);
	_colorInfo.remove(e->publicID());
	emit updateRequested();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void EventLayer::tick() {
	bool hasActiveDecorators = false;
	auto it = _eventSymbols.begin();
	for ( ; it != _eventSymbols.end(); ++it ) {
		if ( it.value().origin->decorator() ) {
			if ( !it.value().origin->decorator()->isVisible() ) {
				it.value().origin->setDecorator(nullptr);
			}
			else
				hasActiveDecorators = true;
		}
	}

	if ( _currentEvent ) {
		if ( _currentEvent->type() == 0 ) {
			_currentEvent->setType(1);
			_currentEvent->setColor(QColor(33,53,81));
			_currentEvent->setFillColor(QColor(122,212,226,204));
		}
		else {
			_currentEvent->setType(0);
			applyColor(_currentEventID);
		}

		emit updateRequested();
	}
	else if ( hasActiveDecorators )
		emit updateRequested();

	// Event ages change continuously
	if ( (_colorMode == ColorMode::Age) && (++_ageTicks % 30 == 0) ) {
		applyColors();
		emit updateRequested();
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void EventLayer::setHoverId(const std::string &id) {
	bool updateRequired = false;

	if ( !_hoverId.empty() ) {
		auto it = _eventSymbols.find(_hoverId);
		if ( it != _eventSymbols.end() ) {
			applyColor(_hoverId);
			updateRequired = true;
		}
	}

	_hoverId = id;

	if ( !_hoverId.empty() ) {
		auto it = _eventSymbols.find(_hoverId);
		if ( it != _eventSymbols.end() ) {
			it.value().origin->setFillColor(it.value().origin->color());
			it.value().origin->setColor(QColor(33,53,81));
			updateRequired = true;
		}
	}

	if ( updateRequired ) {
		emit updateRequested();
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
