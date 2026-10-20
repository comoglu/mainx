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
#include <seiscomp/gui/core/application.h>
#include <seiscomp/gui/core/compat.h>
#include <seiscomp/gui/core/icon.h>
#include <seiscomp/gui/map/canvas.h>

#include <seiscomp/datamodel/sensorlocation.h>
#include <seiscomp/datamodel/stream.h>
#include <seiscomp/datamodel/waveformquality.h>

#include <QApplication>
#include <QSet>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "networklayer.h"
#include "settings.h"


using namespace std;


namespace Seiscomp::MapViewX {


namespace {


static QColor defaultColor(45, 105, 192);
static QColor defaultFrameColor(64,64,64,128);
//static QColor selectedFrameColor(60,139,255);
static QColor selectedFrameColor(255,255,255);
static QColor hoverFrameColor(255,255,255,192);
static qreal defaultFrameWidth = 1.5;


typedef QList<QRect> Row;
typedef QVector<Row> Grid;


struct Lab {
	double L, a, b;
};


Lab toLab(const QColor &c) {
	auto linear = [](double v) {
		return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
	};

	double r = linear(c.redF());
	double g = linear(c.greenF());
	double b = linear(c.blueF());

	// sRGB to XYZ (D65), normalized by the reference white
	double x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047;
	double y = (0.2126 * r + 0.7152 * g + 0.0722 * b);
	double z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883;

	auto f = [](double t) {
		return t > 216.0 / 24389.0 ? std::cbrt(t) : (24389.0 / 27.0 * t + 16.0) / 116.0;
	};

	double fx = f(x), fy = f(y), fz = f(z);
	return { 116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz) };
}


double labDistance(const Lab &c1, const Lab &c2) {
	double dL = c1.L - c2.L;
	double da = c1.a - c2.a;
	double db = c1.b - c2.b;
	return std::sqrt(dL*dL + da*da + db*db);
}


//! Candidate colors for automatic network colors. All are saturated
//! enough to be distinguishable from the gray of disabled stations.
const QVector<QColor> &networkColorCandidates() {
	static QVector<QColor> candidates;
	if ( candidates.isEmpty() ) {
		const qreal tiers[][2] = {
			{0.80, 0.90}, {0.65, 0.70}, {0.90, 0.55}, {0.45, 0.95}
		};
		for ( const auto &tier : tiers ) {
			for ( int hue = 0; hue < 360; hue += 6 ) {
				candidates.append(QColor::fromHsvF(hue / 360.0, tier[0], tier[1]));
			}
		}
	}
	return candidates;
}


bool topToBottom(const NetworkLayerSymbol *s1, const NetworkLayerSymbol *s2) {
	return s1->latitude() > s2->latitude();
}


/**
 * @brief Checks whether a station is no longer operating.
 *
 * A station qualifies if all of its stream epochs ended before the reference
 * time, i.e. the last stream epoch is closed. This applies to configured and
 * unconfigured stations alike. Stations without any stream are not considered.
 */
bool isClosedStation(DataModel::Station *sta, const Core::Time &refTime) {
	bool hasStream = false;

	for ( size_t l = 0; l < sta->sensorLocationCount(); ++l ) {
		DataModel::SensorLocation *loc = sta->sensorLocation(l);
		for ( size_t c = 0; c < loc->streamCount(); ++c ) {
			hasStream = true;
			try {
				if ( refTime < loc->stream(c)->end() ) {
					// Stream epoch still open at the reference time
					return false;
				}
			}
			catch ( ... ) {
				// Open-ended stream epoch
				return false;
			}
		}
	}

	return hasStream;
}


/**
 * @brief Checks whether a station epoch is active at the reference time.
 *
 * Epochs without end time are open and thus active once they started.
 */
bool isActiveStation(DataModel::Station *sta, const Core::Time &refTime) {
	if ( refTime < sta->start() ) {
		return false;
	}

	try {
		return refTime < sta->end();
	}
	catch ( ... ) {
		// Open-ended station epoch
		return true;
	}
}


/**
 * @brief Returns the epoch which should represent a station on the map.
 *
 * An epoch active at the reference time always wins over a closed one.
 * Otherwise the most recent epoch is taken so that a station which stopped
 * operating is still rendered with its latest metadata.
 */
DataModel::Station *preferredEpoch(DataModel::Station *lhs,
                                   DataModel::Station *rhs,
                                   const Core::Time &refTime) {
	bool lhsActive = isActiveStation(lhs, refTime);
	if ( lhsActive != isActiveStation(rhs, refTime) ) {
		return lhsActive ? lhs : rhs;
	}

	return lhs->start() < rhs->start() ? rhs : lhs;
}


void drawInfoSymbol(QPainter &painter, const QPoint &lowerLeft, const QPixmap &pixmap) {
	static QColor errorBorder(QColor(255,128,0));
	QSize layoutSize = pixmap.size() / pixmap.devicePixelRatio();
	int size = qMax(layoutSize.width(), layoutSize.height());
	int radius = size * 75 / 100;
	QPoint center = lowerLeft + QPoint(radius, -radius);
	painter.setPen(QPen(errorBorder, qMax(2, size / 5)));
	painter.setBrush(Qt::white);
	painter.drawEllipse(center, radius, radius);
	painter.drawPixmap(
		center.x() - layoutSize.width() / 2,
		center.y() - layoutSize.height() / 2,
		pixmap
	);
}


void drawWarningSymbol(QPainter &painter, const QPoint &lowerLeft, const QPixmap &pixmap) {
	static QColor errorBorder(192, 0, 0);
	QSize layoutSize = pixmap.size() / pixmap.devicePixelRatio();
	int size = qMax(layoutSize.width(), layoutSize.height());
	int radius = size * 75 / 100;
	QPoint center = lowerLeft + QPoint(radius, -radius);
	painter.setPen(QPen(errorBorder, qMax(2, size / 5)));
	painter.setBrush(Qt::white);
	painter.drawEllipse(center, radius, radius);
	painter.drawPixmap(
		center.x() - layoutSize.width() / 2,
		center.y() - layoutSize.height() / 2,
		pixmap
	);
}


const NetworkLayerGradient *currentGradient = nullptr;


//! Data delay above which a station counts as delayed
const double statusDelayWarning = 60;
//! Data delay above which a station counts as silent
const double statusDelayError = 600;
//! Latency above which a station counts as late, same as the warning
//! threshold of the latency QC gradient
const double statusLatencyWarning = 60;


class NetworkStatusLegend : public Gui::Map::Legend {
	public:
		struct Counts {
			int visible{0};
			int unbound{0};
			int noDetecStream{0};
			int mismatch{0};
			int disabled{0};
			int delayOK{0};
			int delayWarning{0};
			int delayError{0};
			int noQC{0};
			int latencyWarning{0};

			bool operator==(const Counts &other) const {
				return std::memcmp(this, &other, sizeof(Counts)) == 0;
			}
		};

		NetworkStatusLegend(QObject *parent) : Gui::Map::Legend(parent) {
			setTitle(tr("Network status"));
			setCounts(Counts());
		}

