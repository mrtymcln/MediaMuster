#pragma once

#include <QString>
#include <QDateTime>
#include <QVector>
#include <QVariant>
#include <QMetaType>
#include <cmath>

// Plain data types shared across the app. No logic beyond a
// couple of derived-display helpers; everything else operates
// on these as inputs.

// MARK: - MediaFile

/// One row of the main table. `filePath` is the primary identity;
/// `mobId` and `compositionMobId` are secondary identities used by the
/// bin filter to match against .avb references. Both can legitimately
/// be empty for unmanaged files.
struct MediaFile
{
	// MARK: Identity

	QString mobId;            ///< Avid MOB ID for this essence file (from MDB/PMR).
	QString compositionMobId; ///< Master clip MOB; V01/A01/A02 relatives share this.
	QString umid;             ///< SMPTE UMID embedded in the MXF header.

	// MARK: MDB and PMR metadata

	QString clipName;
	QString project;     ///< "UNMANAGED_FILES" when the file isn't in any database.
	QString originalBin; ///< MDB _ORG_BIN — frozen at import time.

	// MARK: MXF or MDB technical metadata

	QString codec;      ///< "DNxHD 145", "PCM Audio", etc.
	QString codecHex;   ///< Raw hex of the MXF essence container label.
	QString resolution; ///< "1920x1080" — em-dash for audio.
	QString fps;        ///< "23.976", "25", "48000 Hz" for audio sample rate.
	QString bitDepth;   ///< "10-bit", "24-bit".
	int sampleRate = 0; ///< Audio only.
	int channels = 0;   ///< Audio only.
	qint64 durationFrames = 0;
	QString startTimecode; ///< From MDB _COLUMN_START if present.
	QString sourceFilePath; ///< Path Avid recorded when the media was first imported.
	QString sourceFileName;
	QString sourceContainer; ///< "QTFF", "MXF", "MOV", etc.
	bool isImported = false;

	QString kind;          ///< "Media" or "Precompute".
	QString bitRateString; ///< "120", "36", "48 kHz" computed in the scanner.

	// MARK: Filesystem

	QString filePath;
	QString fileName;
	QString extension;
	QString volumeName;
	QString volumePath;
	QString mxfFolder;     ///< The numbered subfolder under Avid MediaFiles/MXF.
	QString volumeDisplay; ///< "VolumeName/relative/path" for the Volume column.
	qint64 sizeBytes = 0;
	double sizeMB = 0.0;
	QDateTime modified;
	QDateTime created;

	// MARK: Classification

	enum class Type : int
	{
		Video,
		Audio
	};
	Type mediaType = Type::Video;

	bool isUnmanaged = false;    ///< Not in any .mdb.
	bool isBadUmid = false;      ///< UMID null or zeroed — Avid never wrote a real Id.
	bool isNonPortable = false;  ///< Filename has chars Avid can't round-trip safely.
	bool isUnreferenced = false; ///< In the MDB but not referenced by any project's PMR.
	bool isDSStore = false;      ///< .DS_Store / Thumbs.db noise.

	// MARK: Derived display

	/// HH:MM:SS:FF for video. For audio (no fps), treats `durationFrames`
	/// as a sample count and formats HH:MM:SS using `sampleRate`. Empty
	/// when there isn't enough info to format.
	QString durationDisplay() const
	{
		if (durationFrames <= 0)
			return {};
		double rate = fps.toDouble();
		if (rate <= 0)
		{
			// Audio fallback: durationFrames is a sample count, not frames.
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
};

// MARK: - VolumeInfo

/// One mounted volume the user might want to scan. Built by
/// VolumeManager from QStorageInfo plus Avid-aware heuristics.
struct VolumeInfo
{
	QString name;
	QString path;
	qint64 totalBytes = 0;
	qint64 usedBytes = 0;
	qint64 freeBytes = 0;
	QString volumeType;        ///< "Local", "Network", "Nexis", "System", "Removable".
	bool hasAvidMedia = false; ///< True if `<path>/Avid MediaFiles` exists.
	bool isMounted = true;

	double usedPercent() const
	{
		if (totalBytes == 0)
			return 0;
		return 100.0 * usedBytes / totalBytes;
	}
};

// MARK: - ProjectSummary

struct ProjectSummary
{
	QString name;
	int videoCount = 0;
	int audioCount = 0;
	qint64 totalBytes = 0;
	QVector<QString> bins;
	QVector<QString> volumes;
};

// MARK: - Qt metatype registration

Q_DECLARE_METATYPE(MediaFile)
Q_DECLARE_METATYPE(VolumeInfo)