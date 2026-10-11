#include "avbobjects_p.h"

#include <QtEndian>
#include <initializer_list>

// AVB descriptors explain the stored media; these other objects hold locators,
// attributes and effect settings. Every field stays attached to its source object,
// including repeated attributes and both legacy and Unicode text. Unsupported
// layouts stop at that field so the caller can retain the untouched remainder.
// Grammar reference: Mark Reid's MIT-licensed pyavb essence/misc/attributes readers.
// PCMA extension 2 is also verified against Avid's native descriptor Get/Put methods.

namespace MediaEngine::Detail
{
	namespace
	{
		void section(AvbFieldReader &c, quint8 version)
		{
			c.expectTag(2);
			c.expectTag(version);
		}

		[[noreturn]] void unknownExtension(AvbFieldReader &c, const QString &owner, int extension)
		{
			c.failUnsupported(QStringLiteral("Unsupported %1 extension %2.").arg(owner).arg(extension));
		}

		QString indexed(const QString &name, qint64 index)
		{
			return name + QStringLiteral("[%1]").arg(index);
		}

		void signed32Fields(AvbFieldReader &c, const QString &prefix,
							std::initializer_list<const char *> names)
		{
			for (const auto *name : names)
				c.s32(prefix + QLatin1Char('.') + QString::fromLatin1(name));
		}

		void references(AvbFieldReader &c, const QString &name, qint64 count)
		{
			c.requireCount(count, 4);
			for (qint64 i = 0; i < count; ++i)
				c.readObjectReference(indexed(name, i));
		}

		void mediaDescriptor(AvbFieldReader &c)
		{
			c.setObjectRole(AvidObject::Role::Descriptor);
			section(c, 3);
			c.u8(QStringLiteral("MediaDescriptor.mob_kind"));
			c.readObjectReference(QStringLiteral("MediaDescriptor.locator"));
			c.boolean(QStringLiteral("MediaDescriptor.intermediate"));
			c.readObjectReference(QStringLiteral("MediaDescriptor.physical_media"));
			for (int extension; (extension = c.readExtensionTag()) >= 0;)
			{
				switch (extension)
				{
				case 1:
					c.expectTag(65);
					if (c.s32(QStringLiteral("MediaDescriptor.uuid_length")) != 16)
						c.failMalformed(QStringLiteral("MediaDescriptor UUID length is not 16."));
					c.readBinaryUuid(QStringLiteral("MediaDescriptor.uuid"));
					break;
				case 2:
				{
					c.expectTag(65);
					const auto size = c.s32(QStringLiteral("MediaDescriptor.wchar_size"));
					// The reference grammar does not establish this field's byte order.
					c.readBytes(QStringLiteral("MediaDescriptor.wchar"), size);
					break;
				}
				case 3:
					c.expectTag(72);
					c.readObjectReference(QStringLiteral("MediaDescriptor.attributes"));
					break;
				default:
					unknownExtension(c, QStringLiteral("MediaDescriptor"), extension);
				}
			}
		}

		void fileDescriptor(AvbFieldReader &c)
		{
			mediaDescriptor(c);
			section(c, 3);
			c.readMantissaExponent(QStringLiteral("MediaFileDescriptor.edit_rate"));
			c.s32(QStringLiteral("MediaFileDescriptor.length"));
			c.s16(QStringLiteral("MediaFileDescriptor.is_omfi"));
			c.s32(QStringLiteral("MediaFileDescriptor.data_offset"));
		}

		void audioFields(AvbFieldReader &c, const QString &owner)
		{
			c.u16(owner + QStringLiteral(".channels"));
			c.u16(owner + QStringLiteral(".quantization_bits"));
			c.readMantissaExponent(owner + QStringLiteral(".sample_rate"));
			c.boolean(owner + QStringLiteral(".locked"));
			c.s16(owner + QStringLiteral(".audio_ref_level"));
			c.s32(owner + QStringLiteral(".electro_spatial_formulation"));
			c.u16(owner + QStringLiteral(".dial_norm"));
			c.u32(owner + QStringLiteral(".coding_format"));
		}