		bool setCounts(const Counts &counts) {
			if ( _rows.size() && (counts == _counts) ) {
				return false;
			}

			_counts = counts;
			_rows.clear();

			_rows.append({ tr("Visible stations"), counts.visible, QColor(), true });
			_rows.append({ tr("Configuration"), -1, QColor(), true });
			_rows.append({ tr("Missing global bindings"), counts.unbound, QColor(), false });
			_rows.append({ tr("Missing detecStream"), counts.noDetecStream, QColor(), false });
			_rows.append({ tr("Bindings mismatching inventory"), counts.mismatch, QColor(), false });
			_rows.append({ tr("Disabled"), counts.disabled, QColor(), false });
			_rows.append({ tr("Data delay"), -1, QColor(), true });
			_rows.append({ tr("< 1 min"), counts.delayOK, SCScheme.colors.qc.qcOk, false });
			_rows.append({ tr("1 - 10 min"), counts.delayWarning, SCScheme.colors.qc.qcWarning, false });
			_rows.append({ tr("> 10 min"), counts.delayError, SCScheme.colors.qc.qcError, false });
			_rows.append({ tr("No QC"), counts.noQC, SCScheme.colors.qc.qcNotSet, false });
			_rows.append({ tr("Latency"), -1, QColor(), true });
			_rows.append({ tr("> 1 min"), counts.latencyWarning, SCScheme.colors.qc.qcWarning, false });

			// Opaque swatches
			for ( auto &row : _rows ) {
				if ( row.color.isValid() ) {
					row.color.setAlpha(255);
				}
			}

			QFontMetrics fm(qApp->font());
			QFont bold = qApp->font();
			bold.setBold(true);
			QFontMetrics fmBold(bold);

			int fontHeight = fm.height();
			int labelWidth = 0;
			int valueWidth = 0;
			for ( const auto &row : _rows ) {
				const auto &metrics = row.bold ? fmBold : fm;
				labelWidth = std::max(labelWidth, QT_FM_WIDTH(metrics, row.label));
				if ( row.value >= 0 ) {
					valueWidth = std::max(valueWidth, QT_FM_WIDTH(metrics, QString::number(row.value)));
				}
			}

			// Swatch, label, gap, value
			_size = QSize(fontHeight * 3 / 2 + labelWidth + fontHeight + valueWidth + fontHeight / 2,
			              int(_rows.size()) * fontHeight * 5 / 4 + fontHeight / 2);
			return true;
		}

		void draw(const QRect &rect, QPainter &painter) override {
			int fontHeight = QFontMetrics(qApp->font()).height();
			int x = rect.left() + fontHeight / 2;
			int right = rect.right() - fontHeight / 2;
			int y = rect.top() + fontHeight / 4;

			QFont normal = painter.font();
			QFont bold = normal;
			bold.setBold(true);

			for ( const auto &row : _rows ) {
				painter.setFont(row.bold ? bold : normal);

				int textX = x;
				if ( !row.bold || row.color.isValid() ) {
					textX += fontHeight;
				}

				if ( row.color.isValid() ) {
					QRect swatch(x + (row.bold ? 0 : fontHeight / 4), y + fontHeight / 4,
					             fontHeight / 2, fontHeight / 2);
					painter.setBrush(row.color);
					painter.setPen(QColor(0, 0, 0, 128));
					painter.drawRect(swatch);
				}

				painter.setPen(Qt::black);
				painter.drawText(textX, y, right - textX, fontHeight,
				                 Qt::AlignLeft | Qt::AlignVCenter, row.label);

				if ( row.value >= 0 ) {
					// Highlight problems
					bool problem = (row.value > 0) && !row.bold && !row.color.isValid();
					problem = problem || ((row.value > 0) && row.color.isValid()
					                      && (row.color.rgb() == SCScheme.colors.qc.qcError.rgb()));
					painter.setPen(problem ? QColor(192, 0, 0) : QColor(Qt::black));
					painter.drawText(x, y, right - x, fontHeight,
					                 Qt::AlignRight | Qt::AlignVCenter, QString::number(row.value));
				}

				y += fontHeight * 5 / 4;
			}

			painter.setFont(normal);
		}

	private:
		struct Row {
			QString label;
			int     value; //!< -1 for section headers
			QColor  color;
			bool    bold;
		};

		Counts       _counts;
		QVector<Row> _rows;
};


}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
NetworkLayerSymbol::NetworkLayerSymbol(NetworkLayer *layer,
                                       DataModel::Station *station,
                                       Gui::Map::AnnotationItem *annotation)
