#include "writerthread.h"
#include "util.h"
#include <memory.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <thread>
#include <chrono>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#include <cstdint>
#endif

namespace {

#ifdef _WIN32
int fastp_errno_from_win32(DWORD error) {
    switch (error) {
    case ERROR_ACCESS_DENIED:
        return EACCES;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        return ENOENT;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
        return ENOSPC;
    case ERROR_INVALID_HANDLE:
        return EBADF;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        return ENOMEM;
    case ERROR_OPERATION_ABORTED:
        return EIO;
    default:
        return EIO;
    }
}

class FastpWin32PwriteEvent {
public:
    FastpWin32PwriteEvent() {
        mHandle = CreateEventA(NULL, TRUE, FALSE, NULL);
        mError = (mHandle == NULL) ? GetLastError() : ERROR_SUCCESS;
    }

    ~FastpWin32PwriteEvent() {
        if (mHandle != NULL)
            CloseHandle(mHandle);
    }

    HANDLE resetAndGet() {
        if (mHandle == NULL) {
            errno = fastp_errno_from_win32(mError);
            return NULL;
        }
        if (!ResetEvent(mHandle)) {
            errno = fastp_errno_from_win32(GetLastError());
            return NULL;
        }
        return mHandle;
    }

private:
    HANDLE mHandle;
    DWORD mError;
};

HANDLE fastp_get_thread_pwrite_event() {
    // Windows/MSYS2-UCRT64: reuse one manual-reset event per worker thread.
    // Previous Windows implementation created and closed an event for each
    // pwrite-compatible block write, which is unnecessary in this hot path.
    thread_local FastpWin32PwriteEvent event;
    return event.resetAndGet();
}
#endif

int fastp_open_pwrite_file(const string& filename) {
#ifdef _WIN32
    // Windows/MSYS2-UCRT64: open an overlapped binary handle so fastp_pwrite()
    // can write gzip blocks at explicit offsets without sharing a file pointer.
    HANDLE handle = CreateFileA(filename.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                                NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        errno = fastp_errno_from_win32(GetLastError());
        return -1;
    }

    int fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_BINARY);
    if (fd < 0) {
        CloseHandle(handle);
        return -1;
    }
    return fd;
#else
    return open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
#endif
}

int fastp_ftruncate(int fd, size_t size) {
#ifdef _WIN32
    // Previous Windows implementation:
    // errno_t result = _chsize_s(fd, static_cast<__int64>(size));
    // Windows/MSYS2-UCRT64: use the native HANDLE API for the overlapped file
    // handle instead of CRT resizing helpers.
    intptr_t osHandle = _get_osfhandle(fd);
    if (osHandle == -1) {
        errno = EBADF;
        return -1;
    }

    LARGE_INTEGER pos;
    pos.QuadPart = static_cast<LONGLONG>(size);
    HANDLE handle = reinterpret_cast<HANDLE>(osHandle);
    if (!SetFilePointerEx(handle, pos, NULL, FILE_BEGIN)) {
        errno = fastp_errno_from_win32(GetLastError());
        return -1;
    }
    if (!SetEndOfFile(handle)) {
        errno = fastp_errno_from_win32(GetLastError());
        return -1;
    }
    return 0;
#else
    return ftruncate(fd, size);
#endif
}

ssize_t fastp_pwrite(int fd, const void* buf, size_t nbytes, size_t offset) {
#ifdef _WIN32
    // Windows/MSYS2-UCRT64: MinGW UCRT64 has no POSIX pwrite(), so emulate it
    // with an overlapped WriteFile call against the handle created above.
    intptr_t osHandle = _get_osfhandle(fd);
    if (osHandle == -1) {
        errno = EBADF;
        return -1;
    }

    OVERLAPPED overlapped;
    memset(&overlapped, 0, sizeof(overlapped));
    uint64_t pos = static_cast<uint64_t>(offset);
    overlapped.Offset = static_cast<DWORD>(pos & 0xffffffffu);
    overlapped.OffsetHigh = static_cast<DWORD>(pos >> 32);
    // Previous Windows implementation:
    // overlapped.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    // Windows/MSYS2-UCRT64: reuse a thread-local event to avoid per-block
    // kernel object creation while keeping each overlapped write independent.
    overlapped.hEvent = fastp_get_thread_pwrite_event();
    if (overlapped.hEvent == NULL) {
        return -1;
    }

    DWORD writeSize = nbytes > 0xffffffffu ? 0xffffffffu : static_cast<DWORD>(nbytes);
    DWORD written = 0;
    HANDLE handle = reinterpret_cast<HANDLE>(osHandle);
    BOOL ok = WriteFile(handle, buf, writeSize, &written, &overlapped);
    if (!ok) {
        DWORD error = GetLastError();
        if (error == ERROR_IO_PENDING) {
            ok = GetOverlappedResult(handle, &overlapped, &written, TRUE);
            error = ok ? ERROR_SUCCESS : GetLastError();
        }
        if (!ok) {
            errno = fastp_errno_from_win32(error);
            return -1;
        }
    }

    return static_cast<ssize_t>(written);
#else
    return pwrite(fd, buf, nbytes, offset);
#endif
}

}