		void pcmDescriptor(AvbFieldReader &c)
		{
			fileDescriptor(c);
			section(c, 1);
			audioFields(c, QStringLiteral("PCMADescriptor"));
			c.u32(QStringLiteral("PCMADescriptor.block_align"));
			c.u16(QStringLiteral("PCMADescriptor.sequence_offset"));
			c.u32(QStringLiteral("PCMADescriptor.average_bps"));
			c.boolean(QStringLiteral("PCMADescriptor.has_peak_envelope_data"));
			signed32Fields(c, QStringLiteral("PCMADescriptor"),
						   {"peak_envelope_version", "peak_envelope_format", "points_per_peak_value",
							"peak_envelope_block_size", "peak_channel_count", "peak_frame_count"});
			c.u64(QStringLiteral("PCMADescriptor.peak_of_peaks_offset"));
			c.s32(QStringLiteral("PCMADescriptor.peak_envelope_timestamp"));
			for (int extension; (extension = c.readExtensionTag()) >= 0;)
			{
				switch (extension)
				{
				case 1:
					c.expectTag(77);
					c.s64(QStringLiteral("PCMADescriptor.ebu_timestamp"));
					break;
				case 2:
					// Avid PCMAudioDescriptor::Get calls SetSubframeAlignment here.
					c.expectTag(71);
					c.s32(QStringLiteral("PCMADescriptor.sub_frame_alignment"));
					break;
				case 3:
					c.expectTag(76);
					c.string(QStringLiteral("PCMADescriptor.timecode_framerate"));
					break;
				default:
					unknownExtension(c, QStringLiteral("PCMADescriptor"), extension);
				}
			}
			c.expectTag(3);
		}

		void mpegAudioDescriptor(AvbFieldReader &c)
		{
			fileDescriptor(c);
			section(c, 1);
			audioFields(c, QStringLiteral("MPGADescriptor"));
			c.u32(QStringLiteral("MPGADescriptor.bit_rate"));
			for (int extension; (extension = c.readExtensionTag()) >= 0;)
			{
				if (extension != 1 && extension != 2)
					unknownExtension(c, QStringLiteral("MPGADescriptor"), extension);
				c.expectTag(77);
				c.u64(extension == 1 ? QStringLiteral("MPGADescriptor.sub_frame_alignment")
									 : QStringLiteral("MPGADescriptor.origin"));
			}
			c.expectTag(3);
		}

		void box(AvbFieldReader &c, const QString &name)
		{
			for (int i = 0; i < 4; ++i)
			{
				c.expectTag(71);
				c.s32(indexed(name, i) + QStringLiteral(".x"));
				c.expectTag(71);
				c.s32(indexed(name, i) + QStringLiteral(".y"));
			}
		}

		void didDescriptor(AvbFieldReader &c)
		{
			fileDescriptor(c);
			section(c, 2);
			signed32Fields(c, QStringLiteral("DIDDescriptor"),
						   {"stored_height", "stored_width", "sampled_height", "sampled_width",
							"sampled_x_offset", "sampled_y_offset", "display_height", "display_width",
							"display_x_offset", "display_y_offset"});
			c.s16(QStringLiteral("DIDDescriptor.frame_layout"));
			c.s32(QStringLiteral("DIDDescriptor.aspect_ratio.numerator"));
			c.s32(QStringLiteral("DIDDescriptor.aspect_ratio.denominator"));
			const auto lineMapBytes = c.s32(QStringLiteral("DIDDescriptor.line_map_byte_size"));
			if (lineMapBytes < 0 || lineMapBytes % 4 != 0)
				c.failMalformed(QStringLiteral("DIDDescriptor line map length is not a nonnegative multiple of four."));
			c.requireCount(lineMapBytes / 4, 4);
			for (qint32 i = 0; i < lineMapBytes / 4; ++i)
				c.s32(indexed(QStringLiteral("DIDDescriptor.line_map"), i));
			c.s32(QStringLiteral("DIDDescriptor.alpha_transparency"));
			c.boolean(QStringLiteral("DIDDescriptor.uniformness"));
			c.s32(QStringLiteral("DIDDescriptor.did_image_size"));
			c.readObjectReference(QStringLiteral("DIDDescriptor.next_did_desc"));
			c.readFourcc(QStringLiteral("DIDDescriptor.compress_method"));
			c.s32(QStringLiteral("DIDDescriptor.resolution_id"));
			c.s32(QStringLiteral("DIDDescriptor.image_alignment_factor"));
			for (int extension; (extension = c.readExtensionTag()) >= 0;)
			{
				switch (extension)
				{
				case 1:
					c.expectTag(69);
					c.s16(QStringLiteral("DIDDescriptor.frame_index_byte_order"));
					break;
				case 2:
					c.expectTag(71);
					c.s32(QStringLiteral("DIDDescriptor.frame_sample_size"));
					break;
				case 3:
					c.expectTag(71);
					c.s32(QStringLiteral("DIDDescriptor.first_frame_offset"));
					break;
				case 4:
					c.expectTag(71);
					c.s32(QStringLiteral("DIDDescriptor.client_fill_start"));
					c.expectTag(71);
					c.s32(QStringLiteral("DIDDescriptor.client_fill_end"));
					break;
				case 5:
					c.expectTag(71);
					c.s32(QStringLiteral("DIDDescriptor.offset_to_rle_frame_index"));
					break;
				case 6:
					c.expectTag(71);
					c.s32(QStringLiteral("DIDDescriptor.frame_start_offset"));
					break;
				case 8:
					box(c, QStringLiteral("DIDDescriptor.valid_box"));
					box(c, QStringLiteral("DIDDescriptor.essence_box"));
					box(c, QStringLiteral("DIDDescriptor.source_box"));
					break;
				case 9:
					box(c, QStringLiteral("DIDDescriptor.framing_box"));
					c.expectTag(71);
					c.s32(QStringLiteral("DIDDescriptor.reformatting_option"));
					break;
				case 10:
					c.expectTag(80);
					c.readBinaryUuid(QStringLiteral("DIDDescriptor.transfer_characteristic"));
					break;
				case 11:
					c.expectTag(80);
					c.readBinaryUuid(QStringLiteral("DIDDescriptor.color_primaries"));
					c.expectTag(80);
					c.readBinaryUuid(QStringLiteral("DIDDescriptor.coding_equations"));
					break;
				case 12:
					c.expectTag(80);
					c.readBinaryUuid(QStringLiteral("DIDDescriptor.essence_compression"));
					break;
				case 14:
					c.expectTag(68);
					c.u8(QStringLiteral("DIDDescriptor.essence_element_size_kind"));
					break;
				case 15:
					c.expectTag(66);
					c.boolean(QStringLiteral("DIDDescriptor.frame_checked_with_mapper"));
					break;
				default:
					unknownExtension(c, QStringLiteral("DIDDescriptor"), extension);
				}
			}
		}

