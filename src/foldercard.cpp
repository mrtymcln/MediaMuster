#include "foldercard.h"
#include "avidlimits.h"
#include "formatutil.h"
#include "rebalancer.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QPixmap>

namespace
{
	QColor capColor(int fileCount)
	{
		if (fileCount >= AvidLimits::kFolderCritical)
			return QColor(0xff, 0x3b, 0x30);
		if (fileCount >= AvidLimits::kFolderWarn)
			return QColor(0xff, 0x95, 0x00);
		return QColor(0x34, 0xc7, 0x59);
	}

	QBrush makeStripeBrush(const QColor &color)
	{
		constexpr int kTile = 8;
		QPixmap tile(kTile, kTile);
		tile.fill(Qt::transparent);

		QPainter p(&tile);
		p.setRenderHint(QPainter::Antialiasing);
		QPen pen(color, 3.0);
		pen.setCapStyle(Qt::FlatCap);
		p.setPen(pen);
		p.drawLine(0, kTile, kTile, 0);
		p.end();

		return QBrush(tile);
	}

	QBrush makeAquaBrush(const QColor &base, const QRect &rect)
	{
		QLinearGradient grad(rect.topLeft(), rect.bottomLeft());
		grad.setColorAt(0.00, base.lighter(140));
		grad.setColorAt(0.45, base);
		grad.setColorAt(1.00, base.darker(115));
		return QBrush(grad);
	}

	void paintAquaGloss(QPainter &p, const QRect &rect, int radius)
	{
		QRect top = rect;
		top.setHeight(rect.height() / 2);
		QLinearGradient grad(top.topLeft(), top.bottomLeft());
		grad.setColorAt(0.0, QColor(255, 255, 255, 110));
		grad.setColorAt(1.0, QColor(255, 255, 255, 0));
		p.setBrush(grad);
		p.setPen(Qt::NoPen);
		p.drawRoundedRect(top, radius, radius);
	}

	// Cards have a fixed height, but fluid width.
	constexpr int kCardMinWidth = 200;
	constexpr int kCardHeight = 100;
	constexpr int kPad = 12;
	constexpr int kBarHeight = 14;
	constexpr int kBarRadius = 4;

} // namespace

