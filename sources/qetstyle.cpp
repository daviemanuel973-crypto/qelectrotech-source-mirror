/*
	Copyright 2006-2026 The QElectroTech Team
	This file is part of QElectroTech.

	QElectroTech is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	QElectroTech is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with QElectroTech.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "qetstyle.h"

#include <QApplication>
#include <QImage>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QStyleOption>
#include <QStyleFactory>
#include <QWidget>

#include "qetpalette.h"

#ifdef Q_OS_WIN
namespace {
	QColor menuInk(QColor ink, const QColor &face, bool disabled)
	{
		const qreal alpha = ink.alphaF();
		ink = QColor(qRound(ink.red() * alpha + face.red() * (1 - alpha)),
		             qRound(ink.green() * alpha + face.green() * (1 - alpha)),
		             qRound(ink.blue() * alpha + face.blue() * (1 - alpha)));
		if (QET::Palette::contrastRatio(ink, face) >= (disabled ? 1.5 : 4.5))
			return ink;
		ink = QET::Palette::contrastRatio(Qt::black, face) >= QET::Palette::contrastRatio(Qt::white, face)
			? QColor(Qt::black) : QColor(Qt::white);
		if (disabled)
			ink = QColor(qRound(.55 * ink.red() + .45 * face.red()),
			             qRound(.55 * ink.green() + .45 * face.green()),
			             qRound(.55 * ink.blue() + .45 * face.blue()));
		return ink;
	}

	QPalette popupPalette(QPalette palette)
	{
		// Disabled actions share the popup surface too; a custom light QET
		// palette may still carry Windows' dark Disabled Window/Base roles.
		QColor face = palette.color(QPalette::Active, QPalette::Window);
		face.setAlpha(255);
		for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
			QColor highlight = palette.color(group, QPalette::Highlight);
			highlight.setAlpha(255);
			const bool disabled = group == QPalette::Disabled;
			const auto ink = menuInk(palette.color(group, QPalette::Text), face, disabled);
			palette.setColor(group, QPalette::Window, face);
			palette.setColor(group, QPalette::Base, face);
			palette.setColor(group, QPalette::Button, face);
			palette.setColor(group, QPalette::Text, ink);
			palette.setColor(group, QPalette::WindowText, ink);
			palette.setColor(group, QPalette::ButtonText, ink);
			palette.setColor(group, QPalette::Highlight, highlight);
			palette.setColor(group, QPalette::HighlightedText,
			                 menuInk(palette.color(group, QPalette::HighlightedText), highlight, disabled));
		}
		return palette;
	}
}
#endif

/**
	@brief QETStyle::QETStyle
	@param base the style to wrap; this object takes ownership of it.
*/
QETStyle::QETStyle(QStyle *base) :
	QProxyStyle(base)
{
	setObjectName(base ? base->objectName() : QString());
#ifdef Q_OS_WIN
	menu_style_ = QStyleFactory::create(QStringLiteral("Fusion"));
	if (menu_style_)
		menu_style_->setParent(this);
#endif
}

/**
	@brief QETStyle::isLineArt
	The same rule misc/make_icon_themes.py uses to pick the icons that
	get a dark variant: an icon is line art when fewer than 20% of its
	visible pixels are saturated.
*/
bool QETStyle::isLineArt(const QImage &image)
{
	const QImage source = image.convertToFormat(QImage::Format_ARGB32);
	int visible = 0;
	int saturated = 0;
	for (int y = 0; y < source.height(); ++y)
	{
		const QRgb *line = reinterpret_cast<const QRgb *>(source.constScanLine(y));
		for (int x = 0; x < source.width(); ++x)
		{
			const QRgb pixel = line[x];
			if (qAlpha(pixel) <= 64)
				continue;
			++visible;
			const QColor color(pixel);
			if (color.hslSaturationF() > 0.25 && color.value() > 60)
				++saturated;
		}
	}
	return visible > 0 && saturated < visible * 0.20;
}

/**
	@brief QETStyle::hoverColor
	The palette's highlight color is the accent users already know from
	selections, moved away from the hovered button face until it reaches
	3:1 (WCAG 1.4.11) against the Light role: Fusion paints a hovered
	auto-raise button with a gradient that runs from Button up to about
	that color, and the icon has to read on the lightest part of it. On a
	dark face the accent is lightened, a step at a time; on a light face it
	is darkened, which keeps a pale accent (macOS's green or yellow
	selection color, which comes with black selection text) from being
	pushed to white. Should twenty steps not get there, the button text
	color serves, which reads on the face by construction.
*/
QColor QETStyle::hoverColor(const QPalette &palette)
{
	const QColor face = palette.color(QPalette::Active, QPalette::Light);
	const bool light_face = face.lightnessF() > 0.5;
	QColor ink = palette.color(QPalette::Active, QPalette::Highlight);
	// 3.5 rather than 3.0: the top of Fusion's hover gradient is a shade
	// lighter than the Light role, so the icon needs some headroom there.
	for (int step = 0; step < 20 && QET::Palette::contrastRatio(ink, face) < 3.5; ++step)
		ink = light_face ? ink.darker(110) : ink.lighter(110);
	if (QET::Palette::contrastRatio(ink, face) < 3.5)
		ink = palette.color(QPalette::Active, QPalette::ButtonText);
	return ink;
}