		void cdciDescriptor(AvbFieldReader &c)
		{
			didDescriptor(c);
			section(c, 2);
			c.u32(QStringLiteral("CDCIDescriptor.horizontal_subsampling"));
			c.u32(QStringLiteral("CDCIDescriptor.vertical_subsampling"));
			c.s32(QStringLiteral("CDCIDescriptor.component_width"));
			c.s16(QStringLiteral("CDCIDescriptor.color_sitting"));
			c.u32(QStringLiteral("CDCIDescriptor.black_ref_level"));
			c.u32(QStringLiteral("CDCIDescriptor.white_ref_level"));
			c.u32(QStringLiteral("CDCIDescriptor.color_range"));
			c.s64(QStringLiteral("CDCIDescriptor.frame_index_offset"));
			for (int extension; (extension = c.readExtensionTag()) >= 0;)
			{
				if (extension != 1 && extension != 2)
					unknownExtension(c, QStringLiteral("CDCIDescriptor"), extension);
				c.expectTag(72);
				c.u32(extension == 1 ? QStringLiteral("CDCIDescriptor.alpha_sampled_width")
									 : QStringLiteral("CDCIDescriptor.ignore_bw"));
			}
		}

		void rgbaDescriptor(AvbFieldReader &c)
		{
			didDescriptor(c);
			section(c, 1);
			const auto layoutSize = c.u32(QStringLiteral("RGBADescriptor.layout_size"));
			c.readBytes(QStringLiteral("RGBADescriptor.pixel_layout.codes"), layoutSize);
			const auto structSize = c.u32(QStringLiteral("RGBADescriptor.struct_size"));
			c.readBytes(QStringLiteral("RGBADescriptor.pixel_layout.sizes"), structSize);
			if (layoutSize != structSize)
				c.failMalformed(QStringLiteral("RGBA component codes and widths have different counts."));
			for (const auto *field : {"palette_layout_size", "palette_struct_size", "palette_size"})
			{
				if (c.u32(QStringLiteral("RGBADescriptor.") + QString::fromLatin1(field)) != 0)
					c.failUnsupported(QStringLiteral("Nonempty AVB RGBA palettes are not yet understood."));
			}
			for (int extension; (extension = c.readExtensionTag()) >= 0;)
			{
				switch (extension)
				{
				case 1:
					c.expectTag(77);
					c.u64(QStringLiteral("RGBADescriptor.frame_index_offset"));
					break;
				case 2:
					c.expectTag(66);
					c.boolean(QStringLiteral("RGBADescriptor.has_comp_min_ref"));
					c.expectTag(72);
					c.u32(QStringLiteral("RGBADescriptor.comp_min_ref"));
					c.expectTag(66);
					c.boolean(QStringLiteral("RGBADescriptor.has_comp_max_ref"));
					c.expectTag(72);
					c.u32(QStringLiteral("RGBADescriptor.comp_max_ref"));
					break;
				case 3:
					c.expectTag(72);
					c.u32(QStringLiteral("RGBADescriptor.alpha_min_ref"));
					c.expectTag(72);
					c.u32(QStringLiteral("RGBADescriptor.alpha_max_ref"));
					break;
				default:
					unknownExtension(c, QStringLiteral("RGBADescriptor"), extension);
				}
			}
			c.expectTag(3);
		}

