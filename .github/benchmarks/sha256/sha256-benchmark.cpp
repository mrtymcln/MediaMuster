#include <QByteArray>
#include <QCryptographicHash>
#include <QLibraryInfo>
#include <QScopeGuard>
#include <QString>
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr std::size_t MIB = 1024 * 1024;
constexpr std::size_t BUFFER_BYTES = 64 * MIB;
constexpr std::size_t CHUNK_BYTES = 4 * MIB;
constexpr std::size_t PASSES = 16;
constexpr std::size_t ROUNDS = 5;
constexpr std::size_t SHA256_BYTES = 32;

void checkStatus(NTSTATUS status, const char *operation)
{
    if (status < 0)
        throw std::runtime_error(std::string(operation) + " failed with NTSTATUS " + std::to_string(status));
}

template <typename Update>
void feedBuffer(std::vector<char> &buffer, std::size_t passes, Update update)
{
    for (std::size_t pass = 0; pass < passes; ++pass)
        for (std::size_t offset = 0; offset < buffer.size(); offset += CHUNK_BYTES)
            update(buffer.data() + offset);
}

QByteArray hashWithQt(std::vector<char> &buffer, std::size_t passes)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    feedBuffer(buffer, passes, [&](const char *data)
    {
        hash.addData(QByteArrayView(data, static_cast<qsizetype>(CHUNK_BYTES)));
    });
    return hash.result();
}

QByteArray hashWithCng(std::vector<char> &buffer, std::size_t passes)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    checkStatus(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0),
                "BCryptOpenAlgorithmProvider");
    const auto closeAlgorithm = qScopeGuard([&] { BCryptCloseAlgorithmProvider(algorithm, 0); });
    BCRYPT_HASH_HANDLE hash = nullptr;
    checkStatus(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0), "BCryptCreateHash");
    const auto destroyHash = qScopeGuard([&] { BCryptDestroyHash(hash); });

    feedBuffer(buffer, passes, [&](char *data)
    {
        checkStatus(BCryptHashData(hash, reinterpret_cast<PUCHAR>(data), static_cast<ULONG>(CHUNK_BYTES), 0),
                    "BCryptHashData");
    });
    std::array<UCHAR, SHA256_BYTES> digest{};
    checkStatus(BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0), "BCryptFinishHash");
    return QByteArray(reinterpret_cast<const char *>(digest.data()), static_cast<qsizetype>(digest.size()));
}

template <typename Hash>
double measure(Hash hash, std::vector<char> &buffer, QByteArray &digest)
{
    const auto started = std::chrono::steady_clock::now();
    digest = hash(buffer, PASSES);
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - started;
    return static_cast<double>(buffer.size() / MIB * PASSES) / elapsed.count();
}

QByteArray loadedQtPath()
{
    const auto module = GetModuleHandleW(L"Qt6Core.dll");
    if (!module)
        throw std::runtime_error("Qt6Core.dll is not loaded");
    std::array<wchar_t, 32768> path{};
    const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size())
        throw std::runtime_error("Cannot identify the loaded Qt6Core.dll");
    return QString::fromWCharArray(path.data(), static_cast<qsizetype>(length)).toUtf8();
}

int runBenchmark()
{
    if (QByteArrayView(qVersion()) != QByteArrayView("6.5.3") || QLibraryInfo::isDebugBuild())
        throw std::runtime_error("The benchmark requires the Qt 6.5.3 Release runtime");
    std::printf("Loaded Qt %s Release DLL: %s\n", qVersion(), loadedQtPath().constData());

    std::vector<char> buffer(BUFFER_BYTES);
    std::uint32_t sequence = 42;
    for (auto &byte : buffer)
    {
        sequence = sequence * 1664525u + 1013904223u;
        byte = static_cast<char>(sequence >> 24);
    }
    if (hashWithQt(buffer, 1) != hashWithCng(buffer, 1))
        throw std::runtime_error("SHA-256 digests differed during warm-up");

    std::array<double, ROUNDS> qtRates{}, cngRates{};
    std::printf("%zu MiB hashed per trial, %zu MiB chunks, %zu trials per API\n",
                buffer.size() / MIB * PASSES, CHUNK_BYTES / MIB, ROUNDS);
    std::fflush(stdout);
    for (std::size_t round = 0; round < ROUNDS; ++round)
    {
        QByteArray qtDigest, cngDigest;
        if (round % 2 == 0)
        {
            qtRates[round] = measure(hashWithQt, buffer, qtDigest);
            cngRates[round] = measure(hashWithCng, buffer, cngDigest);
        }
        else
        {
            cngRates[round] = measure(hashWithCng, buffer, cngDigest);
            qtRates[round] = measure(hashWithQt, buffer, qtDigest);
        }
        if (qtDigest != cngDigest || qtDigest.size() != SHA256_BYTES)
            throw std::runtime_error("SHA-256 digests differed during a timed trial");
        std::printf("Trial %zu: Qt %.1f MiB/s, CNG %.1f MiB/s\n", round + 1, qtRates[round], cngRates[round]);
        std::fflush(stdout);
    }
    std::sort(qtRates.begin(), qtRates.end());
    std::sort(cngRates.begin(), cngRates.end());
    const auto qtMedian = qtRates[ROUNDS / 2];
    const auto cngMedian = cngRates[ROUNDS / 2];
    std::printf("Median: Qt %.1f MiB/s, CNG %.1f MiB/s, CNG / Qt %.2fx\n",
                qtMedian, cngMedian, cngMedian / qtMedian);
    std::puts("SHA-256 digests matched in every trial.");
    return EXIT_SUCCESS;
}
}

int main()
{
    try
    {
        return runBenchmark();
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return EXIT_FAILURE;
    }
}
