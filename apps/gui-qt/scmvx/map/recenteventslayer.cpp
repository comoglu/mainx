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


#include <seiscomp/gui/core/application.h>
#include <seiscomp/gui/core/compat.h>
#include <seiscomp/gui/map/canvas.h>

#include <QApplication>
#include <QMouseEvent>

#include <algorithm>

#include "recenteventslayer.h"


namespace Seiscomp::MapViewX {

namespace {


QString shortAge(const Core::TimeSpan &ts) {
	auto seconds = static_cast<long>(static_cast<double>(ts));
	if ( seconds < 0 ) {
		seconds = 0;
	}

	if ( seconds < 60 ) {
		return QObject::tr("%1 s").arg(seconds);
	}
	if ( seconds < 3600 ) {
		return QObject::tr("%1 min").arg(seconds / 60);
	}
	if ( seconds < 86400 ) {
		return QObject::tr("%1 h").arg(seconds / 3600);
	}
	return QObject::tr("%1 d").arg(seconds / 86400);
}


}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
class RecentEventsLegend : public Gui::Map::Legend {
	public:
		RecentEventsLegend(QObject *parent) : Gui::Map::Legend(parent) {
			setTitle(tr("Recent events"));
			setArea(Qt::AlignLeft | Qt::AlignBottom);
			_boldFont = qApp->font();
			_boldFont.setBold(true);
			setItems({});
		}

		void setItems(const QVector<RecentEventsLayer::Item> &items) {
			_items = items;
			_hoverRow = -1;

			QFont normalFont = qApp->font();
			QFontMetrics fm(normalFont);
			QFontMetrics fmBold(_boldFont);
			int em = fm.height();

			_rowHeight = em * 3 / 2;
			_dotWidth = em;
			_timeWidth = QT_FM_WIDTH(fm, "00-00 00:00:00");
			int magTypeWidth = QT_FM_WIDTH(fm, "Mwp");
			for ( const auto &item : _items ) {
				magTypeWidth = std::max(magTypeWidth, QT_FM_WIDTH(fm, item.magnitudeType));
			}
			_magWidth = QT_FM_WIDTH(fmBold, "0.0") + em / 4 + magTypeWidth;
			_regionWidth = em * 12;
			_ageWidth = QT_FM_WIDTH(fm, "000 min");

			_size = QSize(em / 2 + _dotWidth + _timeWidth + em / 2 + _magWidth + em / 2
			              + _regionWidth + em / 2 + _ageWidth + em / 2,
			              int(_items.size()) * _rowHeight + em / 2);

			setEnabled(!_items.isEmpty());
		}

		void draw(const QRect &rect, QPainter &p) override {
			_contentRect = rect;
			_rowRects.clear();
			_lastAges.clear();

			p.save();

			QFont normalFont = p.font();
			QFontMetrics fmBold(_boldFont);
			int em = QFontMetrics(normalFont).height();
			auto now = Core::Time::UTC();
			std::string todayStr = now.toString("%F");

			p.setRenderHint(QPainter::Antialiasing, true);

			int rowY = rect.top() + em / 4;
			for ( int i = 0; i < _items.size(); ++i, rowY += _rowHeight ) {
				const auto &item = _items[i];
				QRect rowRect(rect.left(), rowY, rect.width(), _rowHeight);
				_rowRects.append(rowRect);

				if ( (i == _hoverRow) || (item.eventID == _highlightedEventID) ) {
					p.fillRect(rowRect, QColor(0xD6,0xEA,0xF8));
				}

				int cx = rect.left() + em / 2;

				// Depth colored dot as the event symbols on the map
				QColor dotColor(0x7A,0xD4,0xE2);
				if ( item.depth ) {
					dotColor = SCScheme.colors.originSymbol.depth.gradient.colorAt(
						*item.depth, SCScheme.colors.originSymbol.depth.discrete
					);
					dotColor.setAlpha(255);
				}
				int dotSize = em * 2 / 3;
				p.setPen(QPen(QColor(0,0,0,160), 1));
				p.setBrush(dotColor);
				p.drawEllipse(QRectF(cx, rowY + (_rowHeight - dotSize) / 2.0, dotSize, dotSize));
				cx += _dotWidth;

				// The latest event is the first one
				QFont rowFont = i == 0 ? _boldFont : normalFont;
				p.setFont(rowFont);
				p.setPen(QColor(0x33,0x33,0x33));

				QString timeStr = item.originTime.toString("%F") == todayStr ?
					QString(item.originTime.toString("%T").c_str()) :
					QString(item.originTime.toString("%m-%d %H:%M").c_str());
				p.drawText(QRect(cx, rowY, _timeWidth, _rowHeight), Qt::AlignLeft | Qt::AlignVCenter, timeStr);
				cx += _timeWidth + em / 2;

				QString magStr = item.magnitude ? QString::number(*item.magnitude, 'f', 1) : QString("-.-");
				p.setFont(_boldFont);
				p.drawText(QRect(cx, rowY, _magWidth, _rowHeight), Qt::AlignLeft | Qt::AlignVCenter, magStr);
				if ( !item.magnitudeType.isEmpty() ) {
					int magValueWidth = QT_FM_WIDTH(fmBold, magStr) + em / 4;
					p.setFont(normalFont);
					p.setPen(QColor(0x66,0x66,0x66));
					p.drawText(QRect(cx + magValueWidth, rowY, _magWidth - magValueWidth, _rowHeight),
					           Qt::AlignLeft | Qt::AlignVCenter, item.magnitudeType);
					p.setPen(QColor(0x33,0x33,0x33));
				}
				cx += _magWidth + em / 2;

				p.setFont(rowFont);
				p.drawText(QRect(cx, rowY, _regionWidth, _rowHeight), Qt::AlignLeft | Qt::AlignVCenter,
				           QFontMetrics(rowFont).elidedText(item.region, Qt::ElideRight, _regionWidth));
				cx += _regionWidth + em / 2;

				QString age = shortAge(now - item.originTime);
				_lastAges.append(age);
				p.setFont(normalFont);
				p.setPen(QColor(0x66,0x66,0x66));
				p.drawText(QRect(cx, rowY, _ageWidth, _rowHeight), Qt::AlignRight | Qt::AlignVCenter, age);
			}

			p.restore();
		}