		void dataDescriptor(AvbFieldReader &c)
		{
			fileDescriptor(c);
			section(c, 1);
			c.boolean(QStringLiteral("DataDescriptor.is_offset_to_frame_indexes_valid"));
			c.u64(QStringLiteral("DataDescriptor.offset_to_frame_indexes"));
			signed32Fields(c, QStringLiteral("DataDescriptor"),
						   {"first_frame_offset", "min_sample_size", "max_sample_size"});
			// DATD itself has no final 0x03 in the reference reader or writer.
		}

		void waveSummary(AvbFieldReader &c, bool wave)
		{
			fileDescriptor(c);
			section(c, 1);
			const auto prefix = wave ? QStringLiteral("WaveDescriptor") : QStringLiteral("AIFCDescriptor");
			const auto signature = c.readBytes(prefix + QStringLiteral(".signature"), 4);
			if (signature != (wave ? QByteArrayLiteral("RIFF") : QByteArrayLiteral("FORM")))
				c.failMalformed(QStringLiteral("Audio summary has an unexpected signature."));
			// Embedded RIFF/FORM lengths retain their own endian order even in a
			// bin whose scalar fields use the opposite order.
			const auto bytes = c.readBytes(prefix + QStringLiteral(".summary_size"), 4);
			const auto size = wave ? qFromLittleEndian<quint32>(bytes.constData())
								   : qFromBigEndian<quint32>(bytes.constData());
			c.readBytes(prefix + QStringLiteral(".summary"), size);
			if (!wave)
			{
				for (int extension; (extension = c.readExtensionTag()) >= 0;)
				{
					if (extension != 1)
						unknownExtension(c, prefix, extension);
					c.expectTag(71);
					c.s32(QStringLiteral("AIFCDescriptor.data_pos"));
				}
			}
			c.expectTag(3);
		}

		void mpegVideoDescriptor(AvbFieldReader &c)
		{
			cdciDescriptor(c);
			section(c, 1);
			for (const auto *field : {"mpeg_version", "profile", "gop_structure", "stream_type"})
				c.u8(QStringLiteral("MPGIDescriptor.") + QString::fromLatin1(field));
			c.boolean(QStringLiteral("MPGIDescriptor.random_access"));
			c.boolean(QStringLiteral("MPGIDescriptor.leading_discard"));
			c.boolean(QStringLiteral("MPGIDescriptor.trailing_discard"));
			c.u16(QStringLiteral("MPGIDescriptor.min_gop_length"));
			c.u16(QStringLiteral("MPGIDescriptor.max_gop_length"));
			const auto size = c.s32(QStringLiteral("MPGIDescriptor.sequence_hdr_size"));
			c.readBytes(QStringLiteral("MPGIDescriptor.sequence_hdr"), size);
			c.expectTag(3);
		}

		void jpegDescriptor(AvbFieldReader &c)
		{
			cdciDescriptor(c);
			section(c, 1);
			c.s32(QStringLiteral("JPEGDescriptor.jpeg_table_id"));
			c.u64(QStringLiteral("JPEGDescriptor.jpeg_frame_index_offset"));
			const auto size = c.s32(QStringLiteral("JPEGDescriptor.table_size"));
			c.readBytes(QStringLiteral("JPEGDescriptor.quantization_tables"), size);
			for (int extension; (extension = c.readExtensionTag()) >= 0;)
			{
				if (extension != 1)
					unknownExtension(c, QStringLiteral("JPEGDescriptor"), extension);
				c.expectTag(71);
				c.s32(QStringLiteral("JPEGDescriptor.image_start_align"));
			}
			c.expectTag(3);
		}

		void fileLocator(AvbFieldReader &c)
		{
			section(c, 2);
			c.string(QStringLiteral("FileLocator.path"));
			for (int extension; (extension = c.readExtensionTag()) >= 0;)
			{
				if (extension < 1 || extension > 3)
					unknownExtension(c, QStringLiteral("FileLocator"), extension);
				c.expectTag(76);
				if (extension == 1)
					c.string(QStringLiteral("FileLocator.path_posix"));
				else
					c.string(extension == 2 ? QStringLiteral("FileLocator.path_utf8")
											: QStringLiteral("FileLocator.path2_utf8"),
							 TextEncoding::Utf8);
			}
			c.expectTag(3);
		}