WriterThread::WriterThread(Options* opt, string filename, bool isSTDOUT){
    mOptions = opt;
    mWriter1 = NULL;
    mInputCompleted = false;
    mFilename = filename;

    mPwriteMode = !isSTDOUT && ends_with(filename, ".gz") && mOptions->thread > 1;
    mFd = -1;
    mOffsetRing = NULL;
    mNextSeq = NULL;
    mCompressors = NULL;
    mCompBufs = NULL;
    mCompBufSizes = NULL;
    mBufferLists = NULL;

    if (mPwriteMode) {
        // Original POSIX-only open:
        // mFd = open(mFilename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        // Windows/MSYS2-UCRT64: use a compatibility opener for binary,
        // offset-addressable writes while keeping POSIX behavior unchanged.
        mFd = fastp_open_pwrite_file(mFilename);
        if (mFd < 0)
            error_exit("Failed to open for pwrite: " + mFilename);
        mOffsetRing = new OffsetSlot[OFFSET_RING_SIZE];
        mNextSeq = new size_t[mOptions->thread];
        for (int t = 0; t < mOptions->thread; t++)
            mNextSeq[t] = t;
        mCompressors = new libdeflate_compressor*[mOptions->thread];
        for (int t = 0; t < mOptions->thread; t++)
            mCompressors[t] = libdeflate_alloc_compressor(mOptions->compression);
        size_t initBufSize = PACK_SIZE * 500;
        mCompBufs = new char*[mOptions->thread];
        mCompBufSizes = new size_t[mOptions->thread];
        for (int t = 0; t < mOptions->thread; t++) {
            mCompBufs[t] = new char[initBufSize];
            mCompBufSizes[t] = initBufSize;
        }
        mWorkingBufferList = 0;
        mBufferLength = 0;
    } else {
        initWriter(filename, isSTDOUT);
        initBufferLists();
        mWorkingBufferList = 0;
        mBufferLength = 0;
    }
}

WriterThread::~WriterThread() {
    cleanup();
}

bool WriterThread::isCompleted()
{
    if (mPwriteMode) return true;  // no writer thread needed
    return mInputCompleted && (mBufferLength==0);
}

bool WriterThread::setInputCompleted() {
    if (mPwriteMode) {
        setInputCompletedPwrite();
        mInputCompleted = true;
        return true;
    }
    mInputCompleted = true;
    for(int t=0; t<mOptions->thread; t++) {
        mBufferLists[t]->setProducerFinished();
    }
    return true;
}

void WriterThread::setInputCompletedPwrite() {
    int W = mOptions->thread;
    size_t lastSeq = 0;
    bool anyProcessed = false;
    for (int t = 0; t < W; t++) {
        if (mNextSeq[t] != (size_t)t) {
            size_t workerLastSeq = mNextSeq[t] - W;
            if (!anyProcessed || workerLastSeq > lastSeq) {
                lastSeq = workerLastSeq;
                anyProcessed = true;
            }
        }
    }
    size_t offset = anyProcessed ?
        mOffsetRing[lastSeq & (OFFSET_RING_SIZE - 1)].cumulative_offset.load(std::memory_order_relaxed) : 0;
    // Original POSIX-only truncation:
    // ftruncate(mFd, offset);
    // Windows/MSYS2-UCRT64: use a compatibility wrapper for MinGW CRT fds.
    if (fastp_ftruncate(mFd, offset) != 0)
        error_exit("ftruncate failed: " + string(strerror(errno)));
}

void WriterThread::output(){
    if (mPwriteMode) return;  // no-op
    SingleProducerSingleConsumerList<string*>* list = mBufferLists[mWorkingBufferList];
    if(!list->canBeConsumed()) {
        usleep(100);
    } else {
        string* str = list->consume();
        mWriter1->write(str->data(), str->length());
        delete str;
        mBufferLength--;
        mWorkingBufferList = (mWorkingBufferList+1)%mOptions->thread;
    }
}

