#pragma once
#include <QJsonObject>
#include <QString>

// Persistent identity of a mounted volume. Labels, capacity and mount paths
// describe a volume but do not authorize recovery. Network matches require an
// OS-reported server/share endpoint and the root directory's object identity;
// the operation journal additionally checks its recorded file identities.
struct VolumeIdentity
{
	enum class Confidence
	{
		Low = 0,
		Med = 1,
		High = 2
	};
	QString uuid;
	quint32 serial = 0;
	QString label;
	QString fsType;
	qint64 capacityBytes = 0;
	QString rootPath;
	QString kind = QStringLiteral("local");
	QString networkId;
	QString rootObjectId;
	Confidence confidence = Confidence::Low;
	static VolumeIdentity capture(const QString &path);
	bool matches(const VolumeIdentity &other) const;
	QJsonObject toJson() const;
	static VolumeIdentity fromJson(const QJsonObject &value);
};