		void mediaLocator(AvbFieldReader &c)
		{
			section(c, 2);
			c.u32(QStringLiteral("MSMLocator.legacy_word0"));
			c.u32(QStringLiteral("MSMLocator.legacy_word1"));
			c.string(QStringLiteral("MSMLocator.last_known_volume"));
			for (int extension; (extension = c.readExtensionTag()) >= 0;)
			{
				switch (extension)
				{
				case 1:
					c.expectTag(71);
					c.s32(QStringLiteral("MSMLocator.domain_type"));
					break;
				case 2:
					c.readMobId(QStringLiteral("MSMLocator.mob_id"));
					break;
				case 3:
					c.expectTag(76);
					c.string(QStringLiteral("MSMLocator.last_known_volume_utf8"), TextEncoding::Utf8);
					break;
				default:
					unknownExtension(c, QStringLiteral("MSMLocator"), extension);
				}
			}
			c.expectTag(3);
		}

		void binReference(AvbFieldReader &c)
		{
			section(c, 1);
			c.s32(QStringLiteral("BinRef.uid_high"));
			c.s32(QStringLiteral("BinRef.uid_low"));
			c.string(QStringLiteral("BinRef.name"));
			for (int extension; (extension = c.readExtensionTag()) >= 0;)
			{
				if (extension != 1)
					unknownExtension(c, QStringLiteral("BinRef"), extension);
				c.expectTag(76);
				c.string(QStringLiteral("BinRef.name_utf8"), TextEncoding::Utf8);
			}
			c.expectTag(3);
		}

		void mobReference(AvbFieldReader &c)
		{
			section(c, 1);
			c.u32(QStringLiteral("MobRef.mob_hi"));
			c.u32(QStringLiteral("MobRef.mob_lo"));
			c.s32(QStringLiteral("MobRef.position"));
			for (int extension; (extension = c.readExtensionTag()) >= 0;)
			{
				if (extension != 1)
					unknownExtension(c, QStringLiteral("MobRef"), extension);
				c.readMobId(QStringLiteral("MobRef.mob_id"));
			}
		}

		void marker(AvbFieldReader &c)
		{
			mobReference(c);
			section(c, 3);
			c.s32(QStringLiteral("Marker.comp_offset"));
			c.readObjectReference(QStringLiteral("Marker.attributes"));
			if (c.s16(QStringLiteral("Marker.version")) != 1)
				c.failUnsupported(QStringLiteral("Unsupported Marker version."));
			for (int i = 0; i < 3; ++i)
				c.u16(indexed(QStringLiteral("Marker.color"), i));
			for (int extension; (extension = c.readExtensionTag()) >= 0;)
			{
				if (extension != 1)
					unknownExtension(c, QStringLiteral("Marker"), extension);
				c.expectTag(66);
				c.boolean(QStringLiteral("Marker.handled_codes"));
			}
			c.expectTag(3);
		}

		void position(AvbFieldReader &c)
		{
			section(c, 1);
			c.u32(QStringLiteral("Position.mob_id_hi"));
			c.u32(QStringLiteral("Position.mob_id_lo"));
			for (int extension; (extension = c.readExtensionTag()) >= 0;)
			{
				if (extension != 1)
					unknownExtension(c, QStringLiteral("Position"), extension);
				c.readMobId(QStringLiteral("Position.mob_id"));
			}
		}

		void bobPosition(AvbFieldReader &c)
		{
			position(c);
			section(c, 1);
			c.s32(QStringLiteral("BOBPosition.sample_num"));
			c.s32(QStringLiteral("BOBPosition.length"));
			c.s16(QStringLiteral("BOBPosition.track_type"));
			c.s16(QStringLiteral("BOBPosition.track_index"));
		}

		void didPosition(AvbFieldReader &c)
		{
			bobPosition(c);
			section(c, 1);
			c.s32(QStringLiteral("DIDPosition.strip"));
			c.u64(QStringLiteral("DIDPosition.offset"));
			c.u64(QStringLiteral("DIDPosition.byte_length"));
			c.boolean(QStringLiteral("DIDPosition.spos_invalid"));
		}

