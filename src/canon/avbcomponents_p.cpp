// AVB components describe clips, sequences, tracks and effects. Read their
// inherited layouts in order, keeping all recorded fields and reference edges.
// A selected angle or rendered effect never hides its other inputs here.
// Layout evidence: pyavb components.py and trackgroups.py (read/write methods).
// Unknown extensions stop that object; the caller preserves its remaining bytes.

#include "avbobjects_p.h"

namespace Canon::Detail
{
	namespace
	{
		void version(AvbCursor &cursor, quint8 value)
		{
			cursor.tag(0x02);
			cursor.tag(value);
		}

		[[noreturn]] void unknownExtension(AvbCursor &cursor, const QString &owner, int tag)
		{
			cursor.unsupported(QStringLiteral("%1 extension 0x%2 has no established layout.")
								   .arg(owner)
								   .arg(tag, 2, 16, QLatin1Char('0')));
		}

		QString element(const QString &name, qint64 index)
		{
			return name + QLatin1Char('[') + QString::number(index) + QStringLiteral("]");
		}

		void pair(AvbCursor &cursor, const QString &name)
		{
			cursor.s32(name + QStringLiteral(".numerator"));
			cursor.s32(name + QStringLiteral(".denominator"));
		}

		void component(AvbCursor &cursor)
		{
			version(cursor, 0x03);
			cursor.ref(QStringLiteral("Component.left_bob"));
			cursor.ref(QStringLiteral("Component.right_bob"));
			cursor.s16(QStringLiteral("Component.media_kind_id"));
			cursor.exp10(QStringLiteral("Component.edit_rate"));
			cursor.string(QStringLiteral("Component.name"));
			cursor.string(QStringLiteral("Component.effect_id"));
			cursor.ref(QStringLiteral("Component.attributes"));
			cursor.ref(QStringLiteral("Component.session_attrs"));
			cursor.ref(QStringLiteral("Component.precomputed"));
			for (int tag = cursor.extension(); tag >= 0; tag = cursor.extension())
			{
				if (tag != 0x01)
					unknownExtension(cursor, QStringLiteral("Component"), tag);
				cursor.tag(72);
				cursor.ref(QStringLiteral("Component.param_list"));
			}
		}

		void clip(AvbCursor &cursor)
		{
			component(cursor);
			version(cursor, 0x01);
			// Both reference read and write use unsigned 32 bits for Clip.length.
			cursor.u32(QStringLiteral("Clip.length"));
		}

		void sequence(AvbCursor &cursor)
		{
			component(cursor);
			version(cursor, 0x03);
			const auto count = cursor.u32(QStringLiteral("Sequence.component_count"));
			cursor.requireCount(count, 4);
			for (quint32 i = 0; i < count; ++i)
				cursor.ref(element(QStringLiteral("Sequence.components"), i));
			cursor.tag(0x03);
		}

		void sourceClip(AvbCursor &cursor)
		{
			clip(cursor);
			version(cursor, 0x03);
			// The reference reader/writer disagree on these variable names. Keep
			// their source order instead of inventing a full identity from two words.
			cursor.u32(QStringLiteral("SourceClip.legacy_word0"));
			cursor.u32(QStringLiteral("SourceClip.legacy_word1"));
			cursor.s16(QStringLiteral("SourceClip.track_id"));
			cursor.s32(QStringLiteral("SourceClip.start_time"));
			for (int tag = cursor.extension(); tag >= 0; tag = cursor.extension())
			{
				if (tag != 0x01)
					unknownExtension(cursor, QStringLiteral("SourceClip"), tag);
				const auto identity = cursor.mobId(QStringLiteral("SourceClip.mob_id"));
				cursor.mobReference(QStringLiteral("SourceClip.mob_id"), identity);
			}
			cursor.tag(0x03);
		}

		void timecode(AvbCursor &cursor)
		{
			clip(cursor);
			version(cursor, 0x01);
			cursor.u32(QStringLiteral("Timecode.flags"));
			cursor.u16(QStringLiteral("Timecode.fps"));
			cursor.raw(QStringLiteral("Timecode.reserved"), 6);
			cursor.u32(QStringLiteral("Timecode.start"));
			cursor.tag(0x03);
		}

