#include "opcopier.h"
#include "third_party/xxhash.h"
#include <QByteArray>

namespace
{
constexpr qint64 chunkSize = 4 * 1024 * 1024;
struct Hash
{
	XXH3_state_t *state = XXH3_createState();
	Hash()
	{
		if (state)
			XXH3_64bits_reset(state);
	}
	~Hash()
	{
		if (state)
			XXH3_freeState(state);
	}
	Hash(const Hash &) = delete;
	Hash &operator=(const Hash &) = delete;
	QString digest() const
	{
		return QStringLiteral("%1").arg(XXH3_64bits_digest(state), 16, 16, QLatin1Char('0'));
	}
};
} // namespace
OpCopier::Result OpCopier::hash(OpFile &file, const std::atomic<bool> &cancel,
								const Progress &progress)
{
	Result out;
	Hash hash;
	const auto before = file.stamp();
	if (!hash.state || !before.valid() || !file.io().seek(0))
	{
		out.error = "Cannot start checksum readback.";
		return out;
	}
	QByteArray buffer(int(chunkSize), Qt::Uninitialized);
	qint64 read = 0;
	while (read < before.size)
	{
		if (cancel.load())
		{
			out.outcome = Outcome::Cancelled;
			return out;
		}
		const auto n = file.io().read(buffer.data(), qMin(chunkSize, before.size - read));
		if (n <= 0)
		{
			out.error = "Checksum readback failed before the complete file was read.";
			return out;
		}
		XXH3_64bits_update(hash.state, buffer.constData(), size_t(n));
		read += n;
		if (progress)
			progress(read, before.size, true);
	}
	if (cancel.load())
	{
		out.outcome = Outcome::Cancelled;
		return out;
	}
	if (!before.unchanged(file.stamp()))
	{
		out.error = "The destination changed during checksum readback.";
		return out;
	}
	out.hash = hash.digest();
	out.outcome = Outcome::Succeeded;
	return out;
}
OpCopier::Result OpCopier::copy(OpFile &source, OpFile &destination,
								const std::atomic<bool> &cancel, const Progress &progress,
								const std::function<void()> &beforeReadback)
{
	Result out;
	Hash hash;
	const auto before = source.stamp();
	if (!source.canStreamCopy(out.error))
		return out;
	if (!hash.state || !before.valid() || !source.io().seek(0))
	{
		out.error = "Cannot start copying.";
		return out;
	}
	QByteArray buffer(int(chunkSize), Qt::Uninitialized);
	qint64 copied = 0;
	while (copied < before.size)
	{
		if (cancel.load())
		{
			out.outcome = Outcome::Cancelled;
			return out;
		}
		const auto n = source.io().read(buffer.data(), qMin(chunkSize, before.size - copied));
		if (n <= 0)
		{
			out.error = "The source could not be read completely.";
			return out;
		}
		qint64 written = 0;
		while (written < n)
		{
			const auto count = destination.io().write(buffer.constData() + written, n - written);
			if (count <= 0)
			{
				out.error = "The destination reported a write error.";
				return out;
			}
			written += count;
		}
		XXH3_64bits_update(hash.state, buffer.constData(), size_t(n));
		copied += n;
		if (progress)
			progress(copied, before.size, false);
	}
	if (cancel.load())
	{
		out.outcome = Outcome::Cancelled;
		return out;
	}
	if (!source.stillAt(source.path(), before))
	{
		out.error = "The source changed during copying; it has been retained.";
		return out;
	}
	QString metadataError;
	out.metadataComplete = destination.preserveMetadataFrom(source, metadataError);
	const auto sync = destination.sync();
	if (sync == NativeFile::SyncResult::Failed)
	{
		out.error = "The destination could not confirm its writes.";
		return out;
	}
	out.durable = sync == NativeFile::SyncResult::Ok;
	if (destination.stamp().size != before.size)
	{
		out.error = "The destination length differs from the source.";
		return out;
	}
	if (beforeReadback)
		beforeReadback();
	const auto actual = OpCopier::hash(destination, cancel, progress);
	if (actual.outcome != Outcome::Succeeded)
	{
		out.outcome = actual.outcome;
		out.error = actual.error;
		return out;
	}
	if (actual.hash != hash.digest())
	{
		out.error = "Checksum verification failed; the source has been retained.";
		return out;
	}
	if (!source.stillAt(source.path(), before))
	{
		out.error = "The source changed before verification finished; it has been retained.";
		return out;
	}
	out.hash = actual.hash;
	out.error = metadataError;
	out.outcome = Outcome::Succeeded;
	return out;
}