void WriterThread::input(int tid, string* data) {
    if (mPwriteMode) {
        inputPwrite(tid, data);
        return;
    }
    mBufferLists[tid]->produce(data);
    mBufferLength++;
}

void WriterThread::inputPwrite(int tid, string* data) {
    size_t bound = libdeflate_gzip_compress_bound(mCompressors[tid], data->size());
    // Grow per-worker buffer if needed
    if (bound > mCompBufSizes[tid]) {
        delete[] mCompBufs[tid];
        mCompBufs[tid] = new char[bound];
        mCompBufSizes[tid] = bound;
    }
    size_t outsize = libdeflate_gzip_compress(mCompressors[tid], data->data(), data->size(),
                                               mCompBufs[tid], bound);
    if (outsize == 0)
        error_exit("libdeflate gzip compression failed");
    delete data;
    const char* writeData = mCompBufs[tid];
    size_t wsize = outsize;

    size_t seq = mNextSeq[tid];

    // Wait for previous batch's cumulative offset.
    // Sleep yields CPU to prevent livelock under contention.
    size_t offset = 0;
    if (seq > 0) {
        size_t prevSlot = (seq - 1) & (OFFSET_RING_SIZE - 1);
        while (mOffsetRing[prevSlot].published_seq.load(std::memory_order_acquire) != seq - 1) {
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
        offset = mOffsetRing[prevSlot].cumulative_offset.load(std::memory_order_relaxed);
    }

    // Publish offset BEFORE pwrite — next worker starts immediately
    size_t mySlot = seq & (OFFSET_RING_SIZE - 1);
    mOffsetRing[mySlot].cumulative_offset.store(offset + wsize, std::memory_order_relaxed);
    mOffsetRing[mySlot].published_seq.store(seq, std::memory_order_release);

    // pwrite (concurrent with other workers on non-overlapping regions)
    if (wsize > 0) {
        size_t written = 0;
        while (written < wsize) {
            // Original POSIX-only offset write:
            // ssize_t ret = pwrite(mFd, writeData + written, wsize - written, offset + written);
            // Windows/MSYS2-UCRT64: fastp_pwrite() delegates to pwrite() on
            // POSIX and uses WriteFile with OVERLAPPED offsets on Windows.
            ssize_t ret = fastp_pwrite(mFd, writeData + written, wsize - written, offset + written);
            if (ret < 0) {
                if (errno == EINTR) continue;
                error_exit("pwrite failed: " + string(strerror(errno)));
            }
            if (ret == 0)
                error_exit("pwrite returned 0 (disk full?)");
            written += ret;
        }
    }

    mNextSeq[tid] += mOptions->thread;
}

void WriterThread::cleanup() {
    if (mPwriteMode) {
        if (mFd >= 0) { close(mFd); mFd = -1; }
        delete[] mOffsetRing; mOffsetRing = NULL;
        delete[] mNextSeq; mNextSeq = NULL;
        if (mCompressors) {
            for (int t = 0; t < mOptions->thread; t++)
                libdeflate_free_compressor(mCompressors[t]);
            delete[] mCompressors; mCompressors = NULL;
        }
        if (mCompBufs) {
            for (int t = 0; t < mOptions->thread; t++)
                delete[] mCompBufs[t];
            delete[] mCompBufs; mCompBufs = NULL;
        }
        delete[] mCompBufSizes; mCompBufSizes = NULL;
        return;
    }
    deleteWriter();
    if (mBufferLists) {
        for(int t=0; t<mOptions->thread; t++)
            delete mBufferLists[t];
        delete[] mBufferLists;
        mBufferLists = NULL;
    }
}

void WriterThread::deleteWriter() {
    if(mWriter1 != NULL) {
        delete mWriter1;
        mWriter1 = NULL;
    }
}

void WriterThread::initWriter(string filename1, bool isSTDOUT) {
    deleteWriter();
    mWriter1 = new Writer(mOptions, filename1, mOptions->compression, isSTDOUT);
}

void WriterThread::initBufferLists() {
    mBufferLists = new SingleProducerSingleConsumerList<string*>*[mOptions->thread];
    for(int t=0; t<mOptions->thread; t++) {
        mBufferLists[t] = new SingleProducerSingleConsumerList<string*>();
    }
}