		void edgecode(AvbCursor &cursor)
		{
			clip(cursor);
			version(cursor, 0x01);
			cursor.raw(QStringLiteral("Edgecode.header"), 8);
			cursor.u8(QStringLiteral("Edgecode.film_kind"));
			cursor.u8(QStringLiteral("Edgecode.code_format"));
			cursor.u16(QStringLiteral("Edgecode.base_perf"));
			cursor.u32(QStringLiteral("Edgecode.reserved"));
			cursor.s32(QStringLiteral("Edgecode.start_ec"));
			cursor.tag(0x03);
		}

		void trackRef(AvbCursor &cursor)
		{
			clip(cursor);
			version(cursor, 0x01);
			cursor.s16(QStringLiteral("TrackRef.relative_scope"));
			cursor.s16(QStringLiteral("TrackRef.relative_track"));
			cursor.tag(0x03);
		}

		void paramValue(AvbCursor &cursor, const QString &name, qint16 type, bool allowReference)
		{
			switch (type)
			{
			case 1:
				cursor.s32(name);
				return;
			case 2:
				cursor.f64(name);
				return;
			case 4:
				if (allowReference)
				{
					cursor.ref(name);
					return;
				}
				break;
			}
			cursor.unsupported(QStringLiteral("%1 has unsupported parameter value type %2.").arg(name).arg(type));
		}

		void paramClip(AvbCursor &cursor)
		{
			clip(cursor);
			version(cursor, 0x01);
			cursor.s32(QStringLiteral("ParamClip.interp_kind"));
			const auto type = cursor.s16(QStringLiteral("ParamClip.value_type"));
			if (type != 1 && type != 2 && type != 4)
				cursor.unsupported(QStringLiteral("ParamClip value type %1 has no established layout.").arg(type));
			const auto count = cursor.s32(QStringLiteral("ParamClip.point_count"));
			cursor.requireCount(count, type == 2 ? 22 : 18);
			for (qint32 i = 0; i < count; ++i)
			{
				const auto point = element(QStringLiteral("ParamClip.control_points"), i);
				pair(cursor, point + QStringLiteral(".offset"));
				cursor.s32(point + QStringLiteral(".timescale"));
				paramValue(cursor, point + QStringLiteral(".value"), type, true);
				const auto properties = cursor.s16(point + QStringLiteral(".pp_count"));
				cursor.requireCount(properties, 8);
				for (qint16 j = 0; j < properties; ++j)
				{
					const auto property = element(point + QStringLiteral(".pp"), j);
					cursor.s16(property + QStringLiteral(".code"));
					const auto propertyType = cursor.s16(property + QStringLiteral(".type"));
					paramValue(cursor, property + QStringLiteral(".value"), propertyType, false);
				}
			}
			for (int tag = cursor.extension(); tag >= 0; tag = cursor.extension())
			{
				switch (tag)
				{
				case 0x01:
					cursor.tag(71);
					cursor.s32(QStringLiteral("ParamClip.extrap_kind"));
					break;
				case 0x02:
					cursor.tag(71);
					cursor.s32(QStringLiteral("ParamClip.fields"));
					break;
				default:
					unknownExtension(cursor, QStringLiteral("ParamClip"), tag);
				}
			}
			cursor.tag(0x03);
		}

		void controlClip(AvbCursor &cursor)
		{
			clip(cursor);
			version(cursor, 0x03);
			cursor.s32(QStringLiteral("ControlClip.interp_kind"));
			const auto count = cursor.s32(QStringLiteral("ControlClip.point_count"));
			cursor.requireCount(count, 13);
			for (qint32 i = 0; i < count; ++i)
			{
				const auto point = element(QStringLiteral("ControlClip.control_points"), i);
				pair(cursor, point + QStringLiteral(".offset"));
				cursor.s32(point + QStringLiteral(".time_scale"));
				if (!cursor.boolean(point + QStringLiteral(".has_value")))
					cursor.unsupported(QStringLiteral("ControlClip point without a value has no established following layout."));
				// The layout is two signed words; its reference property definition
				// calls this a boolean, so do not claim a rational interpretation.
				cursor.s32(point + QStringLiteral(".value.word0"));
				cursor.s32(point + QStringLiteral(".value.word1"));
				const auto properties = cursor.s16(point + QStringLiteral(".pp_count"));
				cursor.requireCount(properties, 10);
				for (qint16 j = 0; j < properties; ++j)
				{
					const auto property = element(point + QStringLiteral(".pp"), j);
					cursor.s16(property + QStringLiteral(".code"));
					pair(cursor, property + QStringLiteral(".value"));
				}
			}
			cursor.tag(0x03);
		}