		void mpegPosition(AvbFieldReader &c)
		{
			didPosition(c);
			section(c, 1);
			c.s16(QStringLiteral("MPGPosition.trailing_discards"));
			c.boolean(QStringLiteral("MPGPosition.need_seq_hdr"));
			const auto count = c.s16(QStringLiteral("MPGPosition.leader_length"));
			c.requireCount(count, 5);
			if (count > 0)
			{
				c.s16(QStringLiteral("MPGPosition.leading_discard_fields"));
				for (qint16 i = 0; i < count; ++i)
				{
					const auto prefix = indexed(QStringLiteral("MPGPosition.fields"), i);
					c.u8(prefix + QStringLiteral(".picture_type"));
					c.u32(prefix + QStringLiteral(".length"));
				}
			}
			c.expectTag(3);
		}

		void attributes(AvbFieldReader &c)
		{
			section(c, 1);
			const auto count = c.u32(QStringLiteral("Attributes.count"));
			c.requireCount(count, 6);
			for (quint32 i = 0; i < count; ++i)
			{
				const auto prefix = indexed(QStringLiteral("Attributes.items"), i);
				const auto type = c.u32(prefix + QStringLiteral(".type"));
				c.string(prefix + QStringLiteral(".name"));
				switch (type)
				{
				case 1:
					c.s32(prefix + QStringLiteral(".value"));
					break;
				case 2:
					c.string(prefix + QStringLiteral(".value"));
					break;
				case 3:
					c.readObjectReference(prefix + QStringLiteral(".value"));
					break;
				case 4:
				{
					const auto size = c.u32(prefix + QStringLiteral(".size"));
					c.readBytes(prefix + QStringLiteral(".value"), size);
					break;
				}
				default:
					c.failUnsupported(QStringLiteral("Unsupported AVB attribute type %1.").arg(type));
				}
			}
			c.expectTag(3);
		}

		void parameterItem(AvbFieldReader &c)
		{
			section(c, 2);
			c.readBinaryUuid(QStringLiteral("ParameterItem.uuid"));
			const auto type = c.s16(QStringLiteral("ParameterItem.value_type"));
			switch (type)
			{
			case 1:
				c.s32(QStringLiteral("ParameterItem.value"));
				break;
			case 2:
				c.f64(QStringLiteral("ParameterItem.value"));
				break;
			case 4:
				c.readObjectReference(QStringLiteral("ParameterItem.value"));
				break;
			default:
				c.failUnsupported(QStringLiteral("Unsupported ParameterItem type %1.").arg(type));
			}
			c.string(QStringLiteral("ParameterItem.name"));
			c.boolean(QStringLiteral("ParameterItem.enable"));
			c.readObjectReference(QStringLiteral("ParameterItem.control_track"));
			for (int extension; (extension = c.readExtensionTag()) >= 0;)
			{
				if (extension != 1)
					unknownExtension(c, QStringLiteral("ParameterItem"), extension);
				c.expectTag(66);
				c.boolean(QStringLiteral("ParameterItem.contribs_to_sig"));
			}
			c.expectTag(3);
		}

		void userParameter(AvbFieldReader &c)
		{
			section(c, 1);
			if (c.s16(QStringLiteral("CFUserParam.byte_order")) != 0x4949)
				c.failUnsupported(QStringLiteral("Unsupported CFUserParam byte order."));
			c.readBinaryUuid(QStringLiteral("CFUserParam.uuid"));
			const auto outerSize = c.s32(QStringLiteral("CFUserParam.value_size1"));
			const auto innerSize = c.s32(QStringLiteral("CFUserParam.value_size2"));
			if (outerSize < 4 || innerSize != outerSize - 4)
				c.failMalformed(QStringLiteral("CFUserParam nested lengths disagree."));
			c.readBytes(QStringLiteral("CFUserParam.data"), innerSize);
			c.expectTag(3);
		}