/**
	@brief QETStyle::tinted
	@return pixmap with every pixel set to color, alpha kept.
*/
QPixmap QETStyle::tinted(const QPixmap &pixmap, const QColor &color)
{
	QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
	QPainter painter(&image);
	painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
	painter.fillRect(image.rect(), color);
	painter.end();

	QPixmap result = QPixmap::fromImage(image);
	result.setDevicePixelRatio(pixmap.devicePixelRatio());
	return result;
}

/**
	@brief QETStyle::menuIcon
	An icon for a highlighted menu item: Normal as the original, Active
	as the original in the HighlightedText color when it is line art.
	Cached per source icon, size and device pixel ratio, since menus
	repaint on every pointer move.
*/
QIcon QETStyle::menuIcon(const QIcon &icon, const QPalette &palette, int size, qreal dpr) const
{
	const QString key = QString("%1/%2/%3/%4")
	                    .arg(icon.cacheKey()).arg(size).arg(dpr)
	                    .arg(palette.color(QPalette::Active, QPalette::HighlightedText).name());
	auto it = menu_icons_.constFind(key);
	if (it != menu_icons_.constEnd())
		return *it;

	const QPixmap normal = icon.pixmap(QSize(size, size), dpr, QIcon::Normal);
	QIcon result;
	result.addPixmap(normal, QIcon::Normal);
	result.addPixmap(isLineArt(normal.toImage())
	                 ? tinted(normal, palette.color(QPalette::Active, QPalette::HighlightedText))
	                 : normal,
	                 QIcon::Active);
	result.addPixmap(icon.pixmap(QSize(size, size), dpr, QIcon::Disabled), QIcon::Disabled);
	menu_icons_.insert(key, result);
	return result;
}

/**
	@brief QETStyle::drawControl
	Windows menus use a palette-driven renderer for text and backgrounds.
	Highlighted menu icons use menuIcon() so line art reads on the
	highlight bar. Other controls go to the base style.
*/
void QETStyle::drawControl(ControlElement element,
                           const QStyleOption *option,
                           QPainter *painter,
                           const QWidget *widget) const
{
#ifdef Q_OS_WIN
	// A popup can use a light/custom QET palette while Windows is dark.
	// The native Windows style picks item text from its OS theme rather than
	// that palette, producing white labels on the light popup. Keep popup
	// painting AND its metrics on one palette-driven style. This includes
	// shortcuts, check marks, submenu arrows, separators and selected icons.
	if (menu_style_ && (element == CE_MenuItem || element == CE_MenuEmptyArea
	                   || element == CE_MenuScroller || element == CE_MenuTearoff)) {
		if (const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option)) {
			QStyleOptionMenuItem copy = *item;
			copy.palette = popupPalette(item->palette);
			if (!copy.icon.isNull() && (copy.state & State_Selected) && (copy.state & State_Enabled)) {
				const int size = menu_style_->pixelMetric(PM_SmallIconSize, &copy, widget);
				copy.icon = menuIcon(copy.icon, copy.palette, size, widget ? widget->devicePixelRatio() : 1.0);
			}
			painter->save();
			painter->fillRect(copy.rect, copy.palette.brush(QPalette::Window));
			painter->setFont(copy.font);
			menu_style_->drawControl(element, &copy, painter, widget);
			painter->restore();
		} else {
			QStyleOption copy = *option;
			copy.palette = popupPalette(option->palette);
			menu_style_->drawControl(element, &copy, painter, widget);
		}
		return;
	}
	// Keep the menu bar's background and text in the same palette. Native
	// Windows theme painting can disagree with the application's dark/custom
	// palette; changing the application style alone does not control both
	// halves. Paint this small surface with Qt's common text renderer instead
	// of delegating its labels back to the native theme. QMenuBar still owns
	// layout, hit testing, overflow, mnemonics and keyboard navigation.
	if (element == CE_MenuBarEmptyArea) {
		painter->fillRect(option->rect, option->palette.brush(QPalette::Window));
		return;
	}
	if (element == CE_MenuBarItem) {
		if (const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option)) {
			QStyleOptionMenuItem copy = *item;
			const bool enabled = item->state & State_Enabled;
			const bool selected = enabled && (item->state & State_Selected);
			const auto group = enabled ? item->palette.currentColorGroup() : QPalette::Disabled;
			const QColor face = item->palette.color(group, selected ? QPalette::Highlight : QPalette::Window);
			QColor ink = item->palette.color(group, selected ? QPalette::HighlightedText : QPalette::WindowText);
			// Saved palettes can outlive an OS theme change. Enabled menu
			// titles must remain readable even with an inconsistent pair.
			if (enabled && QET::Palette::contrastRatio(face, ink) < 4.5) {
				ink = QET::Palette::contrastRatio(face, Qt::black)
				      >= QET::Palette::contrastRatio(face, Qt::white) ? Qt::black : Qt::white;
			}
			copy.palette.setColor(group, QPalette::ButtonText, ink);
			painter->save();
			painter->fillRect(item->rect, face);
			painter->setFont(item->font);
			QCommonStyle::drawControl(element, &copy, painter, widget);
			painter->restore();
			return;
		}
	}