		void filler(AvbCursor &cursor)
		{
			clip(cursor);
			version(cursor, 0x01);
			cursor.tag(0x03);
		}

		void track(AvbCursor &cursor, const QString &name)
		{
			const auto flags = cursor.u16(name + QStringLiteral(".flags"));
			if (flags & 0xfc00)
				cursor.unsupported(QStringLiteral("Track flags 0x%1 include fields with no established layout.")
									   .arg(flags, 4, 16, QLatin1Char('0')));
			if (flags & 0x0001)
				cursor.s16(name + QStringLiteral(".index"));
			if (flags & 0x0002)
				cursor.ref(name + QStringLiteral(".attributes"));
			// Session attributes precede component despite their higher flag bit.
			if (flags & 0x0200)
				cursor.ref(name + QStringLiteral(".session_attr"));
			if (flags & 0x0004)
				cursor.ref(name + QStringLiteral(".component"));
			if (flags & 0x0008)
				cursor.ref(name + QStringLiteral(".filler_proxy"));
			if (flags & 0x0010)
				cursor.ref(name + QStringLiteral(".bob_data"));
			if (flags & 0x0020)
				cursor.s16(name + QStringLiteral(".control_code"));
			if (flags & 0x0040)
				cursor.s16(name + QStringLiteral(".control_sub_code"));
			if (flags & 0x0080)
				cursor.s32(name + QStringLiteral(".start_pos"));
			if (flags & 0x0100)
				cursor.boolean(name + QStringLiteral(".read_only"));
		}

		qint32 trackGroup(AvbCursor &cursor)
		{
			component(cursor);
			version(cursor, 0x08);
			cursor.u8(QStringLiteral("TrackGroup.mc_mode"));
			cursor.s32(QStringLiteral("TrackGroup.length"));
			cursor.s32(QStringLiteral("TrackGroup.num_scalars"));
			const auto count = cursor.s32(QStringLiteral("TrackGroup.track_count"));
			cursor.requireCount(count, 2);
			for (qint32 i = 0; i < count; ++i)
				track(cursor, element(QStringLiteral("TrackGroup.tracks"), i));
			for (int tag = cursor.extension(); tag >= 0; tag = cursor.extension())
			{
				if (tag != 0x01)
					unknownExtension(cursor, QStringLiteral("TrackGroup"), tag);
				cursor.requireCount(count, 3);
				for (qint32 i = 0; i < count; ++i)
				{
					cursor.tag(69);
					cursor.s16(element(QStringLiteral("TrackGroup.tracks"), i) + QStringLiteral(".lock_number"));
				}
			}
			return count;
		}

		void effectFields(AvbCursor &cursor, const QString &owner)
		{
			cursor.s32(owner + QStringLiteral(".left_length"));
			cursor.s32(owner + QStringLiteral(".right_length"));
			cursor.s16(owner + QStringLiteral(".info_version"));
			cursor.s32(owner + QStringLiteral(".info_current"));
			cursor.s32(owner + QStringLiteral(".info_smooth"));
			cursor.s16(owner + QStringLiteral(".info_color_item"));
			cursor.s16(owner + QStringLiteral(".info_quality"));
			cursor.s8(owner + QStringLiteral(".info_is_reversed"));
			cursor.boolean(owner + QStringLiteral(".info_aspect_on"));
			cursor.ref(owner + QStringLiteral(".keyframes"));
			cursor.boolean(owner + QStringLiteral(".info_force_software"));
			cursor.boolean(owner + QStringLiteral(".info_never_hardware"));
		}

		void trackEffect(AvbCursor &cursor)
		{
			trackGroup(cursor);
			version(cursor, 0x06);
			effectFields(cursor, QStringLiteral("TrackEffect"));
			for (int tag = cursor.extension(); tag >= 0; tag = cursor.extension())
			{
				if (tag != 0x02)
					unknownExtension(cursor, QStringLiteral("TrackEffect"), tag);
				cursor.tag(72);
				cursor.ref(QStringLiteral("TrackEffect.trackman"));
			}
		}