		void effectParameters(AvbFieldReader &c)
		{
			section(c, 18);
			c.s32(QStringLiteral("EffectParamList.orig_length"));
			c.s32(QStringLiteral("EffectParamList.window_offset"));
			const auto count = c.s32(QStringLiteral("EffectParamList.count"));
			c.s32(QStringLiteral("EffectParamList.keyframe_size"));
			c.requireCount(count, 102);
			for (qint32 i = 0; i < count; ++i)
			{
				const auto prefix = indexed(QStringLiteral("EffectParamList.parameters"), i);
				signed32Fields(c, prefix, {"percent_time", "level", "pos_x", "floor_x", "ceil_x", "pos_y", "floor_y", "ceil_y", "scale_x", "scale_y", "crop_left", "crop_right", "crop_top", "crop_bottom"});
				for (int j = 0; j < 4; ++j)
					c.s32(indexed(prefix + QStringLiteral(".box"), j));
				for (const auto *name : {"box_xscale", "box_yscale", "box_xpos", "box_ypos"})
					c.boolean(prefix + QLatin1Char('.') + QString::fromLatin1(name));
				c.s32(prefix + QStringLiteral(".border_width"));
				c.s32(prefix + QStringLiteral(".border_soft"));
				for (const auto *name : {"splill_gain2", "splill_gain", "splill_soft2", "splill_soft"})
					c.s16(prefix + QLatin1Char('.') + QString::fromLatin1(name));
				c.s8(prefix + QStringLiteral(".enable_key_flags"));
				const auto colors = c.s32(prefix + QStringLiteral(".color_count"));
				c.requireCount(colors, 4);
				for (qint32 j = 0; j < colors; ++j)
					c.s32(indexed(prefix + QStringLiteral(".colors"), j));
				const auto size = c.s32(prefix + QStringLiteral(".param_size"));
				c.readBytes(prefix + QStringLiteral(".user_param"), size);
				c.boolean(prefix + QStringLiteral(".selected"));
			}
			c.expectTag(3);
		}

		void trackerData(AvbFieldReader &c)
		{
			section(c, 1);
			const auto size = c.s16(QStringLiteral("TrackerData.setting_size"));
			c.readBytes(QStringLiteral("TrackerData.settings"), size);
			c.u32(QStringLiteral("TrackerData.clip_version"));
			const auto count = c.s16(QStringLiteral("TrackerData.count"));
			references(c, QStringLiteral("TrackerData.clips"), count);
			for (int extension; (extension = c.readExtensionTag()) >= 0;)
			{
				switch (extension)
				{
				case 1:
					c.expectTag(72);
					c.u32(QStringLiteral("TrackerData.offset_tracking"));
					break;
				case 2:
					c.expectTag(72);
					c.u32(QStringLiteral("TrackerData.smoothing"));
					break;
				case 3:
					c.expectTag(72);
					c.u32(QStringLiteral("TrackerData.jitter_removal"));
					break;
				case 4:
					c.expectTag(75);
					c.f64(QStringLiteral("TrackerData.filter_amount"));
					break;
				case 5:
					c.expectTag(72);
					c.readObjectReference(QStringLiteral("TrackerData.clip5"));
					break;
				case 6:
					c.expectTag(72);
					c.readObjectReference(QStringLiteral("TrackerData.clip6"));
					break;
				default:
					unknownExtension(c, QStringLiteral("TrackerData"), extension);
				}
			}
			c.expectTag(3);
		}

		void trackerDataSlot(AvbFieldReader &c)
		{
			section(c, 1);
			const auto count = c.s32(QStringLiteral("TrackerDataSlot.count"));
			references(c, QStringLiteral("TrackerDataSlot.tracker_data"), count);
			for (int extension; (extension = c.readExtensionTag()) >= 0;)
			{
				if (extension != 1)
					unknownExtension(c, QStringLiteral("TrackerDataSlot"), extension);
				c.expectTag(66);
				c.boolean(QStringLiteral("TrackerDataSlot.track_fg"));
			}
			c.expectTag(3);
		}
	}

	bool supportsAvbDescriptor(QByteArrayView classId)
	{
		// Admission only: unknown chunks must remain ranges without payload reads.
		static constexpr const char *classes[] = {
			"ABOB", "AIFC", "ANCD", "APOS", "ATTR", "AVUP", "CCFX", "CDCI",
			"DATD", "DIDD", "DIDP", "FILE", "FXPS", "GRFX", "JPED", "MCBR",
			"MCMR", "MDES", "MDFL", "MDFM", "MDNG", "MDTP", "MPGA", "MPGI",
			"MPGP", "MSML", "MULD", "PCMA", "PRIT", "PRLS", "RGBA", "SHLP",
			"TKDA", "TKDS", "TKMN", "TKPA", "TKPS", "TMBC", "TMCS", "URLL",
			"WAVE", "WINF"};
		for (const auto *candidate : classes)
			if (classId == QByteArrayView(candidate, 4))
				return true;
		return false;
	}