#endif
	if (element == CE_MenuItem)
	{
		const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
		if (item && !item->icon.isNull()
		    && (item->state & State_Selected) && (item->state & State_Enabled))
		{
			QStyleOptionMenuItem copy = *item;
			const int size = pixelMetric(PM_SmallIconSize, option, widget);
			const qreal dpr = widget ? widget->devicePixelRatio()
			                         : QApplication::instance() ? qApp->devicePixelRatio() : 1.0;
			copy.icon = menuIcon(item->icon, item->palette, size, dpr);
			QProxyStyle::drawControl(element, &copy, painter, widget);
			return;
		}
	}
	QProxyStyle::drawControl(element, option, painter, widget);
}

void QETStyle::drawPrimitive(PrimitiveElement element,
                             const QStyleOption *option,
                             QPainter *painter,
                             const QWidget *widget) const
{
#ifdef Q_OS_WIN
	if (menu_style_ && (element == PE_PanelMenu || element == PE_FrameMenu)) {
		QStyleOption copy = *option;
		copy.palette = popupPalette(option->palette);
		if (element == PE_PanelMenu)
			painter->fillRect(copy.rect, copy.palette.brush(QPalette::Window));
		menu_style_->drawPrimitive(PE_FrameMenu, &copy, painter, widget);
		return;
	}
	if (element == PE_PanelMenuBar) {
		painter->fillRect(option->rect, option->palette.brush(QPalette::Window));
		return;
	}
#endif
	QProxyStyle::drawPrimitive(element, option, painter, widget);
}

QSize QETStyle::sizeFromContents(ContentsType type, const QStyleOption *option,
                                 const QSize &size, const QWidget *widget) const
{
#ifdef Q_OS_WIN
	if (menu_style_ && type == CT_MenuItem)
		return menu_style_->sizeFromContents(type, option, size, widget);
#endif
	return QProxyStyle::sizeFromContents(type, option, size, widget);
}

int QETStyle::pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const
{
#ifdef Q_OS_WIN
	if (menu_style_ && qobject_cast<const QMenu *>(widget))
		return menu_style_->pixelMetric(metric, option, widget);
#endif
	return QProxyStyle::pixelMetric(metric, option, widget);
}

int QETStyle::styleHint(StyleHint hint, const QStyleOption *option,
                        const QWidget *widget, QStyleHintReturn *result) const
{
#ifdef Q_OS_WIN
	if (menu_style_ && qobject_cast<const QMenu *>(widget))
		return menu_style_->styleHint(hint, option, widget, result);
#endif
	return QProxyStyle::styleHint(hint, option, widget, result);
}

/**
	@brief QETStyle::generatedIconPixmap
	QIcon::Active: a line-art icon comes back tinted in hoverColor(),
	alpha kept, so the hovered icon differs from the normal one on both
	light and dark palettes. Colored icons and every
	other mode go to the base style.
*/
QPixmap QETStyle::generatedIconPixmap(QIcon::Mode mode,
                                      const QPixmap &pixmap,
                                      const QStyleOption *option) const
{
	if (mode != QIcon::Active || pixmap.isNull())
		return QProxyStyle::generatedIconPixmap(mode, pixmap, option);

	if (!isLineArt(pixmap.toImage()))
		return QProxyStyle::generatedIconPixmap(mode, pixmap, option);

	const QPalette palette = option ? option->palette : QApplication::palette();
	return tinted(pixmap, hoverColor(palette));
}