		void panVolume(AvbCursor &cursor)
		{
			trackEffect(cursor);
			version(cursor, 0x05);
			cursor.s32(QStringLiteral("PanVolumeEffect.level"));
			cursor.s32(QStringLiteral("PanVolumeEffect.pan"));
			cursor.boolean(QStringLiteral("PanVolumeEffect.suppress_validation"));
			cursor.boolean(QStringLiteral("PanVolumeEffect.level_set"));
			cursor.boolean(QStringLiteral("PanVolumeEffect.pan_set"));
			for (int tag = cursor.extension(); tag >= 0; tag = cursor.extension())
			{
				switch (tag)
				{
				case 0x01:
					cursor.tag(71);
					cursor.s32(QStringLiteral("PanVolumeEffect.supports_seperate_gain"));
					break;
				case 0x02:
					cursor.tag(71);
					cursor.s32(QStringLiteral("PanVolumeEffect.is_trim_gain_effect"));
					break;
				default:
					unknownExtension(cursor, QStringLiteral("PanVolumeEffect"), tag);
				}
			}
			cursor.tag(0x03);
		}

		void plugin(AvbCursor &cursor, const QString &name)
		{
			cursor.string(name + QStringLiteral(".name"));
			cursor.u32(name + QStringLiteral(".manufacturer_id"));
			cursor.u32(name + QStringLiteral(".product_id"));
			cursor.u32(name + QStringLiteral(".plugin_id"));
			const auto count = cursor.s32(name + QStringLiteral(".chunk_count"));
			cursor.requireCount(count, 26);
			for (qint32 i = 0; i < count; ++i)
			{
				const auto chunk = element(name + QStringLiteral(".chunks"), i);
				const auto size = cursor.s32(chunk + QStringLiteral(".data_size"));
				cursor.requireCount(size, 1);
				cursor.s32(chunk + QStringLiteral(".version"));
				cursor.u32(chunk + QStringLiteral(".manufacturer_id"));
				cursor.u32(chunk + QStringLiteral(".product_id"));
				cursor.u32(chunk + QStringLiteral(".plugin_id"));
				cursor.u32(chunk + QStringLiteral(".chunk_id"));
				cursor.string(chunk + QStringLiteral(".name"));
				cursor.raw(chunk + QStringLiteral(".data"), size);
			}
		}

		void presetPath(AvbCursor &cursor)
		{
			cursor.tag(72);
			const auto size = cursor.u32(QStringLiteral("AudioSuitePluginEffect.preset_path_length"));
			if (size != 0)
			{
				cursor.tag(65);
				const auto repeatedSize = cursor.u32(QStringLiteral("AudioSuitePluginEffect.preset_path_data_length"));
				if (size != repeatedSize)
					cursor.malformed(QStringLiteral("AudioSuitePluginEffect preset path lengths disagree."));
			}
			cursor.raw(QStringLiteral("AudioSuitePluginEffect.preset_path"), size);
		}

		void audioSuite(AvbCursor &cursor)
		{
			trackEffect(cursor);
			version(cursor, 0x01);
			const auto count = cursor.s32(QStringLiteral("AudioSuitePluginEffect.plugin_count"));
			cursor.requireCount(count, 18);
			// The reference writer explicitly emits a counted list, even though
			// its reader only accepts one plugin. Preserve every recorded entry.
			for (qint32 i = 0; i < count; ++i)
				plugin(cursor, element(QStringLiteral("AudioSuitePluginEffect.plugins"), i));
			for (int tag = cursor.extension(); tag >= 0; tag = cursor.extension())
			{
				switch (tag)
				{
				case 0x01:
					cursor.tag(71);
					cursor.s32(QStringLiteral("AudioSuitePluginEffect.legacy_word0"));
					cursor.tag(71);
					cursor.s32(QStringLiteral("AudioSuitePluginEffect.legacy_word1"));
					break;
				case 0x02:
					cursor.tag(77);
					cursor.s64(QStringLiteral("AudioSuitePluginEffect.mark_in"));
					break;
				case 0x03:
					cursor.tag(77);
					cursor.s64(QStringLiteral("AudioSuitePluginEffect.mark_out"));
					break;
				case 0x04:
					cursor.tag(72);
					cursor.s32(QStringLiteral("AudioSuitePluginEffect.tracks_to_affect"));
					break;
				case 0x05:
					cursor.tag(71);
					cursor.s32(QStringLiteral("AudioSuitePluginEffect.rendering_mode"));
					break;
				case 0x06:
					cursor.tag(71);
					cursor.s32(QStringLiteral("AudioSuitePluginEffect.padding_secs"));
					break;
				case 0x08:
					cursor.mobReference(QStringLiteral("AudioSuitePluginEffect.mob_id"),
										cursor.mobId(QStringLiteral("AudioSuitePluginEffect.mob_id")));
					break;
				case 0x09:
					presetPath(cursor);
					break;
				default:
					unknownExtension(cursor, QStringLiteral("AudioSuitePluginEffect"), tag);
				}
			}
			cursor.tag(0x03);
		}

