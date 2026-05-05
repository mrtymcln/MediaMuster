#pragma once

#include <QByteArray>
#include <QString>

// Extracts technical metadata from MXF file headers via direct KLV parsing.
// Only the header partition is read — essence data is never touched.
// Reads are capped at 512 KB so partial or corrupt files don't hang the scanner.

struct MxfMetadata
{
	QString codec;
	QString resolution;
	QString fps;
	QString bitDepth;
	QString umid;
	QString clipName;
	QByteArray essenceContainerLabel;
	int width = 0;
	int height = 0;
	int channels = 0;
	int sampleRate = 0;
	int frameLayout = -1; // 0=Full Frame, 1=Separate Fields, 2=Single Field, 3=Mixed Fields
	qint64 durationFrames = 0;
	bool isAudio = false;
	bool valid = false;
};

class MxfParser
{
public:
	[[nodiscard]] static MxfMetadata parseHeader(const QString &filePath);
	[[nodiscard]] static QString codecFromEssenceLabel(const QByteArray &label, const QString &fps);
	[[nodiscard]] static QString formatUmid(const QByteArray &raw);

private:
	[[nodiscard]] static qint64 readBerLength(const QByteArray &data, qint64 offset, int &bytesUsed);
	[[nodiscard]] static quint16 readUint16BE(const QByteArray &data, qint64 offset);
	[[nodiscard]] static quint32 readUint32BE(const QByteArray &data, qint64 offset);

	static void parseDescriptorSet(const QByteArray &data, qint64 startPos, qint64 length, MxfMetadata &out);
	static void parseMaterialPackage(const QByteArray &data, qint64 startPos, qint64 length, MxfMetadata &out);
	static void parseStructuralComponent(const QByteArray &data, qint64 startPos, qint64 length, MxfMetadata &out);
};
