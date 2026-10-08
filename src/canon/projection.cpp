#include "projection.h"
#include "metadataselectionpolicy.h"
#include "avidtext.h"
#include "avideffects.h"
#include <algorithm>
#include "mobid.h"
#include "omfuid.h"
#include <QtEndian>
#include <QStringDecoder>

namespace Canon
{
	RawProperty withInferredText(const RawProperty &property, qsizetype prefixBytes, bool allowMacRoman)
	{
		RawProperty result = property;
		if (property.state != PropertyReadState::Present || property.decoded.isValid() ||
			property.textEncoding != TextEncoding::Unknown || prefixBytes < 0 || prefixBytes > property.encoding.size())
			return result;
		QByteArrayView bytes(property.encoding);
		bytes = bytes.sliced(prefixBytes);
		const auto end = std::find(bytes.cbegin(), bytes.cend(), '\0');
		bytes = bytes.first(end - bytes.cbegin());
		QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
		const QString decoded = decoder.decode(bytes);
		if (!decoder.hasError())
		{
			result.decoded = decoded;
			result.textEncoding = TextEncoding::Utf8;
			result.textEncodingBasis = EvidenceBasis::Derived;
			result.interpretation = QStringLiteral("UTF-8 inferred from valid byte encoding under the approved display policy; the source does not declare it. Original bytes retained.");
		}
		else if (allowMacRoman)
		{
			QString legacy;
			legacy.reserve(bytes.size());
			for (const unsigned char byte : bytes)
				legacy.append(QChar(byte < 128 ? char16_t(byte) : AvidText::kMacRomanHigh[byte - 128]));
			result.decoded = legacy;
			result.textEncoding = TextEncoding::MacRoman;
			result.textEncodingBasis = EvidenceBasis::Derived;
			result.interpretation = QStringLiteral("MacRoman inferred for unlabelled legacy text after UTF-8 validation failed, under the approved display policy. Original bytes retained.");
		}
		return result;
	}

	QString canonicalMxfId(const QByteArray &bytes)
	{
		if (bytes.size() != MobId::kRawSize)
			return {};
		// MXF serializes the material's numeric fields in big-endian order,
		// including AAF SDK prefix-42 IDs. That family marker also appears in
		// OMF/database IDs; it does not change the encoding of an MXF property.
		QByteArray canonical = bytes;
		auto *data = reinterpret_cast<unsigned char *>(canonical.data());
		MobId::swapMaterialByteOrder(data, data);
		return MobId::format(canonical);
	}

	QString canonicalDatabaseId(const QByteArray &bytes, bool bigEndian)
	{
		QByteArray canonical = bytes;
		if (bigEndian && (bytes.size() == 8 || bytes.size() == 12))
			for (qsizetype offset = 0; offset < bytes.size(); offset += 4)
				qToLittleEndian(qFromBigEndian<quint32>(bytes.constData() + offset), canonical.data() + offset);
		if (canonical.size() == 8)
			return OmfUid::toMobIdText(reinterpret_cast<const unsigned char *>(canonical.constData()));
		return OmfUid::toIdText(canonical);
	}

	MetadataObservation observation(const ParsedSource &source, const AvidObject &object,
									const RawProperty &property, const QVariant &value, EvidenceBasis basis, const QString &explanation)
	{
		MetadataObservation result;
		result.snapshot = source.snapshot;
		result.property = property.locator.name;
		result.objectIdentity = QStringLiteral("object:%1").arg(object.handle);
		result.value = value;
		result.rawValue = property.encoding;
		result.readState = property.state;
		if (result.readState == PropertyReadState::Present && !value.isValid())
			result.readState = PropertyReadState::Unreadable;
		result.basis = basis;
		result.explanation = explanation.isEmpty() ? property.interpretation : explanation;
		result.textEncoding = property.textEncoding;
		result.textEncodingBasis = property.textEncodingBasis;
		if (property.textEncoding.has_value() && property.textEncodingBasis == EvidenceBasis::Derived)
		{
			result.basis = EvidenceBasis::Derived;
			if (!result.explanation.contains(property.interpretation))
				result.explanation += QLatin1Char(' ') + property.interpretation;
		}
		return result;
	}

	void observe(ProjectedFile &file, MediaProperty field, const ParsedSource &source,
				 const AvidObject &object, const RawProperty &property, const QVariant &value,
				 EvidenceBasis basis, const QString &explanation)
	{
		file.evidence.observe(field, observation(source, object, property, value, basis, explanation));
	}