		void equalizer(AvbCursor &cursor)
		{
			trackEffect(cursor);
			version(cursor, 0x05);
			const auto count = cursor.s32(QStringLiteral("EqualizerMultiBand.band_count"));
			cursor.requireCount(count, 17);
			for (qint32 i = 0; i < count; ++i)
			{
				const auto band = element(QStringLiteral("EqualizerMultiBand.bands"), i);
				cursor.s32(band + QStringLiteral(".type"));
				cursor.s32(band + QStringLiteral(".freq"));
				cursor.s32(band + QStringLiteral(".gain"));
				cursor.s32(band + QStringLiteral(".q"));
				cursor.boolean(band + QStringLiteral(".enable"));
			}
			cursor.boolean(QStringLiteral("EqualizerMultiBand.effect_enable"));
			cursor.string(QStringLiteral("EqualizerMultiBand.filter_name"));
			cursor.tag(0x03);
		}

		void timeWarp(AvbCursor &cursor)
		{
			trackGroup(cursor);
			version(cursor, 0x02);
			cursor.s32(QStringLiteral("TimeWarp.phase_offset"));
		}

		void captureMask(AvbCursor &cursor)
		{
			timeWarp(cursor);
			version(cursor, 0x01);
			cursor.boolean(QStringLiteral("CaptureMask.is_double"));
			cursor.u32(QStringLiteral("CaptureMask.mask_bits"));
			cursor.tag(0x03);
		}

		void strobe(AvbCursor &cursor)
		{
			timeWarp(cursor);
			version(cursor, 0x01);
			cursor.s32(QStringLiteral("StrobeEffect.strobe_value"));
			cursor.tag(0x03);
		}

		void motion(AvbCursor &cursor)
		{
			timeWarp(cursor);
			version(cursor, 0x03);
			pair(cursor, QStringLiteral("MotionEffect.speed_ratio"));
			for (int tag = cursor.extension(); tag >= 0; tag = cursor.extension())
			{
				switch (tag)
				{
				case 0x01:
					cursor.tag(75);
					cursor.f64(QStringLiteral("MotionEffect.offset_adjust"));
					break;
				case 0x02:
					cursor.tag(72);
					cursor.ref(QStringLiteral("MotionEffect.source_param_list"));
					break;
				case 0x03:
					cursor.tag(66);
					cursor.boolean(QStringLiteral("MotionEffect.new_source_calculation"));
					break;
				default:
					unknownExtension(cursor, QStringLiteral("MotionEffect"), tag);
				}
			}
			cursor.tag(0x03);
		}

		void repeat(AvbCursor &cursor)
		{
			timeWarp(cursor);
			version(cursor, 0x01);
			cursor.tag(0x03);
		}

		void essenceGroup(AvbCursor &cursor)
		{
			trackGroup(cursor);
			version(cursor, 0x01);
			for (int tag = cursor.extension(); tag >= 0; tag = cursor.extension())
			{
				if (tag != 0x01)
					unknownExtension(cursor, QStringLiteral("EssenceGroup"), tag);
				cursor.tag(71);
				cursor.s32(QStringLiteral("EssenceGroup.rep_set_type"));
			}
			cursor.tag(0x03);
		}

