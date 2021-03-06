/***********************************************************************
 *
 * Copyright (C) 2011, 2012, 2013 Graeme Gott <graeme@gottcode.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ***********************************************************************/

#include "odt_reader.h"

#include "zip_reader.h"

#include <QTextDocument>

//-----------------------------------------------------------------------------

OdtReader::OdtReader() :
	m_in_block(true)
{
	m_encoding = "UTF-8";
	m_xml.setNamespaceProcessing(false);
}

//-----------------------------------------------------------------------------

bool OdtReader::canRead(QIODevice* device)
{
	return ZipReader::canRead(device) &&
			(device->peek(77).right(47) == "mimetypeapplication/vnd.oasis.opendocument.text");
}

//-----------------------------------------------------------------------------

void OdtReader::readData(QIODevice* device)
{
	m_in_block = m_cursor.document()->blockCount();
	m_block_format = m_cursor.blockFormat();

	// Open archive
	ZipReader zip(device);

	// Read archive
	if (zip.isReadable()) {
		const QString files[] = { QString::fromLatin1("styles.xml"), QString::fromLatin1("content.xml") };
		for (int i = 0; i < 2; ++i) {
			QByteArray data = zip.fileData(files[i]);
			if (data.isEmpty()) {
				continue;
			}
			m_xml.addData(data);
			readDocument();
			if (m_xml.hasError()) {
				m_error = m_xml.errorString();
				break;
			}
			m_xml.clear();
		}
	} else {
		m_error = tr("Unable to open archive.");
	}

	// Close archive
	zip.close();

	QCoreApplication::processEvents();
}

//-----------------------------------------------------------------------------

void OdtReader::readDocument()
{
	m_xml.readNextStartElement();
	while (m_xml.readNextStartElement()) {
		if (m_xml.qualifiedName() == "office:styles") {
			readStylesGroup();
		} else if (m_xml.qualifiedName() == "office:automatic-styles") {
			readStylesGroup();
		} else if (m_xml.qualifiedName() == "office:body") {
			readBody();
		}  else {
			m_xml.skipCurrentElement();
		}
	}
}

//-----------------------------------------------------------------------------

void OdtReader::readStylesGroup()
{
	while (m_xml.readNextStartElement()) {
		if (m_xml.qualifiedName() == "style:style") {
			readStyle();
		} else {
			m_xml.skipCurrentElement();
		}
	}
}

//-----------------------------------------------------------------------------

void OdtReader::readStyle()
{
	QXmlStreamAttributes attributes = m_xml.attributes();

	QString name = attributes.value(QLatin1String("style:name")).toString();

	int type = -1;
	QStringRef family = attributes.value(QLatin1String("style:family"));
	if (family == "paragraph") {
		type = 0;
	} else if (family == "text") {
		type = 1;
	} else {
		m_xml.skipCurrentElement();
		return;
	}

	if (!m_styles[type].contains(name)) {
		m_styles[type][name] = Style(m_block_format);
	}
	Style& style = m_styles[type][name];

	QString parent_style = attributes.value(QLatin1String("style:parent-style-name")).toString();
	if (!parent_style.isEmpty()) {
		if (m_styles[0].contains(parent_style)) {
			style = m_styles[0][parent_style];
		} else if (m_styles[1].contains(parent_style)) {
			style = m_styles[1][parent_style];
		}
	}

	while (m_xml.readNextStartElement()) {
		if (m_xml.qualifiedName() == "style:paragraph-properties") {
			readStyleParagraphProperties(style.block_format);
		} else if (m_xml.qualifiedName() == "style:text-properties") {
			readStyleTextProperties(style.char_format);
		} else {
			m_xml.skipCurrentElement();
		}
	}
}

//-----------------------------------------------------------------------------

void OdtReader::readStyleParagraphProperties(QTextBlockFormat& format)
{
	QXmlStreamAttributes attributes = m_xml.attributes();

	QStringRef align = attributes.value(QLatin1String("fo:text-align"));
	if (align == "start") {
		format.setAlignment(Qt::AlignLeading);
	} else if (align == "end") {
		format.setAlignment(Qt::AlignTrailing);
	} else if (align == "left") {
		format.setAlignment(Qt::AlignLeft | Qt::AlignAbsolute);
	} else if (align == "right") {
		format.setAlignment(Qt::AlignRight | Qt::AlignAbsolute);
	} else if (align == "center") {
		format.setAlignment(Qt::AlignHCenter);
	} else if (align == "justify") {
		format.setAlignment(Qt::AlignJustify);
	}

	QStringRef direction = attributes.value(QLatin1String("style:writing-mode"));
	if (direction == "rl-tb" || direction == "rl") {
		format.setLayoutDirection(Qt::RightToLeft);
	} else if (direction == "lr-tb" || direction == "lr") {
		format.setLayoutDirection(Qt::LeftToRight);
	}

	if (attributes.hasAttribute(QLatin1String("fo:margin-left"))) {
		QString margin = attributes.value(QLatin1String("fo:margin-left")).toString();
		QString type = margin.right(2);
		margin.chop(2);

		// Internal indent units are 0.5in
		int indent = 0;
		if (type == QLatin1String("in")) {
			indent = qRound(margin.toDouble() * 2.0);
		} else if (type == QLatin1String("cm")) {
			indent = qRound(margin.toDouble() / 1.27);
		} else if (type == QLatin1String("mm")) {
			indent = qRound(margin.toDouble() / 12.7);
		} else if (type == QLatin1String("pt")) {
			// 72pt to inch
			indent = qRound(margin.toDouble() / 36.0);
		} else if (type == QLatin1String("pc")) {
			// 6pc to inch
			indent = qRound(margin.toDouble() / 3.0);
		} else if (type == QLatin1String("px")) {
			// 96px to inch
			indent = qRound(margin.toDouble() / 48.0);
		}
		format.setIndent(qMax(0, indent));
	}

	m_xml.skipCurrentElement();
}

