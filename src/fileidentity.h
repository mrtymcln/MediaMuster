#pragma once
#include <QJsonObject>
#include <QString>

// Persistent identity of a mounted volume. Labels, capacity and mount paths
// describe a volume but do not authorize recovery. Network volume identities
// remain unqualified until their client/storage configuration is validated.
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
	Confidence confidence = Confidence::Low;
	static VolumeIdentity capture(const QString &path);
	bool matches(const VolumeIdentity &other) const;
	QJsonObject toJson() const;
	static VolumeIdentity fromJson(const QJsonObject &value);
};