		void transition(AvbCursor &cursor)
		{
			trackGroup(cursor);
			version(cursor, 0x01);
			cursor.s32(QStringLiteral("TransitionEffect.cutpoint"));
			version(cursor, 0x05);
			effectFields(cursor, QStringLiteral("TransitionEffect"));
			for (int tag = cursor.extension(); tag >= 0; tag = cursor.extension())
			{
				if (tag != 0x01)
					unknownExtension(cursor, QStringLiteral("TransitionEffect"), tag);
				cursor.tag(72);
				cursor.ref(QStringLiteral("TransitionEffect.trackman"));
			}
			cursor.tag(0x03);
		}

		void selector(AvbCursor &cursor)
		{
			const auto tracks = trackGroup(cursor);
			version(cursor, 0x01);
			cursor.boolean(QStringLiteral("Selector.is_ganged"));
			const auto selected = cursor.u16(QStringLiteral("Selector.selected"));
			if (selected >= tracks)
				cursor.malformed(QStringLiteral("Selector selected track is outside its recorded track list."));
			cursor.tag(0x03);
		}

		void composition(AvbCursor &cursor)
		{
			trackGroup(cursor);
			version(cursor, 0x02);
			cursor.u32(QStringLiteral("Composition.legacy_word0"));
			cursor.u32(QStringLiteral("Composition.legacy_word1"));
			// The reference encodes dates as unsigned epoch seconds. No timezone
			// is stored here, so retain the number rather than a local display date.
			cursor.u32(QStringLiteral("Composition.last_modified"));
			const auto type = cursor.u8(QStringLiteral("Composition.mob_type"));
			cursor.role(type == 1 ? AvidObject::Role::Composition : type == 2 ? AvidObject::Role::Master
																			  : AvidObject::Role::Unknown);
			cursor.s32(QStringLiteral("Composition.usage_code"));
			cursor.ref(QStringLiteral("Composition.descriptor"));
			for (int tag = cursor.extension(); tag >= 0; tag = cursor.extension())
			{
				switch (tag)
				{
				case 0x01:
					cursor.tag(71);
					cursor.u32(QStringLiteral("Composition.creation_time"));
					break;
				case 0x02:
					cursor.identity(cursor.mobId(QStringLiteral("Composition.mob_id")),
									QStringLiteral("AVB MobID; SMPTE label and instance bytes; material UUID in canonical little-endian field order"));
					break;
				default:
					unknownExtension(cursor, QStringLiteral("Composition"), tag);
				}
			}
			cursor.tag(0x03);
		}
	}

	namespace
	{
		using ComponentReader = void (*)(AvbCursor &);

		ComponentReader componentReader(QByteArrayView classId)
		{
			struct Reader
			{
				const char *classId;
				void (*read)(AvbCursor &);
			};
			static constexpr Reader readers[] = {
				{"ASPI", audioSuite},
				{"CMPO", composition},
				{"CTRL", controlClip},
				{"ECCP", edgecode},
				{"EQMB", equalizer},
				{"FILL", filler},
				{"MASK", captureMask},
				{"PRCL", paramClip},
				{"PVOL", panVolume},
				{"REPT", repeat},
				{"RSET", essenceGroup},
				{"SCLP", sourceClip},
				{"SEQU", sequence},
				{"SLCT", selector},
				{"SPED", motion},
				{"STRB", strobe},
				{"TCCP", timecode},
				{"TKFX", [](AvbCursor &c)
				 { trackEffect(c); c.tag(0x03); }},
				{"TNFX", transition},
				{"TRKG", [](AvbCursor &c)
				 { trackGroup(c); }},
				{"TRKR", trackRef},
				{"WARP", timeWarp}};
			for (const auto &reader : readers)
				if (classId == QByteArrayView(reader.classId, 4))
					return reader.read;
			return nullptr;
		}
	}

	bool supportsAvbComponent(QByteArrayView classId)
	{
		return componentReader(classId) != nullptr;
	}

	bool readAvbComponent(AvbCursor &cursor, QByteArrayView classId)
	{
		const auto read = componentReader(classId);
		if (!read)
			return false;
		cursor.role(AvidObject::Role::Component);
		read(cursor);
		return true;
	}
}
