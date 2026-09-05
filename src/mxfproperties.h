/*
 * Identifiers adapted from libMXF baseline and extension data models
 *
 * Copyright (C) 2006, British Broadcasting Corporation
 * Copyright (C) 2009, British Broadcasting Corporation
 * Copyright (C) 2011, British Broadcasting Corporation
 * Copyright (C) 2012, British Broadcasting Corporation
 * All Rights Reserved.
 *
 * Author: Philip de Nier
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the British Broadcasting Corporation nor the names
 *       of its contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once
#include "avidusage.h"

// Property identifiers adapted from BBC libMXF data model definitions.
// https://github.com/BBC-archive/libMXF/tree/main/mxf
// These are canonical property identifiers, not file-local tag numbers.
struct MxfPropertyIdentifier { const char *hex; unsigned short tag; };
inline constexpr MxfPropertyIdentifier kMxfProperties[] = {
	{"060e2b34010101010101150200000000", 0x3c0a}, // InterchangeObject::InstanceUID
	{"060e2b34010101040601010401080000", 0x3b08}, // Preface::PrimaryPackage
	{"060e2b34010101020601010405020000", 0x1902}, // ContentStorage::EssenceContainerData
	{"060e2b34010101020601010601000000", 0x2701}, // EssenceContainerData::LinkedPackageUID
	{"060e2b34010101040103040400000000", 0x3f07}, // EssenceContainerData::BodySID
	{"060e2b34010101010101151000000000", 0x4401}, // GenericPackage::PackageUID
	{"060e2b34010101010103030201000000", 0x4402}, // GenericPackage::Name
	{"060e2b34010101020601010406050000", 0x4403}, // GenericPackage::Tracks
	{"060e2b34010101020107010100000000", 0x4801}, // GenericTrack::TrackID
	{"060e2b34010101020601010402040000", 0x4803}, // GenericTrack::Sequence
	{"060e2b34010101020530040500000000", 0x4b01}, // Track::EditRate
	{"060e2b34010101020407010000000000", 0x0201}, // StructuralComponent::DataDefinition
	{"060e2b34010101020702020101030000", 0x0202}, // StructuralComponent::Duration
	{"060e2b34010101020601010406090000", 0x1001}, // Sequence::StructuralComponents
	{"060e2b34010101020404010102060000", 0x1502}, // TimecodeComponent::RoundedTimecodeBase
	{"060e2b34010101010404010105000000", 0x1503}, // TimecodeComponent::DropFrame
	{"060e2b34010101020601010301000000", 0x1101}, // SourceClip::SourcePackageID
	{"060e2b34010101020601010302000000", 0x1102}, // SourceClip::SourceTrackID
	{"060e2b34010101020601010402030000", 0x4701}, // SourcePackage::Descriptor
	{"060e2b34010101050601010305000000", 0x3006}, // FileDescriptor::LinkedTrackID
	{"060e2b34010101010406010100000000", 0x3001}, // FileDescriptor::SampleRate
	{"060e2b34010101010406010200000000", 0x3002}, // FileDescriptor::ContainerDuration
	{"060e2b34010101020601010401020000", 0x3004}, // FileDescriptor::EssenceContainer
	{"060e2b34010101010401030104000000", 0x320c}, // GenericPictureEssenceDescriptor::FrameLayout
	{"060e2b34010101010401050202000000", 0x3203}, // GenericPictureEssenceDescriptor::StoredWidth
	{"060e2b34010101010401050201000000", 0x3202}, // GenericPictureEssenceDescriptor::StoredHeight
	{"060e2b34010101020401060100000000", 0x3201}, // GenericPictureEssenceDescriptor::PictureEssenceCoding
	{"060e2b3401010102040105030a000000", 0x3301}, // CDCIEssenceDescriptor::ComponentDepth
	{"060e2b34010101020401050306000000", 0x3401}, // RGBAEssenceDescriptor::PixelLayout
	{"060e2b34010101050402030101010000", 0x3d03}, // GenericSoundEssenceDescriptor::AudioSamplingRate
	{"060e2b34010101050402010104000000", 0x3d07}, // GenericSoundEssenceDescriptor::ChannelCount
	{"060e2b34010101040402030304000000", 0x3d01}, // GenericSoundEssenceDescriptor::QuantizationBits
	{"060e2b34010101020402040200000000", 0x3d06}, // GenericSoundEssenceDescriptor::SoundEssenceCompression
	{"060e2b340101010406010104060b0000", 0x3f01}, // MultipleDescriptor::SubDescriptorUIDs
	{"060e2b34010101040103040400000000", 0x3f07}, // DCTimedTextResourceSubDescriptor::EssenceStreamID
	{"060e2b3401010102030201020c000000", 0x4406}, // GenericPackage::UserComments
	{"060e2b34010101020302010209010000", 0x5001}, // TaggedValue::Name
	{"060e2b3401010102030201020a010000", 0x5003}, // TaggedValue::Value
	{"060e2b34010101070501010800000000", 0x4408}, // GenericPackage::UsageCode (AAF/MXF; corpus primer)
	{"a01c0004ac969f506095818347b111d4", 0xf001}, // Avid MobAttributeList
	{"a01c0004ac969f506095818547b111d4", 0xf002}, // Avid TaggedValueAttributeList
	{AvidUsage::kPrivateMxfPropertyHex, AvidUsage::kPrivateMxfTag}, // Avid MobAppCode integer; not standard UsageCode UID
};

// libMXF mxf_avid_labels_and_keys.h: AvidUncRGBA. The generic AAF-KLV
// descriptor wrapper is not codec-specific; this label is in the partition.
inline constexpr char kAvidUncRgbaContainerHex[] = "060e2b34040101010e04030102080100";
