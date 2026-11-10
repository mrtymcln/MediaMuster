#pragma once

#include "omf_parser.h"

#include <QByteArrayView>
#include <QFile>
#include <QStringList>
#include <atomic>
#include <optional>

namespace Bella::Detail
{
	// MARK: - Native audio results

	struct EmbeddedOmfLocation
	{
		qint64 payloadOffset = 0;
		qint64 payloadLength = 0;
		std::optional<qint64> labelOffset{};
	};

	struct NativeAudioRead
	{
		OmfReadResult reading = OmfReadResult::NotRead;
		OmfNativeAudioInformation information;
		QStringList problemNotes;
		std::optional<EmbeddedOmfLocation> omfLocation{};
		bool multipleOmfLocations = false;
		bool locationDiscoveryComplete = false;
		std::optional<qint64> declaredContainerEnd{};
	};

	// MARK: - Native audio reading

	// The caller owns this already-open binary file and the initial signature read.
	// No source checks or OMF interpretation occur here. Sound samples stay on disk.
	[[nodiscard]] NativeAudioRead readNativeAudio(QFile &file, QByteArrayView first12Bytes,
												  const std::atomic_bool &stopRequested);
}