	bool readAvbDescriptor(AvbFieldReader &c, QByteArrayView classId)
	{
		if (classId == "MDES")
		{
			mediaDescriptor(c);
			c.expectTag(3);
		}
		else if (classId == "MDTP")
		{
			mediaDescriptor(c);
			section(c, 2);
			c.s16(QStringLiteral("TapeDescriptor.cframe"));
			c.expectTag(3);
		}
		else if (classId == "MDFM" || classId == "MDNG")
		{
			mediaDescriptor(c);
			section(c, 1);
			c.expectTag(3);
		}
		else if (classId == "MDFL")
		{
			fileDescriptor(c);
			c.expectTag(3);
		}
		else if (classId == "MULD")
		{
			fileDescriptor(c);
			section(c, 1);
			const auto count = c.s32(QStringLiteral("MultiDescriptor.count"));
			references(c, QStringLiteral("MultiDescriptor.descriptors"), count);
			c.expectTag(3);
		}
		else if (classId == "WAVE" || classId == "AIFC")
			waveSummary(c, classId == "WAVE");
		else if (classId == "PCMA")
			pcmDescriptor(c);
		else if (classId == "MPGA")
			mpegAudioDescriptor(c);
		else if (classId == "DIDD")
		{
			didDescriptor(c);
			c.expectTag(3);
		}
		else if (classId == "CDCI")
		{
			cdciDescriptor(c);
			c.expectTag(3);
		}
		else if (classId == "RGBA")
			rgbaDescriptor(c);
		else if (classId == "MPGI")
			mpegVideoDescriptor(c);
		else if (classId == "JPED")
			jpegDescriptor(c);
		else if (classId == "DATD")
			dataDescriptor(c);
		else if (classId == "ANCD")
		{
			dataDescriptor(c);
			section(c, 1);
			c.s32(QStringLiteral("ANCDataDescriptor.manifest_element_count"));
			c.expectTag(3);
		}
		else if (classId == "FILE" || classId == "WINF")
			fileLocator(c);
		else if (classId == "URLL")
			c.expectTag(3);
		else if (classId == "MSML")
			mediaLocator(c);
		else if (classId == "MCBR")
			binReference(c);
		else if (classId == "MCMR")
		{
			mobReference(c);
			c.expectTag(3);
		}
		else if (classId == "TMBC")
			marker(c);
		else if (classId == "APOS")
		{
			position(c);
			c.expectTag(3);
		}
		else if (classId == "ABOB")
		{
			bobPosition(c);
			c.expectTag(3);
		}
		else if (classId == "DIDP")
		{
			didPosition(c);
			c.expectTag(3);
		}
		else if (classId == "MPGP")
			mpegPosition(c);
		else if (classId == "ATTR")
			attributes(c);
		else if (classId == "PRLS" || classId == "TMCS")
		{
			section(c, 1);
			const auto owner = classId == "PRLS" ? QStringLiteral("ParameterList") : QStringLiteral("TimeCrumbList");
			const auto count = classId == "PRLS" ? c.s32(owner + QStringLiteral(".count"))
												 : c.s16(owner + QStringLiteral(".count"));
			references(c, owner + QStringLiteral(".items"), count);
			c.expectTag(3);
		}
		else if (classId == "PRIT")
			parameterItem(c);
		else if (classId == "AVUP")
			userParameter(c);
		else if (classId == "FXPS")
			effectParameters(c);
		else if (classId == "GRFX" || classId == "SHLP" || classId == "CCFX")
		{
			section(c, 1);
			const auto owner = classId == "GRFX"   ? QStringLiteral("GraphicEffect")
							   : classId == "SHLP" ? QStringLiteral("ShapeList")
												   : QStringLiteral("ColorCorrectionEffect");
			const auto size = classId == "CCFX" ? c.s16(owner + QStringLiteral(".data_size"))
												: c.s32(owner + QStringLiteral(".data_size"));
			const auto field = classId == "GRFX"   ? QStringLiteral(".pict_data")
							   : classId == "SHLP" ? QStringLiteral(".shape_data")
												   : QStringLiteral(".color_correction");
			c.readBytes(owner + field, size);
			c.expectTag(3);
		}
		else if (classId == "TKMN")
		{
			section(c, 1);
			c.readObjectReference(QStringLiteral("TrackerManager.data_slots"));
			c.readObjectReference(QStringLiteral("TrackerManager.param_slots"));
			c.expectTag(3);
		}
		else if (classId == "TKDS")
			trackerDataSlot(c);
		else if (classId == "TKDA")
			trackerData(c);
		else if (classId == "TKPS" || classId == "TKPA")
		{
			section(c, 1);
			const auto owner = classId == "TKPS" ? QStringLiteral("TrackerParameterSlot") : QStringLiteral("TrackerParameter");
			const auto size = c.s16(owner + QStringLiteral(".size"));
			c.readBytes(owner + QStringLiteral(".settings"), size);
			if (classId == "TKPS")
			{
				const auto count = c.s32(owner + QStringLiteral(".count"));
				references(c, owner + QStringLiteral(".params"), count);
			}
			c.expectTag(3);
		}
		else
			return false;
		return true;
	}
}
