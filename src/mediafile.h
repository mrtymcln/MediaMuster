#pragma once

#include <QString>
#include <QDateTime>
#include <QVector>
#include <QVariant>
#include <QMetaType>
#include <cmath>

// One entry in the Avid media database.

struct MediaFile
{
	// Identity: filePath is primary; mobId/compositionMobId are secondary.
	QString mobId;			  // Avid MOB ID from MDB/PMR
	QString compositionMobId; // master-clip MOB; V01/A01/A02 share this
	QString umid;			  // Unique Material Identifier from the MXF header

	// Database metadata (.mdb / .pmr).
	QString clipName;
	QString project;	 // "UNMANAGED_FILES" if not in any project DB
	QString originalBin; // MDB _ORG_BIN — frozen at import; live value lives in interplayCurrentBin

	// Technical metadata from the MXF header or MDB.
	QString codec;		// "DNxHD 145", "PCM Audio"
	QString codecHex;	// raw hex of the MXF essence container label
	QString resolution; // "1920x1080" (dash for audio)
	QString fps;		// "23.976" (or "48000 Hz" for audio)
	QString bitDepth;	// "10-bit", "24-bit"
	int sampleRate = 0; // audio only
	int channels = 0;	// audio only
	qint64 durationFrames = 0;
	QString startTimecode; // from MDB _COLUMN_START if present

	// AMA-link metadata. When isImported is true, sourceFilePath is what
	// Avid recorded at import (typically the QuickTime/camera original).
	QString sourceFilePath;
	QString sourceFileName;
	QString sourceContainer; // "QTFF", "MXF", etc.
	bool isImported = false;

	QString kind;		   // "Media", "Precompute"
	QString bitRateString; // "120", "36", "48 kHz"

	// Filesystem.
	QString filePath;
	QString fileName;
	QString extension;
	QString driveName;
	QString drivePath;
	QString mxfFolder; // numbered folder ("1", "2", ...)

	// Pre-baked "DATA / 2" — rebuilding per paint was measurable on 290k rows.
	QString volumeDisplay;

	qint64 sizeBytes = 0;
	double sizeMB = 0.0;
	QDateTime modified;
	QDateTime created;

	// Classification
	enum class Type
	{
		Video,
		Audio
	};
	Type mediaType = Type::Video;

	// Flags.
	bool isUnmanaged = false;	 // not in any .mdb
	bool isBadUmid = false;		 // UMID null/zeroed
	bool isNonPortable = false;	 // filename has non-portable chars
	bool isUnreferenced = false; // in MDB but not in any project PMR
	bool isDSStore = false;		 // .DS_Store / Thumbs.db rubbish

	// CTMS/Interplay stubs — carried through scanner→model→export so a later
	// schema bump isn't needed. Empty in standalone mode.
	QString interplayAssetId;	   // CTMS base.id GUID
	QString interplayCurrentBin;   // CTMS loc:locations — live bin, not frozen
	bool interplayRegistered = false;
	QDateTime interplayLastSeen;

	// Convenience
	QString mediaTypeString() const
	{
		return mediaType == Type::Audio ? "audio" : "video";
	}

	// HH:MM:SS:FF; empty if fps isn't enough info (uses sampleRate for audio).
	QString durationDisplay() const
	{
		if (durationFrames <= 0)
			return {};
		double rate = fps.toDouble();
		if (rate <= 0)
		{
			// Audio fallback: durationFrames is sample count.
			if (mediaType == Type::Audio && sampleRate > 0)
			{
				qint64 totalSecs = durationFrames / sampleRate;
				return QString("%1:%2:%3")
					.arg(totalSecs / 3600, 2, 10, QChar('0'))
					.arg((totalSecs / 60) % 60, 2, 10, QChar('0'))
					.arg(totalSecs % 60, 2, 10, QChar('0'));
			}
			return {};
		}
		int nominalRate = static_cast<int>(std::round(rate));
		qint64 totalFrames = durationFrames;
		int frames = static_cast<int>(totalFrames % nominalRate);
		qint64 secs = totalFrames / nominalRate;
		int h = static_cast<int>(secs / 3600);
		int m = static_cast<int>((secs / 60) % 60);
		int s = static_cast<int>(secs % 60);
		return QString("%1:%2:%3:%4")
			.arg(h, 2, 10, QChar('0'))
			.arg(m, 2, 10, QChar('0'))
			.arg(s, 2, 10, QChar('0'))
			.arg(frames, 2, 10, QChar('0'));
	}

	QString sizeDisplay() const
	{
		constexpr qint64 KB = 1024;
		constexpr qint64 MB = KB * 1024;
		constexpr qint64 GB = MB * 1024;
		if (sizeBytes >= GB)
			return QString::number(sizeBytes / double(GB), 'f', 2) + " GB";
		if (sizeBytes >= MB)
			return QString::number(sizeBytes / double(MB), 'f', 1) + " MB";
		if (sizeBytes >= KB)
			return QString::number(sizeBytes / double(KB), 'f', 1) + " KB";
		return QString::number(sizeBytes) + " B";
	}
};

// A mounted volume that may contain Avid media.
struct DriveInfo
{
	QString name;
	QString path; // Mount point
	qint64 totalBytes = 0;
	qint64 usedBytes = 0;
	qint64 freeBytes = 0;
	QString driveType; // "NEXIS", "ISIS", "SAN", "Local", "USB", "Network"
	bool hasAvidMedia = false;
	bool isMounted = true;

	double usedPercent() const
	{
		if (totalBytes == 0)
			return 0;
		return 100.0 * usedBytes / totalBytes;
	}

	QString totalDisplay() const { return formatTbOrGb(totalBytes); }
	QString usedDisplay() const { return formatTbOrGb(usedBytes); }

private:
	static QString formatTbOrGb(qint64 bytes)
	{
		constexpr qint64 GB = qint64(1024) * 1024 * 1024;
		constexpr qint64 TB = GB * 1024;
		if (bytes >= TB)
			return QString::number(bytes / double(TB), 'f', 1) + " TB";
		return QString::number(bytes / double(GB), 'f', 1) + " GB";
	}
};

// Aggregate stats per project.
struct ProjectSummary
{
	QString name;
	int videoCount = 0;
	int audioCount = 0;
	qint64 totalBytes = 0;
	QVector<QString> bins;
	QVector<QString> drives;
	QVector<QString> codecs;
};

// Register custom types for cross-thread signal/slot delivery.
Q_DECLARE_METATYPE(MediaFile)
Q_DECLARE_METATYPE(QVector<MediaFile>)
Q_DECLARE_METATYPE(DriveInfo)
Q_DECLARE_METATYPE(QVector<DriveInfo>)