FolderCard::FolderCard(QWidget *parent)
	: QFrame(parent)
{
	setObjectName(QStringLiteral("folderCard"));
	setFrameShape(QFrame::StyledPanel);
	setStyleSheet(QStringLiteral("QFrame#folderCard { border: 1px solid palette(mid); "
								 "border-radius: 6px; background: palette(base); }"));
	setFixedHeight(kCardHeight);
	setMinimumWidth(kCardMinWidth);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void FolderCard::setFolder(const FolderState &fs)
{
	m_folderName = fs.name;
	m_inScope = fs.inScope;
	m_isNew = fs.isNew;
	m_finished = false;
	m_currentCount = fs.count;
	m_initialCount = fs.count;
	m_projectedCount = fs.count + fs.filesIn - fs.filesOut;
	update();
}

void FolderCard::setCurrentCount(int current)
{
	if (current == m_currentCount)
		return;
	m_currentCount = current;
	update();
}

void FolderCard::markFinished()
{
	m_currentCount = m_projectedCount;
	m_finished = true;
	update();
}

QSize FolderCard::sizeHint() const
{
	return {kCardMinWidth, kCardHeight};
}

QSize FolderCard::minimumSizeHint() const
{
	return {kCardMinWidth, kCardHeight};
}

void FolderCard::paintEvent(QPaintEvent *event)
{
	QFrame::paintEvent(event);

	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::TextAntialiasing);

	const QRect content = rect().adjusted(kPad, kPad - 2, -kPad, -kPad + 2);
	int y = content.top();

	// MARK: Folder name

	QFont nameFont = font();
	nameFont.setBold(true);
	nameFont.setPointSize(nameFont.pointSize() + 1);
	const QFontMetrics fmName(nameFont);
	p.setFont(nameFont);
	p.setPen(palette().color(QPalette::WindowText));
	// ⚠️ for bloated folders.
	// 🆕 for new folders.
	QString displayName = m_folderName;
	if (m_inScope && qMax(m_currentCount, m_projectedCount) > Rebalancer::kFolderCap)
		displayName = QStringLiteral("⚠️ ") + displayName;
	if (m_isNew)
		displayName = QStringLiteral("🆕 ") + displayName;
	p.drawText(QRect(content.left(), y, content.width(), fmName.height()),
			   Qt::AlignLeft | Qt::AlignVCenter,
			   fmName.elidedText(displayName, Qt::ElideRight, content.width()));
	y += fmName.height() + 6;

	// MARK: Capacity bar

	const QRect track(content.left(), y, content.width(), kBarHeight);
	if (m_inScope)
	{
		// `barFrac` maps file counts onto the visual scale.
		const auto barFrac = [](int n)
		{ return qBound(0.0, double(n) / AvidLimits::kFolderMax, 1.0); };

		// Track shade derived from the theme's text colour: dark on
		// light backgrounds, light on dark backgrounds.
		p.setPen(Qt::NoPen);
		QColor trackColor = palette().color(QPalette::WindowText);
		trackColor.setAlpha(70);
		p.setBrush(trackColor);
		p.drawRoundedRect(track, kBarRadius, kBarRadius);

		const int settledEnd = int(track.width() * barFrac(qMin(m_currentCount, m_projectedCount)));
		const int outerEnd = int(track.width() * barFrac(qMax(m_currentCount, m_projectedCount)));
		const QColor fillColor = capColor(qMax(m_currentCount, m_projectedCount));
		p.save();
		QPainterPath barClip;
		barClip.addRoundedRect(track, kBarRadius, kBarRadius);
		p.setClipPath(barClip);

		if (settledEnd > 0)
		{
			QRect solid(track.x(), track.y(), settledEnd, track.height());
			p.fillRect(solid, makeAquaBrush(fillColor, solid));
			paintAquaGloss(p, solid, 0);
		}

		if (outerEnd > settledEnd)
		{
			QRect ghost(track.x() + settledEnd, track.y(), outerEnd - settledEnd, track.height());
			p.fillRect(ghost, makeStripeBrush(fillColor));
		}

		p.restore();
	}
	y += kBarHeight + 8;

	// MARK: Count caption

	QFont countFont = font();
	const QFontMetrics fmCount(countFont);
	p.setFont(countFont);
	p.setPen(palette().color(QPalette::WindowText));

	QString countText;
	if (!m_inScope || m_currentCount == m_projectedCount)
		countText = Format::count(m_currentCount);
	else
		countText = QStringLiteral("%1 → %2").arg(Format::count(m_currentCount),
												  Format::count(m_projectedCount));
	p.drawText(QRect(content.left(), y, content.width(), fmCount.height()),
			   Qt::AlignLeft | Qt::AlignVCenter, countText);
	y += fmCount.height() + 2;

	// MARK: Delta caption

	QFont deltaFont = font();
	deltaFont.setPointSize(qMax(8, deltaFont.pointSize() - 1));
	const QFontMetrics fmDelta(deltaFont);
	p.setFont(deltaFont);
	QColor secondaryText = palette().color(QPalette::WindowText);
	secondaryText.setAlpha(160);
	p.setPen(secondaryText);

	QString deltaText;
	if (m_finished)
	{
		// Suppress the delta caption after rebalancing.
	}
	else if (!m_inScope)
	{
		deltaText = tr("out of scope");
	}
	else
	{
		const int df = m_projectedCount - m_initialCount;
		if (df == 0)
			deltaText = tr("no change");
		else
			// %n drives the plural; the leading sign is a symbol, not translatable.
			deltaText = df > 0 ? tr("+%n file(s)", nullptr, df) : tr("−%n file(s)", nullptr, -df);
	}
	if (!deltaText.isEmpty())
	{
		p.drawText(QRect(content.left(), y, content.width(), fmDelta.height()),
				   Qt::AlignLeft | Qt::AlignVCenter,
				   fmDelta.elidedText(deltaText, Qt::ElideRight, content.width()));
	}
}