//-----------------------------------------------------------------------------

void OdtReader::readStyleTextProperties(QTextCharFormat& format)
{
	QXmlStreamAttributes attributes = m_xml.attributes();

	if (attributes.value(QLatin1String("fo:font-weight")) == "bold") {
		format.setFontWeight(QFont::Bold);
	}
	if (attributes.value(QLatin1String("fo:font-style")) == "italic") {
		format.setFontItalic(true);
	}
	if (attributes.hasAttribute(QLatin1String("style:text-underline-style")) &&
			attributes.value(QLatin1String("style:text-underline-style")) != "none") {
		format.setFontUnderline(true);
	}
	if (attributes.hasAttribute((QLatin1String("style:text-underline-type"))) &&
			attributes.value(QLatin1String("style:text-underline-type")) != "none") {
		format.setFontUnderline(true);
	}
	if (attributes.hasAttribute((QLatin1String("style:text-line-through-type"))) &&
			attributes.value(QLatin1String("style:text-line-through-type")) != "none") {
		format.setFontStrikeOut(true);
	}

	if (attributes.hasAttribute(QLatin1String("style:text-position"))) {
		QStringRef position = attributes.value((QLatin1String("style:text-position")));
		if (position == "super") {
			format.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
		} else if (position == "sub") {
			format.setVerticalAlignment(QTextCharFormat::AlignSubScript);
		} else {
			QString value = position.toString();
			value.chop(1);
			if (value.toInt() > 0) {
				format.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
			} else {
				format.setVerticalAlignment(QTextCharFormat::AlignSubScript);
			}
		}
	}

	m_xml.skipCurrentElement();
}

//-----------------------------------------------------------------------------

void OdtReader::readBody()
{
	m_cursor.beginEditBlock();
	while (m_xml.readNextStartElement()) {
		if (m_xml.qualifiedName() == "office:text") {
			readBodyText();
		} else {
			m_xml.skipCurrentElement();
		}
	}
	m_cursor.endEditBlock();
}

//-----------------------------------------------------------------------------

void OdtReader::readBodyText()
{
	while (m_xml.readNextStartElement()) {
		if (m_xml.qualifiedName() == "text:p" || m_xml.qualifiedName() == "text:h") {
			readParagraph();
		} else {
			m_xml.skipCurrentElement();
		}
	}
}

//-----------------------------------------------------------------------------

void OdtReader::readParagraph()
{
	QTextBlockFormat block_format;
	QTextCharFormat char_format;

	// Style paragraph
	QXmlStreamAttributes attributes = m_xml.attributes();
	if (attributes.hasAttribute(QLatin1String("text:style-name"))) {
		const Style& style = m_styles[0][attributes.value(QLatin1String("text:style-name")).toString()];
		block_format = style.block_format;
		char_format = style.char_format;
	}

	// Create paragraph
	if (!m_in_block) {
		m_cursor.insertBlock(block_format, char_format);
		m_in_block = true;
	} else {
		m_cursor.mergeBlockFormat(block_format);
		m_cursor.mergeBlockCharFormat(char_format);
	}

	// Read paragraph text
	readText();
	m_in_block = false;

	QCoreApplication::processEvents();
}

//-----------------------------------------------------------------------------

void OdtReader::readSpan()
{
	QXmlStreamAttributes attributes = m_xml.attributes();

	// Style text
	QTextCharFormat format = m_cursor.charFormat();
	if (attributes.hasAttribute(QLatin1String("text:style-name"))) {
		const Style& style = m_styles[1][attributes.value(QLatin1String("text:style-name")).toString()];
		m_cursor.mergeCharFormat(style.char_format);
	}

	if (attributes.hasAttribute(QLatin1String("text:class-names"))) {
		QStringList styles = attributes.value(QLatin1String("text:class-names")).toString().simplified().split(QLatin1Char(' '), QString::SkipEmptyParts);
		int count = styles.count();
		for (int i = 0; i < count; ++i) {
			const Style& style = m_styles[1][styles.at(i)];
			m_cursor.mergeCharFormat(style.char_format);
		}
	}

	// Read styled text
	readText();

	// Restore previous style
	m_cursor.setCharFormat(format);
}

//-----------------------------------------------------------------------------

void OdtReader::readText()
{
	int depth = 1;
	while (depth && (m_xml.readNext() != QXmlStreamReader::Invalid)) {
		if (m_xml.isCharacters()) {
			m_cursor.insertText(m_xml.text().toString());
		} else if (m_xml.isStartElement()) {
			++depth;
			if (m_xml.qualifiedName() == "text:span") {
				readSpan();
				--depth;
			} else if (m_xml.qualifiedName() == "text:s") {
				int spaces = m_xml.attributes().value(QLatin1String("text:c")).toString().toInt();
				m_cursor.insertText(QString(qMax(1, spaces), QLatin1Char(' ')));
			} else if (m_xml.qualifiedName() == "text:tab") {
				m_cursor.insertText(QLatin1String("\t"));
			} else if (m_xml.qualifiedName() == "text:line-break") {
				m_cursor.insertText(QChar(0x2028));
			}
		} else if (m_xml.isEndElement()) {
			--depth;
		}
	}
}

//-----------------------------------------------------------------------------
