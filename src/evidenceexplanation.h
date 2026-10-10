#pragma once

#include <QSharedData>
#include <QString>
#include <QtGlobal>
#include <utility>

// Evidence stores a small reason code instead of a QString wrapper for each
// repeated explanation. Compose the original wording when diagnostics need it.
// Reader-provided explanations remain intact in shared immutable text.
class EvidenceExplanation
{
public:
	enum class Reason : quint8
	{
		None,
		Custom,
		LayoutDoesNotStore,
		CheckedInputsAbsent,
		CheckedInputsUninterpreted,
		CheckedInputsIncomplete,
		PmrKnownField,
		PmrModificationMissing,
		PmrModificationUnreadable,
		PmrModificationUninterpreted,
		SoundDescriptor,
		EqualRankConflict,
		PreferredSource,
		ContradictoryMasterTiming,
		ExcludedSources,
		MasterAssociations,
		AvbAssociationConflict,
		DisplayClock
	};

	EvidenceExplanation() = default;
	explicit EvidenceExplanation(Reason reason) : m_reason(reason) {}
	EvidenceExplanation(QString text)
		: m_reason(text.isNull() ? Reason::None : Reason::Custom),
		  m_parameter(text.isNull() ? nullptr : new Parameter(std::move(text)))
	{
	}

	// The checked names retain their exact order and spelling. Only the repeated
	// sentence surrounding them is replaced by the typed reason.
	static EvidenceExplanation checkedInputs(Reason reason, QString names)
	{
		EvidenceExplanation result(reason);
		result.m_parameter = new Parameter(std::move(names));
		return result;
	}
	static EvidenceExplanation withDisplayClock(const EvidenceExplanation &selected,
												const EvidenceExplanation &clock)
	{
		if (selected.m_reason == Reason::PreferredSource && !clock.m_parameter &&
			clock.m_reason != Reason::DisplayClock)
		{
			EvidenceExplanation result(Reason::DisplayClock);
			result.m_clockReason = clock.m_reason;
			return result;
		}
		return selected.text() + clockIntroduction() + clock.text();
	}

	Reason reason() const noexcept { return m_reason; }
	bool isEmpty() const noexcept
	{
		return m_reason == Reason::None ||
			(m_reason == Reason::Custom && (!m_parameter || m_parameter->text.isEmpty()));
	}
	QString text() const
	{
		if (m_reason == Reason::Custom)
			return m_parameter ? m_parameter->text : QString{};
		if (m_reason == Reason::DisplayClock)
			return wording(Reason::PreferredSource) + clockIntroduction() + wording(m_clockReason);
		QString result = wording(m_reason);
		if (m_reason == Reason::CheckedInputsAbsent || m_reason == Reason::CheckedInputsUninterpreted ||
			m_reason == Reason::CheckedInputsIncomplete)
			result += QStringLiteral(". Checked properties: %1").arg(m_parameter ? m_parameter->text : QString{});
		return result;
	}

private:
	struct Parameter : QSharedData
	{
		explicit Parameter(QString value) : text(std::move(value)) {}
		QString text;
	};
	static QString clockIntroduction()
	{
		return QStringLiteral("; display clock resolved independently from matching duration observations: ");
	}
	static QString wording(Reason reason)
	{
		switch (reason)
		{
		case Reason::LayoutDoesNotStore:
			return QStringLiteral("This source record layout does not store this field");
		case Reason::CheckedInputsAbsent:
			return QStringLiteral("The complete owning object has none of the checked input properties");
		case Reason::CheckedInputsUninterpreted:
			return QStringLiteral("Recognized input properties are retained; the field has no usable interpretation unless an observation establishes it");
		case Reason::CheckedInputsIncomplete:
			return QStringLiteral("Owning object coverage is incomplete; missing inputs cannot establish absence");
		case Reason::PmrKnownField:
			return QStringLiteral("Known PMR record field; its observation records the read outcome");
		case Reason::PmrModificationMissing:
			return QStringLiteral("The PMR record did not reach its modification word");
		case Reason::PmrModificationUnreadable:
			return QStringLiteral("The PMR modification word could not be read");
		case Reason::PmrModificationUninterpreted:
			return QStringLiteral("PMR ModificationWord is retained without a proven timestamp interpretation");
		case Reason::SoundDescriptor:
			return QStringLiteral("This sound descriptor does not describe a picture raster");
		case Reason::EqualRankConflict:
			return QStringLiteral("Equally eligible sources disagree; no defensible winner");
		case Reason::PreferredSource:
			return QStringLiteral("Selected by the field-specific source priority; alternatives retained");
		case Reason::ContradictoryMasterTiming:
			return QStringLiteral("Selected source records contradictory timing for the same master track; alternatives retained");
		case Reason::ExcludedSources:
			return QStringLiteral("Read values retained; the policy excludes their sources from selection");
		case Reason::MasterAssociations:
			return QStringLiteral("Every eligible master association retained; multiple masters do not merge physical files");
		case Reason::AvbAssociationConflict:
			return QStringLiteral("Matching AVB master or original-bin associations disagree; no value selected.");
		case Reason::None:
		case Reason::Custom:
		case Reason::DisplayClock:
			return {};
		}
		return {};
	}

	Reason m_reason = Reason::None;
	Reason m_clockReason = Reason::None;
	QExplicitlySharedDataPointer<Parameter> m_parameter;
};

static_assert(sizeof(EvidenceExplanation) < sizeof(QString),
			  "The evidence reason must be smaller than the text it replaces.");