	private:
		QVector<RecentEventsLayer::Item> _items;
		std::string                      _highlightedEventID;
		int                              _hoverRow{-1};

		QFont                            _boldFont;
		int                              _rowHeight{0};
		int                              _dotWidth{0};
		int                              _timeWidth{0};
		int                              _magWidth{0};
		int                              _regionWidth{0};
		int                              _ageWidth{0};

		//! Where the content was drawn in the last frame. Empty if the
		//! legend was not drawn, e.g. because another legend is shown in
		//! the same area.
		QRect                            _contentRect;
		QVector<QRect>                   _rowRects;
		QStringList                      _lastAges;

	friend class RecentEventsLayer;
};
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
RecentEventsLayer::RecentEventsLayer(QObject *parent)
: Gui::Map::Layer(parent) {
	setVisible(true);
	_legend = new RecentEventsLegend(this);
	_legend->setLayer(this);
	addLegend(_legend);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void RecentEventsLayer::setItems(const QVector<Item> &items) {
	_legend->setItems(items);
	emit updateRequested();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
Gui::Map::Legend *RecentEventsLayer::legend() const {
	return _legend;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void RecentEventsLayer::tick() {
	if ( !isVisible() || _legend->_contentRect.isEmpty() ) {
		return;
	}

	auto now = Core::Time::UTC();
	const auto &items = _legend->_items;
	for ( int i = 0; i < items.size() && i < _legend->_lastAges.size(); ++i ) {
		if ( shortAge(now - items[i].originTime) != _legend->_lastAges[i] ) {
			emit updateRequested();
			return;
		}
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void RecentEventsLayer::setHighlightedEvent(const std::string &eventID) {
	if ( _legend->_highlightedEventID == eventID ) {
		return;
	}

	_legend->_highlightedEventID = eventID;
	if ( !_legend->_contentRect.isEmpty() ) {
		emit updateRequested();
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void RecentEventsLayer::draw(const Gui::Map::Canvas *, QPainter &) {
	// Layers are drawn before the legends. Reset the legend area so that
	// clicks are not handled if the legend is not drawn in this frame.
	_legend->_contentRect = QRect();
	_legend->_rowRects.clear();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
int RecentEventsLayer::rowAt(const QPoint &pos) const {
	const auto &rects = _legend->_rowRects;
	for ( int i = 0; i < rects.size() && i < _legend->_items.size(); ++i ) {
		if ( rects[i].contains(pos) ) {
			return i;
		}
	}
	return -1;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool RecentEventsLayer::isInside(const QMouseEvent *event, const QPointF &) {
	return isVisible() && _legend->_contentRect.contains(event->pos());
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void RecentEventsLayer::handleLeaveEvent() {
	if ( _legend->_hoverRow >= 0 ) {
		_legend->_hoverRow = -1;
		emit updateRequested();
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool RecentEventsLayer::filterMouseMoveEvent(QMouseEvent *event, const QPointF &) {
	int row = rowAt(event->pos());
	if ( row != _legend->_hoverRow ) {
		_legend->_hoverRow = row;
		emit updateRequested();
	}
	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool RecentEventsLayer::filterMouseReleaseEvent(QMouseEvent *event, const QPointF &) {
	if ( event->button() != Qt::LeftButton ) {
		return false;
	}

	int row = rowAt(event->pos());
	if ( row >= 0 ) {
		emit clicked(_legend->_items[row].eventID);
	}

	// Swallow clicks on the legend to not interact with the map below
	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool RecentEventsLayer::filterMouseDoubleClickEvent(QMouseEvent *event, const QPointF &) {
	if ( event->button() != Qt::LeftButton ) {
		return false;
	}

	int row = rowAt(event->pos());
	if ( row >= 0 ) {
		emit doubleClicked(_legend->_items[row].eventID);
	}

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