: _annotation(annotation)
, _model(station)
, _selected(false)
, _value(-1)
, _layer(layer)
, _state(Settings::OK) {
	setDefaultVisibility();
	setColor(defaultColor);
	setPen(defaultFrameColor);

	auto it = global.stationConfig.find(_model);
	if ( it != global.stationConfig.end() ) {
		_data = it->second.get();
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
NetworkLayerSymbol::~NetworkLayerSymbol() {
	delete _annotation;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayerSymbol::setDefaultVisibility() {
	try {
		setLocation(_model->latitude(), _model->longitude());
		setVisible(true);
	}
	catch ( ... ) {
		setVisible(false);
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayerSymbol::setVisible(bool visible) {
	StationSymbol::setVisible(visible);
	updateAnnotationVisibility();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayerSymbol::updateAnnotationVisibility() {
	if ( _annotation ) {
		_annotation->visible = !isClipped() && isVisible();
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool NetworkLayerSymbol::setSelected(bool f) {
	if ( _selected == f ) return false;
	_selected = f;
	setPen(_selected ? selectedFrameColor : defaultFrameColor);
	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayerSymbol::setColor(QColor c) {
	_color = c;
	setFill(_color);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayerSymbol::setColorFromValue(double value) {
	if ( _layer && (value != _value) ) {
		_layer->invalidateDrawOrder();
	}
	setValue(value);
	updateColor();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayerSymbol::updateColor() {
	bool enabled = true;

	auto it  = global.stationConfig.find(model());
	if ( it != global.stationConfig.end() ) {
		enabled = it->second->enabled;
	}

	if ( enabled ) {
		if ( _value < 0 ) {
			setColor(currentGradient ? currentGradient->unsetColor : QColor(0,0,0,128));
		}
		else if ( currentGradient ) {
			setColor(currentGradient->colorAt(_value, true));
		}
		else {
			setColor(Qt::white);
		}
	}
	else {
		setColor(SCScheme.colors.stations.disabled);
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayerSymbol::calculateMapPosition(const Seiscomp::Gui::Map::Canvas *canvas) {
	StationSymbol::calculateMapPosition(canvas);
	updateAnnotationVisibility();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayerSymbol::customDraw(const Gui::Map::Canvas *canvas, QPainter &painter) {
	StationSymbol::customDraw(canvas, painter);

	if ( _annotation && _annotation->visible ) {
		_annotation->updateLabelRect(painter, _position - QPoint(0, width() + painter.fontMetrics().height() / 2));
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
NetworkLayerLegend::NetworkLayerLegend(QObject *parent)
: Gui::Map::Legend(parent) {
	_maxColumns = 1;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayerLegend::contextResizeEvent(const QSize &size) {
	updateLayout();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayerLegend::draw(const QRect &rect, QPainter &painter) {
	int fontHeight = QFontMetricsF(qApp->font()).height();
	int w = rect.width();
	int x = rect.left() + fontHeight/2;
	int idx = 0;

	for ( int c = 0; c < _columns; ++c ) {
		int y = rect.top() + fontHeight/2;
		int textOffset = fontHeight*3/2;
		int textWidth = w - fontHeight - textOffset;

		int itemsPerColumn = (_items.count() + _columns - 1) / _columns;
		int cnt = _items.count() - itemsPerColumn * c;
		if ( cnt > itemsPerColumn ) cnt = itemsPerColumn;

		for ( int i = 0; i < cnt; ++i, ++idx ) {
			painter.fillRect(x, y, fontHeight, fontHeight, _items[idx].second);
			painter.drawText(x + textOffset, y, textWidth, fontHeight,
			                 Qt::AlignLeft | Qt::AlignVCenter, _items[idx].first);
			y += fontHeight*3/2;
		}

		x += _columnWidth + fontHeight*4/2;
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayerLegend::updateFrom(NetworkLayer *layer) {
	switch ( layer->colorMode() ) {
		case NetworkLayer::Network:
		{
			setTitle(tr("Networks"));

			const NetworkLayer::NetworkColors &colors = layer->networkColors();

			_items.clear();

			for ( auto &[name, color] : colors ) {
				_items.append(QPair<QString, QColor>(name.c_str(), color));
			}

			_maxColumns = 6;
			break;
		}

		case NetworkLayer::GroundMotion:
		{
			_items.clear();

			auto gradient = layer->gmGradient();
			auto it = gradient->begin();

			setTitle(gradient->title);

			QColor currentColor = it.value().first;
			++it;

			_items.append(QPair<QString, QColor>(QString("<%1").arg(it.key()), currentColor));

			for ( ; it != gradient->end(); ++it ) {
				_items.append(QPair<QString, QColor>(QString("%1").arg(it.key()), it.value().first));
			}

			if ( gradient->unsetColor.isValid() ) {
				_items.append(QPair<QString, QColor>(tr("Unset"), gradient->unsetColor));
			}

			_maxColumns = 1;
			break;
		}

		case NetworkLayer::QC:
		{
			auto gradient = layer->qcGradient();
			if ( gradient ) {
				_items.clear();
				auto it = gradient->begin();

				setTitle(gradient->title);

				QColor currentColor = it.value().first;
				QString currentTag = it.value().second;
				qreal lastValue = it.key();
				++it;

				for ( ; it != gradient->end(); ++it ) {
					lastValue = it.key();

					if ( currentTag.isEmpty() ) {
						_items.append(QPair<QString, QColor>(QString("< %1").arg(lastValue), currentColor));
					}
					else {
						_items.append(QPair<QString, QColor>(currentTag, currentColor));
					}

					currentColor = it.value().first;
					currentTag = it.value().second;
				}

				if ( currentTag.isEmpty() ) {
					_items.append(QPair<QString, QColor>(QString(">= %1").arg(lastValue), currentColor));
				}
				else {
					_items.append(QPair<QString, QColor>(currentTag, currentColor));
				}

				if ( gradient->unsetColor.isValid() ) {
					_items.append(QPair<QString, QColor>(tr("Unset"), gradient->unsetColor));
				}

				_maxColumns = 1;
				break;
			}
			// Fall through to default case
		}

		default:
			setTitle(QString());
			setEnabled(false);
			_items.clear();
			break;
	}

	if ( !_items.empty() ) {
		_items.append(QPair<QString, QColor>(tr("Disabled"), SCScheme.colors.stations.disabled));
		updateLayout();
		setEnabled(true);
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayerLegend::updateLayout() {
	QSize size = layer()->size();

	if ( !size.isValid() ) return;

	int ch = size.height();
	int fontHeight = QFontMetricsF(qApp->font()).height();

	_columns = 1;

	_columnWidth = 0;
	for ( int i = 0; i < _items.count(); ++i ) {
		int itemWidth = QT_FM_WIDTH(QFontMetricsF(qApp->font()), _items[i].first);
		if ( itemWidth > _columnWidth )
			_columnWidth = itemWidth;
	}

	_size.setWidth((_columnWidth + fontHeight*3/2)*_columns + fontHeight + fontHeight/2*(_columns-1));
	_size.setHeight(qMax(((_items.count()+_columns-1)/_columns)*fontHeight*3/2+fontHeight/2, 0));

	if ( ch <= 0 ) return;

	// TODO: What is a good offset value?
	while ( (_size.height() > ch - 30) && (_columns < _maxColumns) ) {
		++_columns;
		_size.setWidth((_columnWidth + fontHeight*3/2)*_columns + fontHeight + fontHeight/2*(_columns-1));
		_size.setHeight(qMax(((_items.count()+_columns-1)/_columns)*fontHeight*3/2+fontHeight/2, 0));
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
NetworkLayer::NetworkLayer(QObject *parent)
: Gui::Map::Layer(parent)
, _showIssues(true)
, _colorMode(Network)
, _currentSymbol(nullptr)
, _isInsideSymbol(nullptr) {
	setName("stations");
	_activeQCParameter = "delay";

	_gmGradient.title = "PGV in nm/s";
	_gmGradient.unsetColor = SCScheme.colors.gm.gmNotSet;
	_gmGradient.setColorAt(0, SCScheme.colors.gm.gm0);
	_gmGradient.setColorAt(200, SCScheme.colors.gm.gm1);
	_gmGradient.setColorAt(400, SCScheme.colors.gm.gm2);
	_gmGradient.setColorAt(800, SCScheme.colors.gm.gm3);
	_gmGradient.setColorAt(1500, SCScheme.colors.gm.gm4);
	_gmGradient.setColorAt(4000, SCScheme.colors.gm.gm5);
	_gmGradient.setColorAt(12000, SCScheme.colors.gm.gm6);
	_gmGradient.setColorAt(30000, SCScheme.colors.gm.gm7);
	_gmGradient.setColorAt(60000, SCScheme.colors.gm.gm8);
	_gmGradient.setColorAt(150000, SCScheme.colors.gm.gm9);

	auto g = &_qcGradients["delay"];
	g->title = "Delay";
	g->unsetColor = SCScheme.colors.qc.qcNotSet;
	g->setColorAt(0, SCScheme.colors.qc.delay0, "< 20s");
	g->setColorAt(20, SCScheme.colors.qc.delay1, "[20s, 1min)");
	g->setColorAt(60, SCScheme.colors.qc.delay2, "[1min, 3min)");
	g->setColorAt(180, SCScheme.colors.qc.delay3, "[3min, 10min)");
	g->setColorAt(600, SCScheme.colors.qc.delay4, "[10min, 15min)");
	g->setColorAt(1800, SCScheme.colors.qc.delay5, "[15min, 12h)");
	g->setColorAt(43200, SCScheme.colors.qc.delay6, "[12h, 1d)");
	g->setColorAt(86400, SCScheme.colors.qc.delay7, ">= 1d");

	g = &_qcGradients["latency"];
	g->title = "Latency";
	g->unsetColor = SCScheme.colors.qc.qcNotSet;
	g->setColorAt(0, SCScheme.colors.qc.qcOk, "OK < 60s");
	g->setColorAt(60, SCScheme.colors.qc.qcWarning, "Warning >= 60s");

	g = &_qcGradients["timing quality"];
	g->title = "Timing Quality";
	g->unsetColor = SCScheme.colors.qc.qcNotSet;
	g->setColorAt(0, SCScheme.colors.qc.qcWarning, "Warning < 50");
	g->setColorAt(50, SCScheme.colors.qc.qcOk, "OK >= 50");

	g = &_qcGradients["gaps interval"];
	g->title = "Gaps Interval";
	g->setColorAt(0, SCScheme.colors.qc.qcOk, "OK");
	g->setColorAt(1, SCScheme.colors.qc.qcError, "Error");

	g = &_qcGradients["gaps length"];
	g->title = "Gaps Length in s";
	g->setColorAt(0, SCScheme.colors.qc.qcOk, "OK");
	g->setColorAt(1, SCScheme.colors.qc.qcError, "Error");

	g = &_qcGradients["overlaps interval"];
	g->title = "Overlaps interval";
	g->setColorAt(0, SCScheme.colors.qc.qcOk, "OK");
	g->setColorAt(1, SCScheme.colors.qc.qcError, "Error");

	g = &_qcGradients["availability"];
	g->title = "Availability";
	g->setColorAt(0, SCScheme.colors.qc.qcError, "Error");
	g->setColorAt(75, SCScheme.colors.qc.qcWarning, "Warning");
	g->setColorAt(100, SCScheme.colors.qc.qcOk, "OK");

	g = &_qcGradients["offset"];
	g->title = "Offset";
	g->setColorAt(0, SCScheme.colors.qc.qcOk, "OK");
	g->setColorAt(100000, SCScheme.colors.qc.qcWarning, "Warning");

	g = &_qcGradients["rms"];
	g->title = "RMS";
	g->setColorAt(0, SCScheme.colors.qc.qcError, "Error");
	g->setColorAt(10, SCScheme.colors.qc.qcOk, "OK");

	_legend = new NetworkLayerLegend(this);
	_legend->setEnabled(true);
	_legend->setArea(Qt::AlignRight | Qt::AlignTop);
	_legend->setLayer(this);
	addLegend(_legend);

	_statusLegend = new NetworkStatusLegend(this);
	_statusLegend->setArea(Qt::AlignRight | Qt::AlignTop);
	_statusLegend->setLayer(this);
	_statusLegend->setEnabled(true);
	addLegend(_statusLegend);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
NetworkLayer::~NetworkLayer() {
	disposeSymbols();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayer::updateAnnotations() {
	foreach ( NetworkLayerSymbol *s, _stationSymbols ) {
		DataModel::Station *sta = s->model();
		if ( _showChannelCodes && s->data() && s->data()->channel ) {
			s->setAnnotation(
				(
					sta->network()->code() + "." + sta->code() + "." +
					s->data()->channel->sensorLocation()->code() + "." +
					s->data()->channel->code()
				).c_str()
			);
		}
		else {
			s->setAnnotation(
				(
					sta->network()->code() + "." + sta->code()
				).c_str()
			);
		}
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayer::disposeSymbols() {
	foreach ( NetworkLayerSymbol *s, _stationSymbols )
		delete s;

	_stationSymbols.clear();
	_stationSymbolLookup.clear();
	_drawOrder.clear();
	_drawOrderDirty = true;
	_currentSymbol = nullptr;
	_currentClickSymbol = nullptr;
	_selectedSymbol = nullptr;

	// Remove link to global array
	for ( auto & [model, data] : global.stationConfig ) {
		data->viewData = nullptr;
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayer::setColorMode(ColorMode mode, bool force) {
	if ( (_colorMode == mode) && !force ) {
		return;
	}

	_colorMode = mode;
	currentGradient = nullptr;

	switch ( _colorMode ) {
		case GroundMotion:
			currentGradient = &_gmGradient;
			break;
		case QC:
			currentGradient = qcGradient();
			break;
		default:
			break;
	}

	foreach ( NetworkLayerSymbol *s, _stationSymbols ) {
		updateColor(s);
		if ( s->isClosed() ) {
			s->setVisible(closedStationsVisible());
		}
	}

	_legend->updateFrom(this);

	emit updateRequested();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayer::setInventory(DataModel::Inventory *inv,
                                Gui::Map::Annotations *annotations,
                                const Core::Time *time) {
	disposeSymbols();
	_hiddenNetworks.clear();

	if ( !inv ) {
		return;
	}

	size_t n, s;

	Core::Time refTime;
	if ( time ) {
		refTime = *time;
	}
	else {
		refTime = Core::Time::UTC();
	}

	// A station is rendered with one symbol but may come with several epochs,
	// either below the same network epoch or spread over multiple ones. Pick
	// the representative epoch first, the inventory does not guarantee any
	// order.
	std::map<std::string, DataModel::Station*> stations;

	for ( n = 0; n < inv->networkCount(); ++n ) {
		DataModel::Network *net = inv->network(n);

		for ( s = 0; s < net->stationCount(); ++s ) {
			DataModel::Station *sta = net->station(s);

			if ( refTime < sta->start() ) {
				continue;
			}

			auto [it, inserted] = stations.emplace(net->code() + "." + sta->code(), sta);
			if ( !inserted ) {
				it->second = preferredEpoch(it->second, sta, refTime);
			}
		}
	}

	for ( const auto &[staID, sta] : stations ) {
		// A station whose epoch is closed is normally not shown. It is
		// still shown when its last stream epoch is closed - regardless
		// of configuration - but marked as closed (rendered with a cross).
		bool closed = isClosedStation(sta, refTime);

		if ( !isActiveStation(sta, refTime) && !closed ) {
			continue;
		}

		double lat, lon;
		try {
			lat = sta->latitude();
			lon = sta->longitude();
		}
		catch ( ... ) {
			continue;
		}

		// Got a valid station epoch
		NetworkLayerSymbol *symbol = new NetworkLayerSymbol(this, sta, annotations->add(QString()));
		symbol->setPenWidth(defaultFrameWidth);
		symbol->setLocation(lat, lon);
		symbol->setClosed(closed);
		if ( closed && !closedStationsVisible() ) {
			symbol->setVisible(false);
		}
		updateColor(symbol);

		_stationSymbolLookup[staID] = symbol;
		_stationSymbols.append(symbol);

		// Register symbol with config
		auto it = global.stationConfig.find(symbol->model());
		if ( it != global.stationConfig.end() ) {
			auto data = it->second.get();
			data->viewData = symbol;
		}
	}

	updateAnnotations();

	std::sort(_stationSymbols.begin(), _stationSymbols.end(), topToBottom);

	assignNetworkColors();
	if ( _colorMode == Network ) {
		foreach ( NetworkLayerSymbol *s, _stationSymbols ) {
			updateColor(s);
		}
	}

	_drawOrderDirty = true;
	_legend->updateFrom(this);

	emit updateRequested();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayer::clear() {
	disposeSymbols();
	emit updateRequested();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayer::setGMGradient(const NetworkLayerGradient &gradient) {
	_gmGradient = gradient;

	if ( _colorMode == GroundMotion ) {
		setColorMode(GroundMotion, true);
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
const NetworkLayerGradient *NetworkLayer::qcGradient() const {
	auto it = _qcGradients.find(_activeQCParameter);
	if ( it == _qcGradients.end() ) {
		return nullptr;
	}
	return &it.value();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayer::setActiveQCParameter(const std::string &param) {
	_activeQCParameter = param;
	setColorMode(QC, true);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayer::setStationsVisible(QSet<const DataModel::Station*> *set) {
	// Match by "networkCode.stationCode" rather than by pointer: a station's
	// symbol may use a different (e.g. closed) epoch than the one stored in
	// the station configuration the search selection refers to.
	std::set<std::string> ids;
	if ( set ) {
		foreach ( const DataModel::Station *sta, *set ) {
			ids.insert(sta->network()->code() + "." + sta->code());
		}
	}

	foreach ( NetworkLayerSymbol *s, _stationSymbols ) {
		if ( !set ) {
			// No selection: restore the regular filter-based visibility.
			updateSymbolVisibility(s);
		}
		else if ( ids.find(s->model()->network()->code() + "." +
		                   s->model()->code()) != ids.end() ) {
			// A searched station is always shown, even when it would be
			// hidden by the closed/unbound/network filters.
			s->setDefaultVisibility();
		}
		else {
			s->setVisible(false);
		}
	}

	emit updateRequested(Position);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayer::setShowChannelCodes(bool enable) {
	if ( _showChannelCodes == enable ) {
		return;
	}
	_showChannelCodes = enable;

	updateAnnotations();

	emit updateRequested();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayer::setShowIssues(bool enable) {
	if ( _showIssues == enable ) {
		return;
	}
	_showIssues = enable;
	emit updateRequested();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayer::setShowUnbound(bool enable) {
	if ( _showUnbound == enable ) {
		return;
	}
	_showUnbound = enable;

	foreach ( NetworkLayerSymbol *s, _stationSymbols ) {
		updateSymbolVisibility(s);
	}

	emit updateRequested(Position);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayer::setShowClosed(bool enable) {
	if ( _showClosed == enable ) {
		return;
	}
	_showClosed = enable;

	foreach ( NetworkLayerSymbol *s, _stationSymbols ) {
		updateSymbolVisibility(s);
	}

	emit updateRequested(Position);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




namespace {

template <typename Accept>
QString buildStationList(const QVector<NetworkLayerSymbol*> &symbols, Accept accept) {
	QStringList lines;

	foreach ( NetworkLayerSymbol *s, symbols ) {
		if ( !s->isVisible() || !accept(s) ) {
			continue;
		}

		DataModel::Station *sta = s->model();
		lines << QString("%1.%2").arg(sta->network()->code().c_str(),
		                              sta->code().c_str());
	}

	lines.sort();
	lines.removeDuplicates();

	return lines.join('\n');
}


int stationListCount(const QString &list) {
	return list.isEmpty() ? 0 : list.count('\n') + 1;
}

}


// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
QString NetworkLayer::closedStationList() const {
	return buildStationList(_stationSymbols,
	                        [](const NetworkLayerSymbol *s) { return s->isClosed(); });
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
std::set<std::string> NetworkLayer::closedStationCodes() const {
	std::set<std::string> result;
	foreach ( NetworkLayerSymbol *s, _stationSymbols ) {
		if ( s->isClosed() ) {
			result.insert(s->model()->network()->code() + "." + s->model()->code());
		}
	}
	return result;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
std::set<std::string> NetworkLayer::visibleStationCodes() const {
	std::set<std::string> result;
	foreach ( NetworkLayerSymbol *s, _stationSymbols ) {
		if ( s->isVisible() ) {
			result.insert(s->model()->network()->code() + "." + s->model()->code());
		}
	}
	return result;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
QString NetworkLayer::unboundStationList() const {
	return buildStationList(_stationSymbols,
	                        [](const NetworkLayerSymbol *s) { return s->state() == Settings::Unconfigured; });
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
QString NetworkLayer::mismatchStationList() const {
	return buildStationList(_stationSymbols,
	                        [](const NetworkLayerSymbol *s) {
	                        	return (s->state() == Settings::NoPrimaryStream)
	                        	    || (s->state() == Settings::NoChannelGroupMetaData)
	                        	    || (s->state() == Settings::NoVerticalChannelMetaData);
	                        });
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
QString NetworkLayer::noDetecStreamStationList() const {
	return buildStationList(_stationSymbols,
	                        [](const NetworkLayerSymbol *s) { return s->state() == Settings::NoPrimaryStream; });
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
QString NetworkLayer::disabledStationList() const {
	return buildStationList(_stationSymbols,
	                        [](const NetworkLayerSymbol *s) {
	                        	return s->data() && !s->data()->enabled;
	                        });
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
QString NetworkLayer::enabledStationList() const {
	return buildStationList(_stationSymbols,
	                        [](const NetworkLayerSymbol *s) {
	                        	return s->data() && s->data()->enabled;
	                        });
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool NetworkLayer::stationLocation(const QString &netSta, QPointF &location) const {
	auto it = _stationSymbolLookup.find(netSta.toStdString());
	if ( it == _stationSymbolLookup.end() ) {
		return false;
	}

	DataModel::Station *sta = it->second->model();
	try {
		location = QPointF(sta->longitude(), sta->latitude());
	}
	catch ( ... ) {
		return false;
	}

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
int NetworkLayer::visibleStationCount() const {
	int count = 0;
	foreach ( NetworkLayerSymbol *s, _stationSymbols ) {
		if ( s->isVisible() ) {
			++count;
		}
	}
	return count;
}

int NetworkLayer::stationCount() const {
	int count = 0;
	foreach ( NetworkLayerSymbol *s, _stationSymbols ) {
		if ( !s->isClosed() ) {
			++count;
		}
	}
	return count;
}

int NetworkLayer::visibleOpenStationCount() const {
	int count = 0;
	foreach ( NetworkLayerSymbol *s, _stationSymbols ) {
		if ( s->isVisible() && !s->isClosed() ) {
			++count;
		}
	}
	return count;
}

int NetworkLayer::closedStationCount() const {
	return stationListCount(closedStationList());
}

int NetworkLayer::unboundStationCount() const {
	return stationListCount(unboundStationList());
}

int NetworkLayer::mismatchStationCount() const {
	return stationListCount(mismatchStationList());
}

int NetworkLayer::noDetecStreamStationCount() const {
	return stationListCount(noDetecStreamStationList());
}

int NetworkLayer::disabledStationCount() const {
	return stationListCount(disabledStationList());
}

int NetworkLayer::enabledStationCount() const {
	return stationListCount(enabledStationList());
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
QStringList NetworkLayer::networkCodes() const {
	QSet<QString> codes;
	foreach ( NetworkLayerSymbol *s, _stationSymbols ) {
		codes.insert(s->model()->network()->code().c_str());
	}

	QStringList list = codes.values();
	list.sort();
	return list;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool NetworkLayer::isNetworkVisible(const QString &code) const {
	return _hiddenNetworks.find(code.toStdString()) == _hiddenNetworks.end();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayer::setNetworkVisible(const QString &code, bool visible) {
	std::string c = code.toStdString();
	if ( visible ) {
		_hiddenNetworks.erase(c);
	}
	else {
		_hiddenNetworks.insert(c);
	}

	foreach ( NetworkLayerSymbol *s, _stationSymbols ) {
		if ( s->model()->network()->code() == c ) {
			updateSymbolVisibility(s);
		}
	}

	emit updateRequested();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayer::updateSymbolVisibility(NetworkLayerSymbol *s) const {
	s->setDefaultVisibility();
	if ( !s->isVisible() ) {
		return;
	}

	if ( _hiddenNetworks.find(s->model()->network()->code()) != _hiddenNetworks.end() ) {
		s->setVisible(false);
		return;
	}

	if ( s->isClosed() && !closedStationsVisible() ) {
		s->setVisible(false);
		return;
	}

	if ( (s->state() == Settings::Unconfigured) && !_showUnbound ) {
		s->setVisible(false);
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
Gui::Map::Legend *NetworkLayer::mainLegend() const {
	return _legend;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayer::updateStation(const std::string &staID) {
	auto it = _stationSymbolLookup.find(staID);
	if ( it == _stationSymbolLookup.end() ) {
		return;
	}

	updateColor(it->second);
	update();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayer::tick() {
	Core::Time now = Core::Time::UTC();
	bool changed = false;

	foreach ( NetworkLayerSymbol *s, _stationSymbols ) {
		s->setPriority(Gui::Map::Symbol::NONE);

		if ( !s->_data ) {
			continue;
		}

		if ( !s->_data->triggerTime ) {
			continue;
		}

		Core::TimeSpan diff = now - *s->_data->triggerTime;

		if ( diff > global.triggerTimeout ) {
			// Reset trigger time if outdated
			s->_data->triggerTime = Core::None;
			s->setFrameSize(0);
			changed = true;
		}
		else if ( diff < Core::TimeSpan(0,0) ) {
			if ( s->frameSize() > 0 ) {
				s->setFrameSize(0);
				changed = true;
			}
		}
		else {
			s->setPriority(Gui::Map::Symbol::HIGH);

			if ( global.tickToggleState ) {
				// Trigger on
				int alpha = qMax(
					0,
					qMin(
						255,
						static_cast<int>(
							255 * static_cast<double>(global.triggerTimeout - diff) / static_cast<double>(global.triggerTimeout)
						)
					)
				);
				s->setFrameColor(QColor(255,0,0,alpha));
				s->setFrameSize(global.triggerFrameSize);
			}
			else {
				// Trigger off
				s->setFrameSize(0);
			}

			changed = true;
		}
	}

	if ( updateStatusLegend() ) {
		changed = true;
	}

	if ( changed )
		emit updateRequested();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool NetworkLayer::isInside(const QMouseEvent *event, const QPointF &geoPos) {
	_isInsideAlert = hasHiddenNetworks() && _filterAlertRect.contains(event->pos());
	if ( _isInsideAlert ) {
		_isInsideSymbol = nullptr;
		return true;
	}

	int x = event->pos().x();
	int y = event->pos().y();
	const auto &symbols = drawOrder();
	auto it = symbols.end();
	_isInsideSymbol = nullptr;

	while ( it != symbols.begin() ) {
		--it;
		if ( (*it)->isVisible() && (*it)->isInside(x, y) ) {
			_isInsideSymbol = *it;
			return true;
		}
	}

	return false;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayer::calculateMapPosition(const Gui::Map::Canvas *canvas) {
	foreach ( NetworkLayerSymbol *s, _stationSymbols )
		s->calculateMapPosition(canvas);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayer::draw(const Gui::Map::Canvas *canvas, QPainter &p) {
	p.save();

	bool showIssues = _showIssues && (_colorMode == Network);

	const auto &symbols = drawOrder();

	foreach ( NetworkLayerSymbol *s, symbols ) {
		if ( s->isClipped() || !s->isVisible() ) {
			continue;
		}

		s->drawShadow(p);
	}

	for ( int i = Gui::Map::Symbol::NONE; i <= Gui::Map::Symbol::HIGH; ++i ) {
		foreach ( NetworkLayerSymbol *s, symbols ) {
			if ( s->isClipped() || !s->isVisible() || (s->priority() != i) ) {
				continue;
			}

			s->draw(canvas, p);

			if ( s->isClosed() ) {
				static OPT(QPixmap) pmClosed;
				if ( !pmClosed ) {
					pmClosed = Gui::pixmap(p.fontMetrics(), "close", QColor(Qt::black), p.device()->devicePixelRatioF());
				}
				drawInfoSymbol(p, s->pos() + QPoint(0, -s->width() / 2), *pmClosed);
			}

			if ( showIssues && (s->state() != Settings::OK) && !s->isClosed() ) {
				QPoint lowerLeft = s->pos() + QPoint(0, -s->width() / 2);

				static OPT(QPixmap) pmQuestion, pmWrench, pmUnlink, pmDatabase;

				switch ( s->state() ) {
					case Settings::Unknown:
						if ( !pmQuestion ) {
							pmQuestion = Gui::pixmap(p.fontMetrics(), "question_mark", QColor(Qt::black), p.device()->devicePixelRatioF());
						}
						drawWarningSymbol(p, lowerLeft, *pmQuestion);
						break;

					case Settings::Unconfigured:
						if ( !pmWrench ) {
							pmWrench = Gui::pixmap(p.fontMetrics(), "settings", QColor(Qt::black), p.device()->devicePixelRatioF());
						}
						drawWarningSymbol(p, lowerLeft, *pmWrench);
						break;

					case Settings::NoPrimaryStream:
						if ( !pmUnlink ) {
							pmUnlink = Gui::pixmap(p.fontMetrics(), "unlink", QColor(Qt::black), p.device()->devicePixelRatioF());
						}
						drawWarningSymbol(p, lowerLeft, *pmUnlink);
						break;

					case Settings::NoChannelGroupMetaData:
					case Settings::NoVerticalChannelMetaData:
						if ( !pmDatabase ) {
							pmDatabase = Gui::pixmap(p.fontMetrics(), "database", QColor(Qt::black), p.device()->devicePixelRatioF());
						}
						drawWarningSymbol(p, lowerLeft, *pmDatabase);
						break;

					default:
						break;
				}
			}
		}
	}

	// Alert the user when the stations are filtered by network code so that
	// only a subset of them is shown. Drawn here, independently of the scale
	// layer, at the same position (just above the scale box).
	_filterAlertRect = QRect();
	if ( hasHiddenNetworks() ) {
		p.setRenderHint(QPainter::Antialiasing, true);
		p.setFont(qApp->font());

		QColor orange(255, 127, 0);
		QFont normalFont = p.font();
		QFont boldFont = normalFont;
		boldFont.setBold(true);

		QFontMetrics fmN(normalFont);
		QFontMetrics fmB(boldFont);

		QString prefix = tr("Open stations: ");
		QString shownStr = QString::number(visibleOpenStationCount());
		QString suffix = QString(" / %1").arg(stationCount());

		int eh = fmN.height();
		int iconSz = eh;
		int gap = 4;
		int pad = 4;

		int prefixW = QT_FM_WIDTH(fmN, prefix);
		int shownW = QT_FM_WIDTH(fmB, shownStr);
		int suffixW = QT_FM_WIDTH(fmN, suffix);
		int contentW = iconSz + gap + prefixW + shownW + suffixW;

		// Same anchor as the scale box (bottom-center), placed just above it.
		int scaleBoxHeight = eh + 4 + eh/2 + 8;
		int scaleBoxTop = canvas->height() - 10 - scaleBoxHeight;

		QRect box(0, 0, contentW + 2*pad, eh + 2*pad);
		box.moveLeft((canvas->width() - box.width())/2);
		box.moveBottom(scaleBoxTop - 6);
		_filterAlertRect = box;

		p.setPen(Qt::darkGray);
		p.setBrush(QColor(255,255,255,192));
		p.drawRect(box);

		int x = box.left() + pad;

		QPixmap icon = Gui::icon("filter", orange).pixmap(iconSz);
		p.drawPixmap(x, box.top() + (box.height() - iconSz)/2, icon);
		x += iconSz + gap;

		int baseY = box.top() + pad + fmN.ascent();

		p.setFont(normalFont);
		p.setPen(Qt::black);
		p.drawText(x, baseY, prefix);
		x += prefixW;

		p.setFont(boldFont);
		p.setPen(orange);
		p.drawText(x, baseY, shownStr);
		x += shownW;

		p.setFont(normalFont);
		p.setPen(Qt::black);
		p.drawText(x, baseY, suffix);
	}

	p.restore();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayer::handleLeaveEvent() {
	if ( _currentSymbol ) {
		_currentSymbol->setPen(_currentSymbol->isSelected() ? selectedFrameColor : defaultFrameColor);
		_currentSymbol = nullptr;
		emit stationLeft();
		emit updateRequested();
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool NetworkLayer::filterMouseMoveEvent(QMouseEvent *e, const QPointF &) {
	// Hover changed
	if ( _isInsideSymbol != _currentSymbol ) {
		// Just in case we missed the release event
		if ( e->button() != Qt::LeftButton )
			_currentClickSymbol = nullptr;

		if ( _currentSymbol ) _currentSymbol->setPen(_currentSymbol->isSelected() ? selectedFrameColor : defaultFrameColor);
		if ( _isInsideSymbol ) {
			if ( !_currentClickSymbol || (_isInsideSymbol == _currentClickSymbol) ) {
				_isInsideSymbol->setPen(hoverFrameColor);
				emit stationEntered(_isInsideSymbol->model());
			}
		}

		_currentSymbol = _isInsideSymbol;

		if ( _currentSymbol ) {
			DataModel::Station *sta = _currentSymbol->model();
			QString toolTip = QString("%1.%2").arg(sta->network()->code().c_str()).arg(sta->code().c_str());
			if ( !sta->description().empty() ) {
				toolTip += "\n";
				toolTip += sta->description().c_str();
			}
			if ( _currentSymbol->value() >= 0 )
				toolTip += QString("\nValue: %1").arg(_currentSymbol->value());
		}

		emit updateRequested();
	}

	return false;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool NetworkLayer::filterMousePressEvent(QMouseEvent *, const QPointF &) {
	if ( _isInsideAlert ) {
		return true;
	}

	_currentClickSymbol = _currentSymbol;
	return _currentClickSymbol != nullptr;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool NetworkLayer::filterMouseReleaseEvent(QMouseEvent *, const QPointF &) {
	if ( _isInsideAlert ) {
		emit filterAlertClicked();
		return true;
	}

	if ( _clickSuppressed ) {
		_currentClickSymbol = nullptr;
		return false;
	}

	if ( !_currentClickSymbol ) {
		return false;
	}

	bool sameSymbol = (_currentClickSymbol == _currentSymbol);
	_currentClickSymbol = nullptr;
	if ( sameSymbol ) {
		// A single symbol here; overlapping symbols are resolved by the map
		// widget's chooser.
		selectStation(_currentSymbol);
	}

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayer::selectStation(NetworkLayerSymbol *symbol) {
	if ( !symbol ) {
		return;
	}

	_selectedSymbol = symbol;
	emit stationClicked(symbol->model());
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
QVector<NetworkLayerSymbol*> NetworkLayer::symbolsUnder(int x, int y) const {
	QVector<NetworkLayerSymbol*> result;

	const auto &symbols = drawOrder();
	auto it = symbols.end();
	while ( it != symbols.begin() ) {
		--it;
		NetworkLayerSymbol *s = *it;
		if ( s->isVisible() && s->isInside(x, y) ) {
			result.append(s);
		}
	}

	// Sort alphabetically by network then station code.
	std::sort(result.begin(), result.end(),
	          [](const NetworkLayerSymbol *a, const NetworkLayerSymbol *b) {
	          	const std::string &na = a->model()->network()->code();
	          	const std::string &nb = b->model()->network()->code();
	          	if ( na != nb ) {
	          		return na < nb;
	          	}
	          	return a->model()->code() < b->model()->code();
	          });

	return result;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool NetworkLayer::filterMouseDoubleClickEvent(QMouseEvent *, const QPointF &) {
	/*
	if ( _currentSymbol ) {
		return true;
	}
	*/

	return false;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayer::updateColor(NetworkLayerSymbol *symbol) {
	bool enabled = true;

	symbol->setState(Settings::OK);
	auto it  = global.stationConfig.find(symbol->model());
	if ( it != global.stationConfig.end() ) {
		const Settings::StationData *data = it->second.get();
		// Closed stations keep the OK state so the cross stays their only
		// marker and no issue icon is stacked on top of it.
		if ( !symbol->isClosed() ) {
			symbol->setState(data->state);
		}
		enabled = data->enabled;
	}
	else if ( !symbol->isClosed() ) {
		symbol->setState(Settings::Unknown);
	}

	if ( enabled ) {
		switch ( _colorMode ) {
			case Default:
				symbol->setColor(defaultColor);
				break;

			case Network:
			{
				auto it = _networkColors.find(symbol->model()->network()->code());
				symbol->setColor(it != _networkColors.end() ? it->second : defaultColor);
				break;
			}

			case GroundMotion:
				symbol->setColorFromValue(symbol->data() ? symbol->data()->maximumAmplitude : -1);
				break;

			case QC:
			{
				auto data = symbol->data();
				if ( data ) {
					auto dit = data->qc.find(_activeQCParameter);
					symbol->setColorFromValue(dit != data->qc.end() ? dit->second->value() : -1);
				}
				else {
					symbol->setColorFromValue(-1);
				}
				break;
			}

			default:
				symbol->setColorFromValue(-1);
				break;
		}
	}
	else {
		symbol->setColor(SCScheme.colors.stations.disabled);
	}

	_drawOrderDirty = true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
Gui::Map::Legend *NetworkLayer::statusLegend() const {
	return _statusLegend;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool NetworkLayer::updateStatusLegend() {
	NetworkStatusLegend::Counts counts;
	auto now = Core::Time::UTC();

	foreach ( NetworkLayerSymbol *s, _stationSymbols ) {
		if ( !s->isVisible() ) {
			continue;
		}

		++counts.visible;

		// Same criteria as the station lists of the context menu
		switch ( s->state() ) {
			case Settings::Unconfigured:
				++counts.unbound;
				break;
			case Settings::NoPrimaryStream:
				++counts.noDetecStream;
				++counts.mismatch;
				break;
			case Settings::NoChannelGroupMetaData:
			case Settings::NoVerticalChannelMetaData:
				++counts.mismatch;
				break;
			default:
				break;
		}

		auto data = s->data();
		if ( !data ) {
			continue;
		}

		if ( !data->enabled ) {
			++counts.disabled;
			continue;
		}

		if ( s->isClosed() || (s->state() != Settings::OK) ) {
			continue;
		}

		// Data delay of enabled, operational stations. QC reports are sent at
		// intervals of minutes. If the last report is older than the error
		// threshold, the station counts as silent regardless of the reported
		// delay, e.g. because QC stopped as no data arrive anymore.
		auto it = data->qc.find("delay");
		if ( it == data->qc.end() ) {
			++counts.noQC;
		}
		else {
			const auto *wfq = it->second.get();
			double delay = wfq->value();
			double reportAge = 0;
			try {
				reportAge = static_cast<double>(now - wfq->end());
			}
			catch ( ... ) {
				try {
					reportAge = static_cast<double>(now - wfq->start());
				}
				catch ( ... ) {}
			}

			if ( reportAge > statusDelayError ) {
				delay = std::max(delay, reportAge);
			}

			if ( delay < statusDelayWarning ) {
				++counts.delayOK;
			}
			else if ( delay < statusDelayError ) {
				++counts.delayWarning;
			}
			else {
				++counts.delayError;
			}
		}

		it = data->qc.find("latency");
		if ( (it != data->qc.end()) && (it->second->value() > statusLatencyWarning) ) {
			++counts.latencyWarning;
		}
	}

	return static_cast<NetworkStatusLegend*>(_statusLegend)->setCounts(counts);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void NetworkLayer::assignNetworkColors() {
	_networkColors.clear();

	std::set<std::string> codes;
	for ( auto *s : _stationSymbols ) {
		codes.insert(s->model()->network()->code());
	}

	// Configured colors first
	for ( const auto &entry : global.networkColors ) {
		auto pos = entry.find(':');
		if ( pos == std::string::npos ) {
			SEISCOMP_WARNING("Invalid network color '%s', expected NET:color",
			                 entry.c_str());
			continue;
		}

		auto code = QString::fromStdString(entry.substr(0, pos)).trimmed().toStdString();
		QColor color(QString::fromStdString(entry.substr(pos + 1)).trimmed());
		if ( !color.isValid() ) {
			SEISCOMP_WARNING("Invalid color for network %s: %s",
			                 code.c_str(), entry.substr(pos + 1).c_str());
			continue;
		}

		if ( codes.find(code) != codes.end() ) {
			_networkColors[code] = color;
		}
	}

	QVector<Lab> used;
	for ( const auto &[code, color] : _networkColors ) {
		used.append(toLab(color));
	}

	// Assign the remaining networks in alphabetical order. Each one gets
	// the candidate color which is perceptually most distinct from all
	// colors already in use.
	const auto &candidates = networkColorCandidates();
	QVector<Lab> candidateLabs;
	candidateLabs.reserve(candidates.size());
	for ( const auto &c : candidates ) {
		candidateLabs.append(toLab(c));
	}

	QVector<bool> taken(candidates.size(), false);

	for ( const auto &code : codes ) {
		if ( _networkColors.find(code) != _networkColors.end() ) {
			continue;
		}

		QColor color;

		if ( used.isEmpty() ) {
			color = defaultColor;
		}
		else {
			int best = -1;
			double bestDistance = -1;
			for ( int i = 0; i < candidates.size(); ++i ) {
				if ( taken[i] ) {
					continue;
				}

				double minDistance = std::numeric_limits<double>::max();
				for ( const auto &u : used ) {
					minDistance = std::min(minDistance, labDistance(candidateLabs[i], u));
				}

				if ( minDistance > bestDistance ) {
					bestDistance = minDistance;
					best = i;
				}
			}

			if ( best < 0 ) {
				color = defaultColor;
			}
			else {
				taken[best] = true;
				color = candidates[best];
			}
		}

		_networkColors[code] = color;
		used.append(toLab(color));
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
double NetworkLayer::drawRank(const NetworkLayerSymbol *s) const {
	if ( (_colorMode != GroundMotion) && (_colorMode != QC) ) {
		return 0;
	}

	if ( s->data() && !s->data()->enabled ) {
		return -2;
	}

	if ( s->value() < 0 || !currentGradient || currentGradient->isEmpty() ) {
		return -1;
	}

	if ( _colorMode == GroundMotion ) {
		// Higher amplitudes on top
		return s->value();
	}

	// QC: rank by the distance of the value's gradient bin to the bin
	// flagged as OK. That covers parameters where low values are good
	// (delay, latency) as well as those where high values are good
	// (availability, timing quality).
	int okIndex = 0;
	int valueIndex = 0;
	int idx = 0;
	for ( auto it = currentGradient->begin(); it != currentGradient->end(); ++it, ++idx ) {
		if ( it.value().first == SCScheme.colors.qc.qcOk ) {
			okIndex = idx;
		}
		if ( it.key() <= s->value() ) {
			valueIndex = idx;
		}
	}

	return std::abs(valueIndex - okIndex);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
const QVector<NetworkLayerSymbol*> &NetworkLayer::drawOrder() const {
	if ( _drawOrderDirty ) {
		// _stationSymbols is sorted north to south. A stable sort keeps
		// that order among stations of equal rank.
		QVector<QPair<double, NetworkLayerSymbol*>> ranked;
		ranked.reserve(_stationSymbols.size());
		for ( auto *s : _stationSymbols ) {
			ranked.append({drawRank(s), s});
		}

		std::stable_sort(ranked.begin(), ranked.end(),
		                 [](const auto &a, const auto &b) { return a.first < b.first; });

		_drawOrder.clear();
		_drawOrder.reserve(ranked.size());
		for ( const auto &item : ranked ) {
			_drawOrder.append(item.second);
		}

		_drawOrderDirty = false;
	}

	return _drawOrder;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
