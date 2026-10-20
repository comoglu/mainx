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
#include <seiscomp/datamodel/origin.h>
#include <seiscomp/datamodel/magnitude.h>
#include <seiscomp/datamodel/utils.h>
#include <seiscomp/gui/core/application.h>
#include <seiscomp/gui/core/compat.h>

#include <QApplication>
#include <QMouseEvent>

#include "currenteventlayer.h"


using namespace Seiscomp::DataModel;
using namespace Seiscomp::Gui;


namespace Seiscomp::MapViewX {

namespace {


QString timeAgoToString(const Core::TimeSpan &ts) {
	int seconds = abs(ts.seconds());
	int minutes = seconds / 60;
	int hours = minutes / 60;
	int days = hours / 24;
	int years = days / 365;

	QString strTimespan;

	if ( years > 1 )
		strTimespan = QObject::tr("%1 years").arg(years);
	else if ( years == 1 )
		strTimespan = "a year";
	else if ( days > 45 )
		strTimespan = QObject::tr("%1 months").arg(days/30);
	else if ( days > 25 )
		strTimespan = "a month";
	else if ( days > 1 )
		strTimespan = QObject::tr("%1 days").arg(days);
	else if ( days == 1 || hours >= 22 )
		strTimespan = "a day";
	else if ( hours > 1 )
		strTimespan = QObject::tr("%1 hours").arg(hours);
	else if ( hours == 1 || minutes >= 45 )
		strTimespan = "an hour";
	else if ( minutes > 1 )
		strTimespan = QObject::tr("%1 minutes").arg(minutes);
	else if ( minutes == 1 || seconds >= 45 )
		strTimespan = "a minute";
	else
		strTimespan = QString("%1 seconds").arg(seconds);

	if ( ts.length() > 0 )
		return QObject::tr("%1 ago").arg(strTimespan);
	else
		return QObject::tr("in %1").arg(strTimespan);
}


}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
CurrentEventLayer::CurrentEventLayer(QObject* parent)
: Map::Layer(parent) {
	//setName("event");
	setVisible(true);

	_magFont = qApp->font();
	_magFont.setPointSize(_magFont.pointSize()*4);

	int mag_em = QFontMetrics(_magFont).height()*3/4;

	_magTypeFont = qApp->font();
	_magTypeFont.setBold(true);

	_boldFont = qApp->font();
	_boldFont.setPointSize(_boldFont.pointSize()*3/2);
	_boldFont.setBold(true);

	int bold_em = QFontMetrics(_boldFont).height()*3/4;

	_normalFont = qApp->font();
	_normalFont.setPointSize(_normalFont.pointSize()*3/2);

	_magSize = QSize(2*mag_em, 2*mag_em);
	_minWidth = qMax(_magSize.width() + 10*bold_em, 300);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void CurrentEventLayer::setEvent(Event *evt) {
	_isValid = evt != nullptr;
	if ( !_isValid ) {
		_eventID.clear();
		emit updateRequested();
		return;
	}

	_eventID = evt->publicID();

	auto *mag = Magnitude::Find(evt->preferredMagnitudeID());
	if ( mag ) {
		_magnitude = mag->magnitude().value();
		_magnitudeType = mag->type().c_str();
	}
	else {
		_magnitude = Core::None;
		_magnitudeType.clear();
	}

	if ( !mag && !evt->preferredMagnitudeID().empty() ) {
		SEISCOMP_WARNING("Preferred magnitude not found: %s",
		                 evt->preferredMagnitudeID().c_str());
	}

	auto *org = Origin::Find(evt->preferredOriginID());
	if ( !org ) {
		_isValid = false;
		emit updateRequested();
		return;
	}

	_originTime = org->time().value();
	_region = eventRegion(evt).c_str();

	try {
		_depth = org->depth().value();
	}
	catch ( ... ) {
		_depth = Core::None;
	}

	_status.clear();
	try {
		_status = org->evaluationStatus().toString();
	}
	catch ( ... ) {
		try {
			_status = org->evaluationMode().toString();
		}
		catch ( ... ) {}
	}

	emit updateRequested();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void CurrentEventLayer::setColorProvider(std::function<QColor (const std::string &)> provider) {
	_colorProvider = std::move(provider);
	emit updateRequested();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void CurrentEventLayer::tick() {
	if ( !_isValid || !_originTime || !isVisible() ) {
		return;
	}

	if ( timeAgoToString(Core::Time::UTC() - *_originTime) != _lastAgo ) {
		emit updateRequested();
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void CurrentEventLayer::draw(const Map::Canvas *canvas, QPainter &p) {
	if ( !_isValid ) {
		_boxRect = QRect();
		return;
	}

	p.save();

	const QColor textColor(0x33,0x33,0x33);
	const QColor secondaryTextColor(0x66,0x66,0x66);
	const int margin = 9;

	QFontMetrics fmNormal(_normalFont);
	QFontMetrics fmBold(_boldFont);
	QFontMetrics fmSmall(qApp->font());

	int em2 = fmSmall.height() / 2;
	int headerHeight = fmNormal.height() * 3 / 2;

	// Text column right of the magnitude tile
	int textX = margin + _magSize.width() + em2;
	int textWidth = _minWidth - _magSize.width() - 2*em2;

	QString details;
	if ( _depth ) {
		details = tr("Depth %1 km").arg(*_depth, 0, 'f', 0);
	}
	if ( !_status.isEmpty() ) {
		if ( !details.isEmpty() ) {
			details += QString::fromUtf8(" · ");
		}
		details += _status;
	}

	_lastAgo = _originTime ? timeAgoToString(Core::Time::UTC() - *_originTime) : QString();

	QRect regionRect = fmBold.boundingRect(
		QRect(0, 0, textWidth, fmBold.height() * 2),
		Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, _region
	);

	int textHeight = em2 + regionRect.height() + em2/2;
	if ( !details.isEmpty() ) {
		textHeight += fmSmall.height() + em2/2;
	}
	textHeight += fmNormal.height() + em2;

	int bodyHeight = qMax(_magSize.height(), textHeight);

	QRect headerRect(margin, margin, _minWidth, headerHeight);
	QRect bodyRect(margin, margin + headerHeight, _minWidth, bodyHeight);
	QRect magRect(margin, margin + headerHeight, _magSize.width(), bodyHeight);

	_boxRect = headerRect.united(bodyRect);

	p.setRenderHint(QPainter::Antialiasing, false);
	p.fillRect(headerRect, QColor(0xE5,0xE5,0xE5,235));
	p.fillRect(bodyRect, QColor(255,255,255,235));

	// The tile uses the same color as the event symbol on the map
	QColor magColor(0x7A,0xD4,0xE2);
	QColor modeColor = _colorProvider ? _colorProvider(_eventID) : QColor();
	if ( modeColor.isValid() ) {
		magColor = modeColor;
		magColor.setAlpha(255);
	}
	else if ( _depth ) {
		magColor = SCScheme.colors.originSymbol.depth.gradient.colorAt(
			*_depth, SCScheme.colors.originSymbol.depth.discrete
		);
		magColor.setAlpha(255);
	}
	p.fillRect(magRect, magColor);

	p.setPen(QColor(0,0,0,48));
	p.setBrush(Qt::NoBrush);
	p.drawRect(_boxRect.adjusted(0, 0, -1, -1));

	p.setRenderHint(QPainter::Antialiasing, true);

	// Pick a magnitude text color with enough contrast to the tile
	double luminance = 0.299*magColor.redF() + 0.587*magColor.greenF() + 0.114*magColor.blueF();
	QColor magTextColor = luminance < 0.55 ? QColor(Qt::white) : QColor(0x21,0x35,0x51);

	QRect magValueRect = magRect;
	if ( !_magnitudeType.isEmpty() ) {
		magValueRect.setBottom(magRect.bottom() - fmSmall.height());
	}

	p.setPen(magTextColor);
	p.setFont(_magFont);
	p.drawText(magValueRect, Qt::AlignCenter,
	           _magnitude ? QString::number(*_magnitude, 'f', 1) : QString("-.-"));

	if ( !_magnitudeType.isEmpty() ) {
		p.setFont(_magTypeFont);
		p.drawText(QRect(magRect.left(), magRect.bottom() - fmSmall.height() * 3 / 2,
		                 magRect.width(), fmSmall.height()),
		           Qt::AlignCenter, _magnitudeType);
	}

	int y = bodyRect.top() + em2;

	p.setPen(textColor);
	if ( !_region.isEmpty() ) {
		p.setFont(_boldFont);
		p.drawText(QRect(textX, y, textWidth, regionRect.height()),
		           Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, _region);
	}
	y += regionRect.height() + em2/2;

	if ( !details.isEmpty() ) {
		p.setFont(qApp->font());
		p.setPen(secondaryTextColor);
		p.drawText(QRect(textX, y, textWidth, fmSmall.height()),
		           Qt::AlignLeft | Qt::AlignVCenter, details);
		y += fmSmall.height() + em2/2;
	}

	// Pin the relative time to the bottom of the box
	p.setFont(_normalFont);
	p.setPen(textColor);
	p.drawText(QRect(textX, bodyRect.bottom() - em2 - fmNormal.height() + 1,
	                 textWidth, fmNormal.height()),
	           Qt::AlignLeft | Qt::AlignVCenter, _lastAgo);

	QRect headerTextRect = headerRect.adjusted(em2, 0, -em2, 0);
	p.drawText(headerTextRect, Qt::AlignLeft | Qt::AlignVCenter, tr("Latest event"));

	if ( _originTime ) {
		// Drop the date if the header is too narrow to show it
		int titleWidth = QT_FM_WIDTH(fmNormal, tr("Latest event"));
		QString otime = QString("%1 UTC").arg(_originTime->toString("%F %T").c_str());
		if ( titleWidth + em2 + QT_FM_WIDTH(fmSmall, otime) > headerTextRect.width() ) {
			otime = QString("%1 UTC").arg(_originTime->toString("%T").c_str());
		}

		p.setFont(qApp->font());
		p.setPen(secondaryTextColor);
		p.drawText(headerTextRect, Qt::AlignRight | Qt::AlignVCenter, otime);
	}

	p.restore();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<








// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void CurrentEventLayer::calculateMapPosition(const Map::Canvas *canvas) {}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool CurrentEventLayer::isInside(const QMouseEvent *event, const QPointF &) {
	if ( !_isValid ) {
		return false;
	}

	return _boxRect.contains(event->pos());
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool CurrentEventLayer::filterMouseReleaseEvent(QMouseEvent *event, const QPointF &) {
	if ( event->button() != Qt::LeftButton || _eventID.empty() ) {
		return false;
	}

	emit clicked(_eventID);
	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
