#ifndef TRANSMITFILE_CHUNKING_H
#define TRANSMITFILE_CHUNKING_H

#include <algorithm>
#include <vector>

// TransmitFile documents a single call maximum of 2147483646 bytes see
// https colon slash slash learn microsoft com windows win32 api mswsock
// nf-mswsock-transmitfile a file at or beyond 4294967296 bytes silently
// wraps when the byte count is cast to a 32 bit DWORD and a file between
// the two limits makes the single call fail outright this splits any
// size into a sequence of chunks that each fit safely within the
// documented limit
//
// pure function with no windows types so it can be exercised by a
// print flag test see the workflow document for the citation
constexpr long long kTransmitFileMaxChunkBytes = 2147483646LL;

inline std::vector<long long> ComputeTransmitFileChunkSizes(long long totalBytes) {
    std::vector<long long> chunks;
    long long remaining = totalBytes;
    while (remaining > 0) {
        long long chunk = (std::min)(remaining, kTransmitFileMaxChunkBytes);
        chunks.push_back(chunk);
        remaining -= chunk;
    }
    return chunks;
}

#endif // TRANSMITFILE_CHUNKING_H