	void recordPropertyCoverage(ProjectedFile &file, MediaProperty field, const ParsedSource &source,
								const AvidObject &object, std::initializer_list<const char *> names,
								bool completeObject)
	{
		PropertyReadResult status;
		status.reason = PropertyReadReason::CoverageNotEstablished;
		bool found = false;
		for (const auto &property : object.properties)
			if (std::any_of(names.begin(), names.end(), [&](const char *name)
							{ return property.locator.name == QLatin1String(name); }))
			{
				found = true;
				if (property.state == PropertyReadState::Unreadable)
				{
					status.state = PropertyReadState::Unreadable;
					status.reason = PropertyReadReason::ValueUnreadable;
				}
				else if (status.state != PropertyReadState::Unreadable)
					status.reason = PropertyReadReason::UnsupportedInterpretation;
			}
		if (!found && completeObject)
		{
			status.state = PropertyReadState::Absent;
			status.reason = PropertyReadReason::NotPresentInObject;
			status.explanation = QStringLiteral("The complete owning object has none of the checked input properties");
		}
		else if (found)
			status.explanation = QStringLiteral("Recognized input properties are retained; the field has no usable interpretation unless an observation establishes it");
		else
			status.explanation = QStringLiteral("Owning object coverage is incomplete; missing inputs cannot establish absence");
		QStringList checked;
		for (const char *name : names)
			checked.append(QString::fromLatin1(name));
		status.explanation += QStringLiteral(". Checked properties: %1").arg(checked.join(QStringLiteral(", ")));
		file.evidence.recordReadStatus(field, source.snapshot, QStringLiteral("object:%1").arg(object.handle), std::move(status));
	}

	QVariantMap rateValue(MediaRate rate)
	{
		return {{QStringLiteral("Numerator"), rate.numerator}, {QStringLiteral("Denominator"), rate.denominator}};
	}
	MediaRate mediaRate(const QVariant &value)
	{
		const auto map = value.toMap();
		return {map.value(QStringLiteral("Numerator")).toInt(), map.value(QStringLiteral("Denominator")).toInt()};
	}
	QVariantMap durationValue(const MediaDuration &duration)
	{
		return {{QStringLiteral("Units"), duration.units}, {QStringLiteral("Rate"), rateValue(duration.rate)}, {QStringLiteral("DisplayRate"), rateValue(duration.displayRate)}, {QStringLiteral("Source"), int(duration.source)}};
	}
	MediaDuration mediaDuration(const QVariant &value)
	{
		const auto map = value.toMap();
		return {map.value(QStringLiteral("Units")).toLongLong(), mediaRate(map.value(QStringLiteral("Rate"))),
				mediaRate(map.value(QStringLiteral("DisplayRate"))), MediaDuration::Source(map.value(QStringLiteral("Source")).toInt())};
	}
	void selectEffectMetadata(MediaEvidence &evidence)
	{
		const auto fields = {MediaProperty::Effect, MediaProperty::EffectCategory, MediaProperty::EffectSequence};
		const QString locator = QStringLiteral("Canon.EffectFromClipName");
		for (const auto field : fields)
			evidence.excludeInterpretation(field, locator);
		const auto name = evidence.selected(MediaProperty::ClipName);
		const auto type = evidence.selected(MediaProperty::Type);
		const auto &names = evidence.observations(MediaProperty::ClipName);
		if (type.value.isValid() && type.value.toInt() == int(MediaType::Precompute) &&
			name.selectedObservation >= 0 && name.selectedObservation < names.size())
		{
			const auto hit = AvidEffects::lookup(name.value.toString());
			for (const auto field : fields)
			{
				auto item = names[name.selectedObservation];
				item.property = locator;
				item.value = field == MediaProperty::Effect ? hit.name : field == MediaProperty::EffectCategory ? hit.category
																												: hit.sequence;
				item.basis = EvidenceBasis::Derived;
				item.explanation = QStringLiteral("Inferred from the selected precompute clip name using the Avid effect-name catalogue; editable names do not prove effect identity. Source property: %1").arg(names[name.selectedObservation].property);
				evidence.observe(field, std::move(item));
			}
		}
		for (const auto field : fields)
			evidence.select(field, resolveProperty(evidence, propertyPolicy(field)));
	}
	void appendEvidence(MediaEvidence &target, const MediaEvidence &source, bool eligible)
	{
		for (const auto &coverage : source.sourceCoverage())
			target.appendCoverage(coverage, eligible);
		for (int index = int(MediaProperty::ClipName); index < int(MediaProperty::Count); ++index)
			for (auto value : source.observations(MediaProperty(index)))
			{
				value.eligible = value.eligible && eligible;
				target.observe(MediaProperty(index), std::move(value));
			}
	}
}
