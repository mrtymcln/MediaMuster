#pragma once

// Holds one database's exact bytes whilst its reader works. This temporary
// buffer does not interpret the format or outlive source preparation.

#include "mediaengine/scanmodel.h"
#include <QByteArray>
#include <QIODevice>
#include <QStringList>

namespace MediaEngine
{
	class DatabaseImage
	{
	public:
		// The caller owns an already-open, readable, seekable binary device. Capture
		// starts at zero and leaves it open. Expected input failures return an outcome;
		// allocation failures propagate. A failed capture retains only acquired bytes.
		static DatabaseImage read(QIODevice &source, const MediaEngine::Cancellation &cancellation);

		const QByteArray &bytes() const { return m_bytes; }
		qint64 expectedSize() const { return m_expectedSize; }
		// This is the acquisition outcome, independent of format interpretation.
		MediaEngine::ParsedSource::Outcome outcome() const { return m_outcome; }
		const QStringList &diagnostics() const { return m_diagnostics; }
		bool acquisitionComplete() const { return m_outcome == MediaEngine::ParsedSource::Outcome::Complete; }

	private:
		DatabaseImage() = default;
		QByteArray m_bytes;
		qint64 m_expectedSize = -1; // No valid extent was obtained.
		MediaEngine::ParsedSource::Outcome m_outcome = MediaEngine::ParsedSource::Outcome::NotRead;
		QStringList m_diagnostics;
	};
}
